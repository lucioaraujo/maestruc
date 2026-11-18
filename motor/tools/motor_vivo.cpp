// Maestruc — host ao vivo pela linha de comando (antes da janela).
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
//
// Abre um cliente JACK "Maestruc" no PipeWire com uma entrada estéreo por
// stem e as saídas master/fones; liga os apps pedidos; aceita comandos
// digitados (ganho, mudo, solo, gravar…). Nível, mudo e solo só mudam por
// comando de quem toca.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "LiveHost.h"

using namespace maestruc;

namespace {

std::atomic<bool> g_stop { false };
void onSignal(int) { g_stop = true; }

int usage() {
    std::fputs(
        "uso: maestruc_motor_vivo --stem nome [--stem …] [--ligar nome=PADRÃO] [--tocar]\n"
        "       [--ganho nome=dB] [--pan nome=-1..1] [--rota nome=master|fones|nenhuma]\n"
        "       [--influencia fonte:destino:ganho|brilho:quantidade:ms] [--master dB]\n"
        "       [--gravar PASTA] [--segundos N]\n"
        "       maestruc_motor_vivo --listar          (saídas de áudio disponíveis)\n"
        "--ligar  liga as saídas do app cujo nome contém PADRÃO ao stem e as tira das caixas\n"
        "         (o app passa a soar só pelo Maestruc; ao sair, volta às caixas)\n"
        "--tocar  liga o master do Maestruc às caixas\n"
        "comandos durante: ganho NOME dB | pan NOME v | mudo NOME | som NOME | solo NOME | semsolo NOME\n"
        "                  master dB | gravar PASTA | parar | medidores | sair\n",
        stderr);
    return 2;
}

bool splitOnce(const std::string& s, char c, std::string& a, std::string& b) {
    const auto p = s.find(c);
    if (p == std::string::npos) return false;
    a = s.substr(0, p); b = s.substr(p + 1);
    return !a.empty() && !b.empty();
}

float dbOf(float x) { return x > 1.0e-9f ? 20.0f * std::log10(x) : -120.0f; }

void printMeters(LiveHost& h) {
    for (int s = 0; s < static_cast<int>(h.stems().size()); ++s)
        std::printf("  %-14s %6.1f dB rms  %6.1f dB pico\n", h.stems()[static_cast<std::size_t>(s)].c_str(),
                    dbOf(h.mixer().stripMeter(s).rms.load()), dbOf(h.mixer().stripMeter(s).peak.load()));
    std::printf("  %-14s %6.1f dB rms  %6.1f dB pico%s\n", "master", dbOf(h.mixer().masterMeter().rms.load()),
                dbOf(h.mixer().masterMeter().peak.load()), h.recording() ? "  ● gravando" : "");
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> stems;
    std::vector<std::pair<std::string, std::string>> opts;
    bool listOnly = false, play = false;
    double seconds = 0.0;
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        if (k == "--ajuda" || k == "-h") return usage();
        if (k == "--listar") { listOnly = true; continue; }
        if (k == "--tocar") { play = true; continue; }
        if (i + 1 >= argc) return usage();
        const std::string v = argv[++i];
        if (k == "--stem") stems.push_back(v);
        else if (k == "--segundos") seconds = std::atof(v.c_str());
        else opts.emplace_back(k, v);
    }
    if (listOnly && stems.empty()) stems.push_back("escuta");
    if (stems.empty()) return usage();
    for (const auto& s : stems)
        if (s == "master" || s == "fones" || s.find_first_of(":/ ") != std::string::npos) {
            std::fprintf(stderr, "nome de stem inválido: %s\n", s.c_str());
            return 2;
        }

    LiveHost host;
    std::string err;
    if (!host.open("Maestruc", stems, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    if (listOnly) {
        for (const auto& p : host.outputPorts()) std::printf("%s\n", p.c_str());
        return 0;
    }
    std::printf("Maestruc ao vivo: %d Hz, %zu stem(s)\n", host.sampleRate(), stems.size());

    auto& m = host.mixer();
    auto idx = [&](const std::string& n) {
        const int i = host.stemIndex(n);
        if (i < 0) std::fprintf(stderr, "stem desconhecido: %s\n", n.c_str());
        return i;
    };
    int nInf = 0;
    for (const auto& [k, v] : opts) {
        std::string a, b;
        if (k == "--master") m.masterGainDb() = static_cast<float>(std::atof(v.c_str()));
        else if (k == "--gravar") {
            if (!host.startRecording(v, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
            std::printf("gravando em %s\n", v.c_str());
        } else if ((k == "--ganho" || k == "--pan" || k == "--rota" || k == "--ligar") && splitOnce(v, '=', a, b)) {
            const int s = idx(a);
            if (s < 0) return 2;
            if (k == "--ganho") m.strip(s).gainDb = static_cast<float>(std::atof(b.c_str()));
            else if (k == "--pan") m.strip(s).pan = static_cast<float>(std::atof(b.c_str()));
            else if (k == "--rota") m.strip(s).route = b == "fones" ? RouteFones : b == "nenhuma" ? RouteNenhuma : RouteMaster;
            else {
                err.clear();
                for (const auto& l : host.connectSource(a, b, err, true)) std::printf("ligado: %s\n", l.c_str());
                if (!err.empty()) std::fprintf(stderr, "%s\n", err.c_str());
            }
        } else if (k == "--influencia") {
            std::vector<std::string> f;
            std::stringstream ss(v);
            for (std::string part; std::getline(ss, part, ':');) f.push_back(part);
            if (f.size() != 5 || (f[2] != "ganho" && f[2] != "brilho") || nInf >= kMaxInfluences) return usage();
            const int src = idx(f[0]), dst = idx(f[1]);
            if (src < 0 || dst < 0) return 2;
            auto& inf = m.influence(nInf++);
            inf.source = src; inf.dest = dst;
            inf.target = static_cast<int>(f[2] == "ganho" ? Target::Ganho : Target::Brilho);
            inf.amount = static_cast<float>(std::atof(f[3].c_str()));
            inf.timeMs = static_cast<float>(std::max(10.0, std::atof(f[4].c_str())));
            inf.active = true;
        } else return usage();
    }
    if (play && !host.connectMasterToPlayback(err)) std::fprintf(stderr, "%s\n", err.c_str());

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    const auto t0 = std::chrono::steady_clock::now();
    auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };

    // Comandos de quem toca: lidos numa thread à parte para o laço poder
    // parar por tempo ou sinal.
    std::vector<std::string> pending;
    std::mutex pendingMutex;
    std::thread reader([&] {
        for (std::string line; !g_stop && std::getline(std::cin, line);) {
            std::lock_guard<std::mutex> lock(pendingMutex);
            pending.push_back(line);
        }
    });
    reader.detach();

    unsigned tick = 0;
    if (host.takenCount()) std::printf("%zu ligação(ões) do(s) app(s) às caixas tirada(s); voltam ao sair\n", host.takenCount());
    while (!g_stop && host.running() && (seconds <= 0.0 || elapsed() < seconds)) {
        std::vector<std::string> lines;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            lines.swap(pending);
        }
        for (const auto& line : lines) {
            std::stringstream ss(line);
            std::string cmd, a, b;
            ss >> cmd >> a >> b;
            if (cmd.empty()) continue;
            if (cmd == "sair") { g_stop = true; break; }
            if (cmd == "medidores") { printMeters(host); continue; }
            if (cmd == "parar") { host.stopRecording(); std::printf("gravação parada (%llu quadros perdidos)\n",
                                  static_cast<unsigned long long>(host.droppedFrames())); continue; }
            if (cmd == "gravar" && !a.empty()) {
                if (host.startRecording(a, err)) std::printf("gravando em %s\n", a.c_str());
                else std::printf("%s\n", err.c_str());
                continue;
            }
            if (cmd == "master" && !a.empty()) { m.masterGainDb() = static_cast<float>(std::atof(a.c_str())); continue; }
            const int s = a.empty() ? -1 : idx(a);
            if (s < 0) { std::printf("?\n"); continue; }
            if (cmd == "ganho" && !b.empty()) m.strip(s).gainDb = static_cast<float>(std::atof(b.c_str()));
            else if (cmd == "pan" && !b.empty()) m.strip(s).pan = static_cast<float>(std::atof(b.c_str()));
            else if (cmd == "mudo") m.strip(s).mute = true;
            else if (cmd == "som") m.strip(s).mute = false;
            else if (cmd == "solo") m.strip(s).solo = true;
            else if (cmd == "semsolo") m.strip(s).solo = false;
            else std::printf("?\n");
        }
        if (++tick % 10 == 0) {   // ~0,5 s: o gerenciador de sessão pode religar
            const int cut = host.enforceTaken();
            if (cut) std::printf("religado às caixas por fora; desliguei de novo (%d)\n", cut);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const bool wasRecording = host.recording();
    const bool ok = host.stopRecording();
    if (wasRecording) std::printf("gravação fechada%s, %llu quadros perdidos\n", ok ? "" : " COM ERRO",
                                  static_cast<unsigned long long>(host.droppedFrames()));
    if (host.oversizedCycles()) std::printf("ciclos acima do bloco máximo: %llu\n",
                                            static_cast<unsigned long long>(host.oversizedCycles()));
    if (!host.running() && !g_stop) std::printf("o servidor de áudio encerrou o Maestruc\n");
    const std::size_t restored = host.takenCount();
    host.close();
    if (restored) std::printf("%zu ligação(ões) devolvida(s) às caixas\n", restored);
    std::fflush(stdout);
    std::_Exit(ok ? 0 : 1);   // a thread de leitura pode estar presa no stdin
}
