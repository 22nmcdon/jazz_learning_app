#include "TestFramework.h"
#include "jazz/core/VoicingAnalyzer.h"

using namespace jazz::core;

namespace
{
    ChordSymbol chordFrom (const std::string& text)
    {
        const auto chord = ChordSymbol::parse (text);
        CHECK (chord.has_value());
        return *chord;
    }

    bool mentions (const VoicingAnalysis& analysis, const std::string& fragment)
    {
        if (analysis.summary.find (fragment) != std::string::npos)
            return true;

        for (const auto& finding : analysis.findings)
            if (finding.message.find (fragment) != std::string::npos)
                return true;

        for (const auto& suggestion : analysis.suggestions)
            if (suggestion.find (fragment) != std::string::npos)
                return true;

        return false;
    }

    bool hasProblem (const VoicingAnalysis& analysis)
    {
        for (const auto& finding : analysis.findings)
            if (finding.severity == FindingSeverity::problem)
                return true;

        return false;
    }
}

TEST ("recognises a rootless left-hand voicing as correct")
{
    // Cm7 played as the A-form rootless shape: Eb3 G3 Bb3 D4.
    const VoicingAnalyzer analyzer;
    const auto analysis = analyzer.analyse (Voicing::fromNotes ({ 51, 55, 58, 62 }), chordFrom ("Cm7"));

    CHECK (analysis.type == VoicingType::rootlessLeftHand);
    CHECK (analysis.matchesChord);
    CHECK (! hasProblem (analysis));
    CHECK_EQ (analysis.score, 100);
}

TEST ("does not penalise a rootless voicing for having no root")
{
    const VoicingAnalyzer analyzer;
    const auto analysis = analyzer.analyse (Voicing::fromNotes ({ 52, 57, 58, 62 }), chordFrom ("C7"));

    CHECK (analysis.missingTones.empty());
    CHECK (mentions (analysis, "No root"));
}

TEST ("but does flag the missing root when the caller demands it")
{
    VoicingAnalyzer::Options options;
    options.requireRoot = true;

    const VoicingAnalyzer analyzer { options };
    const auto analysis = analyzer.analyse (Voicing::fromNotes ({ 52, 57, 58, 62 }), chordFrom ("C7"));

    CHECK_EQ (analysis.missingTones.size(), std::size_t (1));
    CHECK (analysis.missingTones.front().role == ChordToneRole::root);
}

TEST ("catches a missing third")
{
    // C, G, Bb: no third, so the chord quality is undefined.
    const VoicingAnalyzer analyzer;
    const auto analysis = analyzer.analyse (Voicing::fromNotes ({ 48, 55, 58 }), chordFrom ("C7"));

    CHECK (! analysis.matchesChord);
    CHECK (hasProblem (analysis));
    CHECK (mentions (analysis, "Missing the 3"));
    CHECK (analysis.score < 70);
}

TEST ("catches a missing seventh")
{
    const VoicingAnalyzer analyzer;
    const auto analysis = analyzer.analyse (Voicing::fromNotes ({ 48, 52, 55 }), chordFrom ("Cmaj7"));

    CHECK (! analysis.matchesChord);
    CHECK (mentions (analysis, "Missing the maj7"));
}

TEST ("flags a note that is outside both the chord and its scale")
{
    // Dm7 with a C# in it: outside D Dorian.
    const VoicingAnalyzer analyzer;
    const auto analysis = analyzer.analyse (Voicing::fromNotes ({ 50, 53, 57, 61 }), chordFrom ("Dm7"));

    CHECK (! analysis.matchesChord);
    CHECK_EQ (analysis.outsideNotes.size(), std::size_t (1));
    CHECK_EQ (analysis.outsideNotes.front(), 61);
}

TEST ("flags a scale tone that clashes a semitone above a chord tone")
{
    // Cmaj7 with the natural 4th (F) held against the 3rd (E).
    const VoicingAnalyzer analyzer;
    const auto analysis = analyzer.analyse (Voicing::fromNotes ({ 60, 64, 65, 71 }), chordFrom ("Cmaj7"));

    CHECK (! analysis.matchesChord);
    CHECK (mentions (analysis, "semitone above a chord tone"));
    CHECK (analysis.outsideNotes.empty());  // in the scale, just not against this chord
}

TEST ("accepts an unnamed tension as good colour")
{
    // Dm7 with the 9th on top: not in the symbol, but idiomatic.
    const VoicingAnalyzer analyzer;
    const auto analysis = analyzer.analyse (Voicing::fromNotes ({ 50, 53, 60, 64 }), chordFrom ("Dm7"));

    CHECK (analysis.matchesChord);
    CHECK (mentions (analysis, "good colour"));
}

TEST ("warns about close intervals in the low register")
{
    // Root-position Cmaj7 an octave too low: C2 E2 G2 B2.
    const VoicingAnalyzer analyzer;
    const auto analysis = analyzer.analyse (Voicing::fromNotes ({ 36, 40, 43, 47 }), chordFrom ("Cmaj7"));

    CHECK (mentions (analysis, "muddy"));
}

TEST ("classifies the common voicing shapes")
{
    const auto cmaj7 = chordFrom ("Cmaj7");

    CHECK (VoicingAnalyzer::classify (Voicing::fromNotes ({ 48, 52, 59 }), cmaj7) == VoicingType::shell);
    CHECK (VoicingAnalyzer::classify (Voicing::fromNotes ({ 48, 52, 55, 59 }), cmaj7) == VoicingType::rootPosition);
    CHECK (VoicingAnalyzer::classify (Voicing::fromNotes ({ 52, 55, 59, 62 }), cmaj7) == VoicingType::rootlessLeftHand);
    CHECK (VoicingAnalyzer::classify (Voicing::fromNotes ({ 52, 59, 62, 64 }), cmaj7) == VoicingType::twoHandedRootless);
    CHECK (VoicingAnalyzer::classify (Voicing::fromNotes ({ 60 }), cmaj7) == VoicingType::singleNote);
    CHECK (VoicingAnalyzer::classify (Voicing::fromNotes ({}), cmaj7) == VoicingType::unknown);
}

TEST ("suggests idiomatic voicings when the played one can be improved")
{
    const VoicingAnalyzer analyzer;
    const auto analysis = analyzer.analyse (Voicing::fromNotes ({ 48, 55 }), chordFrom ("Cm7"));

    CHECK (! analysis.examples.empty());
    CHECK (! analysis.suggestions.empty());
}

TEST ("idiomatic voicings actually contain the chord's guide tones")
{
    for (const auto& text : { "Cm7", "F7", "Bbmaj7", "Em7b5", "A7alt" })
    {
        const auto chord = chordFrom (text);

        for (auto type : { VoicingType::shell, VoicingType::rootPosition,
                           VoicingType::rootlessLeftHand, VoicingType::twoHandedRootless })
        {
            for (const auto& voicing : idiomaticVoicings (chord, type))
            {
                CHECK (! voicing.isEmpty());

                for (const auto& guide : chord.guideTones())
                    CHECK (voicing.containsPitchClass (chord.root() + guide.semitones));
            }
        }
    }
}

TEST ("generated rootless voicings pass their own analysis")
{
    const VoicingAnalyzer analyzer;

    for (const auto& text : { "Cm7", "F7", "Bbmaj7", "Dm7", "G7b9" })
    {
        const auto chord = chordFrom (text);

        for (const auto& voicing : idiomaticVoicings (chord, VoicingType::rootlessLeftHand, 53))
        {
            const auto analysis = analyzer.analyse (voicing, chord);
            CHECK (analysis.matchesChord);
        }
    }
}

TEST ("problems are reported before confirmations")
{
    // A rootless Dm7 with an outside Bb: the "no root is fine" note must not
    // push the outside note out of a short feedback panel.
    const VoicingAnalyzer analyzer;
    const auto analysis = analyzer.analyse (Voicing::fromNotes ({ 53, 57, 58, 60, 64 }), chordFrom ("Dm7"));

    CHECK (! analysis.findings.empty());
    CHECK (analysis.findings.front().severity == FindingSeverity::problem);

    auto lastSeverity = static_cast<int> (FindingSeverity::problem) + 1;

    for (const auto& finding : analysis.findings)
    {
        CHECK (static_cast<int> (finding.severity) <= lastSeverity);
        lastSeverity = static_cast<int> (finding.severity);
    }
}

TEST ("describes a voicing by note name")
{
    CHECK_EQ (Voicing::fromNotes ({ 51, 55, 58, 62 }).describe(), std::string ("Eb3 G3 Bb3 D4"));
}

//==============================================================================
// Practising one shape: the analyser judges the voicing as an exercise as well
// as a chord.

namespace
{
    VoicingAnalyzer practising (VoicingType type)
    {
        VoicingAnalyzer::Options options;
        options.practiseType = type;
        return VoicingAnalyzer { options };
    }
}

TEST ("accepts the shape being practised")
{
    // Root-position Cmaj7: C E G B.
    const auto analysis = practising (VoicingType::rootPosition)
                              .analyse (Voicing::fromNotes ({ 48, 52, 55, 59 }), chordFrom ("Cmaj7"));

    CHECK (analysis.matchesStyle);
    CHECK (analysis.expectedType.has_value());
    CHECK (mentions (analysis, "as asked for"));
}

TEST ("says so when a rootless voicing turns up in a root-position exercise")
{
    // E G B D spells Cmaj7 perfectly - but there is no root under it.
    const auto analysis = practising (VoicingType::rootPosition)
                              .analyse (Voicing::fromNotes ({ 52, 55, 59, 62 }), chordFrom ("Cmaj7"));

    CHECK (analysis.matchesChord);      // the notes are right
    CHECK (! analysis.matchesStyle);    // the exercise is not
    CHECK (hasProblem (analysis));
    CHECK (mentions (analysis, "C needs to be the lowest note"));
    CHECK (analysis.score < 85);
}

TEST ("says so when the root turns up in a rootless exercise")
{
    const auto analysis = practising (VoicingType::rootlessLeftHand)
                              .analyse (Voicing::fromNotes ({ 48, 52, 55, 59 }), chordFrom ("Cmaj7"));

    CHECK (! analysis.matchesStyle);
    CHECK (hasProblem (analysis));
    CHECK (mentions (analysis, "leave the C to the bass"));
}

TEST ("a shape mismatch that is not about the root is a milder note")
{
    // A rootless left-hand voicing where two-handed ones are being practised.
    const auto analysis = practising (VoicingType::twoHandedRootless)
                              .analyse (Voicing::fromNotes ({ 52, 55, 59, 62 }), chordFrom ("Cmaj7"));

    CHECK (! analysis.matchesStyle);
    CHECK (! hasProblem (analysis));
    CHECK (mentions (analysis, "this exercise is on two-handed rootless voicings"));
}

TEST ("the root is reported once, not twice, while practising")
{
    const auto analysis = practising (VoicingType::rootPosition)
                              .analyse (Voicing::fromNotes ({ 52, 55, 59, 62 }), chordFrom ("Cmaj7"));

    auto mentionsOfRoot = 0;

    for (const auto& finding : analysis.findings)
        if (finding.message.find ("root") != std::string::npos)
            ++mentionsOfRoot;

    CHECK_EQ (mentionsOfRoot, 1);
}

TEST ("examples show the shape being practised, not the one played")
{
    const auto analysis = practising (VoicingType::rootPosition)
                              .analyse (Voicing::fromNotes ({ 52, 55, 59, 62 }), chordFrom ("Cmaj7"));

    CHECK (! analysis.examples.empty());

    // Every example puts the root at the bottom, which is what was asked for.
    for (const auto& example : analysis.examples)
        CHECK_EQ (jazz::core::toPitchClass (example.lowestNote()), 0);
}

TEST ("with no shape being practised, any shape is accepted")
{
    const VoicingAnalyzer analyzer;
    const auto analysis = analyzer.analyse (Voicing::fromNotes ({ 52, 55, 59, 62 }), chordFrom ("Cmaj7"));

    CHECK (analysis.matchesStyle);
    CHECK (! analysis.expectedType.has_value());
    CHECK (! hasProblem (analysis));
}

TEST ("the summary names the shape that was missed")
{
    const auto analysis = practising (VoicingType::rootlessLeftHand)
                              .analyse (Voicing::fromNotes ({ 48, 52, 55, 59 }), chordFrom ("Cmaj7"));

    CHECK (analysis.summary.find ("rootless left-hand voicing") != std::string::npos);
}
