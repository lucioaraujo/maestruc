#!/bin/sh
# Instala o Maestruc na sua pasta pessoal (~/.local), sem root.
#   ./install.sh            instala (ou atualiza)
#   ./install.sh --remove   remove o que este script instalou
# Outro destino: PREFIX=/opt/rasgo ./install.sh
set -eu
AQUI=$(cd "$(dirname "$0")" && pwd)
PREFIX=${PREFIX:-"$HOME/.local"}
APPS="$PREFIX/share/applications"
ICONES="$PREFIX/share/icons/hicolor"

if [ "${1:-}" = "--remove" ]; then
    rm -f "$PREFIX/bin/maestruc" \
          "$APPS/maestruc.desktop" \
          "$ICONES/scalable/apps/maestruc.svg" \
          "$ICONES/512x512/apps/maestruc.png"
    echo "Maestruc removido de / removed from $PREFIX."
    echo "Dados / data: ~/.config/RASGO/Maestruc · gravações / recordings: ~/Music/RASGO/Maestruc (ficam / kept)"
    exit 0
fi

mkdir -p "$PREFIX/bin" "$APPS" "$ICONES/scalable/apps" "$ICONES/512x512/apps"
cp "$AQUI/bin/maestruc" "$PREFIX/bin/"
cp "$AQUI/share/icons/hicolor/scalable/apps/maestruc.svg" "$ICONES/scalable/apps/"
cp "$AQUI/share/icons/hicolor/512x512/apps/maestruc.png" "$ICONES/512x512/apps/"
# caminho absoluto no Exec: ~/.local/bin nem sempre está no PATH do menu
sed "s|^Exec=.*|Exec=\"$PREFIX/bin/maestruc\"|" \
    "$AQUI/share/applications/maestruc.desktop" > "$APPS/maestruc.desktop"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" 2>/dev/null || true
echo "Maestruc instalado em / installed to $PREFIX/bin/maestruc"
echo "(no menu de aplicativos / in the application menu)"
