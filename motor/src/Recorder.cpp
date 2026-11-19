// Maestruc — gravação por pista.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
#include "Recorder.h"

#include <cmath>

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace maestruc {

Ring::Ring(std::size_t capacityFloats) {
    std::size_t n = 1;
    while (n < capacityFloats) n <<= 1;
    buf_.assign(n, 0.0f);
    mask_ = n - 1;
}

std::size_t Ring::available() const {
    return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
}

std::size_t Ring::freeSpace() const { return buf_.size() - available(); }

std::size_t Ring::write(const float* l, const float* r, std::size_t frames) {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t room = (buf_.size() - (head - tail_.load(std::memory_order_acquire))) / 2;
    const std::size_t n = std::min(frames, room);
    for (std::size_t i = 0; i < n; ++i) {
        buf_[(head + 2 * i) & mask_] = l ? l[i] : 0.0f;
        buf_[(head + 2 * i + 1) & mask_] = r ? r[i] : (l ? l[i] : 0.0f);
    }
    head_.store(head + 2 * n, std::memory_order_release);
    return n;
}

std::size_t Ring::read(float* dst, std::size_t maxFloats) {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t n = std::min(maxFloats, head_.load(std::memory_order_acquire) - tail);
    for (std::size_t i = 0; i < n; ++i) dst[i] = buf_[(tail + i) & mask_];
    tail_.store(tail + n, std::memory_order_release);
    return n;
}

namespace {
// Transparente até −1 dBFS; acima, tanh até 0 dBFS (contínua e com derivada 1
// no joelho). Evita o corte seco do PCM 24 quando uma influência sobe o stem.
void tetoSuave(float* x, std::size_t n) {
    constexpr float joelho = 0.891251f;          // −1 dBFS
    constexpr float folga = 0.999f - joelho;
    for (std::size_t i = 0; i < n; ++i) {
        const float a = std::fabs(x[i]);
        if (a <= joelho) continue;
        const float y = joelho + folga * std::tanh((a - joelho) / folga);
        x[i] = x[i] < 0.0f ? -y : y;
    }
}
} // namespace

bool Recorder::start(const std::string& dir, const std::vector<std::string>& trackNames,
                     int sampleRate, double ringSeconds, std::string& error, bool blocking) {
    stop();
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) { error = "não cria a pasta " + dir + ": " + ec.message(); return false; }
    // Folga de disco: ~1 GB por pista estéreo 24 bits/48 kHz por hora
    // (LEVANTAMENTO_CONEXOES.md). Exige pelo menos 1 GB livre para começar.
    const auto space = fs::space(dir, ec);
    if (!ec && space.available < (1ull << 30)) { error = "menos de 1 GB livre em " + dir; return false; }
    rings_.clear();
    writers_.clear();
    teto_.clear();
    const auto cap = static_cast<std::size_t>(std::max(0.25, ringSeconds) * sampleRate * 2);
    for (const auto& name : trackNames) {
        auto w = std::make_unique<WavWriter>();
        const std::string path = (fs::path(dir) / (name + ".wav")).string();
        if (!w->open(path, sampleRate)) { error = "não grava " + path; writers_.clear(); return false; }
        writers_.push_back(std::move(w));
        teto_.push_back(name != "master");
        rings_.push_back(std::make_unique<Ring>(cap));
    }
    scratch_.assign(16384, 0.0f);
    blocking_ = blocking;
    dropped_ = 0;
    writeError_ = false;
    stopRequested_ = false;
    running_ = true;
    thread_ = std::thread([this] { writerLoop(); });
    return true;
}

void Recorder::push(int track, const float* left, const float* right, int frames) {
    if (!running_.load(std::memory_order_relaxed) || track < 0
        || track >= static_cast<int>(rings_.size()) || frames <= 0)
        return;
    auto& ring = *rings_[static_cast<std::size_t>(track)];
    std::size_t done = ring.write(left, right, static_cast<std::size_t>(frames));
    while (blocking_ && done < static_cast<std::size_t>(frames) && !writeError_.load()) {
        std::this_thread::sleep_for(std::chrono::microseconds(200));
        done += ring.write(left ? left + done : nullptr, right ? right + done : nullptr,
                           static_cast<std::size_t>(frames) - done);
    }
    if (done < static_cast<std::size_t>(frames))
        dropped_.fetch_add(static_cast<std::size_t>(frames) - done, std::memory_order_relaxed);
}

bool Recorder::drain() {
    bool any = false;
    for (std::size_t t = 0; t < rings_.size(); ++t) {
        std::size_t n;
        while ((n = rings_[t]->read(scratch_.data(), scratch_.size())) > 0) {
            any = true;
            if (teto_[t]) tetoSuave(scratch_.data(), n);
            if (!writers_[t]->writeInterleaved(scratch_.data(), n / 2)) writeError_ = true;
        }
    }
    return any;
}

void Recorder::writerLoop() {
    while (!stopRequested_.load()) {
        if (!drain()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    drain();
}

bool Recorder::stop() {
    if (!running_.exchange(false)) return !writeError_.load();
    stopRequested_ = true;
    if (thread_.joinable()) thread_.join();
    bool ok = !writeError_.load();
    for (auto& w : writers_) ok = w->close() && ok;
    return ok;
}

std::uint64_t Recorder::framesWritten(int track) const {
    if (track < 0 || track >= static_cast<int>(writers_.size())) return 0;
    return writers_[static_cast<std::size_t>(track)]->frames();
}

} // namespace maestruc
