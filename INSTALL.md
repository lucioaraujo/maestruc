# Maestruc — instalação / installation

> **Estado (19 nov. 2026):** versão 0.1.0 publicada. Os pacotes estão na
> [release v0.1.0](https://github.com/lucioaraujo/maestruc/releases/tag/v0.1.0).
> *English:* version 0.1.0 released; packages on the
> [v0.1.0 release](https://github.com/lucioaraujo/maestruc/releases/tag/v0.1.0).

O Maestruc não instala instrumentos: ele abre os instrumentos RASGO que já
estão instalados no computador. Cada instrumento se instala pela sua própria
página (o card NÃO INSTALADO leva até ela).

## Matriz de plataformas

Cada combinação está marcada com o que foi **realmente feito**, não com o
que deveria funcionar.

| Plataforma | Build | Testes | Abertura | Áudio tocado | Pacote |
|---|---|---|---|---|---|
| Linux x86-64 | ✅ local e CI (Ubuntu 22.04) | ✅ local e CI | ✅ CI em Debian 12, Ubuntu 24.04, Fedora e Arch (18/11) | ✅ em hardware real (código-fonte) | `.deb`, AppImage, `.tar.gz` |
| Windows x86-64 | ✅ CI (MSVC) | ✅ CI | ❌ nunca aberto num Windows real | ❌ | instalador `.exe` (NSIS) e `.zip` |
| macOS (Universal 2) | ✅ CI | ✅ CI | ❌ nunca aberto num Mac real | ❌ | `.dmg` (assinatura ad-hoc) |

O workflow `.github/workflows/package.yml` (porte do Batruc) compila, testa
e empacota nos três sistemas, e testa a abertura do pacote Linux em quatro
distribuições.

## Português

### Instalar, passo a passo

Os avisos de segurança abaixo **são esperados**: não indicam defeito nem
vírus e só aparecem na primeira vez. Os pacotes ficam na
[página de releases](https://github.com/lucioaraujo/maestruc/releases).

#### Windows

1. Baixe `maestruc-<versão>-windows-x64.exe`.
2. Abra o arquivo. O Windows mostra uma janela azul **"O Windows protegeu o
   computador"** (ou outra mensagem dizendo que o programa não é
   reconhecido). Clique em **"Mais informações"** e depois em
   **"Executar assim mesmo"**.
3. Se o Windows pedir permissão de administrador, clique em **"Sim"**.
4. Clique em **"Avançar"** até o fim. Depois disso, o Maestruc fica no
   **Menu Iniciar**.

**Sem instalar:** baixe o `.zip`, descompacte-o e abra `Maestruc.exe`. O
aviso do passo 2 aparece do mesmo jeito na primeira vez.

#### macOS

1. Baixe `maestruc-<versão>-macos-universal.dmg`. O mesmo arquivo serve
   para Mac Intel e Apple Silicon.
2. Abra o `.dmg` e **arraste o Maestruc para a pasta Aplicativos**.
3. Abra o Maestruc em Aplicativos. Na primeira vez, o macOS **bloqueia** o
   app e diz que **a Apple não pôde confirmar que ele está livre de software
   malicioso** (ou "não é possível verificar o desenvolvedor", ou "está
   danificado"). Isso **não indica defeito nem vírus**: o pacote tem
   assinatura ad-hoc, sem Developer ID nem notarização. Clique em **"OK"**
   ou **"Concluído"**, **não** em "Mover para o Lixo".
4. Libere o app uma vez, de um destes dois jeitos:
   - **Pelos Ajustes:** **Ajustes do Sistema → Privacidade e Segurança**,
     na parte de segurança, clique em **"Abrir Mesmo Assim"** ao lado de
     "Maestruc foi bloqueado…", confirme e clique em **"Abrir"**. No macOS
     14 ou anterior há um atalho: no Finder, botão direito no app →
     **"Abrir"** → **"Abrir"**.
   - **Pelo Terminal**, se o botão não aparecer: cole a linha abaixo e
     aperte Enter (se nada aparecer, deu certo). Ela só retira a marca de
     quarentena que o macOS põe em todo arquivo baixado.

     ```sh
     xattr -dr com.apple.quarantine "/Applications/Maestruc.app"
     ```

Se ainda assim não abrir, escreva para **rasgo.instruments@gmail.com** com
a versão do sistema e a mensagem que aparece.

#### Linux

- **Ubuntu, Debian e Mint:** dê dois cliques no `.deb` ou use
  `sudo apt install ./maestruc-<versão>-linux-x86_64.deb`.
- **Outras distribuições:** use o **AppImage** (marque como executável e
  abra) ou o **`.tar.gz`** (descompacte e rode `./install.sh`, que instala
  em `~/.local`, sem root). Requer x86-64 e glibc 2.35 ou mais nova.
- O aviso de versão nova usa o programa `curl` do sistema; o som pelo
  PipeWire/JACK é usado quando existe. Sem eles o Maestruc funciona, com a
  placa de som pelo próprio app.

### Compilar a partir do código

Requisitos:
- CMake 3.22 ou mais novo e um compilador C++17;
- o checkout do **JUCE 9.0.3**;
- no Linux, as bibliotecas de desenvolvimento de ALSA, FreeType, Fontconfig
  e X11 (com XInput2).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DMAESTRUC_JUCE_DIR=/caminho/para/JUCE
cmake --build build --parallel 2
ctest --test-dir build

# pacote (.deb no Linux, .exe no Windows, .dmg no macOS)
cd build && cpack -C Release
```

No Linux, `./run` na pasta do projeto compila e abre o Maestruc lendo os
instrumentos das pastas de desenvolvimento (`MAESTRUC_FONTE=desenvolvimento`).

### Onde o Maestruc guarda as coisas

Pela convenção de pastas da família RASGO (a mesma em todos os
instrumentos):

- **Dados do app** em `RASGO/Maestruc/`, na pasta de dados do sistema:
  `~/.config/RASGO/Maestruc/` no Linux, `%APPDATA%\RASGO\Maestruc\` no
  Windows, `~/Library/Application Support/RASGO/Maestruc/` no macOS. Lá
  ficam as preferências (`Maestruc.settings`), o catálogo próprio opcional
  (`catalog.json`, acrescenta ou substitui instrumentos) e os logs dos apps
  abertos (`logs/`).
- **Gravações** em `Música/RASGO/Maestruc/recordings/`, uma pasta por dia e
  uma por take (`2026-11-18/take-01-142210/`): `master.wav`,
  `master-streaming.wav`, uma faixa por instrumento e `log.json`. O botão
  … escolhe outra pasta.
- `RASGO_DATA_DIR` e `RASGO_MATERIAL_DIR` mudam as duas raízes.
- Instalar, atualizar ou desinstalar o Maestruc não apaga nada disso.

### Licença

GNU AGPL-3.0-or-later ([`LICENSE`](LICENSE)).

## English

### Installing, step by step

The security warnings below **are expected**: they do not mean anything is
wrong, and they only appear the first time. Packages are on the
[releases page](https://github.com/lucioaraujo/maestruc/releases).
Maestruc does not install instruments: it opens the RASGO instruments
already installed, each from its own page.

#### Windows

1. Download `maestruc-<version>-windows-x64.exe` and open it.
2. Windows shows a blue **"Windows protected your PC"** box. Click
   **"More info"**, then **"Run anyway"**. If asked for administrator
   permission, click **"Yes"**.
3. Click **"Next"** to the end. Maestruc is then in the **Start menu**.

**Without installing:** unzip the `.zip` and open `Maestruc.exe`.

#### macOS

1. Download `maestruc-<version>-macos-universal.dmg` (Intel and Apple
   Silicon), open it and **drag Maestruc into Applications**.
2. The first time, macOS **blocks** the app, saying Apple could not confirm
   it is free of malicious software. This is **not a defect or a virus**:
   the package is ad-hoc signed, without Developer ID or notarisation.
   Click **"OK"** or **"Done"**, **not** "Move to Trash".
3. Allow it once: **System Settings → Privacy & Security → "Open Anyway"**
   (on macOS 14 or earlier: right-click the app → **Open → Open**). If the
   button does not appear, run this in Terminal (no output means it
   worked):

   ```sh
   xattr -dr com.apple.quarantine "/Applications/Maestruc.app"
   ```

If it still does not open, write to **rasgo.instruments@gmail.com**.

#### Linux

- **Ubuntu, Debian and Mint:** double-click the `.deb`, or run
  `sudo apt install ./<file>.deb`.
- **Other distributions:** the **AppImage** (make it executable, then open
  it) or the **`.tar.gz`** (unpack it and run `./install.sh`, which installs
  for your user only). x86-64, glibc 2.35+.

### Build from source

See the commands above (CMake ≥ 3.22, C++17, JUCE 9.0.3;
`-DMAESTRUC_JUCE_DIR=…`).

### Where Maestruc keeps things

App data (settings, optional `catalog.json`, `logs/`) in `RASGO/Maestruc/`
inside the system's data folder (`~/.config`, `%APPDATA%`,
`~/Library/Application Support`); recordings in
`Music/RASGO/Maestruc/recordings/<date>/<take>/`. Installing, upgrading or
uninstalling never deletes them.

### License

AGPL-3.0-or-later.

## Français / Español

- **Français :** paquets pour Linux, Windows et macOS (compilés et testés en
  CI ; seul Linux a été joué sur une vraie machine). Maestruc n'installe pas
  les instruments : il ouvre ceux qui sont déjà installés. Installation pas à
  pas : voir la section anglaise. Compilation : voir les commandes ci-dessus.
  Licence AGPL-3.0-or-later.
- **Español:** paquetes para Linux, Windows y macOS (compilados y probados
  en CI; solo Linux se ha tocado en una máquina real). Maestruc no instala
  los instrumentos: abre los que ya están instalados. Instalación paso a
  paso: ver la sección en inglés. Compilación: ver los comandos de arriba.
  Licencia AGPL-3.0-or-later.
