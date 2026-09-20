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
  -s EXPORTED_FUNCTIONS='["_jazzParseChart","_jazzScalesForChord","_jazzReharmonise","_jazzAnalyseVoicing","_jazzIdiomaticVoicings","_jazzCompingVoicing","_jazzCompStyles","_jazzCompPlan","_jazzCompHit","_jazzCompTake","_jazzWalkingBass","_jazzRecogniseSubstitution","_jazzIdentifyChord","_jazzReharmPlans","_jazzGuideTones","_jazzImportIRealPro","_jazzExportIRealPro","_jazzChartFromPage","_jazzScaleStyles","_jazzSoloStartTake","_jazzSoloSetBar","_jazzSoloPlayNote","_jazzSoloEndTake","_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString"]'

echo "Built $out/jazz-engine.js ($(du -h "$out/jazz-engine.js" | cut -f1))"

# The band's recorded instruments. They live in assets/ rather than in web/
# because both shells want them: the app compiles the same files in, and the
# page fetches them from here. Copied rather than symlinked, so the deployed
# directory stands on its own.
mkdir -p "$out/assets"
cp "$here"/../assets/*.wav "$out/assets/"
echo "Copied $(ls "$out/assets" | wc -l | tr -d ' ') samples into $out/assets"

# The PDF reader, put beside the page rather than fetched from a CDN when
# someone opens a PDF.
#
# It used to be a <script src> pointing at cdnjs, which is a third party able to
# put code in this page - and the desktop app hosts the same page, with the
# bridge to the native side in it. Served from here, the only script that can
# run in the page is one that shipped with it, which is what the page's own
# Content-Security-Policy now says.
#
# npm rather than a CDN because npm is the source cdnjs itself builds from, and
# it verifies what it downloads. The version is pinned; moving it is a decision,
# not a deploy. Best-effort: a build with no npm or no network still produces a
# page, and one without pdf.js says so where a PDF is opened and points at the
# paste box, which reads the same charts. CI checks the file is there, so a
# skip cannot reach the deployed site quietly.
pdfjs="3.11.174"

if pack="$(cd "$out" && npm pack "pdfjs-dist@$pdfjs" --silent 2>/dev/null)" && [[ -n $pack ]]; then
  tar xzf "$out/$pack" -C "$out" package/build/pdf.min.js package/build/pdf.worker.min.js
  mv "$out/package/build/pdf.min.js" "$out/package/build/pdf.worker.min.js" "$out"
  rm -rf "$out/package" "$out/$pack"
  echo "Fetched pdf.js $pdfjs into $out"
else
  echo "warning: could not fetch pdf.js $pdfjs - the built page will not read PDFs" >&2
fi
