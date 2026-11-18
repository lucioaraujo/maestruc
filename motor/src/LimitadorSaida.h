// Maestruc — proteção técnica de saída (master e fones), 16/11/2026.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
//
// Pedido do autor: "pode fazer o limitador no master" — com "excelência de
// qualidade sonora", "sobretudo considerando a quantidade de áudios que o
// maestruc deve controlar" (até 32 faixas somadas).
//
// Proveniência: cópia adaptada de
// RASGO_SYNTH_ASTERISMUS/include/rasgo/performance/PerformanceMasterStage.hpp
// (repositório próprio do Asterismus, commit b94f547, SHA-256 a769d5a37cc7a68a…)
// e do PerformanceTruePeakDetector de PerformanceLoudnessMeter.hpp no mesmo
// repositório. Aquele master já é técnica adaptada de
// ANTITOTEM/src/core/OutputStage.h (commit 645e5ad, AGPL-3.0-or-later) e o
// detector de true peak 4× vem do Navalha 2 (TruePeakDetector, commit
// 34f8fd7, GPL-3.0-or-later, ITU-R BS.1770). Mesma autoria (Lúcio de Araújo
// / RASGO); a combinação GPLv3 + AGPLv3 é permitida (GPLv3 §13).
//
// Adaptações para o Maestruc:
//  - sem ganho de compensação: o Maestruc só protege a soma, nunca a
//    aumenta (o nível é de quem mixa); o MASTER/FONES continuam no Mixer;
//  - precisão dupla dentro, amostras float fora; uma instância por saída;
//  - redução (dB) publicada para a tela e os ensaios;
//  - DC fora em 5 Hz (na família, 18 Hz): o master soma instrumentos que já
//    tiram o próprio DC, e em 18 Hz o filtro de 1ª ordem já tirava 0,11 dB a
//    110 Hz e ~0,5 dB a 50 Hz; em 5 Hz fica < 0,01 dB acima de 50 Hz.
//
// Cadeia: DC fora (5 Hz) → limitador com antecipação de 2 ms e detecção de
// true peak 4× → joelho suave até o teto 0,85 (≈ −1,4 dBTP), o teto comum da
// família. Abaixo de 0,72 (≈ −2,9 dBFS) nada muda no som.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace maestruc {

// Pico entre amostras (true peak) por superamostragem 4× (FIR polifásico,
// sinc com janela Blackman-Harris, 16 coeficientes por fase).
class TruePeak4x {
public:
    static constexpr std::size_t kOver = 4, kTaps = 16;

    TruePeak4x() noexcept {
        constexpr double pi = 3.14159265358979323846;
        constexpr auto total = kOver * kTaps;
        constexpr double centre = (static_cast<double>(total) - 1.0) * 0.5;
        std::array<double, total> proto {};
        for (std::size_t t = 0; t < total; ++t) {
            const double x = (static_cast<double>(t) - centre) / static_cast<double>(kOver);
            const double sinc = std::fabs(x) < 1.0e-12 ? 1.0 : std::sin(pi * x) / (pi * x);
            const double ph = 2.0 * pi * static_cast<double>(t) / static_cast<double>(total - 1);
            proto[t] = sinc * (0.35875 - 0.48829 * std::cos(ph) + 0.14128 * std::cos(2.0 * ph) - 0.01168 * std::cos(3.0 * ph));
        }
        for (std::size_t b = 0; b < kOver; ++b) {
            double soma = 0.0;
            for (std::size_t t = 0; t < kTaps; ++t) soma += proto[b + t * kOver];
            for (std::size_t t = 0; t < kTaps; ++t) coef_[b][t] = proto[b + t * kOver] / soma;
        }
        reset();
    }
    void reset() noexcept { hist_.fill(0.0); w_ = 0; }
    double process(double x) noexcept {
        hist_[w_] = std::isfinite(x) ? x : 0.0;
        w_ = (w_ + 1) % kTaps;
        double pico = 0.0;
        for (const auto& ramo : coef_) {
            double v = 0.0;
            auto r = w_;
            for (std::size_t t = 0; t < kTaps; ++t) {
                r = r == 0 ? kTaps - 1 : r - 1;
                v += ramo[t] * hist_[r];
            }
            pico = std::max(pico, std::fabs(v));
        }
        return pico;
    }

private:
    std::array<std::array<double, kTaps>, kOver> coef_ {};
    std::array<double, kTaps> hist_ {};
    std::size_t w_ = 0;
};

class LimitadorSaida {
public:
    static constexpr double kTeto = 0.85;     // ≈ −1,4 dBTP antes da conversão
    static constexpr double kJoelho = 0.72;
    static constexpr std::size_t kMaxAntecipa = 1024;

    // Antes do áudio começar.
    void prepare(double taxa) noexcept {
        taxa_ = std::max(8000.0, taxa);
        dc_ = std::exp(-2.0 * 3.14159265358979323846 * 5.0 / taxa_);
        antecipa_ = std::clamp<std::size_t>(static_cast<std::size_t>(taxa_ * 0.002), 1, kMaxAntecipa);
        ataque_ = coef(0.35);
        soltura_ = coef(90.0);
        reset();
    }
    void reset() noexcept {
        atrasoL_.fill(0.0); atrasoR_.fill(0.0); pos_ = 0;
        inL_ = inR_ = outL_ = outR_ = 0.0;
        env_ = 0.0; ganho_ = 1.0;
        tp_[0].reset(); tp_[1].reset();
    }

    // Thread de áudio: no lugar, sem alocação.
    // Devolve a maior redução do bloco, em dB (≤ 0).
    float process(float* l, float* r, int quadros) noexcept {
        double menor = ganho_;
        for (int n = 0; n < quadros; ++n) {
            const double a = semDc(finito(l[n]), inL_, outL_);
            const double b = semDc(finito(r[n]), inR_, outR_);
            const double pico = std::max(tp_[0].process(a), tp_[1].process(b));
            env_ = pico > env_ ? pico : env_ + (pico - env_) * soltura_;
            const double alvo = env_ > kTeto ? kTeto / env_ : 1.0;
            ganho_ += (alvo - ganho_) * (alvo < ganho_ ? ataque_ : soltura_);
            menor = std::min(menor, ganho_);
            l[n] = static_cast<float>(joelho(atrasoL_[pos_] * ganho_));
            r[n] = static_cast<float>(joelho(atrasoR_[pos_] * ganho_));
            atrasoL_[pos_] = a;
            atrasoR_[pos_] = b;
            pos_ = (pos_ + 1) % antecipa_;
        }
        return menor >= 1.0 ? 0.0f : static_cast<float>(20.0 * std::log10(std::max(1.0e-6, menor)));
    }
    int latenciaQuadros() const noexcept { return static_cast<int>(antecipa_); }

private:
    static double finito(float v) noexcept { return std::isfinite(v) ? static_cast<double>(v) : 0.0; }
    double coef(double ms) const noexcept { return 1.0 - std::exp(-1.0 / std::max(1.0, ms * taxa_ / 1000.0)); }
    double semDc(double v, double& antIn, double& antOut) const noexcept {
        const double y = v - antIn + dc_ * antOut;
        antIn = v; antOut = y;
        return y;
    }
    static double joelho(double v) noexcept {
        const double m = std::fabs(v);
        if (m <= kJoelho) return v;
        const double c = kJoelho + (kTeto - kJoelho) * (1.0 - std::exp(-(m - kJoelho) / (kTeto - kJoelho)));
        return std::copysign(c, v);
    }

    double taxa_ = 48000.0, dc_ = 0.997;
    std::size_t antecipa_ = 96, pos_ = 0;
    double ataque_ = 0.5, soltura_ = 0.01;
    double inL_ = 0.0, inR_ = 0.0, outL_ = 0.0, outR_ = 0.0;
    double env_ = 0.0, ganho_ = 1.0;
    std::array<double, kMaxAntecipa> atrasoL_ {}, atrasoR_ {};
    std::array<TruePeak4x, 2> tp_ {};
};

} // namespace maestruc
