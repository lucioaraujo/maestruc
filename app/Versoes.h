// Maestruc — aviso de versão nova dos instrumentos (18/11/2026).
//
// Decisões do autor (15/11/2026, "priorize sempre a segurança"):
//   1. só avisar: mostra a versão nova e o link da página oficial da release;
//      nunca baixa nem instala;
//   2. perguntar na primeira abertura; dá para desligar depois; sem internet,
//      nada muda;
//   3. HTTPS com certificado conferido;
//   4. endereços fixos (api.github.com, repositórios lucioaraujo/…), resposta
//      com limite de tamanho, notas não usadas, tempo limite, fora do áudio;
//   5. não travar o GitHub: sem conta são 60 consultas por hora por IP; aqui,
//      no máximo uma rodada por dia (8 consultas), resposta guardada, nenhuma
//      repetição automática em caso de erro.
//
// No Linux o build não tem o libcurl de desenvolvimento: a consulta usa o
// programa curl do sistema (certificado conferido por padrão), chamado sem
// shell, só HTTPS (--proto =https), sem seguir redirecionamento, com tempo e
// tamanho limitados. No Windows e no macOS, o HTTPS do próprio sistema.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <map>

namespace maestruc {

struct VersaoPublicada {
    juce::String versao;   // sem o "v"
    juce::String pagina;   // a página da release no GitHub
};

namespace versoes {

constexpr int kLimiteResposta = 256 * 1024;
constexpr int kTempoLimiteMs = 10000;
constexpr juce::int64 kIntervaloMs = 24LL * 60 * 60 * 1000;

// Só repositórios do autor, com nome simples: um catálogo do usuário não
// consegue apontar a consulta para outro lugar.
inline bool repositorioValido(const juce::String& repo) {
    if (!repo.startsWith("lucioaraujo/")) return false;
    const auto nome = repo.fromFirstOccurrenceOf("/", false, false);
    return nome.isNotEmpty() && nome.length() <= 64
           && nome.containsOnly("abcdefghijklmnopqrstuvwxyz0123456789-_.") && !nome.contains("..");
}

// "v0.1.10" → "0.1.10"; recusa o que não for número.número(.número…)
inline juce::String limpar(juce::String v) {
    v = v.trim();
    if (v.startsWithIgnoreCase("v")) v = v.substring(1);
    v = v.upToFirstOccurrenceOf("-", false, false).upToFirstOccurrenceOf("~", false, false).upToFirstOccurrenceOf("+", false, false);
    if (v.isEmpty() || v.length() > 32 || !v.containsOnly("0123456789.") || v.startsWithChar('.') || v.endsWithChar('.')
        || v.contains(".."))
        return {};
    return v;
}

// < 0 se a < b; 0 se iguais; > 0 se a > b (números por componente: 0.1.10 > 0.1.9)
inline int comparar(const juce::String& a, const juce::String& b) {
    const auto pa = juce::StringArray::fromTokens(limpar(a), ".", ""), pb = juce::StringArray::fromTokens(limpar(b), ".", "");
    for (int i = 0; i < juce::jmax(pa.size(), pb.size()); ++i) {
        const auto x = i < pa.size() ? pa[i].getLargeIntValue() : 0, y = i < pb.size() ? pb[i].getLargeIntValue() : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

inline bool haVersaoNova(const juce::String& instalada, const juce::String& publicada) {
    return limpar(instalada).isNotEmpty() && limpar(publicada).isNotEmpty() && comparar(instalada, publicada) < 0;
}

// Lê só o que interessa da resposta da API (tag_name e html_url) e confere.
inline bool interpretar(const juce::String& repo, const juce::String& json, VersaoPublicada& saida) {
    juce::var v;
    if (json.length() > kLimiteResposta || juce::JSON::parse(json, v).failed() || !v.isObject()) return false;
    const auto versao = limpar(v.getProperty("tag_name", {}).toString());
    const auto pagina = v.getProperty("html_url", {}).toString();
    if (versao.isEmpty() || !pagina.startsWith("https://github.com/" + repo + "/releases/tag/") || pagina.length() > 200
        || pagina.containsAnyOf(" \"'<>\\"))
        return false;
    saida = { versao, pagina };
    return true;
}

inline juce::String endereco(const juce::String& repo) { return "https://api.github.com/repos/" + repo + "/releases/latest"; }

// Uma consulta. Devolve o corpo da resposta (ou vazio).
inline juce::String consultar(const juce::String& repo) {
    if (!repositorioValido(repo)) return {};
#if JUCE_LINUX
    const juce::File curl("/usr/bin/curl");
    if (!curl.existsAsFile()) return {};
    juce::ChildProcess p;
    const juce::StringArray args { curl.getFullPathName(), "--proto", "=https", "--tlsv1.2", "--silent", "--fail",
                                   "--max-time", "10", "--max-filesize", juce::String(kLimiteResposta), "--max-redirs", "0",
                                   "-H", "Accept: application/vnd.github+json", "-A", "Maestruc", endereco(repo) };
    if (!p.start(args, juce::ChildProcess::wantStdOut)) return {};
    juce::MemoryOutputStream saida;
    char buf[4096];
    const auto fim = juce::Time::getMillisecondCounter() + kTempoLimiteMs + 2000;
    while (saida.getDataSize() <= static_cast<size_t>(kLimiteResposta)) {
        const int n = p.readProcessOutput(buf, sizeof(buf));
        if (n > 0) { saida.write(buf, static_cast<size_t>(n)); continue; }
        if (!p.isRunning() || juce::Time::getMillisecondCounter() > fim || juce::Thread::currentThreadShouldExit()) break;
        juce::Thread::sleep(20);
    }
    if (p.isRunning()) { p.kill(); return {}; }
    if (p.getExitCode() != 0 || saida.getDataSize() > static_cast<size_t>(kLimiteResposta)) return {};
    return saida.toUTF8();
#else
    auto in = juce::URL(endereco(repo)).createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(kTempoLimiteMs)
            .withNumRedirectsToFollow(0)
            .withExtraHeaders("Accept: application/vnd.github+json\r\nUser-Agent: Maestruc"));
    if (in == nullptr) return {};
    juce::MemoryOutputStream saida;
    saida.writeFromInputStream(*in, kLimiteResposta + 1);
    if (saida.getDataSize() > static_cast<size_t>(kLimiteResposta)) return {};
    return saida.toUTF8();
#endif
}

// O que fica guardado entre as aberturas: quando foi a última rodada e o que
// ela achou. Sem internet, a rodada falha e o guardado continua valendo.
inline juce::String guardar(juce::int64 quando, const std::map<juce::String, VersaoPublicada>& achadas) {
    auto* raiz = new juce::DynamicObject();
    raiz->setProperty("verificadoEm", quando);
    auto* lista = new juce::DynamicObject();
    for (auto& [repo, v] : achadas) {
        auto* x = new juce::DynamicObject();
        x->setProperty("versao", v.versao);
        x->setProperty("pagina", v.pagina);
        lista->setProperty(repo, juce::var(x));
    }
    raiz->setProperty("repositorios", juce::var(lista));
    return juce::JSON::toString(juce::var(raiz), true);
}

inline std::map<juce::String, VersaoPublicada> lerGuardado(const juce::String& texto, juce::int64& quando) {
    std::map<juce::String, VersaoPublicada> r;
    juce::var v;
    quando = 0;
    if (texto.isEmpty() || texto.length() > 64 * 1024 || juce::JSON::parse(texto, v).failed()) return r;
    quando = static_cast<juce::int64>(v.getProperty("verificadoEm", 0));
    if (auto* o = v.getProperty("repositorios", {}).getDynamicObject())
        for (auto& p : o->getProperties()) {
            const auto repo = p.name.toString();
            VersaoPublicada x { limpar(p.value.getProperty("versao", {}).toString()), p.value.getProperty("pagina", {}).toString() };
            if (repositorioValido(repo) && x.versao.isNotEmpty() && x.pagina.startsWith("https://github.com/" + repo + "/releases/"))
                r[repo] = x;
        }
    return r;
}

// Uma rodada de consultas num fio próprio; o resultado volta pelo fio de
// mensagens. Uma por vez; sem nova tentativa.
class Verificador final : private juce::Thread {
public:
    using Resultado = std::function<void(std::map<juce::String, VersaoPublicada>, int falhas)>;
    Verificador() : juce::Thread("Maestruc versoes") {}
    ~Verificador() override { stopThread(kTempoLimiteMs + 3000); }

    bool ocupado() const { return isThreadRunning(); }

    void rodar(const juce::StringArray& repositorios, Resultado aoTerminar) {
        if (isThreadRunning()) return;
        repos_ = repositorios;
        aoTerminar_ = std::move(aoTerminar);
        startThread(juce::Thread::Priority::low);
    }

private:
    void run() override {
        std::map<juce::String, VersaoPublicada> achadas;
        int falhas = 0;
        for (const auto& repo : repos_) {
            if (threadShouldExit()) return;
            VersaoPublicada v;
            if (interpretar(repo, consultar(repo), v)) achadas[repo] = v;
            else ++falhas;
        }
        auto cb = aoTerminar_;
        juce::MessageManager::callAsync([cb, achadas, falhas] { if (cb) cb(achadas, falhas); });
    }

    juce::StringArray repos_;
    Resultado aoTerminar_;
};

} // namespace versoes
} // namespace maestruc
