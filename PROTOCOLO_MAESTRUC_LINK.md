# Maestruc Link — protocolo `maestruc.link/1`

**Estado:** rascunho de 14/11/2026, após o OK do autor à proposta ("ok,
avance com a proposta"). Base: o levantamento de conexões da família (documento interno).
Implementações de referência: [`maestruc_link.py`](maestruc_link.py) (lado do
Maestruc e instrumento simulado) e [`receptor/MaestrucLink.h`](receptor/MaestrucLink.h)
(receptor para apps JUCE). Nenhum instrumento implementa ainda.

## 1. Princípios

1. **Opcional.** Sem o Maestruc aberto, o instrumento funciona como hoje. O
   receptor pode ser desligado no próprio instrumento.
2. **Cada um no seu processo.** Um instrumento que cai não derruba o Maestruc
   nem os outros; o Maestruc percebe a ausência e segue.
3. **Nada no callback de áudio.** Rede, alocação e locks ficam fora dele. O
   que vem da rede entra por fila; o que sai do áudio (acontecimentos) sai por
   fila sem bloqueio.
4. **O instrumento interpreta.** Um comando diz *o que* o Maestruc quer (tocar,
   tempo, tom, seção, intensidade); *como* soar é do instrumento. Quem não
   tem uma capacidade responde que não tem e continua.
5. **Só a máquina local.** UDP em `127.0.0.1`. A rede entre máquinas é outra
   fase (performance distribuída), com outro desenho.

## 2. Transporte

- **OSC 1.0** sobre UDP, tipos `i` (int32), `f` (float32) e `s` (string).
  Sem int64 nem double (o `juce_osc` não tem): instantes viajam como string
  decimal.
- **Porta do Maestruc:** `47900` (registro). Cada instrumento abre uma porta
  própria (escolhida pelo sistema) e a informa no `hello`.
- **Pacote:** até 1024 bytes. Mensagens maiores são descartadas.
- **Endereços:** prefixo `/rasgo/1/`. O número é a versão do protocolo; uma
  versão nova muda o prefixo e convive com a anterior.
- **Instantes:** microssegundos do relógio de parede do sistema
  (`system_clock`), como string. Mesma máquina, mesmo relógio.

## 3. Ciclo de vida

```
instrumento                               Maestruc (porta 47900)
  │ /rasgo/1/hello  nome versão porta pid caps ─►│ registra
  │◄─ /rasgo/1/welcome  sessão               │
  │ /rasgo/1/alive  porta  (a cada 1 s)    ─►│ sem alive por 3 s = ausente
  │◄─ comandos                               │
  │ /rasgo/1/ack  endereço ok nota         ─►│
  │ /rasgo/1/event … /rasgo/1/obs …        ─►│ grava com carimbo de tempo
  │ /rasgo/1/bye  porta                    ─►│ saída limpa
```

O instrumento repete o `hello` a cada 1 s até receber `welcome`; assim tanto
faz quem abre primeiro.

## 4. Mensagens

### Maestruc → instrumento

| Endereço | Argumentos | Sentido |
|---|---|---|
| `/rasgo/1/welcome` | `s` sessão | registro aceito |
| `/rasgo/1/who` | — | o Maestruc recebeu `alive` de quem não conhece (foi reaberto): mandar `hello` de novo |
| `/rasgo/1/transport/play` | `s` início (µs) | começar no instante dado; `"0"` = agora |
| `/rasgo/1/transport/stop` | — | parar |
| `/rasgo/1/tempo` | `f` bpm | andamento comum |
| `/rasgo/1/key` | `i` tônica 0–11 (0 = dó), `s` modo | tom e modo comuns |
| `/rasgo/1/section` | `i` índice, `s` nome | troca de seção da forma |
| `/rasgo/1/seed` | `s` seed | seed no formato do próprio instrumento |
| `/rasgo/1/open` | `s` caminho absoluto | abrir uma peça no formato do próprio instrumento (no Batruc, uma cena `.batruc.txt`); `ack` ok = 0 com a nota se não abriu. Acrescentado em 15/11/2026 |
| `/rasgo/1/macro` | `s` nome, `f` valor 0–1 | `intensidade`, `densidade`, ou outro declarado |
| `/rasgo/1/rec/start` | `s` pasta, `s` take | gravar os stems internos na pasta |
| `/rasgo/1/rec/stop` | — | parar a gravação |
| `/rasgo/1/budget` | `i` nível 0–2 | orçamento de carga (§5) |
| `/rasgo/1/query` | — | pedir `state` |
| `/rasgo/1/mixer/listar` | — | pedir a lista do mixer de dentro do app (15/11/2026) |
| `/rasgo/1/mixer/nivel` | `i` canal (−1 = master), `f` dB (−90…+12) | nível de um canal do mixer do app |
| `/rasgo/1/mixer/mudo` | `i` canal, `i` 0/1 | mudo de um canal do mixer do app |
| `/rasgo/1/mixer/solo` | `i` canal, `i` 0/1 | solo de um canal do mixer do app |

**Senha da sessão** (15/11/2026). O Maestruc sorteia uma senha a cada
abertura e a grava em `sessao.txt` na pasta das pontes (abaixo), só do
usuário (0600). O `welcome` leva essa senha como argumento, e o receptor só o
aceita se ela for igual à do arquivo. Depois, **todo comando acima leva a
senha como último argumento** (`welcome` e `who` não); sem ela, o receptor
ignora o comando. Outro usuário da máquina não lê o arquivo e não consegue
comandar os instrumentos; a conversa continua só em 127.0.0.1.

### Instrumento → Maestruc

| Endereço | Argumentos | Sentido |
|---|---|---|
| `/rasgo/1/hello` | `s` nome, `s` versão, `i` porta, `i` pid, `s` capacidades | anúncio; o pid deixa o Maestruc medir a CPU do processo inteiro |
| `/rasgo/1/alive` | `i` porta | presença |
| `/rasgo/1/bye` | `i` porta | saída |
| `/rasgo/1/ack` | `i` porta, `s` endereço, `i` ok (0/1), `s` nota | resposta a um comando |
| `/rasgo/1/state` | `i` porta, `i` tocando, `s` seed, `f` bpm | estado atual |
| `/rasgo/1/event` | `i` porta, `s` tipo, `f` valor, `s` instante (µs) | acontecimento: `ataque`, `frase`, `secao`, `silencio`… |
| `/rasgo/1/obs` | `i` porta, `s` nome, `f` valor | observação contínua: `energia`, `densidade` |
| `/rasgo/1/load` | `i` porta, `f` carga 0–1, `i` atrasos | carga do callback de áudio e xruns acumulados (§5) |
| `/rasgo/1/ponte` | `i` porta, `s` caminho, `i` pares, `f` taxa, `s` senha | ponte de som aberta (15/11/2026): o arquivo, quantos pares estéreo (o 1º é a mistura) e a taxa do app. Caminho vazio = fechada |
| `/rasgo/1/mixer/canal` | `i` porta, `i` canal (−1 = master), `s` nome, `f` dB, `i` mudo, `i` solo, e (16/11/2026, opcionais) `s` objetos, `f` dB mínimo, `f` dB máximo | um canal do mixer de dentro do app; vem depois do `welcome`, a cada mudança e depois de `mixer/listar` (no máximo 64). Os objetos são os controles que o canal tem de verdade (`nivel`, `mudo`, `solo`, separados por vírgula) e a faixa é a do fader do instrumento: o Maestruc mostra só esses objetos, nessa faixa. Sem eles, vale `nivel,mudo,solo` e −90…+12 dB |
| `/rasgo/1/mixer/fim` | `i` porta, `i` total | fim da lista do mixer |

**Ponte de som** (15/11/2026, `receptor/PonteDeSom.h`). O som do app chega
ao Maestruc por memória compartilhada, igual no Linux, no Windows e no
macOS, sem driver de áudio virtual. Um arquivo `.pds` por app na pasta das
pontes — `$TMPDIR` (ou `/tmp`)`/rasgo-maestruc-<uid>/` no Linux e no macOS,
`%TEMP%\rasgo-maestruc\` no Windows —, que só o usuário lê e escreve. Dentro,
um anel de um produtor (o callback de áudio do app) para um consumidor (o do
Maestruc), sem trava e sem alocação. Enquanto o Maestruc lê, ele pede que o
app cale a própria saída; se o Maestruc some por ~0,5 s, o app volta a tocar
sozinho. O Maestruc corrige o desvio entre os relógios das duas placas
(controle PI lento, amortecido, afinação firme abaixo de 1 cent) e converte
taxas diferentes (Hermite de 4 pontos). Ele só abre arquivos dentro dessa
pasta, do mesmo usuário, sem permissão para outros e com cabeçalho coerente.

A porta nas mensagens de saída identifica o instrumento (dois do mesmo
instrumento podem estar abertos).

**Capacidades** (lista separada por vírgulas no `hello`; desde 15/11/2026
também `mixer` e `audio`, a ponte de som): `transport`,
`tempo`, `key`, `section`, `seed`, `open`, `macro:<nome>`, `rec`, `budget`, `event`,
`obs`, `load`. Um comando fora da lista recebe `ack` com ok = 0.

## 5. Capacidade da máquina e requisitos mínimos

Autor: "precisamos desenvolver um instrumento capaz de trabalhar conforme a
capacidade de cada máquina", "com requisitos mínimos".

**Por que a capacidade cresce com a máquina:** cada instrumento roda no seu
processo, e o sistema distribui os processos pelos núcleos. Mais núcleos
significam mais instrumentos ao mesmo tempo; um núcleo mais rápido dá folga
a cada um. O limite real é o mais apertado entre núcleos, memória e disco
(para gravar).

**Como o Maestruc se adapta:**

1. **Perfil da máquina** ao abrir: núcleos, RAM disponível, espaço no disco
   de gravação. Sai num `perfil` gravado com a sessão.
2. **Carga medida, não suposta:** o Maestruc mede a CPU de cada processo
   pelo pid (`/proc/<pid>/stat`, interface incluída, que em vários
   instrumentos pesa tanto quanto o som); cada instrumento envia `load`
   (fração do bloco de áudio gasta e atrasos/xruns), que dá o risco de falha
   no áudio.
3. **Orçamento** (`budget`): 0 = normal, 1 = econômico, 2 = mínimo. O que cada
   nível corta é decisão de cada instrumento (taxa de desenho da interface,
   vozes, efeitos) e nunca muda a composição; o padrão de qualquer
   instrumento sem `budget` é seguir como está.
4. **Admissão:** antes de chamar mais um instrumento, o Maestruc confere a
   folga. Regra inicial, a ajustar pelo ensaio de carga: a soma das CPUs
   medidas mais um núcleo para o recém-chegado fica abaixo de 70% dos
   núcleos, e nenhum instrumento teve atraso de áudio. Sem folga, ele avisa e
   sugere o orçamento econômico ou deixar um instrumento de fora; não abre à
   força.
5. **Gravação:** antes de `rec/start`, confere espaço para pelo menos 1 h de
   todas as pistas (cerca de 1 GB por pista estéreo de 24 bits a 48 kHz).

**Requisitos mínimos (provisórios, a confirmar no ensaio de carga):**

| | Mínimo | Recomendado |
|---|---|---|
| Processador | 4 núcleos (x86-64 ou ARM64) | 6–8 núcleos |
| Memória | 8 GB | 16 GB |
| Disco para gravar | 10 GB livres | 100 GB livres |
| Sistema | Linux com PipeWire (primeiro alvo) | idem, com interface de áudio USB |
| Orquestra | Maestruc + 2 instrumentos ao vivo | Maestruc + 3–4, gravando todas as pistas |

Windows e macOS ficam para depois: o roteamento de áudio entre apps nesses
sistemas é outro problema (no Linux o PipeWire já resolve).

## 6. O que o receptor faz em cada instrumento

O receptor de referência (`receptor/MaestrucLink.h`) cuida da rede, do
`hello`/`alive`, do `ack` e das filas. O instrumento só liga os comandos às
funções que já tem (o PLAY, a seed, o REC) e decide o que significam tempo,
tom, seção e macros na sua música. O receptor é copiado para o repositório
de cada instrumento com proveniência; não há dependência de build entre
projetos.
