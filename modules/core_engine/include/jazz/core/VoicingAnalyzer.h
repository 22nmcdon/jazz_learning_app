#pragma once

#include "jazz/core/ChordSymbol.h"
#include "jazz/core/ScaleSuggester.h"
#include "jazz/core/Voicing.h"

#include <string>
#include <vector>

namespace jazz::core
{

enum class FindingSeverity
{
    good,       ///< worth confirming back to the player
    suggestion, ///< playable, but there is a better option
    problem     ///< the voicing does not say what the chord symbol says
};

struct VoicingFinding
{
    FindingSeverity severity {};
    std::string message;
    std::vector<int> midiNotes;  ///< the notes the finding refers to, if any
};

struct VoicingAnalysis
{
    bool matchesChord {};              ///< no missing guide tones, nothing outside
    int score {};                      ///< 0-100, for progress tracking
    VoicingType type { VoicingType::unknown };
    std::string summary;               ///< one line for the feedback panel header
    std::vector<VoicingFinding> findings;
    std::vector<std::string> suggestions;
    std::vector<ChordTone> missingTones;
    std::vector<int> outsideNotes;     ///< MIDI notes outside chord and scale
    std::vector<Voicing> examples;     ///< idiomatic alternatives to try
};

/** Compares a played voicing against the chord symbol the chart expects.

    The analyser is deliberately voicing-type aware: a rootless left-hand voicing
    is not penalised for omitting the root, and a two-handed voicing is judged on
    its guide tones rather than on completeness.
*/
class VoicingAnalyzer
{
public:
    struct Options
    {
        /** Demand the root even in voicing types that normally omit it. */
        bool requireRoot { false };

        /** The note below which a minor 3rd turns muddy (F3 by convention).
            Wider intervals are allowed lower, narrower ones only higher.
        */
        int lowIntervalLimit { 53 };

        /** Include example voicings when the played one can be improved. */
        bool includeExamples { true };
    };

    VoicingAnalyzer() = default;
    explicit VoicingAnalyzer (Options optionsToUse) : options (optionsToUse) {}

    VoicingAnalysis analyse (const Voicing& voicing, const ChordSymbol& chord) const;

    /** Works out how the player laid the voicing out. */
    static VoicingType classify (const Voicing& voicing, const ChordSymbol& chord);

private:
    Options options;
    ScaleSuggester suggester { ScaleSuggester::Options { false, 1 } };
};

} // namespace jazz::core
