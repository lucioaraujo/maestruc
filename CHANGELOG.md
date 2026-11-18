# Maestruc — notas de versão / changelog

## 0.1.0 — em preparação

*English:* first release. Maestruc opens the RASGO instruments installed on
the computer and makes them play together: one strip per instrument, mixer,
influences between stems, seed and piece per instrument, play/stop and tempo
for all, recording per track plus a streaming delivery version of the
Master. Four languages. Packages for Linux (.deb, AppImage, .tar.gz),
Windows (installer and .zip) and macOS (.dmg, Universal 2).

- **Instrumentos:** abre os instrumentos RASGO instalados (Batruc,
  Aquorbium, Rasgo Modular, Navalha 2, Asterismus, Antitotem, Trioio, Rasgo
  Synth Studio), cada um com `--maestruc`; o que não está instalado aparece
  como NÃO INSTALADO, com o caminho para a página oficial.
- **Saídas e stems:** o som de cada instrumento chega pela ponte de memória
  compartilhada (Windows, macOS e Linux) ou, no Linux, pelo PipeWire; nível,
  mute, solo e destino por faixa, Master e fone, cada um no seu dispositivo.
  Limitador de true peak no Master.
- **Mixers:** o mixer de cada instrumento que tem um, pelos controles dele.
- **Influências:** um stem muda outro no som (nível ou filtro), ouvindo a
  energia, os ataques, a densidade ou o silêncio.
- **Regência:** seed e peça por instrumento; play e stop para todos; tempo.
- **Gravação:** cada faixa e o Master em WAV 24 bits, uma pasta por take,
  com o registro do que aconteceu e a versão de entrega do Master para
  streaming (−14 LUFS, true peak até −1 dBTP; saída de referência 1.2).
- **Versões (opcional):** uma vez por dia, com a sua permissão, confere se
  há versão nova de cada instrumento instalado, e só avisa.
- **Pastas da família:** dados do app em `<dados>/RASGO/Maestruc/` e
  gravações em `Música/RASGO/Maestruc/recordings/<dia>/<take>/`, pela
  convenção de pastas adotada pela família (folders-ref 1.0). Nomes sem
  espaços nem acentos (`master-streaming.wav`, `log.json`).
- Português, inglês, espanhol e francês.
- Pacotes: `.deb`, AppImage e `.tar.gz` (Linux), instalador e `.zip`
  (Windows), `.dmg` Universal 2 com assinatura ad-hoc (macOS).
