// WebAssembly transport for the engine's JSON API.
//
// This is a platform shell, exactly like app/ is: it owns none of the theory and
// no longer owns the encoding either - jazz::api does that, so the browser and
// the JUCE app hand the same page the same answers. All that is left here is the
// part that is genuinely Emscripten's: C linkage, and a buffer that outlives the
// call so JavaScript can copy the string out.

#include "jazz/api/EngineApi.h"

#include <string>
#include <utility>

#ifdef __EMSCRIPTEN__
 #include <emscripten/emscripten.h>
 #define JAZZ_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
 #define JAZZ_EXPORT extern "C"
#endif

namespace
{
    /** Each entry point owns one buffer; JavaScript copies the string out before
        the next call, which is how Emscripten's UTF8ToString works anyway.
    */
    const char* hold (std::string&& json)
    {
        static std::string buffer;
        buffer = std::move (json);
        return buffer.c_str();
    }

    /** The engine's calls take strings; a null from JavaScript is an empty one. */
    const char* orEmpty (const char* text)
    {
        return text != nullptr ? text : "";
    }
}

JAZZ_EXPORT const char* jazzParseChart (const char* progressionText)
{
    return hold (jazz::api::parseChart (orEmpty (progressionText)));
}

JAZZ_EXPORT const char* jazzScalesForChord (const char* symbol, const char* style)
{
    return hold (jazz::api::scalesForChord (orEmpty (symbol), orEmpty (style)));
}

JAZZ_EXPORT const char* jazzScaleStyles()
{
    return hold (jazz::api::scaleStyles());
}

JAZZ_EXPORT const char* jazzReharmonise (const char* progressionText, int measureIndex,
                                         int includeAdvanced, int includeRisky)
{
    return hold (jazz::api::reharmonise (orEmpty (progressionText), measureIndex,
                                         includeAdvanced, includeRisky));
}

JAZZ_EXPORT const char* jazzAnalyseVoicing (const char* symbol, const char* midiNotesCsv,
                                            const char* practiseStyle)
{
    return hold (jazz::api::analyseVoicing (orEmpty (symbol), orEmpty (midiNotesCsv),
                                            orEmpty (practiseStyle)));
}

JAZZ_EXPORT const char* jazzImportIRealPro (const char* text)
{
    return hold (jazz::api::importIRealPro (orEmpty (text)));
}

JAZZ_EXPORT const char* jazzExportIRealPro (const char* progressionText, const char* title,
                                            const char* composer, const char* style,
                                            int beats, int beatUnit)
{
    return hold (jazz::api::exportIRealPro (orEmpty (progressionText), orEmpty (title),
                                            orEmpty (composer), orEmpty (style),
                                            beats, beatUnit));
}

JAZZ_EXPORT const char* jazzChartFromPage (const char* tabSeparatedItems)
{
    return hold (jazz::api::chartFromPage (orEmpty (tabSeparatedItems)));
}

JAZZ_EXPORT const char* jazzReharmPlans (const char* progressionText)
{
    return hold (jazz::api::reharmPlans (orEmpty (progressionText)));
}

JAZZ_EXPORT const char* jazzGuideTones (const char* progressionText)
{
    return hold (jazz::api::guideTones (orEmpty (progressionText)));
}

JAZZ_EXPORT const char* jazzIdentifyChord (const char* midiNotesCsv)
{
    return hold (jazz::api::identifyChord (orEmpty (midiNotesCsv)));
}

JAZZ_EXPORT const char* jazzRecogniseSubstitution (const char* progressionText,
                                                   int measureIndex,
                                                   const char* midiNotesCsv,
                                                   int includeAdvanced)
{
    return hold (jazz::api::recogniseSubstitution (orEmpty (progressionText), measureIndex,
                                                   orEmpty (midiNotesCsv), includeAdvanced));
}

JAZZ_EXPORT const char* jazzIdiomaticVoicings (const char* symbol, int anchorNote,
                                               const char* practiseStyle, int rich)
{
    return hold (jazz::api::idiomaticVoicings (orEmpty (symbol), anchorNote,
                                               orEmpty (practiseStyle), rich));
}

JAZZ_EXPORT const char* jazzCompingVoicing (const char* symbol, const char* previousNotesCsv)
{
    return hold (jazz::api::compingVoicing (orEmpty (symbol), orEmpty (previousNotesCsv)));
}

JAZZ_EXPORT const char* jazzCompStyles()
{
    return hold (jazz::api::compStyles());
}

JAZZ_EXPORT const char* jazzCompPlan (const char* progressionText, const char* styleKey,
                                      int fromBar, int toBar, int seed)
{
    return hold (jazz::api::compPlan (orEmpty (progressionText), orEmpty (styleKey),
                                      fromBar, toBar, seed));
}

JAZZ_EXPORT const char* jazzWalkingBass (const char* progressionText, int fromBar,
                                         int toBar, int seed)
{
    return hold (jazz::api::walkingBass (orEmpty (progressionText), fromBar, toBar, seed));
}

//==============================================================================
// Comping as an exercise. No take here and none in jazz::api either: a comped
// chord is settled the moment it is struck, so there is nothing for a memory
// to hold.

JAZZ_EXPORT const char* jazzCompHit (const char* progressionText, const char* styleKey,
                                     int measureIndex, int beat, int tick,
                                     const char* midiNotesCsv, int hitsAlreadyInBar)
{
    return hold (jazz::api::compHit (orEmpty (progressionText), orEmpty (styleKey),
                                     measureIndex, beat, tick, orEmpty (midiNotesCsv),
                                     hitsAlreadyInBar));
}

JAZZ_EXPORT const char* jazzCompTake (const char* progressionText, const char* styleKey,
                                      int fromBar, int toBar, const char* hitsText)
{
    return hold (jazz::api::compTake (orEmpty (progressionText), orEmpty (styleKey),
                                      fromBar, toBar, orEmpty (hitsText)));
}

//==============================================================================
// Solo practice. These four carry a take, which lives in jazz::api rather than
// here - a transport remembers nothing.

JAZZ_EXPORT const char* jazzSoloStartTake()
{
    return hold (jazz::api::soloStartTake());
}

JAZZ_EXPORT const char* jazzSoloSetBar (int measureIndex, const char* symbol,
                                        const char* chosenScale, const char* style,
                                        int beatsPerBar)
{
    return hold (jazz::api::soloSetBar (measureIndex, orEmpty (symbol),
                                        orEmpty (chosenScale), orEmpty (style), beatsPerBar));
}

JAZZ_EXPORT const char* jazzSoloPlayNote (int midiNote, int beat, int tick, int withPrevious)
{
    return hold (jazz::api::soloPlayNote (midiNote, beat, tick, withPrevious));
}

JAZZ_EXPORT const char* jazzSoloEndTake()
{
    return hold (jazz::api::soloEndTake());
}
