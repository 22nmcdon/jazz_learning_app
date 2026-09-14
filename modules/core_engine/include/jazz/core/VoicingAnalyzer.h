#pragma once

#include "jazz/core/ChordSymbol.h"
#include "jazz/core/ScaleSuggester.h"
#include "jazz/core/Voicing.h"

#include <optional>
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
    bool matchesStyle { true };        ///< the shape the player was practising
    int score {};                      ///< 0-100, for progress tracking
    VoicingType type { VoicingType::unknown };
    std::optional<VoicingType> expectedType;  ///< the shape being practised, if any
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

        /** The shape the player is working on.

            Set this and the analyser judges the voicing as an exercise as well
            as a chord: a rootless voicing played where root-position ones are
            being practised is reported, even though the notes spell the chord
            perfectly well. Unset, any shape is accepted - which is the right
            default for reading a chart rather than drilling a shape.
        */
        std::optional<VoicingType> practiseType;
    };

    VoicingAnalyzer() = default;
    explicit VoicingAnalyzer (Options optionsToUse) : options (optionsToUse) {}

    VoicingAnalysis analyse (const Voicing& voicing, const ChordSymbol& chord) const;

    /** Works out how the player laid the voicing out. */
    static VoicingType classify (const Voicing& voicing, const ChordSymbol& chord);

    /** True for the shapes that put the root underneath the chord. */
    static bool expectsRoot (VoicingType type);

private:
    Options options;
    ScaleSuggester suggester { ScaleSuggester::Options { false, 1 } };
};

} // namespace jazz::core
