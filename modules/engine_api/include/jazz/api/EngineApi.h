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
std::string scalesForChord (const char* symbol, const char* style);
std::string reharmonise (const char* progressionText, int measureIndex, int includeAdvanced, int includeRisky);
std::string analyseVoicing (const char* symbol, const char* midiNotesCsv, const char* practiseStyle);
std::string importIRealPro (const char* text);
std::string exportIRealPro (const char* progressionText, const char* title, const char* composer, const char* style);
std::string chartFromPage (const char* tabSeparatedItems);
std::string reharmPlans (const char* progressionText);
std::string identifyChord (const char* midiNotesCsv);
std::string recogniseSubstitution (const char* progressionText, int measureIndex, const char* midiNotesCsv, int includeAdvanced);
std::string idiomaticVoicings (const char* symbol, int anchorNote, const char* practiseStyle, int rich);

//==============================================================================
/** Solo practice: reading a line rather than a chord.

    These four are the one stateful corner of this API, and deliberately so. A
    take is a stream with a beginning and an end, and the alternative - having
    the shell send every note played so far on each new note - puts the take in
    the UI, which is where theory is not allowed to live. So the engine holds
    it: one `LineAnalyzer` for the one player this process has.

    Between them they are the whole of the mode:

      soloStartTake   arm. Anything from a previous take is dropped.
      soloSetBar      the bar being soloed over, and the scale to read against.
                      Called on arming and again on every move. Moving during a
                      take does not end it.
      soloPlayNote    one note. Read back whether a take is running or not;
                      counted only when one is.
      soloEndTake     disarm, and hand back the take to read.

    A bar's own numbers come back from `soloSetBar`, because clicking a bar is
    how you ask for them.
*/
/** Every soloing vocabulary the engine offers, in menu order.

    The shells build their picker from this rather than holding a list of their
    own: which scales belong together is theory, and a second copy of it in a
    page is a second copy that goes stale.
*/
std::string scaleStyles();

std::string soloStartTake();
std::string soloSetBar (int measureIndex, const char* symbol, const char* chosenScale, const char* style);
std::string soloPlayNote (int midiNote);
std::string soloEndTake();

} // namespace jazz::api
