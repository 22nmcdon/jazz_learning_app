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

//==============================================================================
// The shapes each voicing type is built from. These are the structures a player
// learns by name, so they are pinned here rather than left to the builder.
namespace
{
    /** The voicing's notes as semitones above the chord's root, in order:
        "4 11 2" is a third, a seventh and a ninth.
    */
    std::string degreesOf (const Voicing& voicing, const ChordSymbol& chord)
    {
        std::string degrees;

        for (auto note : voicing.midiNotes)
            degrees += (degrees.empty() ? "" : " ")
                     + std::to_string (toPitchClass (note - static_cast<int> (chord.root())));

        return degrees;
    }

    Voicing suggestion (const std::string& symbol, VoicingType type, std::size_t index,
                        int anchor = 53, VoicingDensity density = VoicingDensity::plain)
    {
        const auto voicings = idiomaticVoicings (chordFrom (symbol), type, anchor, density);
        CHECK (voicings.size() > index);
        return voicings.size() > index ? voicings[index] : Voicing {};
    }
}

TEST ("a rootless left hand is 3-7-9 and 7-3-13")
{
    // Cmaj7: E B D, then B E A.
    CHECK_EQ (degreesOf (suggestion ("Cmaj7", VoicingType::rootlessLeftHand, 0), chordFrom ("Cmaj7")),
              std::string ("4 11 2"));
    CHECK_EQ (degreesOf (suggestion ("Cmaj7", VoicingType::rootlessLeftHand, 1), chordFrom ("Cmaj7")),
              std::string ("11 4 9"));
}

TEST ("the left-hand shapes take the alterations the symbol names")
{
    // G7alt: 3-7-b9 and 7-3-b13, which is where an altered chord lives.
    CHECK_EQ (degreesOf (suggestion ("G7alt", VoicingType::rootlessLeftHand, 0), chordFrom ("G7alt")),
              std::string ("4 10 1"));
    CHECK_EQ (degreesOf (suggestion ("G7alt", VoicingType::rootlessLeftHand, 1), chordFrom ("G7alt")),
              std::string ("10 4 8"));
}

TEST ("a two-handed voicing is 3-7 under 9-13")
{
    const auto chord = chordFrom ("Cmaj7");
    const auto voicing = suggestion ("Cmaj7", VoicingType::twoHandedRootless, 0);

    CHECK_EQ (degreesOf (voicing, chord), std::string ("4 11 2 9"));

    // The left hand takes the lower two, the right hand the upper two.
    CHECK (voicing.midiNotes[1] - voicing.midiNotes[0] <= 12);
    CHECK (voicing.midiNotes[3] - voicing.midiNotes[2] <= 12);
}

TEST ("a solo voicing holds its own root")
{
    const auto chord = chordFrom ("Cmaj7");
    const auto voicing = suggestion ("Cmaj7", VoicingType::solo, 0, 40);

    // Root and seventh in the left hand, then 3-13-9 in the right.
    CHECK_EQ (degreesOf (voicing, chord), std::string ("0 11 4 9 2"));
    CHECK_EQ (toPitchClass (voicing.lowestNote()), static_cast<int> (chord.root()));
}

TEST ("the left hand of a solo voicing opens out as it goes down")
{
    const auto chord = chordFrom ("Cmaj7");

    CHECK_EQ (soloLeftHandPartner (chord, 60), 11);   // C4: the seventh
    CHECK_EQ (soloLeftHandPartner (chord, 48), 11);   // C3: still the seventh
    CHECK_EQ (soloLeftHandPartner (chord, 43), 7);    // G2: the fifth
    CHECK_EQ (soloLeftHandPartner (chord, 36), 12);   // C2: only the octave
}

TEST ("a solo voicing keeps the root out of the right hand")
{
    for (const auto* symbol : { "Cmaj7", "Dm7", "G7alt", "Bbm6", "Cm7b5" })
    {
        const auto chord = chordFrom (symbol);

        for (auto index : { std::size_t (0), std::size_t (1) })
        {
            const auto voicing = suggestion (symbol, VoicingType::solo, index, 40);

            for (std::size_t i = 2; i < voicing.midiNotes.size(); ++i)
                CHECK (toPitchClass (voicing.midiNotes[i]) != static_cast<int> (chord.root()));
        }
    }
}

TEST ("a rich voicing says more than the plain one it is built on")
{
    const auto chord = chordFrom ("Cmaj7");
    const auto plain = suggestion ("Cmaj7", VoicingType::rootlessLeftHand, 0);
    const auto rich = suggestion ("Cmaj7", VoicingType::rootlessLeftHand, 0, 53, VoicingDensity::rich);

    CHECK (rich.size() > plain.size());

    // The base shape is still in there: 3, 7 and 9, with the 13th added.
    for (auto degree : { 4, 11, 2, 9 })
        CHECK (rich.containsPitchClass (toPitchClass (static_cast<int> (chord.root()) + degree)));
}

TEST ("a shell has no richer form, because a fourth note would stop it being one")
{
    const auto plain = idiomaticVoicings (chordFrom ("Cmaj7"), VoicingType::shell, 53);
    const auto rich = idiomaticVoicings (chordFrom ("Cmaj7"), VoicingType::shell, 53,
                                         VoicingDensity::rich);

    CHECK_EQ (rich.size(), plain.size());

    for (std::size_t i = 0; i < rich.size(); ++i)
        CHECK_EQ (rich[i].describe(), plain[i].describe());
}

TEST ("every voicing offered for a shape is read back as that shape")
{
    // Otherwise "show me one" hands the player a voicing the analyser then marks
    // as the wrong shape.
    const char* symbols[] = { "Cmaj7", "Dm7", "G7", "G7alt", "Cm7b5", "C7sus4",
                              "Ebmaj7", "Am7", "F#7b9", "Bbm6", "CmMaj7", "Adim7" };

    const std::pair<VoicingType, int> types[] = {
        { VoicingType::shell,             48 },
        { VoicingType::rootPosition,      48 },
        { VoicingType::rootlessLeftHand,  53 },
        { VoicingType::twoHandedRootless, 48 },
        { VoicingType::solo,              40 },
        { VoicingType::spread,            40 }
    };

    for (const auto* symbol : symbols)
    {
        const auto chord = chordFrom (symbol);

        for (const auto& entry : types)
            for (auto density : { VoicingDensity::plain, VoicingDensity::rich })
                for (const auto& voicing : idiomaticVoicings (chord, entry.first, entry.second, density))
                    CHECK_EQ (voicingTypeName (VoicingAnalyzer::classify (voicing, chord)),
                              voicingTypeName (entry.first));
    }
}

TEST ("no suggested voicing asks for a stretch no hand has")
{
    // Both of these shapes put two notes in the left hand - 3-7, or the root and
    // its partner - and the rest in the right, so that is where they split. Each
    // side has to fall under one hand: an octave and a little, no more.
    const char* symbols[] = { "Cmaj7", "Dm7", "G7alt", "Cm7b5", "C7sus4", "Bbm6" };

    for (const auto* symbol : symbols)
    {
        const auto chord = chordFrom (symbol);

        for (auto type : { VoicingType::twoHandedRootless, VoicingType::solo })
            for (auto density : { VoicingDensity::plain, VoicingDensity::rich })
                for (const auto& voicing : idiomaticVoicings (chord, type, type == VoicingType::solo ? 40 : 48, density))
                {
                    CHECK (voicing.size() >= 4);

                    if (voicing.size() < 4)
                        continue;

                    CHECK (voicing.midiNotes[1] - voicing.midiNotes[0] <= 14);
                    CHECK (voicing.highestNote() - voicing.midiNotes[2] <= 14);
                }
    }
}

//==============================================================================
// Comping: the same two-handed shapes, chosen for where the last one left the
// hands rather than at a fixed anchor.
namespace
{
    /** Comps a progression straight through, as the page does bar by bar. */
    std::vector<Voicing> compThrough (const std::vector<std::string>& symbols)
    {
        std::vector<Voicing> played;
        std::vector<int> previous;

        for (const auto& symbol : symbols)
        {
            played.push_back (compingVoicing (chordFrom (symbol), previous));
            previous = played.back().midiNotes;
        }

        return played;
    }

    /** The widest a single hand moves anywhere in a comped progression. */
    int biggestLeap (const std::vector<Voicing>& played)
    {
        auto worst = 0;

        for (std::size_t i = 1; i < played.size(); ++i)
            worst = std::max (worst, std::abs (played[i].lowestNote() - played[i - 1].lowestNote()));

        return worst;
    }
}

TEST ("what a comping piano plays is a two-handed rootless voicing")
{
    // The invariant the suggestions are already held to: the app must never
    // play a voicing it would then read as a different kind of voicing.
    for (const auto& symbol : { "Cmaj7", "Dm7", "G7", "Bbmaj7", "Am7b5", "D7alt", "F6", "Ebmaj7" })
    {
        const auto chord = chordFrom (symbol);
        const auto voicing = compingVoicing (chord);

        CHECK (! voicing.isEmpty());
        CHECK (VoicingAnalyzer::classify (voicing, chord) == VoicingType::twoHandedRootless);
    }
}

TEST ("with nothing to lead from, comping opens on the shape the app would show")
{
    const auto chord = chordFrom ("Dm7");
    const auto opening = idiomaticVoicings (chord, VoicingType::twoHandedRootless,
                                            naturalAnchorFor (VoicingType::twoHandedRootless));

    CHECK (! opening.empty());
    CHECK_EQ (compingVoicing (chord).describe(), opening.front().describe());
}

TEST ("comping leads from the voicing before it rather than re-anchoring")
{
    const std::vector<std::string> tune { "Dm7", "G7", "Cmaj7", "Cm7", "F7", "Bbmaj7" };

    // What a fixed anchor costs: every chord spelled from scratch at C3.
    std::vector<Voicing> reanchored;

    for (const auto& symbol : tune)
        reanchored.push_back (idiomaticVoicings (chordFrom (symbol), VoicingType::twoHandedRootless,
                                                 naturalAnchorFor (VoicingType::twoHandedRootless)).front());

    const auto comped = compThrough (tune);

    CHECK (biggestLeap (comped) <= biggestLeap (reanchored));
    CHECK (biggestLeap (comped) <= 6);
}

TEST ("a chord repeated is comped the same way twice")
{
    // Nothing to gain by moving, so nothing moves: the cheapest voicing to
    // lead to from a voicing is that voicing.
    const auto twice = compThrough ({ "Cmaj7", "Cmaj7" });

    CHECK_EQ (twice[0].describe(), twice[1].describe());
}

TEST ("comping stays in its register over a long tune")
{
    /*  Voice leading on its own drifts: every chord picks the nearest voicing
        to the last one, and a progression that keeps rising takes the hands up
        with it until they are somewhere nobody comps. The window is what stops
        that, so the test is a tune that climbs. */
    const auto climbing = compThrough ({ "Cmaj7", "Ebmaj7", "Gbmaj7", "Amaj7", "Cmaj7",
                                         "Ebmaj7", "Gbmaj7", "Amaj7", "Cmaj7" });

    for (const auto& voicing : climbing)
    {
        CHECK (voicing.lowestNote() >= 45);    // A2
        CHECK (voicing.highestNote() <= 84);   // C6
    }
}
