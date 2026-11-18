// Maestruc — leitura e escrita de WAV, sem dependências.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
//
// Escrita: PCM 24 bits com dither TPDF (Dither.h, 16/11/2026), estéreo.
// Leitura: PCM 16/24/32 e float 32, mono ou estéreo (mono vira L = R).
#pragma once

#include "Dither.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace maestruc {

class WavWriter {
public:
    WavWriter() = default;
    ~WavWriter() { close(); }
    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    bool open(const std::string& path, int sampleRate);
    // Intercalado L R L R…, `frames` quadros. Fora da thread de áudio.
    bool writeInterleaved(const float* data, std::size_t frames);
    bool close();   // corrige os tamanhos no cabeçalho
    std::uint64_t frames() const { return frames_; }
    bool isOpen() const { return file_ != nullptr; }

private:
    std::FILE* file_ = nullptr;
    std::uint64_t frames_ = 0;
    std::vector<unsigned char> bytes_;
    TpdfQuantiser dither_ { 24 };
};

struct WavData {
    int sampleRate = 0;
    std::vector<float> left, right;
};

// Devolve false e preenche `error` se o arquivo não for legível.
bool readWav(const std::string& path, WavData& out, std::string& error);

} // namespace maestruc
