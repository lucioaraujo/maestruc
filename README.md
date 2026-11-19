# Maestruc

**English** · [Português](#português) · [Français](#français) · [Español](#español)

Maestruc is the conductor of the [RASGO](https://rasgoinstruments.arquiviagem.net/)
family of instruments by Lúcio de Araújo. It opens the family's instruments and
makes them play together, like an orchestra: each one's sound arrives on its own
strip, is mixed, can influence the others, and is recorded track by track. Each
instrument keeps running in its own process and interprets what Maestruc asks:
influence is not control (RASGO Method).

**Version:** 0.1.0 (19 Nov 2026) · **License:** GNU
AGPL-3.0-or-later ([`LICENSE`](LICENSE))

## What it does

- **Instruments:** opens the RASGO instruments installed on the computer
  (Batruc, Aquorbium, Rasgo Modular, Navalha 2, Asterismus, Antitotem, Trioio,
  Rasgo Synth Studio), each with `--maestruc`. An instrument that is not
  installed shows as NOT INSTALLED, with a link to its official page. Maestruc
  never downloads or installs anything.
- **Outputs and stems:** each instrument's sound reaches Maestruc through a
  shared-memory bridge (Windows, macOS and Linux) or, on Linux, through
  PipeWire; level, mute, solo and destination per strip, a Master and a
  headphone mix, each to its own device. The Master has a true-peak limiter.
- **Mixers:** the mixer inside each instrument that has one, with its own
  controls.
- **Influences:** one stem changes another in the sound (level or filter),
  listening to its energy, attacks, density or silence.
- **Conducting:** seed and piece for each instrument, when opening it or while
  it plays; play and stop for all; tempo.
- **Recording:** each strip and the Master as 24-bit WAV, one folder per take,
  with a log of what happened, and a delivery version of the Master for
  streaming (−14 LUFS, true peak up to −1 dBTP).
- **Versions (optional):** once a day, with your permission, it checks whether
  each installed instrument has a new version, and only tells you.
- Four languages: Portuguese, English, Spanish, French.

## What it does not do (yet)

- Packages for Linux (.deb, AppImage, .tar.gz), Windows (installer and .zip)
  and macOS (.dmg) come with the first release; installation in
  [`INSTALL.md`](INSTALL.md). Windows and macOS are built and tested by the CI
  but have not yet been opened on real machines; Linux is where it is used
  today.
- Asking an instrument to change its own music ("in the music") does not exist
  yet; influences act on the sound.

## Build from source

Needs CMake 3.22+, a C++17 compiler and [JUCE 9.0.3](https://github.com/juce-framework/JUCE/tree/9.0.3)
(not included). On Linux, the usual JUCE build dependencies (ALSA, FreeType,
fontconfig, X11).

```sh
git clone --branch 9.0.3 --depth 1 https://github.com/juce-framework/JUCE.git JUCE
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMAESTRUC_JUCE_DIR=$PWD/JUCE
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

The app is `build/MaestrucApp_artefacts/Release/Maestruc`. Open Maestruc first;
it opens the instruments.

## For instrument developers

The control channel is the `maestruc.link/1` protocol
([`PROTOCOLO_MAESTRUC_LINK.md`](PROTOCOLO_MAESTRUC_LINK.md), in Portuguese):
local OSC, a session password, and only the capabilities each instrument
declares. The receiver (`receptor/MaestrucLink.h`) and the sound bridge
(`receptor/PonteDeSom.h`) are copied into each instrument's own repository; an
instrument stays autonomous and opens no port without `--maestruc`.
[`maestruc_link.py`](maestruc_link.py) is a command-line conductor and probe
for the protocol.

## Credits

Lúcio de Araújo — authorship and direction. Navalha 2 — Glerm Soares and Lúcio
de Araújo. Built with JUCE 9.0.3 (AGPLv3). Typefaces: IBM Plex Sans and IBM
Plex Mono (SIL Open Font License 1.1, © IBM Corp.). Contact:
rasgo.instruments@gmail.com.

---

## Português

O Maestruc é o regente da família de instrumentos
[RASGO](https://rasgoinstruments.arquiviagem.net/), de Lúcio de Araújo. Ele abre
os instrumentos da família e os faz tocar juntos, como uma orquestra: o som de
cada um chega numa faixa própria, é mixado, pode influenciar os outros e é
gravado pista a pista. Cada instrumento continua rodando no seu processo e
interpreta o que o Maestruc pede: influência não é controle (Método RASGO).

**Versão:** 0.1.0 (19 nov. 2026) · **Licença:** GNU
AGPL-3.0-or-later ([`LICENSE`](LICENSE))

- **Instrumentos:** abre os instrumentos RASGO instalados no computador
  (Batruc, Aquorbium, Rasgo Modular, Navalha 2, Asterismus, Antitotem, Trioio,
  Rasgo Synth Studio), cada um com `--maestruc`. Um instrumento não instalado
  aparece como NÃO INSTALADO, com o link da página oficial. O Maestruc nunca
  baixa nem instala nada.
- **Saídas e stems:** o som de cada instrumento chega por uma ponte de memória
  compartilhada (Windows, macOS e Linux) ou, no Linux, pelo PipeWire; nível,
  mudo, solo e destino por faixa, Master e fones, cada um no seu aparelho. O
  Master tem limitador de pico real.
- **Mixers:** o mixer de dentro de cada instrumento que tem um, com os mesmos
  controles.
- **Influências:** um stem muda outro no som (nível ou filtro), escutando a
  energia, os ataques, a densidade ou o silêncio.
- **Regência:** seed e peça de cada instrumento, ao abrir ou com ele tocando;
  tocar e parar para todos; tempo.
- **Gravação:** cada faixa e o Master em WAV 24 bits, uma pasta por take, com o
  registro do que aconteceu, e uma versão de entrega do Master para streaming
  (−14 LUFS, pico real até −1 dBTP).
- **Versões (opcional):** uma vez por dia, com a sua permissão, confere se há
  versão nova de cada instrumento instalado, e só avisa.
- Quatro línguas: português, inglês, espanhol, francês.

**Pacotes:** Linux (.deb, AppImage, .tar.gz), Windows (instalador e .zip) e
macOS (.dmg) saem com a primeira versão; instalação em [`INSTALL.md`](INSTALL.md).
Windows e macOS são compilados e testados pela integração contínua, mas ainda
não foram abertos em máquinas reais; o uso hoje é no Linux.

**Ainda não:** pedir a um instrumento que mude a própria música ("na música").

**Compilar:** CMake 3.22+, compilador C++17 e o JUCE 9.0.3 (não incluído); os
comandos estão na seção em inglês. Abra o Maestruc primeiro: ele abre os
instrumentos.

**Para quem faz instrumentos:** o canal de controle é o protocolo
`maestruc.link/1` ([`PROTOCOLO_MAESTRUC_LINK.md`](PROTOCOLO_MAESTRUC_LINK.md)):
OSC local, senha da sessão e só as capacidades que cada instrumento declara. O
receptor (`receptor/MaestrucLink.h`) e a ponte de som (`receptor/PonteDeSom.h`)
são copiados no repositório de cada instrumento; sem `--maestruc`, o
instrumento continua autônomo e não abre nenhuma porta.

**Créditos:** Lúcio de Araújo — autoria e direção. Navalha 2 — Glerm Soares e
Lúcio de Araújo. Feito com JUCE 9.0.3 (AGPLv3). Letras: IBM Plex Sans e IBM
Plex Mono (SIL Open Font License 1.1, © IBM Corp.). Contato:
rasgo.instruments@gmail.com.

---

## Français

Maestruc est le chef de la famille d'instruments
[RASGO](https://rasgoinstruments.arquiviagem.net/), de Lúcio de Araújo. Il ouvre
les instruments de la famille et les fait jouer ensemble, comme un orchestre :
le son de chacun arrive sur sa propre tranche, est mixé, peut influencer les
autres et est enregistré piste par piste. Chaque instrument continue de tourner
dans son propre processus et interprète ce que Maestruc demande : influencer
n'est pas contrôler (Méthode RASGO).

**Version :** 0.1.0 (19 nov. 2026) · **Licence :** GNU
AGPL-3.0-or-later ([`LICENSE`](LICENSE))

- **Instruments :** ouvre les instruments RASGO installés sur l'ordinateur
  (Batruc, Aquorbium, Rasgo Modular, Navalha 2, Asterismus, Antitotem, Trioio,
  Rasgo Synth Studio), chacun avec `--maestruc`. Un instrument non installé
  apparaît comme NON INSTALLÉ, avec le lien de sa page officielle. Maestruc ne
  télécharge ni n'installe jamais rien.
- **Sorties et stems :** le son de chaque instrument arrive par un pont de
  mémoire partagée (Windows, macOS et Linux) ou, sous Linux, par PipeWire ;
  niveau, muet, solo et destination par tranche, Master et casque, chacun sur
  son appareil. Le Master a un limiteur de crête réelle.
- **Mixeurs :** le mixeur interne de chaque instrument qui en a un, avec ses
  propres commandes.
- **Influences :** un stem en change un autre dans le son (niveau ou filtre),
  en écoutant son énergie, ses attaques, sa densité ou son silence.
- **Direction :** graine et pièce de chaque instrument, à l'ouverture ou
  pendant qu'il joue ; jouer et arrêter pour tous ; tempo.
- **Enregistrement :** chaque tranche et le Master en WAV 24 bits, un dossier
  par prise, avec le journal de ce qui s'est passé, et une version de livraison
  du Master pour le streaming (−14 LUFS, crête réelle jusqu'à −1 dBTP).
- **Versions (facultatif) :** une fois par jour, avec votre accord, il vérifie
  si chaque instrument installé a une nouvelle version, et prévient seulement.
- Quatre langues : portugais, anglais, espagnol, français.

**Paquets :** Linux (.deb, AppImage, .tar.gz), Windows (installateur et .zip)
et macOS (.dmg) arrivent avec la première version ; installation dans
[`INSTALL.md`](INSTALL.md). Windows et macOS sont compilés et testés par
l'intégration continue mais n'ont pas encore été ouverts sur de vraies
machines ; l'usage aujourd'hui est sous Linux.

**Pas encore :** demander à un instrument de changer sa propre musique
(« dans la musique ») n'existe pas
encore.

**Pour qui fait des instruments :** le canal de contrôle est le protocole
`maestruc.link/1` ([`PROTOCOLO_MAESTRUC_LINK.md`](PROTOCOLO_MAESTRUC_LINK.md),
en portugais) : OSC local, mot de passe de session et seulement les capacités
que chaque instrument déclare.

**Crédits :** Lúcio de Araújo — auteur et direction. Navalha 2 — Glerm Soares
et Lúcio de Araújo. Fait avec JUCE 9.0.3 (AGPLv3). Polices : IBM Plex Sans et
IBM Plex Mono (SIL Open Font License 1.1, © IBM Corp.). Contact :
rasgo.instruments@gmail.com.

---

## Español

Maestruc es el director de la familia de instrumentos
[RASGO](https://rasgoinstruments.arquiviagem.net/), de Lúcio de Araújo. Abre los
instrumentos de la familia y los hace tocar juntos, como una orquesta: el
sonido de cada uno llega a su propia tira, se mezcla, puede influir en los
demás y se graba pista a pista. Cada instrumento sigue corriendo en su propio
proceso e interpreta lo que Maestruc pide: influencia no es control (Método
RASGO).

**Versión:** 0.1.0 (19 nov. 2026) · **Licencia:** GNU
AGPL-3.0-or-later ([`LICENSE`](LICENSE))

- **Instrumentos:** abre los instrumentos RASGO instalados en el ordenador
  (Batruc, Aquorbium, Rasgo Modular, Navalha 2, Asterismus, Antitotem, Trioio,
  Rasgo Synth Studio), cada uno con `--maestruc`. Un instrumento no instalado
  aparece como NO INSTALADO, con el enlace a su página oficial. Maestruc nunca
  descarga ni instala nada.
- **Salidas y stems:** el sonido de cada instrumento llega por un puente de
  memoria compartida (Windows, macOS y Linux) o, en Linux, por PipeWire; nivel,
  silencio, solo y destino por tira, Master y auriculares, cada uno en su
  aparato. El Master tiene limitador de pico real.
- **Mezcladores:** el mezclador interno de cada instrumento que lo tiene, con
  sus mismos controles.
- **Influencias:** un stem cambia otro en el sonido (nivel o filtro),
  escuchando su energía, sus ataques, su densidad o su silencio.
- **Dirección:** semilla y pieza de cada instrumento, al abrirlo o mientras
  toca; tocar y parar para todos; tempo.
- **Grabación:** cada tira y el Master en WAV de 24 bits, una carpeta por toma,
  con el registro de lo que ocurrió, y una versión de entrega del Master para
  streaming (−14 LUFS, pico real hasta −1 dBTP).
- **Versiones (opcional):** una vez al día, con su permiso, comprueba si hay
  versión nueva de cada instrumento instalado, y solo avisa.
- Cuatro idiomas: portugués, inglés, español, francés.

**Paquetes:** Linux (.deb, AppImage, .tar.gz), Windows (instalador y .zip) y
macOS (.dmg) salen con la primera versión; instalación en [`INSTALL.md`](INSTALL.md).
Windows y macOS se compilan y prueban con la integración continua, pero todavía
no se han abierto en máquinas reales; el uso hoy es en Linux.

**Todavía no:** pedir a un instrumento que cambie su propia música («en la
música»).

**Para quien hace instrumentos:** el canal de control es el protocolo
`maestruc.link/1` ([`PROTOCOLO_MAESTRUC_LINK.md`](PROTOCOLO_MAESTRUC_LINK.md),
en portugués): OSC local, contraseña de sesión y solo las capacidades que cada
instrumento declara.

**Créditos:** Lúcio de Araújo — autoría y dirección. Navalha 2 — Glerm Soares y
Lúcio de Araújo. Hecho con JUCE 9.0.3 (AGPLv3). Tipografías: IBM Plex Sans e IBM
Plex Mono (SIL Open Font License 1.1, © IBM Corp.). Contacto:
rasgo.instruments@gmail.com.
