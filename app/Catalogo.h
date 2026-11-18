// Maestruc — catálogo dos instrumentos da família: como abrir cada um, como
// achar o som dele no PipeWire e o que ele aceita ao abrir. Cada app roda no
// seu processo; o Maestruc só o abre, mede e fecha. Desde 18/11/2026 os dados
// ficam em app/catalogo.json (embutido), aberto a instrumentos futuros.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
#pragma once

#include <juce_core/juce_core.h>
#include <DadosMaestruc.h>

namespace maestruc {

// Onde procurar os instrumentos (18/11/2026, decisão do autor de 15/11: "os
// usuários irão usar os instrumentos já instalados em suas máquinas"; as
// pastas de desenvolvimento ficam como opção). MAESTRUC_FONTE=desenvolvimento
// (o ./run do código-fonte o põe) ou a preferência "fonte".
enum class Fonte { Instalados, Desenvolvimento };

struct EntradaCatalogo {
    juce::String id;            // identificador estável (o catálogo do usuário substitui por ele)
    juce::String nome;          // como aparece na janela e no hello do receptor
    juce::String grupo;         // A síntese, B composição ao vivo, C composição
    juce::String repositorio;   // lucioaraujo/… (releases oficiais); vazio = não publicado
    juce::String pagina;        // página oficial (como instalar)
    juce::StringArray argsFixos;
    juce::String padraoSom;     // expressão do JACK para achar as saídas de áudio
    juce::String abertura;      // "batruc": --abrir/--seed hex; "modular": seed decimal posicional;
                                // "seed": --seed decimal; "navalha": --abrir projeto; "" = nada
    juce::String nota;          // o que o app aceita do Maestruc hoje
    // resolvidos para a fonte escolhida
    juce::File programa;        // o executável (ou script de desenvolvimento); vazio = não achado
    juce::String processo;      // parte do caminho do executável, para achar o pid
    juce::String versaoInstalada;   // do sistema de pacotes; vazio = desconhecida
    bool temInstalacao = false;     // o catálogo diz onde o pacote oficial instala
    bool achado() const { return programa.existsAsFile(); }
};

inline juce::String pastaProjetos() { return MAESTRUC_PROJETOS_DIR; }

// Versão do executável (Windows: o recurso de versão do .exe, que o JUCE
// grava a partir do VERSION do projeto; 18/11/2026). Vazio nos outros
// sistemas. Em Processos.cpp.
juce::String versaoDoExecutavel(const juce::File& exe);

namespace detalhe {
inline juce::String u8(const char* t) { return juce::String(juce::CharPointer_UTF8(t)); }

inline juce::var lerJson(const juce::String& texto, juce::String& erro) {
    juce::var v;
    const auto r = juce::JSON::parse(texto, v);
    if (r.failed()) { erro = r.getErrorMessage(); return {}; }
    if (v.getProperty("formato", {}).toString() != "maestruc.catalogo/1") { erro = "formato desconhecido"; return {}; }
    if (!v.getProperty("instrumentos", {}).isArray()) { erro = "sem lista de instrumentos"; return {}; }
    return v;
}

inline juce::StringArray textos(const juce::var& lista) {
    juce::StringArray s;
    if (auto* a = lista.getArray())
        for (auto& x : *a) if (x.isString()) s.add(x.toString());
    return s;
}

// O catálogo do usuário, se houver: acrescenta instrumentos ou substitui pelo id.
inline juce::File arquivoDoUsuario() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("RASGO").getChildFile("Maestruc").getChildFile("catalogo.json");
}

// Versão de um pacote .deb pelo registro do dpkg (sem abrir processo).
inline juce::String versaoDpkg(const juce::String& pacote) {
    const juce::File status("/var/lib/dpkg/status");
    if (pacote.isEmpty() || !status.existsAsFile() || status.getSize() > 64 * 1024 * 1024) return {};
    juce::StringArray linhas;
    status.readLines(linhas);
    bool noPacote = false, instalado = false;
    juce::String versao;
    for (const auto& l : linhas) {
        if (l.isEmpty()) {
            if (noPacote) return instalado ? versao : juce::String();
            noPacote = false;
            continue;
        }
        if (l.startsWith("Package: ")) { noPacote = l.substring(9).trim() == pacote; instalado = false; versao.clear(); }
        else if (noPacote && l.startsWith("Status: ")) instalado = l.endsWith(" installed");
        else if (noPacote && l.startsWith("Version: ")) versao = l.substring(9).trim();
    }
    return noPacote && instalado ? versao : juce::String();
}

// Info.plist de um bundle do macOS (os do JUCE são XML).
inline juce::String chavePlist(const juce::File& app, const juce::String& chave) {
    const auto plist = app.getChildFile("Contents").getChildFile("Info.plist");
    if (!plist.existsAsFile() || plist.getSize() > 1024 * 1024) return {};
    auto xml = juce::XmlDocument::parse(plist);
    if (xml == nullptr) return {};
    if (auto* dict = xml->getChildByName("dict")) {
        for (auto* e = dict->getFirstChildElement(); e != nullptr; e = e->getNextElement())
            if (e->hasTagName("key") && e->getAllSubText() == chave)
                if (auto* v = e->getNextElement()) return v->getAllSubText().trim();
    }
    return {};
}

inline void resolverInstalado(EntradaCatalogo& e, const juce::var& inst) {
    e.temInstalacao = inst.isObject();
    if (!e.temInstalacao) return;
#if JUCE_LINUX
    const auto lx = inst.getProperty("linux", {});
    const juce::String caminho = lx.getProperty("programa", {}).toString();
    if (caminho.startsWithChar('/')) {
        juce::File f(caminho);
        // instalação sem root (o install.sh do Studio põe em ~/.local/bin)
        if (!f.existsAsFile())
            f = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".local/bin").getChildFile(f.getFileName());
        if (f.existsAsFile()) { e.programa = f; e.processo = f.getFullPathName(); }
    }
    e.versaoInstalada = versaoDpkg(lx.getProperty("pacote", {}).toString());
#elif JUCE_WINDOWS
    const auto win = inst.getProperty("windows", {});
    const juce::String pasta = win.getProperty("pasta", {}).toString(), exe = win.getProperty("programa", {}).toString();
    if (pasta.isNotEmpty() && exe.isNotEmpty() && !pasta.containsAnyOf("\\/:") && !exe.containsAnyOf("\\/:")) {
        for (auto* raiz : { "ProgramW6432", "ProgramFiles" }) {
            const auto base = juce::SystemStats::getEnvironmentVariable(raiz, {});
            if (base.isEmpty()) continue;
            const auto f = juce::File(base).getChildFile(pasta).getChildFile(exe);
            if (f.existsAsFile()) {
                e.programa = f;
                e.processo = f.getFullPathName();
                e.versaoInstalada = versaoDoExecutavel(f);
                break;
            }
        }
    }
#elif JUCE_MAC
    const juce::String app = inst.getProperty("macos", {}).getProperty("app", {}).toString();
    if (app.endsWith(".app") && !app.containsChar('/')) {
        for (auto raiz : { juce::File("/Applications"),
                           juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Applications") }) {
            const auto bundle = raiz.getChildFile(app);
            const auto exe = chavePlist(bundle, "CFBundleExecutable");
            const auto f = bundle.getChildFile("Contents/MacOS").getChildFile(exe);
            if (exe.isNotEmpty() && !exe.containsChar('/') && f.existsAsFile()) {
                e.programa = f; e.processo = f.getFullPathName();
                e.versaoInstalada = chavePlist(bundle, "CFBundleShortVersionString");
                break;
            }
        }
    }
#endif
}

inline void resolverDesenvolvimento(EntradaCatalogo& e, const juce::var& dev) {
    const juce::String comando = dev.getProperty("comando", {}).toString();
    if (comando.isEmpty() || comando.contains("..")) return;
    const auto f = juce::File(pastaProjetos()).getChildFile(comando);
    if (f.existsAsFile()) e.programa = f;
    e.processo = dev.getProperty("processo", {}).toString();
    if (dev.hasProperty("args")) e.argsFixos = textos(dev.getProperty("args", {}));
}

inline EntradaCatalogo entrada(const juce::var& x, Fonte fonte) {
    EntradaCatalogo e;
    e.id = x.getProperty("id", {}).toString();
    e.nome = x.getProperty("nome", {}).toString();
    e.grupo = x.getProperty("grupo", {}).toString();
    e.repositorio = x.getProperty("repositorio", {}).toString();
    e.pagina = x.getProperty("pagina", {}).toString();
    e.argsFixos = textos(x.getProperty("args", {}));
    e.padraoSom = x.getProperty("padraoSom", {}).toString();
    e.abertura = x.getProperty("abertura", {}).toString();
    e.nota = x.getProperty("nota", {}).toString();
    if (fonte == Fonte::Instalados) resolverInstalado(e, x.getProperty("instalado", {}));
    else resolverDesenvolvimento(e, x.getProperty("desenvolvimento", {}));
    return e;
}
} // namespace detalhe

// O catálogo embutido, mais o do usuário (que acrescenta ou substitui pelo
// id). Um arquivo do usuário inválido é ignorado e dito em `aviso`.
inline juce::Array<EntradaCatalogo> carregarCatalogo(Fonte fonte, juce::String& aviso) {
    juce::String erro;
    auto base = detalhe::lerJson(juce::String::fromUTF8(DadosMaestruc::catalogo_json, DadosMaestruc::catalogo_jsonSize), erro);
    jassert(erro.isEmpty());   // o embutido é conferido no build (test_catalogo)
    juce::Array<juce::var> lista;
    if (auto* a = base.getProperty("instrumentos", {}).getArray()) lista.addArray(*a);
    const auto doUsuario = detalhe::arquivoDoUsuario();
    if (doUsuario.existsAsFile()) {
        juce::String erroU;
        const auto u = doUsuario.getSize() <= 512 * 1024 ? detalhe::lerJson(doUsuario.loadFileAsString(), erroU) : juce::var();
        if (auto* a = u.getProperty("instrumentos", {}).getArray()) {
            for (auto& x : *a) {
                const auto id = x.getProperty("id", {}).toString();
                if (id.isEmpty() || x.getProperty("nome", {}).toString().isEmpty()) continue;
                bool trocou = false;
                for (auto& y : lista) if (y.getProperty("id", {}).toString() == id) { y = x; trocou = true; }
                if (!trocou) lista.add(x);
            }
        } else {
            aviso = detalhe::u8("catálogo do usuário ignorado (") + (erroU.isNotEmpty() ? erroU : juce::String("grande demais")) + "): "
                    + doUsuario.getFullPathName();
        }
    }
    juce::Array<EntradaCatalogo> c;
    for (auto& x : lista) {
        auto e = detalhe::entrada(x, fonte);
        // nos instalados, um instrumento não publicado (sem pacote) não aparece
        if (fonte == Fonte::Instalados && !e.temInstalacao) continue;
        if (e.nome.isNotEmpty()) c.add(e);
    }
    return c;
}

} // namespace maestruc
