#!/bin/bash
# Compila a sonda contra a JUCE 9.0.3 da família (só juce_core, juce_events,
# juce_osc), sem CMake; zlib do sistema. Saída fora do git, em ../../.local-build/.
set -euo pipefail
cd "$(dirname "$0")"
JUCE=${JUCE_DIR:-../../../../third_party/JUCE-9.0.3/modules}
OUT=../../.local-build
mkdir -p "$OUT"
DEFS="-DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0 -DJUCE_MODULE_AVAILABLE_juce_core=1 -DJUCE_MODULE_AVAILABLE_juce_events=1 -DJUCE_MODULE_AVAILABLE_juce_osc=1 -DNDEBUG=1"
for src in juce_core/juce_core juce_core/juce_core_CompilationTime juce_events/juce_events juce_osc/juce_osc; do
  o="$OUT/$(basename "$src").o"
  [ "$o" -nt "$JUCE/$src.cpp" ] 2>/dev/null && continue
  nice -n 10 g++ -std=c++17 -O1 $DEFS -I"$JUCE" -c "$JUCE/$src.cpp" -o "$o"
done
nice -n 10 g++ -std=c++17 -O1 -Wall -Wextra $DEFS -I"$JUCE" link_probe.cpp "$OUT"/juce_core.o "$OUT"/juce_core_CompilationTime.o "$OUT"/juce_events.o "$OUT"/juce_osc.o -o "$OUT/link_probe" -lz -lpthread -ldl -lrt
echo "$OUT/link_probe"
