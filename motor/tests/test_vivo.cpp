// Maestruc — ensaio do host ao vivo no servidor de áudio real (PipeWire/JACK),
// em silêncio: nada é ligado às caixas. Um cliente de ensaio toca um seno
// de 1 kHz no stem "a"; o Maestruc grava; o WAV é conferido.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <thread>

#include "LiveHost.h"
#include "Wav.h"

using namespace maestruc;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "OK  " : "FALHA", what);
    if (!ok) ++failures;
}

struct Tone {
    const jack::Api* api = nullptr;
    jack::port_t* port = nullptr;
    double phase = 0.0, step = 0.0;
};

int toneProcess(jack::nframes_t n, void* p) {
    auto* t = static_cast<Tone*>(p);
    auto* out = static_cast<float*>(t->api->port_get_buffer(t->port, n));
    for (jack::nframes_t i = 0; i < n; ++i) {
        out[i] = static_cast<float>(0.5 * std::sin(t->phase));
        t->phase += t->step;
        if (t->phase > 2.0 * M_PI) t->phase -= 2.0 * M_PI;
    }
    return 0;
}

double rms(const std::vector<float>& x, std::size_t from) {
    double acc = 0.0;
    std::size_t n = 0;
    for (std::size_t i = from; i < x.size(); ++i, ++n) acc += static_cast<double>(x[i]) * x[i];
    return n ? std::sqrt(acc / n) : 0.0;
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::string err;
    const jack::Api* api = jack::load(err);
    if (!api) { std::printf("PULADO  %s\n", err.c_str()); return 0; }

    LiveHost host;
    if (!host.open("maestruc-ensaio", { "a", "b" }, err, 4)) { std::printf("PULADO  %s\n", err.c_str()); return 0; }
    std::printf("      servidor a %d Hz\n", host.sampleRate());

    int status = 0;
    jack::client_t* toneClient = api->client_open("maestruc-ensaio-tom", jack::NoStartServer, &status);
    check(toneClient != nullptr, "cliente de ensaio abre");
    if (!toneClient) return 1;
    Tone tone;
    tone.api = api;
    tone.step = 2.0 * M_PI * 1000.0 / api->get_sample_rate(toneClient);
    tone.port = api->port_register(toneClient, "saida", jack::DefaultAudioType, jack::PortIsOutput, 0);
    api->set_process_callback(toneClient, toneProcess, &tone);
    check(api->activate(toneClient) == 0, "cliente de ensaio ativa");

    const auto links = host.connectSource("a", "maestruc-ensaio-tom:saida", err);
    check(links.size() == 2, "liga a saída do ensaio a a_L e a_R");
    std::string e2;
    check(host.connectSource("zzz", "x", e2).empty() && !e2.empty(), "stem desconhecido é recusado");

    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "maestruc-test-vivo";
    fs::remove_all(dir);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    err.clear();
    check(host.startRecording(dir.string(), err), ("grava ao vivo " + err).c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    check(host.stopRecording(), "fecha a gravação");
    check(host.droppedFrames() == 0, "nenhum quadro perdido");
    check(host.cycles() > 10, "o servidor chama o Maestruc");
    check(host.oversizedCycles() == 0, "blocos dentro do limite");

    // Um app que entra no meio da sessão: stem novo com o áudio já correndo.
    const fs::path dir2 = fs::temp_directory_path() / "maestruc-test-vivo-c";
    fs::remove_all(dir2);
    check(host.addStem("c", err) == 2 && host.addStem("a", err) == 0, "stem novo durante a sessão; nome repetido reaproveita");
    std::string e3;
    check(host.connectSource("c", "maestruc-ensaio-tom:saida", e3).size() == 2, "liga o ensaio ao stem novo");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const std::vector<bool> onlyC { false, false, true };
    check(host.startRecording(dir2.string(), err, &onlyC, false), "grava só as pistas escolhidas");
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    host.stopRecording();
    {
        WavData c;
        const bool okC = readWav((dir2 / "c.wav").string(), c, err);
        check(okC && !fs::exists(dir2 / "a.wav") && !fs::exists(dir2 / "master.wav"), "só c.wav foi criado");
        if (okC) {
            const double rc = rms(c.left, static_cast<std::size_t>(c.sampleRate / 10));
            std::printf("      stem novo: rms %.3f\n", rc);
            check(std::fabs(rc - 0.5 / std::sqrt(2.0)) < 0.01, "o stem novo recebe o som com o nível certo");
        }
    }
    fs::remove_all(dir2);

    api->deactivate(toneClient);
    api->client_close(toneClient);
    host.close();

    WavData a, b, master;
    const bool read = readWav((dir / "a.wav").string(), a, err) && readWav((dir / "b.wav").string(), b, err)
                      && readWav((dir / "master.wav").string(), master, err);
    check(read, "pistas a, b e master legíveis");
    if (read) {
        const double secs = static_cast<double>(a.left.size()) / a.sampleRate;
        // Descarta o início (rampa de ganho do primeiro bloco).
        const std::size_t skip = static_cast<std::size_t>(a.sampleRate / 10);
        const double ra = rms(a.left, skip), rb = rms(b.left, skip), rm = rms(master.right, skip);
        std::printf("      %.2f s gravados; rms a %.3f (esperado %.3f), b %.5f, master %.3f\n",
                    secs, ra, 0.5 / std::sqrt(2.0), rb, rm);
        check(secs > 1.2 && secs < 1.8, "duração ≈ 1,5 s");
        // Seno 0,5 → rms 0,354; o pan central conserva o nível em cada lado.
        check(std::fabs(ra - 0.5 / std::sqrt(2.0)) < 0.01, "o seno chega ao stem a com o nível certo");
        check(rb < 1e-5, "stem b sem entrada fica em silêncio");
        check(std::fabs(rm - ra) < 0.01, "master = stem a");
        check(a.left.size() == master.left.size(), "pistas alinhadas (mesmo número de quadros)");
    }
    fs::remove_all(dir);
    std::printf("%s: %d falha(s)\n", failures ? "FALHOU" : "tudo OK", failures);
    return failures ? 1 : 0;
}
