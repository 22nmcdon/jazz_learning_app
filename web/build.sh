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
  -I"$here/../modules/engine_api/include" \
  "$here/src/JazzWebBindings.cpp" \
  "$here"/../modules/engine_api/src/*.cpp \
  "$here"/../modules/core_engine/src/*.cpp \
  -o "$out/jazz-engine.js" \
  --no-entry \
  -s SINGLE_FILE=1 \
  -s MODULARIZE=1 \
  -s EXPORT_NAME=createJazzEngine \
  -s ENVIRONMENT=web \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s EXPORTED_FUNCTIONS='["_jazzParseChart","_jazzScalesForChord","_jazzReharmonise","_jazzAnalyseVoicing","_jazzIdiomaticVoicings","_jazzCompingVoicing","_jazzVoicingShape","_jazzVoicingFromShape","_jazzCompStyles","_jazzCompPlan","_jazzCompHit","_jazzCompTake","_jazzWalkingBass","_jazzImprovisedLine","_jazzRecogniseSubstitution","_jazzIdentifyChord","_jazzReharmPlans","_jazzReharmStyles","_jazzVoicedGuideTones","_jazzImportIRealPro","_jazzExportIRealPro","_jazzChartFromPage","_jazzScaleStyles","_jazzLineStyles","_jazzGrooves","_jazzSoloStartTake","_jazzSoloSetBar","_jazzSoloPlayNote","_jazzSoloEndTake","_jazzPracticeReading","_jazzTuneProgress","_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString"]'

echo "Built $out/jazz-engine.js ($(du -h "$out/jazz-engine.js" | cut -f1))"

# Everything both shells want, which is why it lives in assets/ rather than in
# web/ or app/: the app compiles these same files into its binary, and the page
# fetches them from here. Copied rather than symlinked, so the deployed
# directory stands on its own.
#
#   *.wav              the band's recorded instruments
#   pdf.min.js         pdf.js, and the worker it hands the parsing to. Vendored
#   pdf.worker.min.js  rather than fetched from a CDN - see the note above the
#                      script tag in index.html for why.
mkdir -p "$out/assets"
cp "$here"/../assets/*.wav "$out/assets/"
cp "$here"/../assets/pdf.min.js "$here"/../assets/pdf.worker.min.js "$out/assets/"
cp "$here"/../assets/pdf.js-LICENSE "$out/assets/"
echo "Copied $(ls "$out"/assets/*.wav | wc -l | tr -d ' ') samples and pdf.js into $out/assets"
