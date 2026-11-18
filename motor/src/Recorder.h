// Maestruc — gravação por pista: cada stem e o master em WAV próprio.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
//
// A thread de áudio só copia para anéis sem trava (push); uma thread de
// escrita esvazia os anéis no disco. Se o disco atrasar além da folga do
// anel, os quadros que não couberem são descartados e contados — a música
// nunca espera pelo disco.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "Wav.h"

namespace maestruc {

class Ring {
public:
    explicit Ring(std::size_t capacityFloats);
    std::size_t freeSpace() const;
    std::size_t available() const;
    // Uma só thread escreve, uma só lê.
    std::size_t write(const float* l, const float* r, std::size_t frames);   // intercala
    std::size_t read(float* dst, std::size_t maxFloats);

private:
    std::vector<float> buf_;
    std::size_t mask_;
    std::atomic<std::size_t> head_ { 0 }, tail_ { 0 };
};

class Recorder {
public:
    ~Recorder() { stop(); }

    // Antes do áudio: cria `dir/<nome>.wav` para cada pista. `ringSeconds` é
    // a folga contra atrasos do disco. `blocking` só para render offline.
    bool start(const std::string& dir, const std::vector<std::string>& trackNames,
               int sampleRate, double ringSeconds, std::string& error, bool blocking = false);
    // Thread de áudio (realtime-safe quando blocking == false).
    void push(int track, const float* left, const float* right, int frames);
    // Fecha os arquivos depois de esvaziar os anéis.
    bool stop();

    bool recording() const { return running_.load(); }
    std::uint64_t droppedFrames() const { return dropped_.load(); }
    std::uint64_t framesWritten(int track) const;

private:
    void writerLoop();
    bool drain();

    std::vector<std::unique_ptr<Ring>> rings_;
    std::vector<std::unique_ptr<WavWriter>> writers_;
    std::vector<float> scratch_;
    std::thread thread_;
    std::atomic<bool> running_ { false };
    std::atomic<bool> stopRequested_ { false };
    std::atomic<bool> writeError_ { false };
    std::atomic<std::uint64_t> dropped_ { 0 };
    bool blocking_ = false;
};

} // namespace maestruc
