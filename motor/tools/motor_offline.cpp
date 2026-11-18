// Maestruc — render offline do motor: WAVs de stems entram, o mixer e as
// influências "no som" processam, o gravador escreve cada pista e o master.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
//
// Serve para ouvir o motor com gravações reais (o REC de cada instrumento)
// antes de existir a ligação ao vivo pelo PipeWire.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "Mixer.h"
#include "Recorder.h"
#include "Wav.h"

using namespace maestruc;

namespace {

int usage() {
    std::fputs(
        "uso: maestruc_motor_offline --saida PASTA --entrada nome=arquivo.wav [--entrada …]\n"
        "       [--ganho nome=dB] [--pan nome=-1..1] [--mudo nome] [--solo nome]\n"
        "       [--rota nome=master|fones|nenhuma] [--master dB]\n"
        "       [--influencia fonte:destino:ganho|brilho:quantidade:ms]   (quantidade -1..1)\n"
        "       [--segundos N]   (limite; padrão: o stem mais longo, máx. 1200)\n"
        "exemplo: --influencia bumbo:aquorbium:ganho:-0.6:300   (o bumbo abre espaço no Aquorbium)\n",
        stderr);
    return 2;
}

bool splitOnce(const std::string& s, char c, std::string& a, std::string& b) {
    const auto p = s.find(c);
    if (p == std::string::npos) return false;
    a = s.substr(0, p); b = s.substr(p + 1);
    return !a.empty() && !b.empty();
}

std::vector<std::string> split(const std::string& s, char c) {
    std::vector<std::string> out;
    std::size_t start = 0, p;
    while ((p = s.find(c, start)) != std::string::npos) { out.push_back(s.substr(start, p - start)); start = p + 1; }
    out.push_back(s.substr(start));
    return out;
}

float dbOf(float x) { return x > 1.0e-9f ? 20.0f * std::log10(x) : -180.0f; }

} // namespace

int main(int argc, char** argv) {
    std::string outDir;
    std::vector<std::pair<std::string, std::string>> inputs;
    std::vector<std::pair<std::string, std::string>> settings;   // (opção, valor)
    double limitSeconds = 1200.0;
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        if (k == "--ajuda" || k == "-h") return usage();
        if (i + 1 >= argc) return usage();
        const std::string v = argv[++i];
        if (k == "--saida") outDir = v;
        else if (k == "--entrada") {
            std::string n, f;
            if (!splitOnce(v, '=', n, f)) return usage();
            inputs.emplace_back(n, f);
        } else if (k == "--segundos") limitSeconds = std::min(1200.0, std::atof(v.c_str()));
        else settings.emplace_back(k, v);
    }
    if (outDir.empty() || inputs.empty() || inputs.size() > static_cast<std::size_t>(kMaxStrips)) return usage();

    std::vector<WavData> wavs(inputs.size());
    std::map<std::string, int> index;
    int sampleRate = 0;
    std::size_t length = 0;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        std::string err;
        if (!readWav(inputs[i].second, wavs[i], err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        if (sampleRate && wavs[i].sampleRate != sampleRate) {
            std::fprintf(stderr, "taxas diferentes (%d e %d Hz): converta antes\n", sampleRate, wavs[i].sampleRate);
            return 1;
        }
        if (index.count(inputs[i].first) || inputs[i].first == "master" || inputs[i].first == "fones") {
            std::fprintf(stderr, "nome repetido ou reservado: %s\n", inputs[i].first.c_str());
            return 1;
        }
        sampleRate = wavs[i].sampleRate;
        length = std::max(length, wavs[i].left.size());
        index[inputs[i].first] = static_cast<int>(i);
    }
    length = std::min(length, static_cast<std::size_t>(limitSeconds * sampleRate));
    const int n = static_cast<int>(inputs.size());

    constexpr int kBlock = 512;
    Mixer mixer;
    mixer.prepare(sampleRate, kBlock, n);
    bool fones = false;
    int nInf = 0;
    auto strip = [&](const std::string& name) -> int {
        auto it = index.find(name);
        if (it == index.end()) { std::fprintf(stderr, "stem desconhecido: %s\n", name.c_str()); std::exit(1); }
        return it->second;
    };
    for (const auto& [k, v] : settings) {
        std::string a, b;
        if (k == "--mudo") mixer.strip(strip(v)).mute = true;
        else if (k == "--solo") mixer.strip(strip(v)).solo = true;
        else if (k == "--master") mixer.masterGainDb() = static_cast<float>(std::atof(v.c_str()));
        else if ((k == "--ganho" || k == "--pan" || k == "--rota") && splitOnce(v, '=', a, b)) {
            auto& p = mixer.strip(strip(a));
            if (k == "--ganho") p.gainDb = static_cast<float>(std::atof(b.c_str()));
            else if (k == "--pan") p.pan = static_cast<float>(std::atof(b.c_str()));
            else if (b == "master") p.route = RouteMaster;
            else if (b == "fones") { p.route = RouteFones; fones = true; }
            else if (b == "nenhuma") p.route = RouteNenhuma;
            else return usage();
        } else if (k == "--influencia") {
            const auto f = split(v, ':');
            if (f.size() != 5 || (f[2] != "ganho" && f[2] != "brilho") || nInf >= kMaxInfluences) return usage();
            auto& inf = mixer.influence(nInf++);
            inf.source = strip(f[0]);
            inf.dest = strip(f[1]);
            inf.target = static_cast<int>(f[2] == "ganho" ? Target::Ganho : Target::Brilho);
            inf.amount = static_cast<float>(std::atof(f[3].c_str()));
            inf.timeMs = static_cast<float>(std::max(10.0, std::atof(f[4].c_str())));
            inf.active = true;
        } else return usage();
    }

    std::vector<std::string> names;
    for (const auto& in : inputs) names.push_back(in.first);
    names.push_back("master");
    if (fones) names.push_back("fones");
    Recorder rec;
    std::string err;
    if (!rec.start(outDir, names, sampleRate, 2.0, err, true)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }

    std::vector<std::vector<float>> post(static_cast<std::size_t>(n) * 2, std::vector<float>(kBlock));
    std::vector<float*> postPtr;
    for (auto& b : post) postPtr.push_back(b.data());
    std::vector<std::vector<float>> inBuf(static_cast<std::size_t>(n) * 2, std::vector<float>(kBlock));
    std::vector<const float*> inPtr;
    for (auto& b : inBuf) inPtr.push_back(b.data());
    std::vector<float> mL(kBlock), mR(kBlock), fL(kBlock), fR(kBlock);
    std::vector<float> peak(static_cast<std::size_t>(n) + 2, 0.0f);
    std::vector<float> minEnvDb(static_cast<std::size_t>(n), 0.0f);

    for (std::size_t pos = 0; pos < length; pos += kBlock) {
        const int frames = static_cast<int>(std::min<std::size_t>(kBlock, length - pos));
        for (int s = 0; s < n; ++s) {
            const auto& w = wavs[static_cast<std::size_t>(s)];
            for (int k = 0; k < frames; ++k) {
                const std::size_t q = pos + static_cast<std::size_t>(k);
                inBuf[static_cast<std::size_t>(s) * 2][k] = q < w.left.size() ? w.left[q] : 0.0f;
                inBuf[static_cast<std::size_t>(s) * 2 + 1][k] = q < w.right.size() ? w.right[q] : 0.0f;
            }
        }
        mixer.process(inPtr.data(), mL.data(), mR.data(), fL.data(), fR.data(), postPtr.data(), frames);
        for (int s = 0; s < n; ++s) {
            rec.push(s, postPtr[static_cast<std::size_t>(s) * 2], postPtr[static_cast<std::size_t>(s) * 2 + 1], frames);
            peak[static_cast<std::size_t>(s)] = std::max(peak[static_cast<std::size_t>(s)], mixer.stripMeter(s).peak.load());
        }
        rec.push(n, mL.data(), mR.data(), frames);
        peak[static_cast<std::size_t>(n)] = std::max(peak[static_cast<std::size_t>(n)], mixer.masterMeter().peak.load());
        if (fones) rec.push(n + 1, fL.data(), fR.data(), frames);
    }
    const bool ok = rec.stop();

    std::printf("render: %.1f s a %d Hz, %d stems, %d influência(s) → %s\n",
                static_cast<double>(length) / sampleRate, sampleRate, n, nInf, outDir.c_str());
    for (int s = 0; s < n; ++s)
        std::printf("  %-14s pico %6.1f dBFS\n", inputs[static_cast<std::size_t>(s)].first.c_str(),
                    dbOf(peak[static_cast<std::size_t>(s)]));
    std::printf("  %-14s pico %6.1f dBFS%s\n", "master", dbOf(peak[static_cast<std::size_t>(n)]),
                peak[static_cast<std::size_t>(n)] > 1.0f ? "  (ACIMA DE 0 dBFS: abaixe --master)" : "");
    if (rec.droppedFrames()) std::printf("  quadros perdidos: %llu\n", static_cast<unsigned long long>(rec.droppedFrames()));
    return ok ? 0 : 1;
}
