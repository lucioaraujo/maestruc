// Maestruc — motor de áudio: mixer de stems.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
#include "Mixer.h"

#include <algorithm>
#include <cmath>

namespace maestruc {

float dbToGain(float db) { return db <= -90.0f ? 0.0f : std::pow(10.0f, db / 20.0f); }

namespace {
float clampf(float v, float lo, float hi) { return std::min(hi, std::max(lo, v)); }
int clampi(int v, int lo, int hi) { return std::min(hi, std::max(lo, v)); }

// Guarda a maior redução (o valor mais negativo) até a tela ler.
void publicarMenor(std::atomic<float>& alvo, float db) {
    float atual = alvo.load(std::memory_order_relaxed);
    while (db < atual && !alvo.compare_exchange_weak(atual, db, std::memory_order_relaxed)) {}
}

// Coeficiente de um seguidor de 1ª ordem para `ms` em blocos de `frames`.
float blockCoeff(float ms, int frames, double sr) {
    const double t = std::max(1.0, static_cast<double>(ms)) * 0.001 * sr;
    return static_cast<float>(1.0 - std::exp(-static_cast<double>(frames) / t));
}
} // namespace

void Mixer::prepare(double sampleRate, int maxBlock, int numStrips, int activeStrips) {
    sampleRate_ = sampleRate;
    maxBlock_ = maxBlock;
    preparedStrips_ = std::min(numStrips, kMaxStrips);
    numStrips_.store(activeStrips < 0 ? preparedStrips_ : std::min(activeStrips, preparedStrips_), std::memory_order_release);
    scratch_.assign(static_cast<std::size_t>(maxBlock) * 2, 0.0f);
    envelope_.fill(0.0f);
    infGainDb_.fill(0.0f);
    infBright_.fill(0.0f);
    lpState_.fill(0.0f);
    infState_.fill(0.0f);
    for (int i = 0; i < kMaxStrips; ++i) {
        curGainL_[static_cast<std::size_t>(i)] = 0.0f;
        curGainR_[static_cast<std::size_t>(i)] = 0.0f;
    }
    curMaster_ = dbToGain(masterGainDb_.load());
    curFones_ = dbToGain(fonesGainDb_.load());
    limMaster_.prepare(sampleRate);
    limFones_.prepare(sampleRate);
    lpMaster_.fill(0.0f);
    ears_.fill(Ear {});
    for (auto& v : listened_) v.store(0.0f, std::memory_order_relaxed);
    for (auto& v : onsetCount_) v.store(0, std::memory_order_relaxed);
}

void Mixer::setActiveStrips(int n) {
    numStrips_.store(std::max(numStrips_.load(), std::min(n, preparedStrips_)), std::memory_order_release);
}

void Mixer::process(const float* const* in, float* masterL, float* masterR,
                    float* fonesL, float* fonesR, float* const* post, int frames) {
    std::fill(masterL, masterL + frames, 0.0f);
    std::fill(masterR, masterR + frames, 0.0f);
    if (fonesL) std::fill(fonesL, fonesL + frames, 0.0f);
    if (fonesR) std::fill(fonesR, fonesR + frames, 0.0f);
    if (frames <= 0) return;
    const int ns = numStrips_.load(std::memory_order_acquire);

    bool anySolo = false;
    for (int s = 0; s < ns; ++s) anySolo = anySolo || strips_[static_cast<std::size_t>(s)].solo.load(std::memory_order_relaxed);

    // 1. Envoltória de cada stem, depois do fader e antes das influências:
    //    o que se ouve dele. Mudo não influencia ninguém.
    const float att = blockCoeff(10.0f, frames, sampleRate_);
    // Escuta: constantes por amostra/bloco.
    const auto sr = static_cast<float>(sampleRate_);
    const float fastFall = std::exp(-1.0f / (0.010f * sr));    // pico solta em 10 ms
    const float slowCoef = 1.0f - std::exp(-1.0f / (0.03f * sr)); // média de 30 ms
    const int refractorySamples = static_cast<int>(0.06f * sr);  // no máximo ~16 ataques/s
    const float pulseFall = std::exp(-static_cast<float>(frames) / (0.08f * sr));
    const float densityFall = std::exp(-static_cast<float>(frames) / (2.0f * sr));
    const float silenceUp = blockCoeff(400.0f, frames, sampleRate_);
    const float silenceDown = blockCoeff(20.0f, frames, sampleRate_);
    const float quiet = dbToGain(-48.0f);
    const float rel = blockCoeff(250.0f, frames, sampleRate_);
    std::array<bool, kMaxStrips> audible {};
    for (int s = 0; s < ns; ++s) {
        auto& p = strips_[static_cast<std::size_t>(s)];
        const bool on = !p.mute.load(std::memory_order_relaxed)
                        && (!anySolo || p.solo.load(std::memory_order_relaxed));
        audible[static_cast<std::size_t>(s)] = on;
        double sum = 0.0;
        const float* l = in[s * 2];
        const float* r = in[s * 2 + 1];
        const float fader = dbToGain(clampf(p.gainDb.load(std::memory_order_relaxed), -90.0f, 12.0f));
        Ear& ear = ears_[static_cast<std::size_t>(s)];
        int onsets = 0;
        if (on && (l || r)) {
            for (int n = 0; n < frames; ++n) {
                const float a = l ? l[n] : 0.0f;
                const float b = r ? r[n] : 0.0f;
                sum += 0.5 * (static_cast<double>(a) * a + static_cast<double>(b) * b);
                // Ataque: o pico rápido da derivada salta acima da média lenta.
                const float x = 0.5f * (a + (r ? b : a)) * fader;
                const float d = std::fabs(x - ear.prev);
                ear.prev = x;
                ear.fast = std::max(d, ear.fast * fastFall);
                if (ear.refractory > 0) {
                    --ear.refractory;
                } else if (ear.fast > 2.5f * ear.slow + 0.002f) {
                    ++onsets;
                    ear.refractory = refractorySamples;
                }
                ear.slow += slowCoef * (ear.fast - ear.slow);
            }
        } else {
            ear.prev = 0.0f;
            ear.fast *= std::pow(fastFall, static_cast<float>(frames));
            ear.slow *= std::pow(1.0f - slowCoef, static_cast<float>(frames));
            ear.refractory = std::max(0, ear.refractory - frames);
        }
        // RMS em escala 0…1 com ~-6 dBFS RMS = cheio.
        const float level = on ? clampf(static_cast<float>(std::sqrt(sum / frames)) * 2.0f * fader, 0.0f, 1.0f)
                               : 0.0f;
        float& e = envelope_[static_cast<std::size_t>(s)];
        e += (level > e ? att : rel) * (level - e);

        ear.pulse = onsets > 0 ? 1.0f : ear.pulse * pulseFall;
        ear.density = ear.density * densityFall + static_cast<float>(onsets);
        const float wantSilence = level < quiet ? 1.0f : 0.0f;
        ear.silence += (wantSilence > ear.silence ? silenceUp : silenceDown) * (wantSilence - ear.silence);
        if (onsets > 0)
            onsetCount_[static_cast<std::size_t>(s)].fetch_add(static_cast<std::uint32_t>(onsets), std::memory_order_relaxed);
        auto* out = &listened_[static_cast<std::size_t>(s) * kNumListen];
        out[0].store(e, std::memory_order_relaxed);
        out[1].store(ear.pulse, std::memory_order_relaxed);
        out[2].store(clampf(ear.density / 2.0f / 8.0f, 0.0f, 1.0f), std::memory_order_relaxed);
        out[3].store(ear.silence, std::memory_order_relaxed);
    }

    // 2. Influências "no som": cada uma suaviza no próprio tempo.
    std::array<float, kMaxStrips> modDb {};
    std::array<float, kMaxStrips> modBright {};
    float modMasterDb = 0.0f, modMasterBright = 0.0f;
    for (int k = 0; k < kMaxInfluences; ++k) {
        auto& f = influences_[static_cast<std::size_t>(k)];
        float goal = 0.0f;
        int dst = -1;
        Target tgt = Target::Ganho;
        int listen = 0;
        if (f.active.load(std::memory_order_relaxed)) {
            const int src = f.source.load(std::memory_order_relaxed);
            dst = f.dest.load(std::memory_order_relaxed);
            if (src >= 0 && src < ns && ((dst >= 0 && dst < ns) || dst == kDestMaster) && src != dst) {
                listen = clampi(f.listen.load(std::memory_order_relaxed), 0, kNumListen - 1);
                goal = clampf(f.amount.load(std::memory_order_relaxed), -1.0f, 1.0f)
                       * listened_[static_cast<std::size_t>(src) * kNumListen + static_cast<std::size_t>(listen)].load(std::memory_order_relaxed);
            } else
                dst = -1;
            tgt = static_cast<Target>(f.target.load(std::memory_order_relaxed));
        }
        float& st = infState_[static_cast<std::size_t>(k)];
        // Entra rápido (30 ms) e solta no tempo pedido: o destino "abre espaço"
        // sem atraso e volta com a respiração escolhida pelo músico.
        // Nos ataques a entrada é quase instantânea, para o golpe marcar.
        const float in_ms = listen == static_cast<int>(Listen::Ataques) ? 2.0f : 30.0f;
        const float c = blockCoeff(std::fabs(goal) > std::fabs(st) ? in_ms : f.timeMs.load(std::memory_order_relaxed),
                                   frames, sampleRate_);
        st += c * (goal - st);
        if (dst < 0) continue;
        float& md = dst == kDestMaster ? modMasterDb : modDb[static_cast<std::size_t>(dst)];
        float& mb = dst == kDestMaster ? modMasterBright : modBright[static_cast<std::size_t>(dst)];
        if (tgt == Target::Ganho)
            md += st < 0.0f ? st * 24.0f : st * 12.0f;
        else
            mb -= st;
    }

    // 3. Cada stem: ganho + pan + brilho, rampa por bloco, soma na rota.
    const float inv = 1.0f / static_cast<float>(frames);
    double mPeak = 0.0;
    for (int s = 0; s < ns; ++s) {
        const auto si = static_cast<std::size_t>(s);
        auto& p = strips_[si];
        infGainDb_[si] = clampf(modDb[si], -48.0f, 12.0f);
        infBright_[si] = clampf(modBright[si], 0.0f, 1.0f);
        const float g = audible[si] ? dbToGain(clampf(p.gainDb.load(std::memory_order_relaxed), -90.0f, 12.0f) + infGainDb_[si]) : 0.0f;
        const float pan = clampf(p.pan.load(std::memory_order_relaxed), -1.0f, 1.0f);
        const float ang = (pan + 1.0f) * 0.25f * 3.14159265f;
        const float tL = g * std::cos(ang) * 1.41421356f;
        const float tR = g * std::sin(ang) * 1.41421356f;
        const float dL = (tL - curGainL_[si]) * inv;
        const float dR = (tR - curGainR_[si]) * inv;
        // Passa-baixa de 1ª ordem: 18 kHz (aberto) a 250 Hz (fechado).
        const float bright = infBright_[si];
        const float fc = 18000.0f * std::pow(250.0f / 18000.0f, bright);
        const float a = 1.0f - std::exp(-2.0f * 3.14159265f * fc / static_cast<float>(sampleRate_));

        const int route = p.route.load(std::memory_order_relaxed);
        float* outL = (route == RouteMaster || route == RouteAmbos) ? masterL : route == RouteFones ? fonesL : nullptr;
        float* outR = (route == RouteMaster || route == RouteAmbos) ? masterR : route == RouteFones ? fonesR : nullptr;
        float* foL = route == RouteAmbos ? fonesL : nullptr;
        float* foR = route == RouteAmbos ? fonesR : nullptr;
        float* pL = post ? post[s * 2] : nullptr;
        float* pR = post ? post[s * 2 + 1] : nullptr;
        const float* l = in[s * 2];
        const float* r = in[s * 2 + 1] ? in[s * 2 + 1] : in[s * 2];

        float gl = curGainL_[si], gr = curGainR_[si];
        float& zL = lpState_[si * 2];
        float& zR = lpState_[si * 2 + 1];
        float peak = 0.0f;
        double sum = 0.0;
        for (int n = 0; n < frames; ++n) {
            float xl = l ? l[n] : 0.0f;
            float xr = r ? r[n] : 0.0f;
            zL += a * (xl - zL);
            zR += a * (xr - zR);
            if (bright > 1.0e-4f) { xl = zL; xr = zR; }
            gl += dL; gr += dR;
            const float yl = xl * gl, yr = xr * gr;
            if (outL) { outL[n] += yl; outR[n] += yr; }
            if (foL) { foL[n] += yl; foR[n] += yr; }
            if (pL) pL[n] = yl;
            if (pR) pR[n] = yr;
            peak = std::max(peak, std::max(std::fabs(yl), std::fabs(yr)));
            sum += 0.5 * (static_cast<double>(yl) * yl + static_cast<double>(yr) * yr);
        }
        curGainL_[si] = tL; curGainR_[si] = tR;
        stripMeters_[si].peak.store(peak, std::memory_order_relaxed);
        stripMeters_[si].rms.store(static_cast<float>(std::sqrt(sum / frames)), std::memory_order_relaxed);
    }

    // 4. Master (com as influências cujo destino é o mix inteiro).
    const float tm = dbToGain(clampf(masterGainDb_.load(std::memory_order_relaxed), -90.0f, 12.0f)
                              + clampf(modMasterDb, -48.0f, 12.0f));
    const float dm = (tm - curMaster_) * inv;
    float gm = curMaster_;
    const float brightM = clampf(modMasterBright, 0.0f, 1.0f);
    const float fcM = 18000.0f * std::pow(250.0f / 18000.0f, brightM);
    const float aM = 1.0f - std::exp(-2.0f * 3.14159265f * fcM / static_cast<float>(sampleRate_));
    double sum = 0.0;
    for (int n = 0; n < frames; ++n) {
        gm += dm;
        lpMaster_[0] += aM * (masterL[n] - lpMaster_[0]);
        lpMaster_[1] += aM * (masterR[n] - lpMaster_[1]);
        if (brightM > 1.0e-4f) { masterL[n] = lpMaster_[0]; masterR[n] = lpMaster_[1]; }
        masterL[n] *= gm;
        masterR[n] *= gm;
    }
    curMaster_ = tm;
    // Proteção técnica: joelho de sobrecarga do Studio (OverloadKnee.h:
    // identidade abaixo de 0,78; arredonda a soma de muitas fontes para o
    // limitador não esmagar), depois DC, limitador true peak com antecipação,
    // teto −1,4 dBTP (LimitadorSaida.h). Os medidores leem o que de fato sai.
    for (int n = 0; n < frames; ++n) joelhoMaster_.process(masterL[n], masterR[n], masterL[n], masterR[n]);
    publicarMenor(reducaoMaster_, limMaster_.process(masterL, masterR, frames));
    for (int n = 0; n < frames; ++n) {
        mPeak = std::max(mPeak, static_cast<double>(std::max(std::fabs(masterL[n]), std::fabs(masterR[n]))));
        sum += 0.5 * (static_cast<double>(masterL[n]) * masterL[n] + static_cast<double>(masterR[n]) * masterR[n]);
    }
    masterMeter_.peak.store(static_cast<float>(mPeak), std::memory_order_relaxed);
    masterMeter_.rms.store(static_cast<float>(std::sqrt(sum / frames)), std::memory_order_relaxed);

    // 5. Fones: o que foi para lá, com nível próprio.
    if (fonesL && fonesR) {
        const float tf = dbToGain(clampf(fonesGainDb_.load(std::memory_order_relaxed), -90.0f, 12.0f));
        const float df = (tf - curFones_) * inv;
        float gf = curFones_;
        double fs = 0.0;
        float fp = 0.0f;
        for (int n = 0; n < frames; ++n) {
            gf += df;
            fonesL[n] *= gf;
            fonesR[n] *= gf;
        }
        for (int n = 0; n < frames; ++n) joelhoFones_.process(fonesL[n], fonesR[n], fonesL[n], fonesR[n]);
        publicarMenor(reducaoFones_, limFones_.process(fonesL, fonesR, frames));
        for (int n = 0; n < frames; ++n) {
            fp = std::max(fp, std::max(std::fabs(fonesL[n]), std::fabs(fonesR[n])));
            fs += 0.5 * (static_cast<double>(fonesL[n]) * fonesL[n] + static_cast<double>(fonesR[n]) * fonesR[n]);
        }
        curFones_ = tf;
        fonesMeter_.peak.store(fp, std::memory_order_relaxed);
        fonesMeter_.rms.store(static_cast<float>(std::sqrt(fs / frames)), std::memory_order_relaxed);
    }
}

} // namespace maestruc
