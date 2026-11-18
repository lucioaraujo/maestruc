// Maestruc — motor de áudio: mixer de stems com rotas e influências "no som".
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
//
// Tudo aqui roda na thread de áudio: sem alocação, sem trava, sem E/S depois
// de prepare(). Parâmetros chegam por atômicos (a janela/CLI escreve, o áudio
// lê) e são suavizados por bloco para não estalar.
#pragma once

#include <array>
#include "LimitadorSaida.h"
#include "OverloadKnee.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace maestruc {

constexpr int kMaxStrips = 32;
constexpr int kMaxInfluences = 32;

// Saídas físicas/lógicas para onde um stem pode ir.
enum Route : int { RouteMaster = 0, RouteFones = 1, RouteNenhuma = 2, RouteAmbos = 3 };   // Ambos: mix e fones

// Destino de influência que é o mix inteiro (o Master), não um stem.
constexpr int kDestMaster = kMaxStrips;

struct StripParams {
    std::atomic<float> gainDb { 0.0f };
    std::atomic<float> pan { 0.0f };        // -1 esquerda … +1 direita
    std::atomic<bool> mute { false };
    std::atomic<bool> solo { false };
    std::atomic<int> route { RouteMaster };
};

// Influência "no som": a energia de um stem (fonte) mexe num parâmetro de
// outro (destino), processado pelo próprio Maestruc. Não precisa de código
// nos instrumentos.
enum class Target : int { Ganho = 0, Brilho = 1 };

// O que o Maestruc escuta na fonte, tirado do próprio áudio do stem (sem
// código no instrumento). Todos os valores vão de 0 a 1.
//   Energia:   o volume que se ouve (envoltória RMS).
//   Ataques:   um pulso a cada golpe/início de nota, que cai em ~80 ms.
//   Densidade: quantos ataques por segundo (janela de ~2 s; 8/s = 1).
//   Silencio:  sobe quando a fonte cala (~0,4 s) e cai logo que ela volta.
enum class Listen : int { Energia = 0, Ataques = 1, Densidade = 2, Silencio = 3 };
constexpr int kNumListen = 4;

struct InfluenceParams {
    std::atomic<bool> active { false };
    std::atomic<int> source { 0 };
    std::atomic<int> dest { 0 };
    std::atomic<int> target { static_cast<int>(Target::Ganho) };
    std::atomic<int> listen { static_cast<int>(Listen::Energia) };
    // amount: -1 … +1. Ganho: -1 abaixa até -24 dB quando a fonte está
    // cheia (ducking), +1 sobe até +12 dB. Brilho: -1 fecha o filtro do
    // destino, +1 abre (se já houver fechamento por outra influência).
    std::atomic<float> amount { 0.0f };
    std::atomic<float> timeMs { 300.0f };  // tempo de resposta (soltura)
};

struct Meter {
    std::atomic<float> peak { 0.0f };
    std::atomic<float> rms { 0.0f };
};

class Mixer {
public:
    // Antes do áudio começar (pode alocar).
    // `activeStrips` < 0: todos os preparados ficam ativos.
    void prepare(double sampleRate, int maxBlock, int numStrips, int activeStrips = -1);
    int numStrips() const { return numStrips_.load(std::memory_order_acquire); }
    // Thread de controle: liga mais stems até o máximo preparado (só cresce;
    // o estado de cada stem novo já foi zerado em prepare).
    void setActiveStrips(int n);
    double sampleRate() const { return sampleRate_; }

    StripParams& strip(int i) { return strips_[static_cast<std::size_t>(i)]; }
    InfluenceParams& influence(int i) { return influences_[static_cast<std::size_t>(i)]; }
    std::atomic<float>& masterGainDb() { return masterGainDb_; }
    std::atomic<float>& fonesGainDb() { return fonesGainDb_; }
    const Meter& fonesMeter() const { return fonesMeter_; }
    const Meter& stripMeter(int i) const { return stripMeters_[static_cast<std::size_t>(i)]; }
    const Meter& masterMeter() const { return masterMeter_; }
    // Proteção de saída (16/11/2026): a maior redução do limitador desde a
    // última leitura, em dB (≤ 0); ler zera. Master e fones.
    float tomarReducaoMasterDb() { return reducaoMaster_.exchange(0.0f, std::memory_order_relaxed); }
    float tomarReducaoFonesDb() { return reducaoFones_.exchange(0.0f, std::memory_order_relaxed); }
    int latenciaProtecaoQuadros() const { return limMaster_.latenciaQuadros(); }
    // Envoltória atual de cada stem (0…1), lida pelas influências e pela tela.
    float envelope(int i) const { return envelope_[static_cast<std::size_t>(i)]; }
    // O que se escuta em cada stem (0…1), publicado por bloco para a tela.
    float listened(int i, Listen what) const {
        return listened_[static_cast<std::size_t>(i) * kNumListen + static_cast<std::size_t>(what)].load(std::memory_order_relaxed);
    }
    // Ataques detectados desde o começo (para a tela e os ensaios).
    std::uint32_t onsetCount(int i) const { return onsetCount_[static_cast<std::size_t>(i)].load(std::memory_order_relaxed); }

    // in[strip*2 + c]: entradas estéreo por stem (nullptr = silêncio).
    // master/fones: saídas estéreo (L, R). post[strip*2 + c] opcional: o stem
    // depois de ganho/mudo/influência, para a gravação por pista.
    void process(const float* const* in, float* masterL, float* masterR,
                 float* fonesL, float* fonesR, float* const* post, int frames);

private:
    double sampleRate_ = 48000.0;
    int maxBlock_ = 0;
    int preparedStrips_ = 0;
    std::atomic<int> numStrips_ { 0 };
    std::array<StripParams, kMaxStrips> strips_ {};
    std::array<InfluenceParams, kMaxInfluences> influences_ {};
    std::array<Meter, kMaxStrips> stripMeters_ {};
    Meter masterMeter_ {};
    std::atomic<float> masterGainDb_ { 0.0f };
    std::atomic<float> fonesGainDb_ { 0.0f };
    Meter fonesMeter_ {};
    float curFones_ = 1.0f;
    std::array<float, 2> lpMaster_ {};

    // Estado da thread de áudio.
    std::array<float, kMaxStrips> envelope_ {};
    // Escuta relativa da energia (18/11/2026): a referência de cada stem é o
    // pico recente da própria envoltória, que solta em ~10 s.
    std::array<float, kMaxStrips> energyRef_ {};
    // Escuta: detector de ataques por amostra e o que sai dele.
    struct Ear {
        float prev = 0.0f;      // amostra anterior (derivada = realça o golpe)
        float fast = 0.0f;      // pico rápido da derivada
        float slow = 0.0f;      // média lenta do pico rápido
        int refractory = 0;     // amostras até aceitar outro ataque
        float pulse = 0.0f;     // 1 no ataque, cai em ~80 ms
        float density = 0.0f;   // ataques acumulados com esquecimento de ~2 s
        float silence = 0.0f;   // 0 soando … 1 calado
    };
    std::array<Ear, kMaxStrips> ears_ {};
    std::array<std::atomic<float>, kMaxStrips * kNumListen> listened_ {};
    std::array<std::atomic<std::uint32_t>, kMaxStrips> onsetCount_ {};
    std::array<float, kMaxStrips> curGainL_ {};
    std::array<float, kMaxStrips> curGainR_ {};
    std::array<float, kMaxStrips> infGainDb_ {};      // modulação de ganho, suavizada
    std::array<float, kMaxStrips> infBright_ {};      // 0 = aberto … 1 = fechado
    std::array<float, kMaxStrips * 2> lpState_ {};
    std::array<float, kMaxInfluences> infState_ {};  // cada influência suavizada
    float curMaster_ = 1.0f;
    // joelho do Studio antes do limitador (16/11/2026, OK do autor): muitas
    // fontes somadas; identidade abaixo de 0,78
    OverloadKnee joelhoMaster_, joelhoFones_;
    LimitadorSaida limMaster_, limFones_;
    std::atomic<float> reducaoMaster_ { 0.0f }, reducaoFones_ { 0.0f };
    std::vector<float> scratch_;                      // 2 * maxBlock
};

float dbToGain(float db);

} // namespace maestruc
