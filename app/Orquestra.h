// Maestruc — o lado do regente no protocolo maestruc.link/1
// (PROTOCOLO_MAESTRUC_LINK.md): registra os instrumentos que se anunciam,
// manda comandos, recebe respostas e acontecimentos. Mesma lógica do
// maestruc_link.py, dentro da janela.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
#pragma once

#include "Idiomas.h"
#include "PonteDeSom.h"   // a pasta das pontes, também a da senha

#include <juce_osc/juce_osc.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <vector>

namespace maestruc {

struct Instrumento {
    juce::String nome, versao;
    juce::StringArray caps;
    int porta = 0;
    int pid = 0;
    double visto = 0.0;          // segundos (Time::getMillisecondCounterHiRes / 1000)
    bool tocando = false;
    juce::String seed;
    float bpm = 0.0f;
    float carga = 0.0f;
    int atrasos = 0;
    juce::String ultimaResposta;
    // Ponte de som anunciada (15/11/2026): caminho, pares estéreo e taxa.
    juce::String ponte;
    int pares = 0;
    float taxa = 0.0f;
    // Mixer de dentro do app (índice -1 = master), como ele o anunciou.
    // `objetos`: os controles que o canal tem no instrumento ("nivel", "mudo",
    // "solo"); `minDb`/`maxDb`: a faixa do fader dele (16/11/2026; sem isso,
    // nivel+mudo+solo e −90…+12 dB).
    struct Canal {
        int indice = 0; juce::String nome; float nivelDb = 0.0f; bool mudo = false, solo = false;
        juce::StringArray objetos { "nivel", "mudo", "solo" };
        float minDb = -90.0f, maxDb = 12.0f;
        bool tem(const char* o) const { return objetos.contains(o); }
    };
    std::vector<Canal> mixer;
    std::vector<int> mixerRodada;   // canais vindos desde o último mixer/fim
    bool tem(const juce::String& cap) const { return caps.contains(cap); }
};

struct Registro {
    juce::int64 quandoUs = 0;
    juce::String tipo, nome, dados;
};

class Orquestra final : private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>,
                        private juce::Timer {
public:
    static constexpr int kPorta = 47900;
    static constexpr double kAusenteSegundos = 3.0;

    ~Orquestra() override { stop(); }

    bool start(juce::String& erro) {
        if (!gravarSenha()) {
            erro = juce::String(juce::CharPointer_UTF8("não gravou a senha da sessão (pasta temporária só do usuário?)"));
            return false;
        }
        socket_ = std::make_unique<juce::DatagramSocket>(false);
        if (!socket_->bindToPort(kPorta, "127.0.0.1")) {
            erro = juce::String(juce::CharPointer_UTF8("a porta 47900 está ocupada (outro Maestruc ou o maestruc_link.py aberto?)"));
            socket_.reset();
            return false;
        }
        if (!receiver_.connectToSocket(*socket_) || !sender_.connectToSocket(*socket_, "127.0.0.1", kPorta)) {
            erro = juce::String(juce::CharPointer_UTF8("não abriu o canal local"));
            socket_.reset();
            return false;
        }
        receiver_.addListener(this);
        startTimer(500);
        return true;
    }

    void stop() {
        stopTimer();
        receiver_.removeListener(this);
        receiver_.disconnect();
        sender_.disconnect();
        socket_.reset();
        apagarSenha();
    }

    // Um instrumento anunciou (ou trocou) a ponte de som.
    std::function<void(const Instrumento&)> onPonte;

    bool ligado() const { return socket_ != nullptr; }
    const std::map<int, Instrumento>& instrumentos() const { return instrumentos_; }
    const Instrumento* porNome(const juce::String& nome) const {
        for (auto& [p, i] : instrumentos_)
            if (i.nome.equalsIgnoreCase(nome)) return &i;
        return nullptr;
    }
    const juce::Array<Registro>& registro() const { return registro_; }
    int comandosEnviados() const { return comandos_; }
    std::function<void()> onChange;

    // Comandos (mensagens de saída). Para um instrumento ou para todos com a
    // capacidade; quem não tem recebe nada (o protocolo responde ok = 0).
    template <typename... Args>
    void enviar(int porta, const juce::String& endereco, Args&&... args) {
        if (!socket_) return;
        juce::OSCMessage m { juce::OSCAddressPattern("/rasgo/1/" + endereco) };
        (m.addArgument(juce::OSCArgument(std::forward<Args>(args))), ...);
        // todo comando leva a senha da sessão por último (welcome a leva
        // como argumento próprio; who não precisa)
        if (endereco != "welcome" && endereco != "who") m.addString(juce::String(senha_));
        sender_.sendToIPAddress("127.0.0.1", porta, m);
        ++comandos_;
        anotar("comando", nomeDe(porta), endereco);
    }
    template <typename... Args>
    void enviarATodos(const juce::String& cap, const juce::String& endereco, Args... args) {
        for (auto& [porta, inst] : instrumentos_)
            if (cap.isEmpty() || inst.tem(cap)) enviar(porta, endereco, args...);
    }

private:
    juce::String nomeDe(int porta) const {
        auto it = instrumentos_.find(porta);
        return it == instrumentos_.end() ? juce::String(porta) : it->second.nome;
    }

    static double agora() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

    void anotar(const juce::String& tipo, const juce::String& nome, const juce::String& dados) {
        if (registro_.size() >= 20000) registro_.removeRange(0, 1000);   // limite de memória
        registro_.add({ juce::Time::currentTimeMillis() * 1000, tipo, nome, dados });
    }

    void oscMessageReceived(const juce::OSCMessage& m) override {
        const auto addr = m.getAddressPattern().toString();
        if (!addr.startsWith("/rasgo/1/")) return;
        const auto nome = addr.substring(9);
        auto isI = [&](int i) { return m.size() > i && m[i].isInt32(); };
        auto isS = [&](int i) { return m.size() > i && m[i].isString(); };
        auto isF = [&](int i) { return m.size() > i && m[i].isFloat32(); };
        if (nome == "hello" && isS(0) && isS(1) && isI(2) && isI(3) && isS(4)) {
            const int porta = m[2].getInt32();
            if (porta < 1024 || porta > 65535) return;
            auto [it, novo] = instrumentos_.try_emplace(porta);
            auto& inst = it->second;
            inst.porta = porta;
            inst.visto = agora();
            if (novo) {
                inst.nome = m[0].getString();
                inst.versao = m[1].getString();
                inst.pid = m[3].getInt32();
                anotar("hello", inst.nome, m[4].getString());
            }
            inst.caps = juce::StringArray::fromTokens(m[4].getString(), ",", "");
            inst.caps.removeEmptyStrings();
            if (inst.ponte.isNotEmpty()) inst.caps.addIfNotAlreadyThere("audio");
            enviar(porta, "welcome", juce::String(senha_));
            enviar(porta, "query");
            mudou();
            return;
        }
        if (!isI(0)) return;
        const int porta = m[0].getInt32();
        auto it = instrumentos_.find(porta);
        if (it == instrumentos_.end()) {
            if (nome == "alive" && porta >= 1024 && porta <= 65535) enviar(porta, "who");
            return;
        }
        auto& inst = it->second;
        inst.visto = agora();
        if (nome == "bye") {
            anotar("bye", inst.nome, {});
            instrumentos_.erase(it);
            mudou();
        } else if (nome == "state" && isI(1) && isS(2) && isF(3)) {
            inst.tocando = m[1].getInt32() != 0;
            inst.seed = m[2].getString();
            inst.bpm = m[3].getFloat32();
            mudou();
        } else if (nome == "load" && isF(1) && isI(2)) {
            inst.carga = m[1].getFloat32();
            inst.atrasos = m[2].getInt32();
        } else if (nome == "ack" && isS(1) && isI(2)) {
            const bool ok = m[2].getInt32() != 0;
            const auto nota = isS(3) ? m[3].getString() : juce::String();
            const auto cmd = m[1].getString().fromLastOccurrenceOf("/rasgo/1/", false, false);
            inst.ultimaResposta = cmd + (ok ? juce::String(": ok") : ": " + T("não") + " (" + (nota.isEmpty() ? T("capacidade ausente") : T(nota)) + ")");
            anotar("ack", inst.nome, inst.ultimaResposta);
            if (ok && cmd != "query") enviar(porta, "query");   // estado novo depois do comando
            mudou();
        } else if (nome == "event" && isS(1)) {
            anotar("event", inst.nome, m[1].getString() + (isF(2) ? " " + juce::String(m[2].getFloat32(), 2) : juce::String()));
        } else if (nome == "obs" && isS(1) && isF(2)) {
            anotar("obs", inst.nome, m[1].getString() + " " + juce::String(m[2].getFloat32(), 2));
        } else if (nome == "ponte" && isS(1) && isI(2) && isF(3) && isS(4) && m[4].getString().toStdString() == senha_) {
            // só com a senha e só dentro da pasta das pontes (o motor confere de novo)
            const auto caminho = m[1].getString();
            if (caminho.isNotEmpty() && !rasgo::maestruc::ponte::caminhoValido(caminho.toStdString())) return;
            inst.ponte = caminho;
            inst.pares = juce::jlimit(0, 16, m[2].getInt32());
            inst.taxa = m[3].getFloat32();
            inst.caps.addIfNotAlreadyThere("audio");
            anotar("ponte", inst.nome, caminho.isEmpty() ? juce::String("fechada") : juce::String(inst.pares) + " par(es)");
            if (onPonte) onPonte(inst);
            mudou();
        } else if (nome == "mixer/canal" && isI(1) && isS(2) && isF(3) && isI(4) && isI(5)) {
            const int idx = m[1].getInt32();
            auto c = std::find_if(inst.mixer.begin(), inst.mixer.end(), [idx](auto& x) { return x.indice == idx; });
            if (c == inst.mixer.end()) {
                if (inst.mixer.size() >= 64) return;
                inst.mixer.push_back({});
                c = inst.mixer.end() - 1;
            }
            c->indice = idx;
            c->nome = m[2].getString().substring(0, 48);
            if (isS(6) && isF(7) && isF(8)) {
                c->objetos = juce::StringArray::fromTokens(m[6].getString().substring(0, 48), ",", "");
                c->objetos.trim();
                c->objetos.removeEmptyStrings();
                const float lo = juce::jlimit(-90.0f, 12.0f, m[7].getFloat32());
                const float hi = juce::jlimit(-90.0f, 12.0f, m[8].getFloat32());
                c->minDb = juce::jmin(lo, hi);
                c->maxDb = juce::jmax(lo, hi) > c->minDb ? juce::jmax(lo, hi) : c->minDb + 1.0f;
            }
            c->nivelDb = juce::jlimit(c->minDb, c->maxDb, m[3].getFloat32());
            c->mudo = m[4].getInt32() != 0;
            c->solo = m[5].getInt32() != 0;
            if (inst.mixerRodada.size() < 64) inst.mixerRodada.push_back(idx);
        } else if (nome == "mixer/fim" && isI(1)) {
            // fica só o que veio nesta lista (o app pode ter mudado de canais)
            auto& r = inst.mixerRodada;
            inst.mixer.erase(std::remove_if(inst.mixer.begin(), inst.mixer.end(), [&r](const auto& c) {
                return std::find(r.begin(), r.end(), c.indice) == r.end();
            }), inst.mixer.end());
            r.clear();
            mudou();
        }
    }

    void timerCallback() override {
        const double limite = agora() - kAusenteSegundos;
        bool alguem = false;
        for (auto it = instrumentos_.begin(); it != instrumentos_.end();) {
            if (it->second.visto < limite) {
                anotar("ausente", it->second.nome, {});
                it = instrumentos_.erase(it);
                alguem = true;
            } else {
                ++it;
            }
        }
        if (alguem) mudou();
    }

    void mudou() { if (onChange) onChange(); }

    // Senha da sessão: sorteada a cada abertura (gerador do sistema), num
    // arquivo só do usuário que o receptor de cada instrumento confere.
    bool gravarSenha() {
        std::random_device rd;
        static const char* hex = "0123456789abcdef";
        senha_.clear();
        for (int i = 0; i < 32; ++i) senha_ += hex[rd() & 15u];
        const auto caminho = rasgo::maestruc::caminhoDaSenhaMaestruc();
        if (caminho.empty()) return false;
        const auto temp = caminho + ".tmp";
        const juce::File t(temp);
        t.deleteFile();
#if JUCE_WINDOWS
        if (!t.replaceWithText(juce::String(senha_) + "\n")) return false;
#else
        const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) return false;
        const std::string linha = senha_ + "\n";
        const bool ok = ::write(fd, linha.data(), linha.size()) == (ssize_t) linha.size();
        ::close(fd);
        if (!ok) { t.deleteFile(); return false; }
#endif
        return t.moveFileTo(juce::File(caminho));
    }
    void apagarSenha() {
        const auto caminho = rasgo::maestruc::caminhoDaSenhaMaestruc();
        if (caminho.empty() || senha_.empty()) return;
        std::ifstream f(caminho);
        std::string s;
        std::getline(f, s);
        if (s == senha_) juce::File(caminho).deleteFile();   // só a nossa
    }

    std::unique_ptr<juce::DatagramSocket> socket_;
    juce::OSCReceiver receiver_;
    juce::OSCSender sender_;
    std::map<int, Instrumento> instrumentos_;
    std::string senha_;
    juce::Array<Registro> registro_;
    int comandos_ = 0;
};

} // namespace maestruc
