#!/usr/bin/env python3
"""Gera o site do Maestruc: Sobre (index.html em português, a canônica, e
en/fr/es.html), Tutorial, Aprender e Instalação, nas 4 línguas.

    python3 gerar_paginas.py && python3 seo.py

Estrutura do site do Batruc (18/11/2026), com a proposta B aprovada pelo
autor: o motivo do ícone (as linhas que se juntam no Master) no topo.
Editar os TEXTOS em textos_paginas.py, não nos .html (eles são derivados).
O Aprender vem do app: lido de ../app/Textos.h (textoAprender), para o site
e a janela dizerem a mesma coisa.

Imagem grande do topo: a logo R4 inteira (assets/maestruc-logo.svg), animada
(autor, 18/11/2026). Cabeçalho e aba do navegador: o favicon simplificado.

PUBLICADO = False enquanto não houver release: a página diz "em preparação",
leva noindex e não mostra a seção Baixar (links quebrados não vão ao ar).
A captura da janela (assets/interface.jpg) entra quando existir.
"""

import html
import pathlib
import re

from textos_paginas import NAV, T, TUTORIAL, APRENDER, INSTALACAO

AQUI = pathlib.Path(__file__).parent
PORTAL = "https://rasgoinstruments.arquiviagem.net/"
REPO = "https://github.com/lucioaraujo/maestruc"
BLOB = REPO + "/blob/main"
PUBLICADO = False
VERSAO = "0.1.0"
RELEASE = REPO + "/releases/tag/v" + VERSAO
DL = REPO + "/releases/download/v" + VERSAO + "/"
# nomes amarrados ao CPACK_PACKAGE_FILE_NAME do CMakeLists (padrão da família)
PACOTES = [
    ("Windows", [("maestruc-%s-windows-x64.exe" % VERSAO, "exe"), ("maestruc-%s-windows-x64.zip" % VERSAO, "zip")]),
    ("macOS", [("maestruc-%s-macos-universal.dmg" % VERSAO, "dmg")]),
    ("Linux", [("maestruc-%s-linux-x86_64.deb" % VERSAO, "deb"), ("maestruc-%s-linux-x86_64.AppImage" % VERSAO, "AppImage"),
               ("maestruc-%s-linux-x86_64.tar.gz" % VERSAO, "tar.gz")]),
]
CAPTURA = AQUI / "assets" / "interface.jpg"

PAGINAS = [("index.html", "pt-BR", "PT"), ("en.html", "en", "EN"), ("fr.html", "fr", "FR"), ("es.html", "es", "ES")]
SUFIXO = {"pt-BR": "", "en": "-en", "fr": "-fr", "es": "-es"}
SOBRE = {lang: arq for arq, lang, _ in PAGINAS}
CODIGO = {lang: c for _, lang, c in PAGINAS}
TIPOS = ("tutorial", "aprender", "instalacao")

e = lambda s: html.escape(s, quote=False)
a = lambda s: html.escape(s, quote=True)
v = lambda s: s.replace("%s", VERSAO)


# ---- o Aprender, lido do app ---------------------------------------------------
ORDEM_NO_APP = ["pt-BR", "en", "es", "fr"]   # ordem do enum Lingua (app/Idiomas.h)


def ler_aprender():
    """Os quatro textos de textoAprender() em app/Textos.h, já decodificados."""
    fonte = (AQUI.parent / "app" / "Textos.h").read_text(encoding="utf-8")
    corpo = fonte[fonte.index("textoAprender"):fonte.index("textoSobre")]
    corpo = corpo[corpo.index("{", corpo.index("t[]")) + 1:corpo.index("};")]
    textos, atual = [], []
    for linha in corpo.splitlines():
        linha = linha.strip()
        if linha.startswith("//") or not linha:
            continue
        for literal in re.findall(r'"((?:[^"\\]|\\.)*)"', linha):
            atual.append(literal.encode("utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8"))
        if linha.endswith(","):
            textos.append("".join(atual))
            atual = []
    if atual:
        textos.append("".join(atual))
    if len(textos) != 4:
        raise SystemExit("Textos.h: esperava 4 textos do Aprender, achei %d" % len(textos))
    return dict(zip(ORDEM_NO_APP, textos))


def topicos(texto):
    """'1. Abrir. Na coluna…' → (1, 'Abrir', 'Na coluna…'); a primeira linha é o título geral."""
    saida = []
    for bloco in texto.split("\n\n")[1:]:
        m = re.match(r"(\d+)\. ([^.]+)\. (.*)", bloco.strip(), re.S)
        if not m:
            raise SystemExit("Aprender: tópico fora do formato 'N. Título. Texto': %r" % bloco[:60])
        saida.append((int(m.group(1)), m.group(2), m.group(3)))
    return saida


# ---- partes comuns ----------------------------------------------------------
def arquivo_de(tipo, lang):
    return SOBRE[lang] if tipo == "sobre" else "%s%s.html" % (tipo, SUFIXO[lang])


def cabecalho(lang, atual):
    n = NAV[lang]
    itens = [(tipo, arquivo_de(tipo, lang)) for tipo in ("sobre",) + TIPOS]
    if PUBLICADO:
        itens.append(("baixar", arquivo_de("sobre", lang) + "#baixar"))
    menu = "\n".join('      <a href="%s"%s>%s</a>' % (href, ' aria-current="page"' if tipo == atual else "", e(n[tipo]))
                     for tipo, href in itens)
    langs = "\n".join('      <a href="%s"%s>%s</a>' % (arquivo_de(atual, l), ' aria-current="true"' if l == lang else "", CODIGO[l])
                      for _, l, _ in PAGINAS)
    return f"""<div class="site-header">
  <a class="brand" href="{arquivo_de("sobre", lang)}" aria-label="Maestruc">
    <img src="assets/maestruc-favicon.svg" alt="" width="36" height="36" />
    <span>MAESTRUC</span>
  </a>
  <div class="header-nav">
    <nav class="page-nav" aria-label="{a(n["aria"])}">
{menu}
    </nav>
    <nav class="lang-switch" aria-label="{a(T[lang]["langnav"])}">
{langs}
    </nav>
  </div>
</div>"""


def rodape(lang):
    t = T[lang]
    return f"""<footer class="site-footer">
  <span>© Lúcio de Araújo · Maestruc 2026 · RASGO</span>
  <nav>
    <a href="{REPO}">GitHub</a> ·
    <a href="{BLOB}/LICENSE">{e(t["footer_license"])}</a>
  </nav>
</footer>
<dialog class="contact-dialog" data-contact-dialog aria-labelledby="contact-title">
  <button class="dialog-close" type="button" data-contact-close aria-label="{a(t["dialog_close"])}">×</button>
  <h2 id="contact-title">{e(t["dialog_title"])}</h2>
  <p>{e(t["dialog_copy_hint"])}</p>
  <code data-contact-address></code>
  <button class="button button-primary" type="button" data-contact-copy>{e(t["dialog_copy"])}</button>
  <p class="contact-feedback" data-contact-feedback data-copied="{a(t["copied"])}" data-fallback="{a(t["fallback"])}" aria-live="polite"></p>
</dialog>"""


def documento(lang, titulo, descricao, atual, corpo):
    t = T[lang]
    noindex = "" if PUBLICADO else '<meta name="robots" content="noindex" />\n'
    return f"""<!doctype html>
<html lang="{lang}">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
{noindex}<title>{e(titulo)}</title>
<meta name="description" content="{a(descricao)}" />
<link rel="icon" href="assets/maestruc-favicon.svg" type="image/svg+xml" />
<link rel="stylesheet" href="styles.css" />
<script src="assets/contact.js" defer></script>
</head>
<body>
<div class="rasgo-strip"><a href="{PORTAL}">{e(t["strip"])}</a></div>
<a class="skip-link" href="#conteudo">{e(t["skip"])}</a>

{cabecalho(lang, atual)}

<main id="conteudo">
{corpo}
<section class="section">
  <div class="contact-row">
    <button type="button" class="button button-primary" data-contact-trigger>{e(t["contact"])}</button>
    <a class="button" href="{REPO}">GitHub</a>
  </div>
</section>
</main>

{rodape(lang)}
</body>
</html>
"""


# ---- Sobre ------------------------------------------------------------------
# Animação do topo (proposta aprovada pelo autor, 18/11/2026): as oito linhas
# da logo R4 se desenham, o Master surge, e depois pulsos correm por cada linha
# até ele, cada um no seu tempo, e o Master solta uma onda leve. Só SVG (SMIL e
# CSS), sem canvas; com prefers-reduced-motion, a logo fica parada.
DURACOES = [3.1, 2.6, 3.6, 2.9, 3.3, 2.7, 3.8, 3.0]
ATRASOS = [0.0, 1.3, 0.6, 2.1, 0.9, 1.8, 0.3, 2.5]
INICIO = 2.2   # segundos: os pulsos começam depois que as linhas chegam


def motivo(lang):
    """A imagem grande do topo: a logo R4 animada, sem o contorno (autor, 18/11)."""
    svg = (AQUI / "assets" / "maestruc-logo.svg").read_text(encoding="utf-8")
    linhas = re.findall(r'<path d="([^"]+)"[^>]*opacity="([^"]+)"/>', svg)
    if len(linhas) != len(DURACOES):
        raise SystemExit("maestruc-logo.svg: esperava %d linhas, achei %d" % (len(DURACOES), len(linhas)))
    tracos, pulsos = [], []
    for k, (d, op) in enumerate(linhas):
        tracos.append('<path id="l%d" class="linha" style="--i:%d" d="%s" fill="none" stroke="#1D6670" '
                      'stroke-width="22" stroke-linecap="round" opacity="%s" pathLength="1"/>' % (k, k, d, op))
        dur, ini = DURACOES[k], INICIO + ATRASOS[k]
        pulsos.append('<circle r="13" fill="#1D6670" opacity="0"><animateMotion dur="%gs" begin="%gs" repeatCount="indefinite" '
                      'keyPoints="0;1" keyTimes="0;1" calcMode="spline" keySplines="0.4 0 0.9 0.6"><mpath href="#l%d"/></animateMotion>'
                      '<animate attributeName="opacity" values="0;.85;.85;0" keyTimes="0;.15;.85;1" dur="%gs" begin="%gs" '
                      'repeatCount="indefinite"/></circle>' % (dur, ini, k, dur, ini))
    return ('<svg class="logo-grande logo-animada" viewBox="0 0 1024 1024" role="img" aria-label="%s">\n'
            '<rect x="32" y="32" width="960" height="960" rx="200" fill="#F2EDE3"/>\n'
            '%s\n<g class="pulsos">%s</g>\n'
            '<circle class="onda" cx="760" cy="512" r="58" fill="none" stroke="#B3362B" stroke-width="6" opacity="0">'
            '<animate attributeName="r" values="58;104" dur="2.4s" begin="%gs" repeatCount="indefinite"/>'
            '<animate attributeName="opacity" values=".45;0" dur="2.4s" begin="%gs" repeatCount="indefinite"/></circle>\n'
            '<circle class="master" cx="760" cy="512" r="58" fill="#B3362B"/>\n</svg>'
            % (a(T[lang]["motivo"]), "\n".join(tracos), "".join(pulsos), INICIO, INICIO))


def corpo_sobre(lang):
    t = T[lang]
    passos = "\n".join("    <li><h3>%s</h3><p>%s</p></li>" % (e(h), e(p)) for h, p in t["steps"])
    fatos = "\n".join("    <li>%s</li>" % e(f) for f in t["facts"])
    pacotes = "\n".join(
        '    <li><h3>%s</h3><p>%s</p><p class="dl-links">%s</p></li>' % (
            e(so), e(t["dl_os"][so]), " ".join('<a class="button" href="%s%s">%s</a>' % (DL, a(f), e(rot)) for f, rot in arqs))
        for so, arqs in PACOTES)
    baixar = f"""
<section class="section section-dark download" id="baixar">
  <h2>{e(t["s_dl"])}</h2>
  <p>{e(t["dl_intro"])}</p>
  <ul class="steps downloads">
{pacotes}
  </ul>
  <p class="example">{e(t["dl_warn"])}</p>
  <div class="contact-row">
    <a class="button button-primary" href="{arquivo_de("instalacao", lang)}">{e(t["dl_install"])}</a>
    <a class="button" href="{RELEASE}">{e(t["dl_all"])}</a>
  </div>
</section>
""" if PUBLICADO else ""
    captura = f"""
<section class="section">
  <h2>{e(t["s_shot"])}</h2>
  <figure class="shot">
    <img src="assets/interface.jpg" alt="{a(t["shot_alt"])}" width="1920" height="1006" loading="lazy" />
    <figcaption>{e(t["shot_cap"])}</figcaption>
  </figure>
</section>
""" if CAPTURA.exists() else ""
    pill = (t["pill_pub"] if PUBLICADO else t["pill_prep"]) % VERSAO
    estado = t["state_pub"] % VERSAO if PUBLICADO else t["state_prep"]
    return f"""<section class="hero hero-b" id="top">
  <div>
    <span class="status-pill">{e(pill)}</span>
    <h1>Maestruc</h1>
    <p class="lead">{e(t["lead"])}</p>
  </div>
  {motivo(lang)}
</section>
{captura}{baixar}
<section class="section">
  <h2>{e(t["s1"])}</h2>
  <ol class="steps">
{passos}
  </ol>
</section>

<section class="section section-dark">
  <h2>{e(t["s2"])}</h2>
  <p>{e(t["inst"])}</p>
  <ul class="facts">
{fatos}
  </ul>
  <p class="example">{e(t["ainda"])}</p>
</section>

<section class="section">
  <h2>{e(t["s3"])}</h2>
  <p>{e(t["dev"])}</p>
  <div class="contact-row">
    <a class="button" href="{BLOB}/PROTOCOLO_MAESTRUC_LINK.md">{e(t["dev_proto"])}</a>
  </div>
</section>

<section class="section section-dark">
  <h2>{e(t["s4"])}</h2>
  <p>{e(t["credits"])}</p>
</section>

<section class="section">
  <h2>{e(t["s5"])}</h2>
  <p>{e(estado)}</p>
</section>"""


# ---- páginas internas -------------------------------------------------------
def interna(h1, lead, resto, toc=""):
    return f"""<section class="hero hero-inner" id="top">
  <h1>{e(h1)}</h1>
  <p class="lead">{e(lead)}</p>{toc}
</section>
{resto}"""


def corpo_tutorial(lang):
    t = TUTORIAL[lang]
    passos = []
    for h, intro, itens in t["steps"]:
        lista = "\n".join("      <li>%s</li>" % e(i) for i in itens)
        passos.append(f"""  <article class="lesson">
    <h2>{e(h)}</h2>
    <p>{e(intro)}</p>
    <ul>
{lista}
    </ul>
  </article>""")
    return interna(t["h1"], t["lead"], f"""<section class="section lessons">
{chr(10).join(passos)}
</section>
<section class="section section-dark">
  <h2>{e(t["rule_h"])}</h2>
  <p>{e(t["rule"])}</p>
</section>""")


def corpo_aprender(lang, textos):
    t = APRENDER[lang]
    tops = topicos(textos[lang])
    indice = "\n".join('    <li><a href="#t%d">%s</a></li>' % (n, e(titulo)) for n, titulo, _ in tops)
    fichas = "\n".join(f"""  <article class="lesson" id="t{n}">
    <h2>{n}. {e(titulo)}</h2>
    <p>{e(texto)}</p>
  </article>""" for n, titulo, texto in tops)
    toc = f"""
  <ol class="toc">
{indice}
  </ol>"""
    return interna(t["h1"], t["lead"], f"""<section class="section lessons">
{fichas}
</section>""", toc)


def corpo_instalacao(lang):
    t = INSTALACAO[lang]
    lista = lambda xs: "\n".join("    <li>%s</li>" % e(v(x)) for x in xs)
    return interna(t["h1"], t["lead"], f"""<section class="section lessons">
  <article class="lesson">
    <h2>{e(t["win_h"])}</h2>
    <ol>
{lista(t["win"])}
    </ol>
  </article>
  <article class="lesson">
    <h2>{e(t["mac_h"])}</h2>
    <ol>
{lista(t["mac"])}
    </ol>
    <pre><code>{e(t["mac_cmd"])}</code></pre>
  </article>
  <article class="lesson">
    <h2>{e(t["lin_h"])}</h2>
    <ul>
{lista(t["lin"])}
    </ul>
  </article>
  <article class="lesson">
    <h2>{e(t["onde_h"])}</h2>
    <ul>
{lista(t["onde"])}
    </ul>
  </article>
</section>
<section class="section section-dark">
  <p>{e(t["estado"])}</p>
  <div class="contact-row">
    <a class="button button-primary" href="{BLOB}/INSTALL.md">{e(t["guia"])}</a>
  </div>
</section>""")


def main():
    textos = ler_aprender()
    n = 0
    for arquivo, lang, _ in PAGINAS:
        (AQUI / arquivo).write_text(documento(lang, T[lang]["title"], T[lang]["desc"], "sobre", corpo_sobre(lang)), encoding="utf-8")
        for tipo, meta, corpo in (("tutorial", TUTORIAL[lang], corpo_tutorial(lang)),
                                  ("aprender", APRENDER[lang], corpo_aprender(lang, textos)),
                                  ("instalacao", INSTALACAO[lang], corpo_instalacao(lang))):
            (AQUI / arquivo_de(tipo, lang)).write_text(documento(lang, meta["title"], meta["desc"], tipo, corpo), encoding="utf-8")
        n += 4
    print("gerar_paginas.py: %d páginas (PUBLICADO=%s, captura=%s)" % (n, PUBLICADO, CAPTURA.exists()))


if __name__ == "__main__":
    main()
