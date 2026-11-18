# Maestruc — motor de áudio

C++17 sem dependências (só a biblioteca padrão e threads). É a camada por
baixo da janela do Maestruc: recebe os stems dos instrumentos, mixa, aplica as
influências "no som" e grava cada pista. Copyright (c) 2026 Lúcio de Araújo —
AGPL-3.0-or-later.

| Arquivo | O que faz |
|---|---|
| `src/Mixer.*` | stems estéreo: ganho, pan, mudo, solo, rota (master, fones, nenhuma), medidores; influências "no som" de um stem sobre outro ou sobre o Master (**ganho**: abrir espaço ou subir; **brilho**: abafar), com entrada rápida (30 ms; 2 ms nos ataques) e soltura no tempo pedido. **Escuta** de cada stem, tirada do próprio som (sem código no instrumento): **energia** (envoltória RMS), **ataques** (pulso a cada golpe: pico rápido da derivada acima de 2,5× a média de 30 ms, período refratário de 60 ms), **densidade** (ataques por segundo em janela de ~2 s; 8/s = 1) e **silêncio** (sobe em ~0,4 s quando o stem fica abaixo de −48 dB). Cada influência escolhe o que escuta. Sem alocação nem trava na thread de áudio. |
| `src/Recorder.*` | gravação por pista (cada stem pós-fader e o master): a thread de áudio copia para anéis sem trava, uma thread de escrita grava WAV 24 bits. Disco lento não para a música: o que não cabe é descartado e contado. Exige 1 GB livre para começar. |
| `src/Wav.*` | escrita WAV PCM 24 bits estéreo; leitura PCM 16/24/32 e float 32, mono ou estéreo. |
| `src/Jack.*` | acesso à API C do JACK carregada em tempo de execução (biblioteca do `pipewire-jack`); não exige o pacote de desenvolvimento do JACK. |
| `src/LiveHost.*` | host ao vivo: cliente JACK com entrada estéreo por stem (`<stem>_L/_R`), saídas `master_L/_R` e `fones_L/_R`, mixer e gravação por pista sem trava. Liga só os apps pedidos; ao ligar, pode tirá-los das caixas e devolve ao fechar. |
| `tools/motor_vivo.cpp` | host ao vivo pela linha de comando, até a janela existir. |
| `tools/motor_offline.cpp` | render offline: WAVs de stems entram, pistas e master saem. Para ouvir o motor com gravações reais antes da ligação ao vivo. |
| `tests/test_vivo.cpp` | ensaio silencioso no servidor real: um cliente de teste toca 1 kHz só para o Maestruc, que grava; confere nível, silêncio do stem sem entrada, master e alinhamento. Sem servidor, é pulado. |
| `tests/test_motor.cpp` | ensaios: ganho, pan, mudo, solo, rota, influência de ganho (com contrafactual e fonte muda), brilho, fones e Master, escuta (golpes a 4/s e 10/s, nota longa, baixo de 60 Hz, ruído, silêncio, stem mudo, influência por ataques e por silêncio), gravador (duração e conteúdo exatos), anel cheio. |

As influências "no som" são mixagem, não composição entre instrumentos. A
camada "na música" segue o Método RASGO: fica com cada instrumento, que
interpreta o que o Maestruc pede.

## Compilar e ensaiar

```sh
cmake -S motor -B .local-build/motor -DCMAKE_BUILD_TYPE=Release
cmake --build .local-build/motor --parallel 2
.local-build/motor/test_motor
```

## Ouvir com gravações reais

```sh
.local-build/motor/maestruc_motor_offline --saida ~/Música/Maestruc/ensaio1 \
  --entrada batruc=Batruc-take.wav --entrada aquorbium=Aquorbium-take.wav \
  --influencia batruc:aquorbium:ganho:-0.6:300 --master -3
```

`--influencia fonte:destino:ganho|brilho:quantidade:ms`. A quantidade vai de
-1 a +1: no ganho, -1 abaixa o destino até 24 dB quando a fonte está cheia e
+1 sobe até 12 dB; no brilho, -1 fecha o filtro até 250 Hz. Os WAVs precisam
ter a mesma taxa de amostragem.

## Ao vivo

```sh
.local-build/motor/maestruc_motor_vivo --listar        # saídas de áudio disponíveis
.local-build/motor/maestruc_motor_vivo --stem batruc --stem aquorbium \
  --ligar batruc=Batruc --ligar aquorbium=Aquorbium \
  --influencia batruc:aquorbium:ganho:-0.6:300 --tocar --gravar ~/Música/Maestruc/vivo1
```

`--ligar stem=PADRÃO` liga ao stem as saídas cujo nome contém o padrão (as duas
primeiras: L e R) e **tira o app das caixas** (decisão do autor, 14/11): o
som do app passa a sair só pelo Maestruc (`--tocar` liga o master às caixas).
Se o gerenciador de sessão religar o app às caixas, o Maestruc desliga de
novo a cada ~0,5 s. Ao sair (`sair`, Ctrl-C ou `--segundos`), as ligações
são devolvidas. Se o Maestruc travar ou for morto à força, o app pode ficar
sem som; religue no `qpwgraph` ou reabra o app.

Durante a execução, digite: `ganho NOME dB`, `pan NOME v`, `mudo NOME`,
`som NOME`, `solo NOME`, `semsolo NOME`, `master dB`, `gravar PASTA`, `parar`,
`medidores`, `sair`.

## Ainda não

Falta a janela (JUCE, layout aprovado). Também ainda não foram feitos: o ensaio
com apps reais tocando e a troca de seção.
