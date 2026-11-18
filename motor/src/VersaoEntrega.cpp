// Maestruc — versão de entrega (streaming) do Master gravado.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
#include "VersaoEntrega.h"

#include "Wav.h"
#include "saida/DeliveryMaster.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace maestruc {
namespace {

// o master.wav do WavWriter: PCM 24, estéreo, dados a partir do byte 44
class LeitorPcm24 final : public saida::DeliveryMaster::Source {
public:
    explicit LeitorPcm24(const std::string& caminho) : f_(std::fopen(caminho.c_str(), "rb")) {
        if (!f_) return;
        unsigned char h[44] {};
        if (std::fread(h, 1, 44, f_) != 44) { std::fclose(f_); f_ = nullptr; return; }
        const auto u16 = [&](int i) { return static_cast<unsigned>(h[i] | (h[i + 1] << 8)); };
        const auto u32 = [&](int i) { return static_cast<std::uint32_t>(h[i] | (h[i + 1] << 8) | (h[i + 2] << 16))
                                           | (static_cast<std::uint32_t>(h[i + 3]) << 24); };
        if (std::string(reinterpret_cast<char*>(h), 4) != "RIFF" || std::string(reinterpret_cast<char*>(h + 36), 4) != "data"
            || u16(20) != 1 || u16(22) != 2 || u16(34) != 24) { std::fclose(f_); f_ = nullptr; return; }
        taxa_ = static_cast<int>(u32(24));
    }
    ~LeitorPcm24() override { if (f_) std::fclose(f_); }
    bool aberto() const { return f_ != nullptr && taxa_ > 0; }
    int taxa() const { return taxa_; }
    bool rewind() override { return f_ && std::fseek(f_, 44, SEEK_SET) == 0; }
    std::size_t read(float* l, float* r, std::size_t quadros) override {
        bytes_.resize(quadros * 6);
        const std::size_t n = std::fread(bytes_.data(), 6, quadros, f_);
        for (std::size_t i = 0; i < n; ++i) {
            const auto amostra = [&](std::size_t k) {
                const unsigned char* p = &bytes_[i * 6 + k * 3];
                std::int32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
                if (v & 0x800000) v -= 0x1000000;
                return static_cast<float>(v) / 8388607.0f;
            };
            l[i] = amostra(0);
            r[i] = amostra(1);
        }
        return n;
    }
private:
    std::FILE* f_ = nullptr;
    int taxa_ = 0;
    std::vector<unsigned char> bytes_;
};

class EscritorWav final : public saida::DeliveryMaster::Sink {
public:
    WavWriter w;
    std::vector<float> intercalado;
    bool write(const float* l, const float* r, std::size_t quadros) override {
        intercalado.resize(quadros * 2);
        for (std::size_t i = 0; i < quadros; ++i) { intercalado[2 * i] = l[i]; intercalado[2 * i + 1] = r[i]; }
        return w.writeInterleaved(intercalado.data(), quadros);
    }
};

} // namespace

bool escreverVersaoDeEntrega(const std::string& masterWav, std::string& resumo) {
    namespace fs = std::filesystem;
    LeitorPcm24 leitor(masterWav);
    if (!leitor.aberto()) { resumo = "master.wav ilegível"; return false; }
    const fs::path origem(masterWav);
    const fs::path destino = origem.parent_path() / (origem.stem().string() + "-streaming.wav");
    EscritorWav escritor;
    if (!escritor.w.open(destino.string(), leitor.taxa())) { resumo = "não abriu " + destino.string(); return false; }
    const auto rel = saida::DeliveryMaster::process(leitor, static_cast<double>(leitor.taxa()), escritor);
    const bool fechou = escritor.w.close();
    if (rel.failed || !fechou) {
        std::error_code ec;
        fs::remove(destino, ec);
        resumo = "falha ao escrever a versão de entrega";
        return false;
    }
    std::ostringstream t;
    t << "Maestruc - versao de entrega (streaming), saida-ref 1.2\n"
      << "master: " << origem.filename().string() << "\n"
      << "alvo: -14 LUFS, true peak <= -1 dBTP; limitador no maximo 6 dB (guarda de dinamica)\n"
      << "entrada: " << rel.inputLufs << " LUFS, " << rel.inputTruePeakDb << " dBTP\n"
      << "ganho: " << rel.gainDb << " dB; limitador: " << rel.limiterReductionDb << " dB\n"
      << "saida: " << rel.outputLufs << " LUFS, " << rel.outputTruePeakDb << " dBTP\n"
      << (rel.silent ? "silencio: copiada como veio\n"
          : rel.reachedTarget ? "alvo atingido\n"
          : "alvo nao atingido sem esmagar a dinamica: entregue no mais alto possivel (os servicos sobem faixas baixas no modo normal)\n");
    std::ofstream(fs::path(destino).replace_extension(".txt")) << t.str();
    resumo = t.str();
    return true;
}

} // namespace maestruc
