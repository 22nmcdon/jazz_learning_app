#pragma once

#include <string>

namespace jazz::api
{

/** The engine's answers, as JSON - the one wire format both shells read.

    Every call takes plain strings and returns a JSON object as a std::string.
    On failure the object is {"ok":false,"error":"..."} rather than an
    exception or an empty string, so a caller in either shell can report what
    went wrong without knowing any C++ types.

    Nothing here decides any theory; it asks the Core Engine and writes down
    what it said. A shell adds a transport for these - Emscripten exports for
    the browser, JUCE native functions for the app - never a rule.
*/
std::string parseChart (const char* progressionText);
std::string scalesForChord (const char* symbol);
std::string reharmonise (const char* progressionText, int measureIndex, int includeAdvanced, int includeRisky);
std::string analyseVoicing (const char* symbol, const char* midiNotesCsv, const char* practiseStyle);
std::string importIRealPro (const char* text);
std::string exportIRealPro (const char* progressionText, const char* title, const char* composer, const char* style);
std::string chartFromPage (const char* tabSeparatedItems);
std::string reharmPlans (const char* progressionText);
std::string identifyChord (const char* midiNotesCsv);
std::string recogniseSubstitution (const char* progressionText, int measureIndex, const char* midiNotesCsv, int includeAdvanced);
std::string idiomaticVoicings (const char* symbol, int anchorNote, const char* practiseStyle, int rich);

} // namespace jazz::api
