// Maestruc — catálogo dos instrumentos (18/11/2026): o arquivo embutido é
// válido; cada instrumento publicado diz onde o pacote oficial instala nos
// três sistemas, com nomes sem caminho; a leitura do dpkg acha um pacote real
// e não inventa um ausente; a fonte de desenvolvimento acha os scripts.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
#include "Catalogo.h"
#include "Versoes.h"
#include "Processos.h"

#include <cstdio>
#include <cstdlib>

namespace {
int falhas = 0;
void conferir(const char* nome, bool ok) {
    std::printf("%s %s\n", ok ? "ok  " : "FALHA", nome);
    if (!ok) ++falhas;
}
} // namespace

int main() {
    using namespace maestruc;
    juce::String erro;
    const auto dados = detalhe::lerJson(
        juce::String::fromUTF8(DadosMaestruc::catalogo_json, DadosMaestruc::catalogo_jsonSize), erro);
    conferir("o catálogo embutido é JSON válido no formato maestruc.catalogo/1", erro.isEmpty());

    juce::StringArray ids;
    int publicados = 0;
    bool camposOk = true, instalacaoOk = true;
    if (auto* lista = dados.getProperty("instrumentos", {}).getArray()) {
        for (auto& x : *lista) {
            const auto id = x.getProperty("id", {}).toString();
            camposOk = camposOk && id.isNotEmpty() && !ids.contains(id)
                       && x.getProperty("nome", {}).toString().isNotEmpty()
                       && x.getProperty("desenvolvimento", {}).getProperty("comando", {}).toString().isNotEmpty();
            ids.add(id);
            const auto inst = x.getProperty("instalado", {});
            if (!inst.isObject()) continue;
            ++publicados;
            const juce::String lx = inst.getProperty("linux", {}).getProperty("programa", {}).toString();
            const juce::String pacote = inst.getProperty("linux", {}).getProperty("pacote", {}).toString();
            const juce::String pasta = inst.getProperty("windows", {}).getProperty("pasta", {}).toString();
            const juce::String exe = inst.getProperty("windows", {}).getProperty("programa", {}).toString();
            const juce::String app = inst.getProperty("macos", {}).getProperty("app", {}).toString();
            const juce::String repo = x.getProperty("repositorio", {}).toString();
            const juce::String pagina = x.getProperty("pagina", {}).toString();
            instalacaoOk = instalacaoOk && lx.startsWith("/usr/bin/") && pacote.isNotEmpty()
                           && pasta.isNotEmpty() && !pasta.containsAnyOf("\\/:")
                           && exe.endsWith(".exe") && !exe.containsAnyOf("\\/:")
                           && app.endsWith(".app") && !app.containsChar('/')
                           && repo.startsWith("lucioaraujo/") && pagina.startsWith("https://");
            if (!instalacaoOk) std::printf("      incompleto: %s\n", id.toRawUTF8());
        }
    }
    conferir("cada instrumento tem id único, nome e comando de desenvolvimento", camposOk);
    conferir("os 8 instrumentos publicados dizem onde o pacote instala (Linux, Windows, macOS) e a página oficial",
             publicados == 8 && instalacaoOk);

    conferir("dpkg: um pacote ausente não tem versão", detalhe::versaoDpkg("maestruc-pacote-que-nao-existe").isEmpty());
    const auto dpkgReal = detalhe::versaoDpkg("libc6");
    conferir("dpkg: um pacote instalado tem versão (libc6)",
             !juce::File("/var/lib/dpkg/status").existsAsFile() || dpkgReal.isNotEmpty());

    juce::String aviso;
    const auto dev = carregarCatalogo(Fonte::Desenvolvimento, aviso);
    int achados = 0;
    for (auto& e : dev) achados += e.achado() ? 1 : 0;
    std::printf("      desenvolvimento: %d de %d acham o script ou build\n", achados, dev.size());
    conferir("desenvolvimento: os 9 aparecem (o Risco inclusive)", dev.size() == 9);
    const auto inst = carregarCatalogo(Fonte::Instalados, aviso);
    conferir("instalados: só os 8 publicados aparecem", inst.size() == 8);
    for (auto& e : inst)
        std::printf("      %-12s %s %s\n", e.nome.toRawUTF8(),
                    e.achado() ? e.programa.getFullPathName().toRawUTF8() : "(não instalado)",
                    e.versaoInstalada.isNotEmpty() ? e.versaoInstalada.toRawUTF8() : "");
    conferir("o Modular de desenvolvimento leva --fg (os args do desenvolvimento valem)",
             [&] { for (auto& e : dev) if (e.nome == "Rasgo Modular") return e.argsFixos.contains("--fg"); return false; }());

    // Versão nova (18/11/2026): comparação, validação e o guardado.
    using namespace versoes;
    conferir("versões: 0.1.10 > 0.1.9, v0.3.1 > 0.3.0, iguais não avisam",
             comparar("0.1.10", "0.1.9") > 0 && haVersaoNova("0.3.0", "v0.3.1") && !haVersaoNova("0.2.1", "v0.2.1")
                 && !haVersaoNova("0.5.2", "0.5.1"));
    conferir("versões: o que não é número.número é recusado (não avisa)",
             limpar("v1.2.x").isEmpty() && limpar("../1").isEmpty() && !haVersaoNova("", "0.2.0") && limpar("0.1.0-1") == "0.1.0");
    conferir("repositórios: só lucioaraujo/<nome simples>",
             repositorioValido("lucioaraujo/batruc") && !repositorioValido("outro/batruc")
                 && !repositorioValido("lucioaraujo/../x") && !repositorioValido("lucioaraujo/a b"));
    VersaoPublicada v;
    conferir("resposta da API: tag e página da release do próprio repositório",
             interpretar("lucioaraujo/batruc",
                         R"({"tag_name":"v0.1.5","html_url":"https://github.com/lucioaraujo/batruc/releases/tag/v0.1.5","body":"<script>"})", v)
                 && v.versao == "0.1.5" && v.pagina == "https://github.com/lucioaraujo/batruc/releases/tag/v0.1.5");
    conferir("resposta da API: página de outro lugar é recusada",
             !interpretar("lucioaraujo/batruc", R"({"tag_name":"v9","html_url":"https://exemplo.com/x"})", v)
                 && !interpretar("lucioaraujo/batruc", "nao e json", v));
    std::map<juce::String, VersaoPublicada> m { { "lucioaraujo/trioio", { "0.2.1", "https://github.com/lucioaraujo/trioio/releases/tag/v0.2.1" } },
                                                { "outro/x", { "1.0", "https://github.com/outro/x/releases/tag/v1" } } };
    juce::int64 quando = 0;
    const auto lido = lerGuardado(guardar(1234567, m), quando);
    conferir("guardado: volta a data e só o que é válido", quando == 1234567 && lido.size() == 1 && lido.count("lucioaraujo/trioio") == 1);

    // Ambiente no catálogo (18/11/2026): só nomes *_MAESTRUC; o Modular o usa.
    {
        const auto json = juce::JSON::parse(R"({"RASGO_MAESTRUC":"1","LD_PRELOAD":"x","_MAESTRUC":"1","A_MAESTRUC":2})");
        const auto amb = detalhe::ambiente(json);
        conferir("ambiente: aceita só *_MAESTRUC com valor texto", amb.size() == 1 && amb["RASGO_MAESTRUC"] == "1");
        juce::String avisoAmb;
        bool modular = false;
        for (auto& e : carregarCatalogo(Fonte::Instalados, avisoAmb))
            if (e.id == "rasgo-modular") modular = e.ambiente["RASGO_MAESTRUC"] == "1";
        auto todos = carregarCatalogo(Fonte::Desenvolvimento, avisoAmb);
        for (auto& e : todos) if (e.id == "rasgo-modular") modular = modular || e.ambiente["RASGO_MAESTRUC"] == "1";
        conferir("ambiente: o Rasgo Modular abre com RASGO_MAESTRUC=1", modular);
    }

    // Processos (18/11/2026, agora com uma implementação por sistema): abrir
    // desprendido, achar pelo executável, medir, fechar com pedido gentil.
#if ! JUCE_WINDOWS
    {
        EntradaCatalogo sono;
        sono.nome = "ensaio-sleep";
        sono.programa = juce::File(juce::File("/usr/bin/sleep").existsAsFile() ? "/usr/bin/sleep" : "/bin/sleep");
        sono.argsFixos = { "30" };
        juce::String erroAbrir;
        Processo p;
        p.lancador = abrirApp(sono, {}, erroAbrir);
        p.pid = static_cast<int>(p.lancador);
        conferir("processos: abre desprendido e o pid está vivo", p.pid > 0 && processoVivo(p.pid));
        juce::Thread::sleep(200);
        medirCpu(p);
        juce::Thread::sleep(300);
        medirCpu(p);
        conferir("processos: mede a CPU (quase zero para um sleep)", p.cpu >= 0.0 && p.cpu < 0.5);
        conferir("processos: acha um pid pelo executável", acharPid(sono.programa.getFullPathName()) > 0);
        fecharApp(p);
        bool saiu = false;
        for (int i = 0; i < 50 && !saiu; ++i) { saiu = lancadorSaiu(p.lancador); if (!saiu) juce::Thread::sleep(50); }
        conferir("processos: fecha com SIGTERM e não fica zumbi", saiu && !processoVivo(p.pid));
        pastaLogs().getChildFile("ensaio-sleep.log").deleteFile();   // o ensaio não deixa rastro
        // ambiente do catálogo (18/11/2026): o filho recebe a variável
        EntradaCatalogo amb;
        amb.nome = "ensaio-ambiente";
        amb.programa = juce::File(juce::File("/usr/bin/env").existsAsFile() ? "/usr/bin/env" : "/bin/env");
        amb.ambiente.set("ENSAIO_MAESTRUC", "1");
        Processo pa;
        pa.lancador = abrirApp(amb, {}, erroAbrir);
        bool fim = false;
        for (int i = 0; i < 60 && !fim; ++i) { fim = lancadorSaiu(pa.lancador); if (!fim) juce::Thread::sleep(50); }
        const auto saida = pastaLogs().getChildFile("ensaio-ambiente.log");
        conferir("processos: o app aberto recebe o ambiente do catálogo",
                 fim && saida.loadFileAsString().contains("ENSAIO_MAESTRUC=1") && saida.loadFileAsString().contains("PATH="));
        saida.deleteFile();
        EntradaCatalogo nada;
        nada.nome = "ausente";
        conferir("processos: um app não instalado não abre, e diz por quê", abrirApp(nada, {}, erroAbrir) == 0 && erroAbrir.isNotEmpty());
    }
#endif
    conferir("memória livre medida", memoriaLivreGb() > 0.0);
    return falhas == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
