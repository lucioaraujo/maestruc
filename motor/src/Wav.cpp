// Maestruc — leitura e escrita de WAV.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
#include "Wav.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace maestruc {
namespace {

void put16(unsigned char* p, std::uint32_t v) { p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; }
void put32(unsigned char* p, std::uint32_t v) { put16(p, v & 0xffff); put16(p + 2, v >> 16); }
std::uint32_t get16(const unsigned char* p) { return p[0] | (p[1] << 8); }
std::uint32_t get32(const unsigned char* p) { return get16(p) | (get16(p + 2) << 16); }

constexpr int kChannels = 2;
constexpr int kBytesPerSample = 3;

} // namespace

bool WavWriter::open(const std::string& path, int sampleRate) {
    close();
    file_ = std::fopen(path.c_str(), "wb");
    if (!file_) return false;
    frames_ = 0;
    unsigned char h[44] {};
    std::memcpy(h, "RIFF", 4);
    std::memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    put16(h + 20, 1);                                       // PCM
    put16(h + 22, kChannels);
    put32(h + 24, static_cast<std::uint32_t>(sampleRate));
    put32(h + 28, static_cast<std::uint32_t>(sampleRate * kChannels * kBytesPerSample));
    put16(h + 32, kChannels * kBytesPerSample);
    put16(h + 34, kBytesPerSample * 8);
    std::memcpy(h + 36, "data", 4);
    return std::fwrite(h, 1, sizeof h, file_) == sizeof h;
}

bool WavWriter::writeInterleaved(const float* data, std::size_t frames) {
    if (!file_) return false;
    const std::size_t n = frames * kChannels;
    bytes_.resize(n * kBytesPerSample);
    for (std::size_t i = 0; i < n; ++i) {
        // TPDF ±1 LSB só aqui, na conversão para inteiro (nunca no float)
        const auto v = dither_.quantise(data[i]);
        unsigned char* p = &bytes_[i * kBytesPerSample];
        p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff;
    }
    frames_ += frames;
    return std::fwrite(bytes_.data(), 1, bytes_.size(), file_) == bytes_.size();
}

bool WavWriter::close() {
    if (!file_) return true;
    const std::uint64_t dataBytes = frames_ * kChannels * kBytesPerSample;
    bool ok = dataBytes + 36 <= 0xffffffffull;   // limite do RIFF: ~4 GB por pista
    unsigned char b[4];
    put32(b, static_cast<std::uint32_t>(std::min<std::uint64_t>(dataBytes + 36, 0xffffffffull)));
    ok = std::fseek(file_, 4, SEEK_SET) == 0 && std::fwrite(b, 1, 4, file_) == 4 && ok;
    put32(b, static_cast<std::uint32_t>(std::min<std::uint64_t>(dataBytes, 0xffffffffull)));
    ok = std::fseek(file_, 40, SEEK_SET) == 0 && std::fwrite(b, 1, 4, file_) == 4 && ok;
    ok = std::fclose(file_) == 0 && ok;
    file_ = nullptr;
    return ok;
}

bool readWav(const std::string& path, WavData& out, std::string& error) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { error = "não abre: " + path; return false; }
    std::vector<unsigned char> b;
    unsigned char chunk[65536];
    std::size_t got;
    while ((got = std::fread(chunk, 1, sizeof chunk, f)) > 0) {
        b.insert(b.end(), chunk, chunk + got);
        if (b.size() > (2ull << 30)) { std::fclose(f); error = "arquivo grande demais (> 2 GB)"; return false; }
    }
    std::fclose(f);
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4)) {
        error = "não é WAV: " + path; return false;
    }
    int format = 0, channels = 0, bits = 0;
    const unsigned char* data = nullptr;
    std::size_t dataSize = 0;
    for (std::size_t pos = 12; pos + 8 <= b.size();) {
        const std::size_t size = get32(&b[pos + 4]);
        const unsigned char* body = &b[pos + 8];
        const std::size_t avail = std::min(size, b.size() - pos - 8);
        if (!std::memcmp(&b[pos], "fmt ", 4) && avail >= 16) {
            format = static_cast<int>(get16(body));
            channels = static_cast<int>(get16(body + 2));
            out.sampleRate = static_cast<int>(get32(body + 4));
            bits = static_cast<int>(get16(body + 14));
            if (format == 0xFFFE && avail >= 26) format = static_cast<int>(get16(body + 24));
        } else if (!std::memcmp(&b[pos], "data", 4)) {
            data = body; dataSize = avail;
        }
        pos += 8 + size + (size & 1);
    }
    const bool pcm = format == 1 && (bits == 16 || bits == 24 || bits == 32);
    const bool flt = format == 3 && bits == 32;
    if (!data || (!pcm && !flt) || channels < 1 || channels > 2 || out.sampleRate <= 0) {
        error = "formato não suportado (PCM 16/24/32 ou float 32, mono/estéreo): " + path; return false;
    }
    const int bps = bits / 8;
    const std::size_t frames = dataSize / static_cast<std::size_t>(bps * channels);
    out.left.resize(frames);
    out.right.resize(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        for (int c = 0; c < channels; ++c) {
            const unsigned char* p = data + (i * channels + c) * bps;
            float v;
            if (flt) { std::memcpy(&v, p, 4); }
            else if (bits == 16) v = static_cast<float>(static_cast<std::int16_t>(get16(p))) / 32768.0f;
            else if (bits == 24) {
                std::int32_t s = p[0] | (p[1] << 8) | (p[2] << 16);
                if (s & 0x800000) s |= ~0xffffff;
                v = static_cast<float>(s) / 8388608.0f;
            } else v = static_cast<float>(static_cast<std::int32_t>(get32(p))) / 2147483648.0f;
            (c == 0 ? out.left : out.right)[i] = v;
        }
        if (channels == 1) out.right[i] = out.left[i];
    }
    return true;
}

} // namespace maestruc
