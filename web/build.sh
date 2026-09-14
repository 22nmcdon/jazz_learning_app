#!/usr/bin/env bash
# Builds the Core Engine to WebAssembly, for the browser demo in web/index.html.
#
# Requires Emscripten (apt install emscripten, or the emsdk). The output is a
# single self-contained JS file with the wasm embedded, so the page can be
# opened from anywhere without a server or extra fetches.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="${1:-$here/dist}"
mkdir -p "$out"

em++ -O2 -std=c++17 \
  -I"$here/../modules/core_engine/include" \
  "$here/src/JazzWebBindings.cpp" \
  "$here"/../modules/core_engine/src/*.cpp \
  -o "$out/jazz-engine.js" \
  --no-entry \
  -s SINGLE_FILE=1 \
  -s MODULARIZE=1 \
  -s EXPORT_NAME=createJazzEngine \
  -s ENVIRONMENT=web \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s EXPORTED_FUNCTIONS='["_jazzParseChart","_jazzScalesForChord","_jazzReharmonise","_jazzAnalyseVoicing","_jazzIdiomaticVoicings","_jazzRecogniseSubstitution","_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString"]'

echo "Built $out/jazz-engine.js ($(du -h "$out/jazz-engine.js" | cut -f1))"
