// Maestruc — os processos dos instrumentos: abrir desprendido, achar pelo
// executável, medir a CPU, fechar com pedido gentil, memória livre.
// Até 18/11/2026 isto só existia para o Linux (/proc, posix_spawn); agora tem
// uma implementação para cada sistema em Processos.cpp (Linux, macOS,
// Windows), atrás da mesma interface.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
#pragma once

#include "Catalogo.h"

#include <juce_core/juce_core.h>

namespace maestruc {

// Um app aberto pelo Maestruc (ou achado já aberto).
struct Processo {
    juce::int64 lancador = 0;   // o que o Maestruc abriu (o script pode virar o app por exec)
    int pid = 0;                // o app de verdade
    juce::uint64 cpuNs = 0;     // tempo de CPU acumulado na última medida
    double medidoEm = 0.0;
    double cpu = 0.0;           // núcleos
};

// Abre o app desprendido do Maestruc: fechar a janela do Maestruc não fecha
// os instrumentos. Devolve o pid (0 se não abriu, com o motivo em `erro`).
// Saída do app em <logs>/<nome>.log (Linux e macOS).
juce::int64 abrirApp(const EntradaCatalogo& e, const juce::StringArray& extras, juce::String& erro);

// Recolhe um lançador que terminou (sem zumbis). true se `pid` saiu.
bool lancadorSaiu(juce::int64 pid);

bool processoVivo(int pid);

// O pid de um app pelo caminho do executável (para apps cujo script se
// desprende sozinho, como o Rasgo Modular, ou abertos fora do Maestruc).
int acharPid(const juce::String& trechoDoExecutavel);

// CPU do processo inteiro (interface incluída), em núcleos.
void medirCpu(Processo& p);

// Pede para fechar (SIGTERM; no Windows, WM_CLOSE nas janelas do app).
void fecharApp(Processo& p);

double memoriaLivreGb();

// CPU do sistema inteiro (Maestruc, apps e o resto: Xorg, navegador…), em
// núcleos. Mede no máximo a cada 0,5 s; antes da 2ª medida devolve 0.
double cpuSistema();

// Pasta dos logs dos apps abertos pelo Maestruc: na pasta de dados do
// usuário (até 18/11/2026 ficava no código-fonte, que não existe numa
// instalação).
inline juce::File pastaLogs() {
    return rasgo::folders::appData("Maestruc").getChildFile("logs");   // convenção, 18/11/2026
}

// Nome do log de um app: sem espaços nem acentos ("rasgo-modular.log").
inline juce::String nomeDoLog(const EntradaCatalogo& e) { return rasgo::folders::safeName(e.nome, "app") + ".log"; }

// Se o script do app está compilando, o último "[ NN%]" do log dele (ou vazio).
inline juce::String progressoCompilacao(const EntradaCatalogo& e) {
    const auto log = pastaLogs().getChildFile(nomeDoLog(e));
    juce::FileInputStream in(log);
    if (!in.openedOk()) return {};
    const auto tam = in.getTotalLength();
    in.setPosition(juce::jmax<juce::int64>(0, tam - 4096));
    const auto fim = in.readEntireStreamAsString();
    const int i = fim.lastIndexOf("%]");
    if (i < 0) return {};
    const int j = fim.substring(0, i).lastIndexOfChar('[');
    if (j < 0 || i - j > 6) return {};
    return fim.substring(j + 1, i).trim() + "%";
}

} // namespace maestruc
