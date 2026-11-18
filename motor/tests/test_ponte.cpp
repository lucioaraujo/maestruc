// Maestruc — ensaios da ponte de som (receptor/PonteDeSom.h).
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
//
// Tempo simulado: o instrumento e o Maestruc rodam em "placas" com relógios
// levemente diferentes, num laço só, sem placa de som de verdade.
#include <cmath>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "LiveHost.h"
#include "PonteDeSom.h"

using namespace rasgo::maestruc;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "OK  " : "FALHA", what);
    if (!ok) ++failures;
}

struct Resultado {
    std::uint32_t faltas = 0, saltos = 0, perdidos = 0;
    double freqSaida = 0.0;      // medida por cruzamentos de zero, na 2ª metade
    double maiorSalto = 0.0;     // maior |x[n] - x[n-1]| na 2ª metade
    double saltoEsperado = 0.0;  // o de um seno perfeito
    double nivelMin = 1e9, nivelMax = 0.0, reserva = 0.0;
    double rms = 0.0;
    double razaoMin = 1e9, razaoMax = 0.0;   // velocidade de leitura, 2ª metade
};

// Instrumento a `taxaIn * (1 + desvio)` reais, blocos de `blocoIn`; Maestruc a
// `taxaOut`, blocos de `blocoOut`. Seno de `freq` Hz no relógio do instrumento.
Resultado simular(double taxaIn, double desvio, int blocoIn, double taxaOut, int blocoOut,
                  double freq, double segundos) {
    Resultado res;
    EmissorDeSom em;
    const std::string caminho = em.abrir("ensaio", 1, taxaIn);
    LeitorDeSom le;
    if (caminho.empty() || !le.abrir(caminho, taxaOut)) { check(false, "abrir a ponte"); return res; }
    le.pedirCalar(true);

    std::vector<float> inL(static_cast<std::size_t>(blocoIn)), inR(static_cast<std::size_t>(blocoIn));
    std::vector<float> outL(static_cast<std::size_t>(blocoOut)), outR(static_cast<std::size_t>(blocoOut));
    const double dtIn = blocoIn / (taxaIn * (1.0 + desvio));
    const double dtOut = blocoOut / taxaOut;
    double tIn = 0.0, tOut = 0.0, fase = 0.0;
    const double passo = 2.0 * M_PI * freq / taxaIn;
    std::vector<float> saida;
    saida.reserve(static_cast<std::size_t>(segundos * taxaOut) + 1024);
    while (tOut < segundos) {
        if (tIn <= tOut) {
            for (int i = 0; i < blocoIn; ++i) {
                const auto v = static_cast<float>(0.5 * std::sin(fase));
                fase += passo;
                if (fase > 2.0 * M_PI) fase -= 2.0 * M_PI;
                inL[static_cast<std::size_t>(i)] = v;
                inR[static_cast<std::size_t>(i)] = v;
            }
            const float* canais[2] = { inL.data(), inR.data() };
            em.escrever(canais, 2, blocoIn);
            tIn += dtIn;
        } else {
            float* canais[2] = { outL.data(), outR.data() };
            le.ler(canais, 2, blocoOut);
            saida.insert(saida.end(), outL.begin(), outL.end());
            if (tOut > segundos * 0.5) {
                res.nivelMin = std::min(res.nivelMin, le.nivel());
                res.nivelMax = std::max(res.nivelMax, le.nivel());
                res.razaoMin = std::min(res.razaoMin, le.razaoAtual());
                res.razaoMax = std::max(res.razaoMax, le.razaoAtual());
            }
            tOut += dtOut;
        }
    }
    res.reserva = std::max(4.0 * blocoOut * taxaIn / taxaOut, 0.03 * taxaIn);
    res.faltas = le.faltas();
    res.saltos = le.saltos();
    res.perdidos = le.perdidosNoInstrumento();
    const std::size_t meio = saida.size() / 2;
    int cruzamentos = 0;
    double soma = 0.0;
    std::size_t primeiro = 0, ultimo = 0;
    for (std::size_t i = meio + 1; i < saida.size(); ++i) {
        if (saida[i - 1] < 0.0f && saida[i] >= 0.0f) {
            if (cruzamentos == 0) primeiro = i;
            ultimo = i;
            ++cruzamentos;
        }
        res.maiorSalto = std::max(res.maiorSalto, static_cast<double>(std::fabs(saida[i] - saida[i - 1])));
        soma += static_cast<double>(saida[i]) * saida[i];
    }
    if (cruzamentos > 1) res.freqSaida = (cruzamentos - 1) * taxaOut / static_cast<double>(ultimo - primeiro);
    res.rms = std::sqrt(soma / static_cast<double>(saida.size() - meio));
    res.saltoEsperado = 0.5 * 2.0 * M_PI * freq * (1.0 + desvio) / taxaOut;
    return res;
}

void testDesvio(double desvio, const char* nome) {
    const auto r = simular(48000.0, desvio, 256, 48000.0, 512, 440.0, 60.0);
    const double esperado = 440.0 * (1.0 + desvio);
    std::printf("      %s: faltas %u, saltos %u, perdidos %u, freq %.4f (esperado %.4f), nível %.0f…%.0f (reserva %.0f), maior salto %.4f (seno %.4f)\n",
                nome, r.faltas, r.saltos, r.perdidos, r.freqSaida, esperado, r.nivelMin, r.nivelMax, r.reserva,
                r.maiorSalto, r.saltoEsperado);
    check(r.faltas == 0 && r.saltos == 0 && r.perdidos == 0, "desvio de relógio: nenhuma falta, salto ou perda em 60 s");
    const double erroCents = 1200.0 * std::fabs(std::log2(r.freqSaida / esperado));
    std::printf("      erro de afinação médio: %.3f cent\n", erroCents);
    check(erroCents < 1.0, "desvio de relógio: a afinação sai certa (erro < 1 cent)");
    check(r.nivelMin > r.reserva * 0.5 && r.nivelMax < r.reserva * 1.6, "desvio de relógio: o anel fica perto da reserva");
    check(r.maiorSalto < r.saltoEsperado * 1.05, "desvio de relógio: sem estalo (nenhum salto maior que o do seno)");
    const double cents = 1200.0 * std::log2(r.razaoMax / r.razaoMin);
    std::printf("      variação da velocidade de leitura: %.3f cent\n", cents);
    check(cents < 1.0, "desvio de relógio: a afinação oscila menos de 1 cent (inaudível)");
}

void testTaxas() {
    const auto r = simular(44100.0, 0.0001, 441, 48000.0, 256, 1000.0, 30.0);
    std::printf("      44,1 → 48 kHz: faltas %u, saltos %u, freq %.3f, rms %.4f, maior salto %.4f (seno %.4f)\n",
                r.faltas, r.saltos, r.freqSaida, r.rms, r.maiorSalto, r.saltoEsperado);
    check(r.faltas == 0 && r.saltos == 0, "44,1 → 48 kHz: sem falta nem salto");
    check(1200.0 * std::fabs(std::log2(r.freqSaida / 1000.1)) < 1.0, "44,1 → 48 kHz: 1 kHz continua 1 kHz (erro < 1 cent)");
    check(std::fabs(r.rms - 0.5 / std::sqrt(2.0)) < 0.005, "44,1 → 48 kHz: o nível se conserva");
    check(r.maiorSalto < r.saltoEsperado * 1.05, "44,1 → 48 kHz: sem estalo");
}

void testCalar() {
    EmissorDeSom em;
    const auto caminho = em.abrir("calar", 1, 48000.0);
    LeitorDeSom le;
    le.abrir(caminho, 48000.0);
    std::vector<float> b(256, 0.1f), o(256);
    const float* in[2] = { b.data(), b.data() };
    float* out[2] = { o.data(), o.data() };
    check(!em.escrever(in, 2, 256), "sem pedido, o instrumento toca sozinho");
    le.pedirCalar(true);
    le.ler(out, 2, 256);
    check(em.escrever(in, 2, 256), "com o Maestruc lendo e pedindo, o instrumento cala a própria saída");
    // O Maestruc some: ~0,5 s de blocos sem pulso devolvem o som ao instrumento.
    bool calado = true;
    int blocos = 0;
    while (calado && blocos < 400) { calado = em.escrever(in, 2, 256); ++blocos; }
    std::printf("      o instrumento voltou a tocar depois de %d blocos (%.2f s)\n", blocos, blocos * 256 / 48000.0);
    check(!calado && blocos * 256 / 48000.0 < 0.6, "Maestruc parado: o instrumento volta a tocar em menos de 0,6 s");
    le.ler(out, 2, 256);
    check(em.escrever(in, 2, 256), "Maestruc de volta: o pedido vale de novo");
    le.fechar();
    // fechar o leitor tira o pedido
    check(!em.escrever(in, 2, 256), "Maestruc fechou a ponte: o instrumento toca sozinho");
}

void testSeguranca() {
    LeitorDeSom le;
    check(!le.abrir("/etc/passwd", 48000.0), "recusa caminho fora da pasta das pontes");
    const std::string pasta = ponte::pastaDasPontes();
    check(!pasta.empty(), "a pasta das pontes existe e é só do usuário");
    check(!le.abrir(pasta + "/../x.pds", 48000.0), "recusa caminho com ..");
    // arquivo com mágica errada
    const std::string falso = pasta + "/falso-ensaio.pds";
    {
        std::FILE* f = std::fopen(falso.c_str(), "wb");
        std::vector<char> lixo(ponte::tamanhoTotal(2, 1024), 'x');
        std::fwrite(lixo.data(), 1, lixo.size(), f);
        std::fclose(f);
        ::chmod(falso.c_str(), 0600);
    }
    check(!le.abrir(falso, 48000.0), "recusa arquivo sem a assinatura da ponte");
    ::unlink(falso.c_str());
    // permissões abertas
    EmissorDeSom em;
    const auto caminho = em.abrir("permissao", 1, 48000.0);
    ::chmod(caminho.c_str(), 0644);
    check(!le.abrir(caminho, 48000.0), "recusa arquivo que outros usuários podem ler");
    ::chmod(caminho.c_str(), 0600);
    check(le.abrir(caminho, 48000.0), "aceita a ponte certa");
    le.fechar();
    em.fechar();
    struct stat st {};
    check(::stat(caminho.c_str(), &st) != 0, "o instrumento apaga o arquivo ao fechar");
}

// Um instrumento fechado à força (kill, travamento) não apaga a ponte; a
// próxima abertura apaga as pontes de processos que já não existem.
void testOrfaos() {
    const std::string pasta = ponte::pastaDasPontes();
    const pid_t filho = ::fork();
    if (filho == 0) ::_exit(0);
    ::waitpid(filho, nullptr, 0);   // o processo acabou: o pid está livre
    auto criar = [&pasta](const std::string& nome) {
        const auto c = pasta + "/" + nome;
        std::FILE* f = std::fopen(c.c_str(), "wb");
        if (f != nullptr) { std::fputs("x", f); std::fclose(f); }
        ::chmod(c.c_str(), 0600);
        return c;
    };
    const auto orfao = criar("Orfao-" + std::to_string(filho) + "-123.pds");
    const auto vivo = criar("Vivo-" + std::to_string(::getpid()) + "-456.pds");
    const auto outro = criar("nao-e-ponte.txt");
    check(ponte::processoDoNome("Asterismus-10972-1794768567759030003.pds") == 10972, "o nome da ponte dá o processo");
    check(ponte::processoDoNome("sessao.txt") == 0 && ponte::processoDoNome("x-y-1.pds") == 0, "nomes alheios não dão processo");
    EmissorDeSom em;
    const auto caminho = em.abrir("Novo", 1, 48000.0);
    struct stat st {};
    check(::stat(orfao.c_str(), &st) != 0, "a ponte de um processo que acabou é apagada");
    check(::stat(vivo.c_str(), &st) == 0, "a ponte de um processo vivo fica");
    check(::stat(outro.c_str(), &st) == 0, "o que não é ponte fica");
    check(::stat(caminho.c_str(), &st) == 0, "a ponte nova abre");
    em.fechar();
    ::unlink(vivo.c_str());
    ::unlink(outro.c_str());
}

void testCanais() {
    EmissorDeSom em;
    const char* nomes[] = { "mix", "bumbo", "baixo" };
    const auto caminho = em.abrir("canais", 3, 48000.0, nomes);
    LeitorDeSom le;
    check(le.abrir(caminho, 48000.0) && le.pares() == 3, "três pares estéreo");
    check(le.nomeDoPar(1) == "bumbo" && le.nomeDoPar(2) == "baixo", "nomes dos canais chegam ao Maestruc");
    // cada par leva o seu sinal
    std::vector<float> a(512, 0.1f), b(512, 0.2f), c(512, 0.3f), o(512);
    const float* in[6] = { a.data(), a.data(), b.data(), b.data(), c.data(), c.data() };
    for (int i = 0; i < 20; ++i) em.escrever(in, 6, 512);
    std::vector<std::vector<float>> outs(6, std::vector<float>(256));
    float* out[6];
    for (int k = 0; k < 6; ++k) out[k] = outs[static_cast<std::size_t>(k)].data();
    le.ler(out, 6, 256);
    check(std::fabs(outs[0][100] - 0.1f) < 1e-4f && std::fabs(outs[2][100] - 0.2f) < 1e-4f
              && std::fabs(outs[5][100] - 0.3f) < 1e-4f,
          "cada canal chega separado");
}

void testHostPortatil() {
    // O Maestruc sem JACK: dois apps por ponte, um com dois canais internos.
    maestruc::LiveHost host;
    std::string err;
    check(host.openPortable(48000, 1024, 7, err), "host portátil abre (sem JACK)");
    EmissorDeSom a, b;
    const char* nomesB[] = { "mix", "bumbo" };
    const auto pa = a.abrir("AppA", 1, 48000.0);
    const auto pb = b.abrir("AppB", 2, 44100.0, nomesB);
    const auto sa = host.attachBridge("AppA", pa, err);
    const auto sb = host.attachBridge("AppB", pb, err);
    check(sa.size() == 1 && sb.size() == 2, "cada par estéreo da ponte vira um stem");
    check(host.stemIndex("AppB/bumbo") == sb[1], "o canal interno ganha nome próprio (AppB/bumbo)");
    check(host.attachBridge("Intruso", "/tmp/qualquer.pds", err).empty(), "o host recusa ponte fora da pasta");
    // Só o bumbo do B deve soar no master: A cala, B/mix cala.
    host.mixer().strip(sa[0]).mute = true;
    host.mixer().strip(sb[0]).mute = true;
    std::vector<float> ia(256), ib0(441), ib1(441), oL(512), oR(512);
    double fa = 0.0, fb = 0.0;
    bool calarA = false, calarB = false;
    double tIn = 0.0, tInB = 0.0, tOut = 0.0, soma = 0.0;
    int amostras = 0;
    while (tOut < 3.0) {
        if (tIn <= tOut) {
            for (auto& v : ia) { v = static_cast<float>(0.3 * std::sin(fa)); fa += 2.0 * M_PI * 220.0 / 48000.0; }
            const float* ca[2] = { ia.data(), ia.data() };
            calarA = a.escrever(ca, 2, 256);
            tIn += 256.0 / 48000.0;
        } else if (tInB <= tOut) {
            for (std::size_t i = 0; i < ib0.size(); ++i) {
                ib0[i] = 0.0f;
                ib1[i] = static_cast<float>(0.4 * std::sin(fb));
                fb += 2.0 * M_PI * 110.0 / 44100.0;
            }
            const float* cb[4] = { ib0.data(), ib0.data(), ib1.data(), ib1.data() };
            calarB = b.escrever(cb, 4, 441);
            tInB += 441.0 / 44100.0;
        } else {
            float* out[4] = { oL.data(), oR.data(), nullptr, nullptr };
            host.processPortable(out, 512);
            if (tOut > 1.5) for (float v : oL) { soma += static_cast<double>(v) * v; ++amostras; }
            tOut += 512.0 / 48000.0;
        }
    }
    const double rms = std::sqrt(soma / std::max(1, amostras));
    std::printf("      master (só o bumbo do B, 0,4 de pico): rms %.4f\n", rms);
    check(calarA && calarB, "com a ponte ligada, os dois apps calam a própria saída");
    check(std::fabs(rms - 0.4 / std::sqrt(2.0) * std::sqrt(0.5) * std::sqrt(2.0)) < 0.03, "o master tem o bumbo do B, no nível certo");
    const auto st = host.bridgeStatus("AppB");
    check(st.open && st.gaps == 0 && st.jumps == 0 && st.lost == 0, "ponte do B sem falta, salto ou perda");
    host.detachBridge("AppA");
    const float* ca[2] = { ia.data(), ia.data() };
    check(!a.escrever(ca, 2, 256), "ponte solta: o app A volta a tocar sozinho");
    host.close();
    const float* cb[4] = { ib0.data(), ib0.data(), ib1.data(), ib1.data() };
    check(!b.escrever(cb, 4, 441), "Maestruc fechado: o app B volta a tocar sozinho");
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    testDesvio(0.0003, "+300 ppm");
    testDesvio(-0.0003, "−300 ppm");
    testDesvio(0.0, "0 ppm");
    {
        // Sessão longa: 10 minutos a +300 ppm, blocos diferentes dos dois lados.
        const auto r = simular(48000.0, 0.0003, 128, 48000.0, 1024, 220.0, 600.0);
        const double cents = 1200.0 * std::log2(r.razaoMax / r.razaoMin);
        std::printf("      10 min: faltas %u, saltos %u, perdidos %u, nível %.0f…%.0f (reserva %.0f), oscilação %.3f cent\n",
                    r.faltas, r.saltos, r.perdidos, r.nivelMin, r.nivelMax, r.reserva, cents);
        check(r.faltas == 0 && r.saltos == 0 && r.perdidos == 0 && r.nivelMin > r.reserva * 0.5
                  && r.nivelMax < r.reserva * 1.6 && cents < 1.0,
              "10 minutos: estável, sem falta, sem salto, afinação firme");
    }
    testTaxas();
    testCalar();
    testSeguranca();
    testOrfaos();
    testCanais();
    testHostPortatil();
    std::printf("%s: %d falha(s)\n", failures ? "FALHOU" : "tudo OK", failures);
    return failures ? 1 : 0;
}
