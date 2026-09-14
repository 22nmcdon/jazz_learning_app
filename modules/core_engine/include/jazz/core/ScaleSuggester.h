#pragma once

#include "jazz/core/ChordSymbol.h"
#include "jazz/core/Scale.h"

#include <string>
#include <vector>

namespace jazz::core
{

/** One scale offered for a chord, with the reasoning the UI shows the user. */
struct ScaleSuggestion
{
    Scale scale;
    int score {};                        ///< higher is a better fit
    std::string rationale;               ///< why this scale fits, in plain language
    std::vector<PitchClass> avoidNotes;  ///< scale tones that clash with a chord tone
    bool isPrimary {};                   ///< the single scale shown by default
};

/** Chooses which scales fit a chord symbol.

    The engine is rule-based: a canonical chord-quality-to-scale mapping supplies
    the primary suggestion, and every other scale in the catalogue that contains
    the chord's essential tones is offered as an alternative, ranked by how many
    avoid notes it introduces. (Whether a data/ML-informed ranking replaces this
    later is an open question in the design doc - the rules live in one place so
    they can be swapped out.)
*/
class ScaleSuggester
{
public:
    struct Options
    {
        /** Include 5- and 6-note scales rooted away from the chord root, e.g.
            "D Minor Pentatonic over Cmaj7". Useful on desktop, noisy on mobile.
        */
        bool includeNonRootedPentatonics { true };

        /** Cap on how many alternatives are returned, primary included. */
        int maxSuggestions { 12 };
    };

    ScaleSuggester() = default;
    explicit ScaleSuggester (Options optionsToUse) : options (optionsToUse) {}

    /** Returns suggestions ordered best-first; the first is marked primary. */
    std::vector<ScaleSuggestion> suggestionsFor (const ChordSymbol& chord) const;

    /** The single scale a player should default to over this chord. */
    ScaleSuggestion primarySuggestionFor (const ChordSymbol& chord) const;

    /** Scale tones that sit a semitone above a chord tone and so clash when held. */
    static std::vector<PitchClass> avoidNotesFor (const Scale& scale, const ChordSymbol& chord);

private:
    Options options;
};

} // namespace jazz::core
