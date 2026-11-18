// Maestruc — host ao vivo: cliente JACK (via PipeWire) com uma entrada
// estéreo por stem, saídas master e fones, mixer e gravação por pista.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
//
// Portas: <stem>_L/_R (entradas), master_L/_R e fones_L/_R (saídas). Nada é
// ligado sozinho: quem liga apps aos stems e o master às caixas é quem usa
// (connectSource / connectMasterToPlayback).
//
// Dois modos (15/11/2026):
//   - JACK (Linux, PipeWire): open(); o som dos apps chega por portas JACK
//     ou pela ponte de som.
//   - portátil (Linux, Windows, macOS): openPortable(); não há JACK; o som
//     dos apps chega só pela ponte de som (receptor/PonteDeSom.h) e quem tem
//     a placa de som (a janela, pelo JUCE) chama processPortable() no
//     callback dela.
// Nos dois, um app com ponte entra com attachBridge(): cada par estéreo da
// ponte (a mistura e os canais internos) vira um stem.
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "Jack.h"
#include "Mixer.h"
#include "PonteDeSom.h"
#include "Recorder.h"

namespace maestruc {

class LiveHost {
public:
    ~LiveHost() { close(); if (entrega_.joinable()) entrega_.join(); }

    // `capacity`: quantos stems a sessão pode ter (apps que entram depois,
    // por addStem); 0 = só os da lista.
    bool open(const std::string& clientName, const std::vector<std::string>& stems, std::string& error,
              int capacity = 0);
    // Thread de controle: mais um stem durante a sessão (o app que acabou de
    // abrir). Devolve o índice (o existente, se o nome já estiver), ou -1.
    int addStem(const std::string& name, std::string& error);
    int capacity() const { return capacity_; }
    void close();
    bool running() const { return (client_ != nullptr || portable_) && !shutdown_.load(); }

    // Modo portátil: sem JACK. `maxBlock`: o maior bloco que a placa pode
    // pedir. Depois, processPortable() no callback de áudio de quem tem a placa.
    bool openPortable(int sampleRate, int maxBlock, int capacity, std::string& error);
    bool portable() const { return portable_; }
    // Callback de áudio (modo portátil): out[0..1] master, out[2..3] fones
    // (nullptr = não há). Sem trava, sem alocação.
    void processPortable(float* const* out, int frames);

    // Ponte de som de um app (thread de controle). Cada par estéreo vira um
    // stem: o 1º (a mistura) com o nome `stem`, os outros "stem/nome do par".
    // Pede ao app que cale a própria saída. Devolve os stems criados (vazio =
    // falhou, com `error`).
    // `maxPairs`: quantos pares viram stem (0 = todos; 1 = só a mistura).
    std::vector<int> attachBridge(const std::string& stem, const std::string& path, std::string& error,
                                  int maxPairs = 0);
    // Solta a ponte do app (o app volta a tocar sozinho); os stems ficam,
    // calados, até a sessão fechar.
    void detachBridge(const std::string& stem);
    bool hasBridge(const std::string& stem) const;
    // Para a tela e os ensaios: estado da ponte do stem (o 1º par).
    struct BridgeStatus { bool open = false; bool waiting = true; std::uint32_t gaps = 0, jumps = 0, lost = 0; double ratio = 1.0; };
    BridgeStatus bridgeStatus(const std::string& stem) const;

    Mixer& mixer() { return mixer_; }
    int stemIndex(const std::string& name) const;
    const std::vector<std::string>& stems() const { return stems_; }
    int sampleRate() const { return sampleRate_; }
    std::string fullPortName(const std::string& shortName) const;

    // Liga as portas de saída cujo nome contém `pattern` (expressão do JACK)
    // às entradas do stem: a 1ª em _L, a 2ª em _R (uma só: nas duas).
    // Com takeFromPlayback, tira essas portas das saídas físicas (o som do
    // app passa a sair só pelo Maestruc); close() devolve as ligações.
    // Devolve as ligações feitas.
    std::vector<std::string> connectSource(const std::string& stem, const std::string& pattern, std::string& error,
                                           bool takeFromPlayback = false);
    // Thread de controle, de tempos em tempos: se o gerenciador de sessão
    // religar um app tomado às caixas, desliga de novo. Devolve quantas.
    int enforceTaken();
    // Religa às caixas o que foi tirado (chamado por close()).
    void restoreTaken();
    std::size_t takenCount() const { return taken_.size(); }
    bool connectMasterToPlayback(std::string& error);
    // Aparelhos de saída físicos (nome do aparelho, como o PipeWire mostra).
    std::vector<std::string> playbackDevices() const;
    // Liga o barramento (0 master, 1 fones) a um aparelho; "" = a nenhum.
    // Desfaz a ligação anterior do barramento.
    bool routeBus(int bus, const std::string& device, std::string& error);
    std::vector<std::string> outputPorts(const std::string& pattern = "") const;

    // Gravação por pista (cada stem pós-fader e o master) — thread de controle.
    // `stemOn` (opcional): quais stems gravar; `masterOn`: gravar o master.
    bool startRecording(const std::string& dir, std::string& error,
                        const std::vector<bool>* stemOn = nullptr, bool masterOn = true);
    bool stopRecording();
    bool recording() const { return recorder_.load() != nullptr; }
    std::uint64_t droppedFrames() const { return lastDropped_; }

    std::uint64_t cycles() const { return cycles_.load(); }
    std::uint64_t oversizedCycles() const { return oversized_.load(); }

private:
    struct Bridge {
        std::string stem;
        rasgo::maestruc::LeitorDeSom reader;
        std::vector<int> stems;                  // stem de cada par
        std::vector<std::vector<float>> buf;     // um por canal, maxBlock
        std::vector<float*> ptr;
    };
    void processCore(float* const* out, int frames, bool fromJack, jack::nframes_t nframes);
    static int processThunk(jack::nframes_t n, void* self);
    static void shutdownThunk(void* self);
    int process(jack::nframes_t n);
    void waitCycles(int count);
    std::vector<std::string> physicalPlayback() const;
    std::vector<std::string> connectionsOf(const std::string& port) const;

    const jack::Api* api_ = nullptr;
    jack::client_t* client_ = nullptr;
    std::vector<std::string> stems_;
    std::vector<jack::port_t*> inPorts_;           // stem*2 + c (capacidade fixa)
    int capacity_ = 0;
    std::atomic<int> activeStems_ { 0 };
    std::array<int, kMaxStrips + 1> recMap_ {};    // stem → pista (-1 = não grava); [kMaxStrips] = master
    int recStems_ = 0;
    jack::port_t* outPorts_[4] {};                 // master L/R, fones L/R
    Mixer mixer_;
    int sampleRate_ = 48000;
    int maxBlock_ = 0;
    std::vector<const float*> inPtr_;
    std::vector<std::vector<float>> post_;
    std::vector<float*> postPtr_;
    std::atomic<Recorder*> recorder_ { nullptr };
    std::unique_ptr<Recorder> recorderOwner_;
    std::uint64_t lastDropped_ = 0;
    // versão de entrega do Master (16/11/2026, VersaoEntrega.h): a pasta da
    // gravação, se o master foi gravado, e o fio que escreve master_streaming.wav
    std::string recDir_;
    bool recMaster_ = false;
    std::thread entrega_;
    std::atomic<std::uint64_t> cycles_ { 0 };
    std::atomic<std::uint64_t> oversized_ { 0 };
    std::atomic<bool> shutdown_ { false };
    bool portable_ = false;
    std::array<std::atomic<Bridge*>, kMaxStrips> bridges_ {};   // lidos pela thread de áudio
    std::vector<std::unique_ptr<Bridge>> bridgeOwners_;
    std::vector<std::vector<float>> silence_;                    // entradas mudas (modo portátil)
    std::vector<std::string> takenSources_;                     // portas de apps tomadas
    std::vector<std::pair<std::string, std::string>> taken_;   // (app, caixa) desligadas
};

} // namespace maestruc
