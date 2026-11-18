#!/bin/sh
# Teste de abertura (porte do Batruc e do Rasgo Modular): inicia o Maestruc numa tela
# virtual (Xvfb), sem placa de som, e confere que ele continua aberto depois
# de alguns segundos. Pega o que um .deb mal resolvido, um AppImage sem
# biblioteca ou uma glibc velha demais causariam: o app que nem abre. Não
# testa áudio nem desenho.
#
#   packaging/linux/testar-arranque.sh <executável ou AppImage> [segundos]
set -eu
EXE=$1
ESPERA=${2:-20}

H=$(mktemp -d)
export HOME="$H"
export APPIMAGE_EXTRACT_AND_RUN=1   # sem FUSE no contêiner

XPID=
if command -v Xvfb >/dev/null 2>&1; then
    Xvfb :77 -screen 0 1600x1000x24 -nolisten tcp >/dev/null 2>&1 &
    XPID=$!
    export DISPLAY=:77
    sleep 2
fi

"$EXE" >"$H/saida.txt" 2>&1 &
PID=$!
sleep "$ESPERA"

echo "--- saída do app (últimas linhas) ---"; tail -n 20 "$H/saida.txt" || true
ok=1
if kill -0 "$PID" 2>/dev/null; then
    echo "aberto depois de ${ESPERA} s"
    kill "$PID"; wait "$PID" 2>/dev/null || true
else
    wait "$PID" && st=0 || st=$?
    echo "FECHOU — código de saída $st"; ok=0
fi
[ -n "$XPID" ] && kill "$XPID" 2>/dev/null || true
rm -rf "$H"
[ "$ok" = 1 ] && echo "OK: $EXE" || { echo "FALHOU: $EXE"; exit 1; }
