// Maestruc — ensaios do motor de áudio (mixer, influências, gravador, WAV).
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <thread>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "Mixer.h"
#include "Recorder.h"
#include "Wav.h"
#include "VersaoEntrega.h"
#include "saida/TakeAnalysis.h"

using namespace maestruc;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "OK  " : "FALHA", what);
    if (!ok) ++failures;
}

constexpr double kSr = 48000.0;
constexpr int kBlock = 240;   // janelas de 0,25 s = 50 blocos exatos

// Rede de ensaio: n stems; cada um é seno de frequência própria, com
// amplitude dada por uma função do tempo. Devolve RMS por stem (pós-fader)
// e do master em janelas de `win` segundos.
struct Run {
    std::vector<std::vector<double>> stripRms;   // [stem][janela]
    std::vector<double> masterRms, fonesRms;
};

template <typename Amp>
Run run(Mixer& m, int n, double seconds, double win, Amp amp, const std::vector<double>& freq) {
    Run r;
    r.stripRms.assign(static_cast<std::size_t>(n), {});
    std::vector<std::vector<float>> in(static_cast<std::size_t>(n) * 2, std::vector<float>(kBlock));
    std::vector<std::vector<float>> post(static_cast<std::size_t>(n) * 2, std::vector<float>(kBlock));
    std::vector<const float*> ip;
    std::vector<float*> pp;
    for (auto& b : in) ip.push_back(b.data());
    for (auto& b : post) pp.push_back(b.data());
    std::vector<float> mL(kBlock), mR(kBlock), fL(kBlock), fR(kBlock);
    const long total = static_cast<long>(seconds * kSr);
    const long winLen = static_cast<long>(win * kSr);
    std::vector<double> acc(static_cast<std::size_t>(n) + 2, 0.0);
    long inWin = 0;
    for (long pos = 0; pos < total; pos += kBlock) {
        for (int s = 0; s < n; ++s)
            for (int k = 0; k < kBlock; ++k) {
                const double t = static_cast<double>(pos + k) / kSr;
                const auto v = static_cast<float>(amp(s, t) * std::sin(2.0 * M_PI * freq[static_cast<std::size_t>(s)] * t));
                in[static_cast<std::size_t>(s) * 2][k] = v;
                in[static_cast<std::size_t>(s) * 2 + 1][k] = v;
            }
        m.process(ip.data(), mL.data(), mR.data(), fL.data(), fR.data(), pp.data(), kBlock);
        for (int k = 0; k < kBlock; ++k) {
            for (int s = 0; s < n; ++s) acc[static_cast<std::size_t>(s)] += pp[static_cast<std::size_t>(s) * 2][k] * pp[static_cast<std::size_t>(s) * 2][k];
            acc[static_cast<std::size_t>(n)] += mL[k] * mL[k];
            acc[static_cast<std::size_t>(n) + 1] += fL[k] * fL[k];
        }
        inWin += kBlock;
        if (inWin >= winLen) {
            for (int s = 0; s < n; ++s) r.stripRms[static_cast<std::size_t>(s)].push_back(std::sqrt(acc[static_cast<std::size_t>(s)] / inWin));
            r.masterRms.push_back(std::sqrt(acc[static_cast<std::size_t>(n)] / inWin));
            r.fonesRms.push_back(std::sqrt(acc[static_cast<std::size_t>(n) + 1] / inWin));
            std::fill(acc.begin(), acc.end(), 0.0);
            inWin = 0;
        }
    }
    return r;
}

double db(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

void testGainMuteSoloRoute() {
    const std::vector<double> f { 220.0, 330.0 };
    auto amp = [](int, double) { return 0.25; };
    {
        Mixer m; m.prepare(kSr, kBlock, 2);
        m.strip(0).gainDb = -6.0f;
        auto r = run(m, 2, 1.0, 0.5, amp, f);
        check(std::fabs(db(r.stripRms[0].back()) - db(r.stripRms[1].back()) + 6.0) < 0.2, "ganho: -6 dB no stem é -6 dB");
        check(std::fabs(r.stripRms[1].back() - 0.25 / std::sqrt(2.0)) < 0.002, "pan central conserva o nível em cada lado");
    }
    {
        Mixer m; m.prepare(kSr, kBlock, 2);
        m.strip(0).mute = true;
        auto r = run(m, 2, 1.0, 0.5, amp, f);
        check(r.stripRms[0].back() < 1e-6, "mudo silencia o stem");
        check(std::fabs(r.masterRms.back() - r.stripRms[1].back()) < 0.002, "master = o stem que sobra");
    }
    {
        Mixer m; m.prepare(kSr, kBlock, 2);
        m.strip(1).solo = true;
        auto r = run(m, 2, 1.0, 0.5, amp, f);
        check(r.stripRms[0].back() < 1e-6 && r.stripRms[1].back() > 0.1, "solo cala os outros");
    }
    {
        Mixer m; m.prepare(kSr, kBlock, 2);
        m.strip(1).route = RouteFones;
        auto r = run(m, 2, 1.0, 0.5, amp, f);
        check(std::fabs(r.fonesRms.back() - r.stripRms[1].back()) < 0.002
              && std::fabs(r.masterRms.back() - r.stripRms[0].back()) < 0.002, "rota fones tira o stem do master");
    }
}

void testFonesEMaster() {
    const std::vector<double> f { 220.0, 330.0 };
    auto amp = [](int, double) { return 0.25; };
    {
        Mixer m; m.prepare(kSr, kBlock, 2);
        m.strip(0).route = RouteAmbos;
        m.fonesGainDb() = -6.0f;
        auto r = run(m, 2, 1.0, 0.5, amp, f);
        check(std::fabs(db(r.fonesRms.back()) - db(r.stripRms[0].back()) + 6.0) < 0.3, "mix + fones: o stem vai aos fones com o nível dos fones (-6 dB)");
        check(r.masterRms.back() > r.stripRms[0].back(), "mix + fones: o stem continua no master");
    }
    {
        // Stem 0 toca de 1 s a 3 s e abaixa o mix inteiro; o stem 1 só vai ao master.
        auto amp2 = [](int s, double t) { return s == 1 ? 0.2 : (t >= 1.0 && t < 3.0 ? 0.5 : 0.0); };
        Mixer m; m.prepare(kSr, kBlock, 2);
        m.strip(0).route = RouteNenhuma;
        auto& inf = m.influence(0);
        inf.source = 0; inf.dest = kDestMaster; inf.amount = -1.0f; inf.timeMs = 300.0f; inf.active = true;
        auto r = run(m, 2, 5.0, 0.25, amp2, { 110.0, 440.0 });
        const double antes = db(r.masterRms[3]), durante = db(r.masterRms[10]), depois = db(r.masterRms[19]);
        std::printf("      master: antes %.1f, durante %.1f, depois %.1f dB\n", antes, durante, depois);
        check(antes - durante > 10.0 && std::fabs(depois - antes) < 0.5, "influência sobre o Master abaixa o mix e o devolve");
    }
}

// Escuta: alimenta o mixer amostra a amostra e devolve, a cada bloco, o que
// ele escutou em cada stem e o RMS de cada stem pós-mixer.
struct Escutado {
    std::vector<std::vector<std::array<float, kNumListen>>> listen;  // [stem][bloco]
    std::vector<std::vector<double>> rms;                             // [stem][bloco]
};

template <typename Gen>
Escutado escutar(Mixer& m, int n, double seconds, Gen gen) {
    Escutado e;
    e.listen.assign(static_cast<std::size_t>(n), {});
    e.rms.assign(static_cast<std::size_t>(n), {});
    std::vector<std::vector<float>> in(static_cast<std::size_t>(n) * 2, std::vector<float>(kBlock));
    std::vector<std::vector<float>> post(static_cast<std::size_t>(n) * 2, std::vector<float>(kBlock));
    std::vector<const float*> ip;
    std::vector<float*> pp;
    for (auto& b : in) ip.push_back(b.data());
    for (auto& b : post) pp.push_back(b.data());
    std::vector<float> mL(kBlock), mR(kBlock), fL(kBlock), fR(kBlock);
    const long total = static_cast<long>(seconds * kSr);
    for (long pos = 0; pos < total; pos += kBlock) {
        for (int s = 0; s < n; ++s)
            for (int k = 0; k < kBlock; ++k) {
                const auto v = static_cast<float>(gen(s, pos + k));
                in[static_cast<std::size_t>(s) * 2][k] = v;
                in[static_cast<std::size_t>(s) * 2 + 1][k] = v;
            }
        m.process(ip.data(), mL.data(), mR.data(), fL.data(), fR.data(), pp.data(), kBlock);
        for (int s = 0; s < n; ++s) {
            std::array<float, kNumListen> a {};
            for (int w = 0; w < kNumListen; ++w) a[static_cast<std::size_t>(w)] = m.listened(s, static_cast<Listen>(w));
            e.listen[static_cast<std::size_t>(s)].push_back(a);
            double acc = 0.0;
            for (int k = 0; k < kBlock; ++k) acc += pp[static_cast<std::size_t>(s) * 2][k] * pp[static_cast<std::size_t>(s) * 2][k];
            e.rms[static_cast<std::size_t>(s)].push_back(std::sqrt(acc / kBlock));
        }
    }
    return e;
}

// Golpe percussivo: seno de 180 Hz com queda de 60 ms, `porSeg` golpes por segundo.
double golpe(long i, double porSeg, double amp = 0.5) {
    const double t = static_cast<double>(i) / kSr;
    const double fase = std::fmod(t * porSeg, 1.0) / porSeg;
    return amp * std::exp(-fase / 0.06) * std::sin(2.0 * M_PI * 180.0 * fase);
}

std::size_t bloco(double t) { return static_cast<std::size_t>(t * kSr / kBlock); }

void testEscuta() {
    {
        Mixer m; m.prepare(kSr, kBlock, 1);
        auto e = escutar(m, 1, 8.0, [](int, long i) { return golpe(i, 4.0); });
        const auto n = m.onsetCount(0);
        std::printf("      golpes a 4/s por 8 s: %u ataques, densidade %.2f\n", n, e.listen[0][bloco(7.99)][2]);
        check(n >= 31 && n <= 33, "ataques: conta os golpes (32)");
        check(std::fabs(e.listen[0][bloco(7.99)][2] - 0.5f) < 0.1f, "densidade: 4 golpes/s ≈ 0,5");
        float mx = 0.0f, mn = 1.0f;
        for (double t = 3.0; t < 3.25; t += kBlock / kSr) { mx = std::max(mx, e.listen[0][bloco(t)][1]); mn = std::min(mn, e.listen[0][bloco(t)][1]); }
        check(mx > 0.95f && mn < 0.15f, "ataques: pulso sobe no golpe e cai antes do próximo");
    }
    {
        Mixer m; m.prepare(kSr, kBlock, 1);
        auto e = escutar(m, 1, 8.0, [](int, long i) { return golpe(i, 10.0, 0.4); });
        std::printf("      golpes a 10/s por 8 s: %u ataques, densidade %.2f\n", m.onsetCount(0), e.listen[0][bloco(7.99)][2]);
        check(m.onsetCount(0) >= 78 && m.onsetCount(0) <= 81 && e.listen[0][bloco(7.99)][2] > 0.95f, "densidade: 10 golpes/s satura em 1");
    }
    {
        Mixer m; m.prepare(kSr, kBlock, 1);
        escutar(m, 1, 3.0, [](int, long i) { return 0.4 * std::sin(2.0 * M_PI * 220.0 * static_cast<double>(i) / kSr); });
        std::printf("      seno contínuo: %u ataque(s)\n", m.onsetCount(0));
        check(m.onsetCount(0) <= 1, "nota longa: só o começo conta como ataque");
    }
    {
        Mixer m; m.prepare(kSr, kBlock, 1);
        escutar(m, 1, 3.0, [](int, long i) { return 0.5 * std::sin(2.0 * M_PI * 60.0 * static_cast<double>(i) / kSr); });
        std::printf("      baixo de 60 Hz contínuo: %u ataque(s)\n", m.onsetCount(0));
        check(m.onsetCount(0) <= 1, "baixo grave contínuo não vira chuva de ataques");
    }
    {
        Mixer m; m.prepare(kSr, kBlock, 1);
        unsigned x = 12345u;
        escutar(m, 1, 3.0, [&x](int, long) { x = x * 1664525u + 1013904223u; return 0.2 * (static_cast<double>(x >> 8) / 8388608.0 - 1.0); });
        std::printf("      ruído contínuo: %u ataque(s)\n", m.onsetCount(0));
        check(m.onsetCount(0) <= 2, "ruído contínuo não vira chuva de ataques");
    }
    {
        Mixer m; m.prepare(kSr, kBlock, 1);
        auto e = escutar(m, 1, 3.0, [](int, long i) { return i < static_cast<long>(kSr) ? 0.3 * std::sin(2.0 * M_PI * 220.0 * static_cast<double>(i) / kSr) : 0.0; });
        const float soando = e.listen[0][bloco(0.9)][3], calado = e.listen[0][bloco(2.5)][3];
        std::printf("      silêncio: soando %.2f, calado %.2f\n", soando, calado);
        check(soando < 0.05f && calado > 0.9f, "silêncio: 0 quando soa, 1 quando cala");
    }
    {
        Mixer m; m.prepare(kSr, kBlock, 1);
        m.strip(0).mute = true;
        escutar(m, 1, 2.0, [](int, long i) { return golpe(i, 4.0); });
        check(m.onsetCount(0) == 0, "stem mudo não é escutado");
    }
    {
        // Ataques: cada golpe da fonte (2/s) abaixa o destino por pouco tempo.
        Mixer m; m.prepare(kSr, kBlock, 2);
        m.strip(0).route = RouteNenhuma;
        auto& inf = m.influence(0);
        inf.source = 0; inf.dest = 1; inf.listen = static_cast<int>(Listen::Ataques);
        inf.amount = -1.0f; inf.timeMs = 80.0f; inf.active = true;
        auto e = escutar(m, 2, 3.0, [](int s, long i) {
            return s == 0 ? golpe(i, 2.0) : 0.2 * std::sin(2.0 * M_PI * 440.0 * static_cast<double>(i) / kSr); });
        const double noGolpe = db(e.rms[1][bloco(2.01)]), entre = db(e.rms[1][bloco(2.45)]);
        std::printf("      destino: no golpe %.1f dB, entre golpes %.1f dB\n", noGolpe, entre);
        check(entre - noGolpe > 10.0, "ataques: o destino abaixa no golpe");
        check(std::fabs(entre - db(0.2 / std::sqrt(2.0))) < 1.0, "ataques: o destino volta entre os golpes");
    }
    {
        // Silêncio: o destino sobe quando a fonte cala.
        Mixer m; m.prepare(kSr, kBlock, 2);
        m.strip(0).route = RouteNenhuma;
        auto& inf = m.influence(0);
        inf.source = 0; inf.dest = 1; inf.listen = static_cast<int>(Listen::Silencio);
        inf.amount = 0.5f; inf.timeMs = 200.0f; inf.active = true;
        auto e = escutar(m, 2, 3.0, [](int s, long i) {
            const double t = static_cast<double>(i) / kSr;
            if (s == 0) return t < 1.5 ? 0.3 * std::sin(2.0 * M_PI * 220.0 * t) : 0.0;
            return 0.1 * std::sin(2.0 * M_PI * 440.0 * t); });
        const double soando = db(e.rms[1][bloco(1.2)]), calado = db(e.rms[1][bloco(2.9)]);
        std::printf("      destino: fonte soando %.1f dB, fonte calada %.1f dB\n", soando, calado);
        check(calado - soando > 5.0 && calado - soando < 6.5, "silêncio +0,5: o destino sobe ~6 dB quando a fonte cala");
    }
}

void testDucking() {
    // Stem 0 (fonte) toca de 1 s a 3 s; stem 1 (destino) toca sempre.
    const std::vector<double> f { 110.0, 440.0 };
    auto amp = [](int s, double t) { return s == 1 ? 0.2 : (t >= 1.0 && t < 3.0 ? 0.5 : 0.0); };
    Mixer m; m.prepare(kSr, kBlock, 2);
    auto& inf = m.influence(0);
    inf.source = 0; inf.dest = 1; inf.target = static_cast<int>(Target::Ganho);
    inf.amount = -1.0f; inf.timeMs = 300.0f; inf.active = true;
    auto r = run(m, 2, 5.0, 0.25, amp, f);
    const double before = db(r.stripRms[1][3]);   // 0,75–1 s
    const double during = db(r.stripRms[1][10]);  // 2,5–2,75 s
    const double after = db(r.stripRms[1][19]);   // 4,75–5 s
    std::printf("      destino: antes %.1f dB, durante %.1f dB, depois %.1f dB\n", before, during, after);
    check(before - during > 10.0, "influência de ganho -1: o destino abre espaço (> 10 dB)");
    check(std::fabs(after - before) < 0.5, "o destino volta quando a fonte para");
    // A entrada é rápida: 250 ms depois de a fonte começar já baixou > 6 dB.
    check(before - db(r.stripRms[1][5]) > 6.0, "entrada rápida (< 250 ms)");

    Mixer off; off.prepare(kSr, kBlock, 2);
    auto r0 = run(off, 2, 5.0, 0.25, amp, f);
    check(std::fabs(db(r0.stripRms[1][10]) - before) < 0.5, "sem influência o destino não muda (contrafactual)");

    Mixer muted; muted.prepare(kSr, kBlock, 2);
    muted.influence(0).source = 0; muted.influence(0).dest = 1; muted.influence(0).amount = -1.0f;
    muted.influence(0).active = true;
    muted.strip(0).mute = true;
    auto rm = run(muted, 2, 5.0, 0.25, amp, f);
    check(std::fabs(db(rm.stripRms[1][10]) - before) < 0.5, "fonte muda não influencia");
}

// Energia relativa (18/11/2026): uma fonte a -30 dBFS abre espaço quase como
// uma a -6 dBFS (antes, a fraca agia ~25 vezes menos).
void testRelativeEnergy() {
    const std::vector<double> f { 110.0, 440.0 };
    double queda[2] {};
    const double amps[2] { 0.045, 0.7 };   // ~ -30 e ~ -6 dBFS de pico
    for (int k = 0; k < 2; ++k) {
        const double a0 = amps[k];
        auto amp = [a0](int s, double t) { return s == 1 ? 0.2 : (t >= 1.0 && t < 3.0 ? a0 : 0.0); };
        Mixer m; m.prepare(kSr, kBlock, 2);
        auto& inf = m.influence(0);
        inf.source = 0; inf.dest = 1; inf.target = static_cast<int>(Target::Ganho);
        inf.amount = -0.6f; inf.timeMs = 300.0f; inf.active = true;
        auto r = run(m, 2, 5.0, 0.25, amp, f);
        queda[k] = db(r.stripRms[1][3]) - db(r.stripRms[1][10]);
    }
    std::printf("      quantidade -0,6: fonte a -30 dBFS abaixa %.1f dB, a -6 dBFS abaixa %.1f dB\n", queda[0], queda[1]);
    check(queda[0] > 8.0 && std::fabs(queda[0] - queda[1]) < 2.0, "energia relativa: fonte fraca e forte abrem espaço parecido");
}

void testBrightness() {
    const std::vector<double> f { 110.0, 9000.0 };
    auto amp = [](int s, double t) { return s == 1 ? 0.2 : (t >= 1.0 ? 0.5 : 0.0); };
    Mixer m; m.prepare(kSr, kBlock, 2);
    auto& inf = m.influence(0);
    inf.source = 0; inf.dest = 1; inf.target = static_cast<int>(Target::Brilho);
    inf.amount = -1.0f; inf.timeMs = 200.0f; inf.active = true;
    auto r = run(m, 2, 3.0, 0.25, amp, f);
    const double drop = db(r.stripRms[1][3]) - db(r.stripRms[1][10]);
    std::printf("      9 kHz no destino caiu %.1f dB\n", drop);
    check(drop > 6.0, "influência de brilho -1 abafa o destino");
}

void testRecorderAndWav() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "maestruc-test-motor";
    fs::remove_all(dir);
    Recorder rec;
    std::string err;
    const bool started = rec.start(dir.string(), { "a", "b" }, 48000, 0.5, err);
    check(started, ("gravador abre as pistas " + err).c_str());
    if (!started) return;
    const int total = 48000 * 3;
    std::vector<float> l(kBlock), r(kBlock);
    for (int pos = 0; pos < total; pos += kBlock) {
        for (int k = 0; k < kBlock; ++k) {
            l[k] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * 1000.0 * (pos + k) / 48000.0));
            r[k] = -l[k];
        }
        rec.push(0, l.data(), r.data(), kBlock);
        rec.push(1, l.data(), nullptr, kBlock);
        // ~2,7× mais rápido que o tempo real (bloco de 5,3 ms a cada ~2 ms)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(rec.stop(), "gravador fecha sem erro");
    check(rec.droppedFrames() == 0, "nenhum quadro perdido");
    WavData a, b;
    check(readWav((dir / "a.wav").string(), a, err) && readWav((dir / "b.wav").string(), b, err), "WAVs gravados são legíveis");
    std::printf("      quadros: a %zu, b %zu, gravados %llu, esperados %d\n", a.left.size(), b.left.size(),
                static_cast<unsigned long long>(rec.framesWritten(0)), total);
    check(a.left.size() == static_cast<std::size_t>(total) && b.left.size() == static_cast<std::size_t>(total), "duração exata");
    double maxErr = 0.0;
    const int frames = static_cast<int>(std::min({ a.left.size(), b.left.size(), static_cast<std::size_t>(total) }));
    for (int i = 0; i < frames; ++i) {
        const double x = 0.5 * std::sin(2.0 * M_PI * 1000.0 * i / 48000.0);
        maxErr = std::max({ maxErr, std::fabs(a.left[static_cast<std::size_t>(i)] - x),
                            std::fabs(a.right[static_cast<std::size_t>(i)] + x),
                            std::fabs(b.right[static_cast<std::size_t>(i)] - x) });
    }
    std::printf("      erro máximo %.2e (24 bits ≈ 1,2e-7)\n", maxErr);
    check(maxErr < 1e-6, "conteúdo idêntico dentro de 24 bits; mono vira L = R");
    fs::remove_all(dir);
}

void testRingOverflowCounts() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "maestruc-test-overflow";
    fs::remove_all(dir);
    Recorder rec;
    std::string err;
    if (!rec.start(dir.string(), { "x" }, 48000, 0.25, err)) { check(false, err.c_str()); return; }
    std::vector<float> big(48000 * 2, 0.1f);
    rec.push(0, big.data(), big.data(), 48000 * 2);   // 2 s de uma vez num anel de ~0,35 s
    rec.stop();
    check(rec.droppedFrames() > 0 && rec.droppedFrames() + rec.framesWritten(0) == 96000u,
          "anel cheio descarta e conta, sem travar o áudio");
    fs::remove_all(dir);
}

} // namespace

// Proteção de saída (16/11/2026, LimitadorSaida.h): abaixo do joelho o som
// passa igual (só atrasado pela antecipação); oito faixas somadas muito acima
// do teto não passam de −1,4 dBTP, nem entre amostras.
void testLimitador() {
    auto rodar = [](int faixas, double amp, double segundos, std::vector<float>& outL, float& reducao) {
        Mixer m; m.prepare(kSr, kBlock, faixas);
        const int total = static_cast<int>(segundos * kSr);
        std::vector<std::vector<float>> ent(static_cast<std::size_t>(faixas * 2), std::vector<float>(kBlock));
        std::vector<const float*> ptr(ent.size());
        std::vector<float> mL(kBlock), mR(kBlock);
        reducao = 0.0f;
        for (int pos = 0; pos < total; pos += kBlock) {
            for (int f = 0; f < faixas; ++f)
                for (int n = 0; n < kBlock; ++n) {
                    const double t = (pos + n) / kSr;
                    const float v = static_cast<float>(amp * std::sin(2.0 * M_PI * (110.0 * (f + 1) + 3.0 * f) * t));
                    ent[static_cast<std::size_t>(f * 2)][static_cast<std::size_t>(n)] = v;
                    ent[static_cast<std::size_t>(f * 2 + 1)][static_cast<std::size_t>(n)] = v;
                }
            for (std::size_t k = 0; k < ent.size(); ++k) ptr[k] = ent[k].data();
            m.process(ptr.data(), mL.data(), mR.data(), nullptr, nullptr, nullptr, kBlock);
            outL.insert(outL.end(), mL.begin(), mL.end());
            reducao = std::min(reducao, m.tomarReducaoMasterDb());
        }
    };
    {
        std::vector<float> out; float red = 0.0f;
        rodar(1, 0.3, 1.0, out, red);
        double soma = 0.0; int n = 0;
        for (std::size_t i = out.size() / 2; i < out.size(); ++i) { soma += out[i] * out[i]; ++n; }
        const double rms = std::sqrt(soma / n);
        check(red == 0.0f, "limitador: abaixo do joelho, nenhuma redução");
        std::printf("      1 faixa: rms %.4f (%.2f dB), esperado %.4f\n", rms, db(rms), 0.3 / std::sqrt(2.0));
        check(std::fabs(db(rms) - db(0.3 / std::sqrt(2.0))) < 0.05, "limitador: abaixo do joelho, o nível passa igual (< 0,05 dB)");
    }
    {
        std::vector<float> out; float red = 0.0f;
        rodar(8, 0.6, 2.0, out, red);   // soma até ~4,8 (≈ +13,6 dBFS)
        TruePeak4x tp;
        double picoTp = 0.0, picoAm = 0.0;
        for (std::size_t i = 0; i < out.size(); ++i) {
            picoAm = std::max(picoAm, static_cast<double>(std::fabs(out[i])));
            picoTp = std::max(picoTp, tp.process(out[i]));
        }
        std::printf("      8 faixas: pico %.3f, true peak %.3f (%.2f dBTP), redução máx. %.1f dB\n",
                    picoAm, picoTp, 20.0 * std::log10(picoTp), red);
        check(picoAm <= LimitadorSaida::kTeto + 1e-6, "limitador: nenhuma amostra acima do teto (0,85)");
        check(picoTp <= LimitadorSaida::kTeto * 1.01, "limitador: true peak no teto (≤ −1,3 dBTP)");
        check(red < -1.0f, "limitador: a redução aparece para a tela");
        // sem o joelho (até 16/11) esta soma levava o limitador a ~15 dB
        check(red > -8.0f, "joelho: poupa o limitador na soma de 8 faixas (< 8 dB de redução)");
    }
}

// Versão de entrega do Master (16/11/2026, saida-ref 1.1): um master.wav baixo
// sai a -14 LUFS, true peak <= -1 dBTP, mesma duração, em blocos.
void testVersaoEntrega() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "maestruc_test_entrega";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const int frames = static_cast<int>(kSr * 12);
    {
        WavWriter w;
        check(w.open((dir / "master.wav").string(), static_cast<int>(kSr)), "entrega: master.wav abre");
        std::vector<float> v(static_cast<std::size_t>(frames) * 2);
        for (int i = 0; i < frames; ++i) {
            const double t = i / kSr;
            const float x = static_cast<float>(0.04 * (std::sin(2.0 * M_PI * 220.0 * t) + 0.5 * std::sin(2.0 * M_PI * 1100.0 * t))
                                               * (0.6 + 0.4 * std::sin(2.0 * M_PI * 0.3 * t)));
            v[static_cast<std::size_t>(2 * i)] = x;
            v[static_cast<std::size_t>(2 * i + 1)] = 0.8f * x;
        }
        w.writeInterleaved(v.data(), static_cast<std::size_t>(frames));
        check(w.close(), "entrega: master.wav fecha");
    }
    std::string resumo;
    check(escreverVersaoDeEntrega((dir / "master.wav").string(), resumo), "entrega: master-streaming.wav escrito");
    WavData out;
    std::string err;
    check(readWav((dir / "master-streaming.wav").string(), out, err), "entrega: master-streaming.wav legível");
    saida::TakeAnalysis medida;
    medida.prepare(kSr);
    for (std::size_t i = 0; i < out.left.size(); ++i) {
        auto l = out.left[i], r = out.right[i];
        medida.push(l, r);
    }
    const auto m = medida.result();
    std::printf("      entrega: %.2f LUFS, %.2f dBTP, %zu quadros (esperados %d)\n", m.integratedLufs, m.truePeakDb,
                out.left.size(), frames);
    check(std::fabs(m.integratedLufs + 14.0f) <= 0.5f, "entrega: -14 LUFS (+-0,5)");
    check(m.truePeakDb <= -0.95f, "entrega: true peak <= -1 dBTP");
    check(out.left.size() == static_cast<std::size_t>(frames), "entrega: mesma duração");
    check(fs::exists(dir / "master-streaming.txt"), "entrega: relatório .txt ao lado");
    fs::remove_all(dir);
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    testGainMuteSoloRoute();
    testFonesEMaster();
    testDucking();
    testRelativeEnergy();
    testBrightness();
    testEscuta();
    testRecorderAndWav();
    testRingOverflowCounts();
    testLimitador();
    testVersaoEntrega();
    std::printf("%s: %d falha(s)\n", failures ? "FALHOU" : "tudo OK", failures);
    return failures ? 1 : 0;
}
