// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Lúcio de Araújo
//
// Ensaio do MaestrucLink.h sem interface: um "instrumento" de console que
// liga os handlers a variáveis, emite acontecimentos de um fio que faz o
// papel do áudio e imprime o que recebeu. Roda por N segundos.
#include "../MaestrucLink.h"

#include <cstdio>
#include <thread>
#include <vector>

int main (int argc, char** argv)
{
    const int seconds = argc > 1 ? juce::jlimit (1, 60, std::atoi (argv[1])) : 5;
    const int port = argc > 2 ? std::atoi (argv[2]) : rasgo::maestruc::maestrucPort;
    juce::ScopedJuceInitialiser_GUI juce;

    bool playing = false;
    juce::String seed = "00000000";
    float bpm = 120.0f;
    std::atomic<bool> running { true };

    rasgo::maestruc::Link link { "Sonda", "0.0.1", { "intensidade" }, { "event", "load" } };
    link.handlers.play  = [&] (juce::int64 us) { playing = true; std::printf ("play %lld\n", (long long) us); };
    link.handlers.stop  = [&] { playing = false; std::printf ("stop\n"); };
    link.handlers.tempo = [&] (float b) { bpm = b; std::printf ("tempo %.1f\n", b); };
    link.handlers.seed  = [&] (const juce::String& s) { seed = s; std::printf ("seed %s\n", s.toRawUTF8()); };
    link.handlers.open  = [&] (const juce::File& f, juce::String& note) {
        std::printf ("open %s\n", f.getFullPathName().toRawUTF8());
        if (! f.getFileName().endsWith (".batruc.txt")) { note = "formato desconhecido"; return false; }
        return true;
    };
    link.handlers.macro = [&] (const juce::String& n, float v) { std::printf ("macro %s %.2f\n", n.toRawUTF8(), v); return true; };
    link.handlers.state = [&] (bool& p, juce::String& s, float& b) { p = playing; s = seed; b = bpm; };
    // mixer interno de ensaio: master e dois canais
    std::vector<rasgo::maestruc::MixerChannel> mix { { -1, "master", 0.0f, false, false, "nivel,mudo", -60.0f, 6.0f },
                                                    { 0, "bumbo", -3.0f, false, false },
                                                    { 1, "baixo", -6.0f, false, false } };
    link.handlers.mixerList = [&] (std::vector<rasgo::maestruc::MixerChannel>& out) { out = mix; };
    link.handlers.mixerSet = [&] (int idx, const juce::String& what, float v) {
        for (auto& c : mix)
            if (c.index == idx)
            {
                if (what == "nivel") c.levelDb = v; else if (what == "mudo") c.mute = v > 0.5f; else c.solo = v > 0.5f;
                std::printf ("mixer %d %s %.1f\n", idx, what.toRawUTF8(), v);
                return true;
            }
        return false;
    };
    std::fflush (stdout);

    if (! link.start (port)) { std::fprintf (stderr, "porta local indisponível\n"); return 1; }
    // ponte de som: mistura e um canal interno, a 48 kHz
    if (! link.openSoundBridge (2, 48000.0, { "mix", "bumbo" })) { std::fprintf (stderr, "ponte não abriu\n"); return 1; }
    rasgo::maestruc::PortaoDeSom portao;
    portao.abrir (&link);
    std::atomic<int> calados { 0 };

    std::thread audio ([&] {   // papel do callback de áudio
        int n = 0;
        std::vector<float> a (480, 0.1f), b (480, 0.2f);
        const float* canais[4] = { a.data(), a.data(), b.data(), b.data() };
        while (running.load())
        {
            if (n % 25 == 0) link.pushEvent ("ataque", 0.5f);
            link.setAudioLoad (0.25f, 0);
            if (portao.escrever (canais, 4, 480)) calados.fetch_add (1);
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
            if (++n > 40000) break;
        }
    });
    juce::Timer::callAfterDelay (seconds * 1000, [] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
    juce::MessageManager::getInstance()->runDispatchLoop();
    portao.fecharEsperar();   // antes de desfazer: o áudio não usa mais o Link
    running = false;
    audio.join();
    std::printf ("conectado=%d\n", link.isConnectedToMaestruc() ? 1 : 0);
    std::printf ("calados=%d\n", calados.load());
    link.stop();
    std::fflush (stdout);
    return 0;
}
