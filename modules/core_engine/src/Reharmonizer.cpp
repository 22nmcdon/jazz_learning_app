#include "jazz/core/Reharmonizer.h"

#include <algorithm>
#include <cstdlib>

namespace jazz::core
{

std::string difficultyName (SubstitutionDifficulty difficulty)
{
    return difficulty == SubstitutionDifficulty::safe ? "Safe" : "Advanced";
}

std::string styleName (ReharmStyle style)
{
    switch (style)
    {
        case ReharmStyle::bebop:     return "Bebop";
        case ReharmStyle::modal:     return "Modal";
        case ReharmStyle::quartal:   return "Quartal";
        case ReharmStyle::brazilian: return "Brazilian";
        case ReharmStyle::common:    break;
    }

    return "Common";
}

std::string familyName (SubstitutionFamily family)
{
    switch (family)
    {
        case SubstitutionFamily::extension:        return "Extension";
        case SubstitutionFamily::diatonic:         return "Diatonic substitution";
        case SubstitutionFamily::dominantFunction: return "Dominant substitution";
        case SubstitutionFamily::modalInterchange: return "Modal interchange";
        case SubstitutionFamily::chromaticMediant: return "Chromatic mediant";
        case SubstitutionFamily::passingChord:     return "Passing chord";
        case SubstitutionFamily::bassMotion:       return "Bass motion";
    }

    return "Substitution";
}

std::string Substitution::replacementText() const
{
    std::string text;

    for (std::size_t i = 0; i < replacement.size(); ++i)
    {
        if (i > 0)
            text += " ";

        text += replacement[i].toString();
    }

    return text;
}

namespace
{
    /** The nearest MIDI note of a given pitch class to @p reference. */
    int nearestNote (PitchClass pitchClass, int reference)
    {
        const auto below = reference - toPitchClass (reference - pitchClass);
        const auto above = below + semitonesPerOctave;
        return (reference - below) <= (above - reference) ? below : above;
    }

    ChordSymbol dominant (PitchClass root, std::vector<Extension> extensions = {})
    {
        return ChordSymbol::build (root, ChordQuality::dominant, SeventhType::minor, std::move (extensions));
    }

    ChordSymbol minorSeventh (PitchClass root, std::vector<Extension> extensions = {})
    {
        return ChordSymbol::build (root, ChordQuality::minor, SeventhType::minor, std::move (extensions));
    }

    ChordSymbol majorSeventh (PitchClass root, std::vector<Extension> extensions = {})
    {
        return ChordSymbol::build (root, ChordQuality::major, SeventhType::major, std::move (extensions));
    }

    ChordSymbol majorTriad (PitchClass root)
    {
        return ChordSymbol::build (root, ChordQuality::major, SeventhType::none);
    }

    ChordSymbol minorSixth (PitchClass root)
    {
        return ChordSymbol::build (root, ChordQuality::minor, SeventhType::none, { Extension::six });
    }

    ChordSymbol minorMajorSeventh (PitchClass root)
    {
        return ChordSymbol::build (root, ChordQuality::minorMajor, SeventhType::major);
    }

    ChordSymbol halfDiminished (PitchClass root)
    {
        return ChordSymbol::build (root, ChordQuality::halfDiminished, SeventhType::minor);
    }

    ChordSymbol diminishedSeventh (PitchClass root)
    {
        return ChordSymbol::build (root, ChordQuality::diminished, SeventhType::diminished);
    }

    /** Names the notes two chords have in common, e.g. "C and G".

        Substitutions that look strange on paper are usually held together by
        shared tones, so the explanation names them rather than asserting that
        the move works.
    */
    std::string sharedTones (const ChordSymbol& first, const ChordSymbol& second)
    {
        std::vector<std::string> names;

        for (PitchClass pitchClass = 0; pitchClass < semitonesPerOctave; ++pitchClass)
            if (first.containsPitchClass (pitchClass) && second.containsPitchClass (pitchClass))
                names.push_back (pitchClassName (pitchClass));

        if (names.empty())
            return {};

        if (names.size() == 1)
            return names.front();

        std::string text;

        for (std::size_t i = 0; i < names.size(); ++i)
        {
            if (i > 0)
                text += i + 1 == names.size() ? " and " : ", ";

            text += names[i];
        }

        return text;
    }

    /** "It keeps C and G from Cmaj7." - or nothing, when there is no overlap. */
    std::string keepsSentence (const ChordSymbol& original, const ChordSymbol& replacement)
    {
        const auto shared = sharedTones (original, replacement);

        if (shared.empty())
            return "It shares no notes with " + original.toString()
                   + ", so it lands as a colour change rather than a substitution. ";

        return "It keeps " + shared + " from " + original.toString() + ". ";
    }

    bool isDominant (const ChordSymbol& chord)
    {
        return chord.quality() == ChordQuality::dominant
               || (chord.quality() == ChordQuality::suspended && chord.seventh() == SeventhType::minor);
    }

    bool isMajorish (const ChordSymbol& chord)
    {
        return chord.quality() == ChordQuality::major;
    }
}

std::vector<GuideToneMotion> guideToneMotion (const ChordSymbol& from, const ChordSymbol& to, int referenceNote)
{
    std::vector<GuideToneMotion> motions;

    const auto targets = to.guideTones();

    if (targets.empty())
        return motions;

    for (const auto& tone : from.guideTones())
    {
        const auto fromNote = nearestNote (toPitchClass (from.root() + tone.semitones), referenceNote);

        // Resolve to whichever guide tone of the next chord is closest.
        auto bestNote = 0;
        auto bestDistance = semitonesPerOctave;
        std::string bestLabel;

        for (const auto& target : targets)
        {
            const auto candidate = nearestNote (toPitchClass (to.root() + target.semitones), fromNote);
            const auto distance = std::abs (candidate - fromNote);

            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestNote = candidate;
                bestLabel = target.label;
            }
        }

        motions.push_back ({ fromNote, bestNote, tone.label, bestLabel, bestNote - fromNote });
    }

    return motions;
}

int voiceLeadingCost (const ChordSymbol& from, const ChordSymbol& to)
{
    auto cost = 0;

    for (const auto& motion : guideToneMotion (from, to))
        cost += std::abs (motion.semitones);

    return cost;
}

std::vector<Substitution> Reharmonizer::substitutionsFor (const Chart& chart, int measureIndex) const
{
    std::vector<Substitution> substitutions;

    const auto* current = chart.chordAt (measureIndex);

    if (current == nullptr)
        return substitutions;

    const auto* next = chart.chordAt (measureIndex + 1);
    const auto chord = *current;

    const auto add = [&substitutions] (Substitution substitution)
    {
        substitutions.push_back (std::move (substitution));
    };

    if (isDominant (chord))
    {
        const auto tritoneRoot = toPitchClass (chord.root() + 6);

        add ({ "Tritone substitution",
               { dominant (tritoneRoot, { Extension::sharpEleven }) },
               "A dominant a tritone away shares the same 3rd and 7th (they swap roles), so the "
               "chord still resolves - but the root moves down a semitone instead of down a fifth.",
               SubstitutionDifficulty::advanced,
               ReharmStyle::bebop,
               SubstitutionFamily::dominantFunction });

        add ({ "Add the related ii-7",
               { minorSeventh (toPitchClass (chord.root() + 7)), chord },
               "Splitting the bar into ii-V gives the line somewhere to walk from and is the most "
               "common way to fill a static dominant bar.",
               SubstitutionDifficulty::safe,
               ReharmStyle::bebop,
               SubstitutionFamily::dominantFunction });

        add ({ "Tritone ii-V",
               { minorSeventh (toPitchClass (tritoneRoot + 7)), dominant (tritoneRoot) },
               "The tritone substitution with its own ii-7 in front, so the whole ii-V slides down "
               "a semitone into the target chord.",
               SubstitutionDifficulty::advanced,
               ReharmStyle::bebop,
               SubstitutionFamily::dominantFunction });

        add ({ "Altered dominant",
               { dominant (chord.root(), { Extension::flatNine, Extension::sharpNine,
                                           Extension::sharpEleven, Extension::flatThirteen }) },
               "Same function, more tension. Play the altered scale (melodic minor a semitone above "
               "the root) over it.",
               SubstitutionDifficulty::advanced,
               ReharmStyle::bebop,
               SubstitutionFamily::extension });

        add ({ "Suspend the dominant",
               { ChordSymbol::build (chord.root(), ChordQuality::suspended, SeventhType::minor,
                                     { Extension::nine }) },
               "Delays the 3rd, softening the pull to the tonic - the modal/quartal sound.",
               SubstitutionDifficulty::safe,
               ReharmStyle::modal,
               SubstitutionFamily::extension });

        add ({ "Whole-tone dominant",
               { dominant (chord.root(), { Extension::sharpFive }) },
               "Raising the 5th drops the chord into the whole-tone scale, where no note has a "
               "home - an older, more impressionistic dominant sound than the altered scale.",
               SubstitutionDifficulty::advanced,
               ReharmStyle::modal,
               SubstitutionFamily::extension });

        if (next != nullptr && isMajorish (*next)
            && toPitchClass (next->root() - chord.root()) == 5)
        {
            add ({ "Backdoor ii-V",
                   { minorSeventh (toPitchClass (next->root() + 5)),
                     dominant (toPitchClass (next->root() + 10)) },
                   "Approaches the target from a whole tone below instead of from its own V. The b7 "
                   "of the backdoor dominant falls to the 3rd of the target chord.",
                   SubstitutionDifficulty::advanced,
                   ReharmStyle::bebop,
                   SubstitutionFamily::dominantFunction });
        }
    }

    if (isMajorish (chord))
    {
        add ({ "Diatonic substitution (iii-7)",
               { minorSeventh (toPitchClass (chord.root() + 4)) },
               "The iii-7 shares three notes with the I chord, so the harmony barely moves while the "
               "bass note changes.",
               SubstitutionDifficulty::safe,
               ReharmStyle::common,
               SubstitutionFamily::diatonic });

        add ({ "Diatonic substitution (vi-7)",
               { minorSeventh (toPitchClass (chord.root() + 9)) },
               "The relative minor: same three upper notes, darker colour.",
               SubstitutionDifficulty::safe,
               ReharmStyle::common,
               SubstitutionFamily::diatonic });

        add ({ "Lydian colour",
               { ChordSymbol::build (chord.root(), ChordQuality::major, SeventhType::major,
                                     { Extension::sharpEleven }) },
               "Raising the 4th removes the one avoid note over a major 7th chord.",
               SubstitutionDifficulty::safe,
               ReharmStyle::modal,
               SubstitutionFamily::extension });

        // --- borrowed from the parallel minor --------------------------------
        const auto flatSix = majorSeventh (toPitchClass (chord.root() + 8));
        add ({ "bVI major seventh",
               { flatSix },
               keepsSentence (chord, flatSix)
                   + "Borrowed from the parallel minor, it holds the root and 5th while the 3rd and "
                     "7th become b3 and b13 - the chord stays underfoot but the light goes out of it.",
               SubstitutionDifficulty::advanced,
               ReharmStyle::modal,
               SubstitutionFamily::modalInterchange });

        const auto flatThree = majorSeventh (toPitchClass (chord.root() + 3));
        add ({ "bIII major seventh",
               { flatThree },
               keepsSentence (chord, flatThree)
                   + "The other borrowed major chord from the parallel minor. Against the original "
                     "root it spells b3, 5, b7 and 9, so the bar turns minor without the bass moving.",
               SubstitutionDifficulty::advanced,
               ReharmStyle::modal,
               SubstitutionFamily::modalInterchange });

        const auto flatSeven = majorSeventh (toPitchClass (chord.root() + 10));
        add ({ "bVII approach",
               { flatSeven, chord },
               "A major 7th a whole tone below, resolving up into the original chord. Borrowed from "
               "Mixolydian - the gospel and rock way into a tonic, and a change of gear after a bar "
               "of straight ii-V.",
               SubstitutionDifficulty::advanced,
               ReharmStyle::modal,
               SubstitutionFamily::modalInterchange });

        const auto minorFour = minorSixth (toPitchClass (chord.root() + 5));
        add ({ "Minor plagal approach",
               { minorFour, chord },
               "The minor IV borrowed from the parallel minor. Its b6 falls a semitone onto the 5th "
               "of the chord it resolves to - the sound at the end of a thousand ballads.",
               SubstitutionDifficulty::advanced,
               ReharmStyle::modal,
               SubstitutionFamily::modalInterchange });

        // --- a third away, sharing tones but not a key -----------------------
        const auto majorThird = majorSeventh (toPitchClass (chord.root() + 4));
        add ({ "III major seventh",
               { majorThird },
               keepsSentence (chord, majorThird)
                   + "Those two notes are the 3rd and 7th of the original chord and the root and 5th "
                     "of this one, which is what lets a chord from no related key sound inevitable.",
               SubstitutionDifficulty::advanced,
               ReharmStyle::modal,
               SubstitutionFamily::chromaticMediant });

        add ({ "Triad over a tonic pedal",
               { majorTriad (toPitchClass (chord.root() + 2)).overBass (chord.root()) },
               "A major triad a whole tone above the root, held over that root: 9, #11 and 13 in one "
               "shape. Lydian without a single altered note written into the symbol.",
               SubstitutionDifficulty::advanced,
               ReharmStyle::modal,
               SubstitutionFamily::bassMotion });
    }

    if (chord.quality() == ChordQuality::minor)
    {
        add ({ "Extend to m11",
               { minorSeventh (chord.root(), { Extension::eleven }) },
               "Stacked fourths over a minor 7th - the 9th comes with the 11th - for the quartal "
               "sound, and safe because every added note is diatonic to Dorian.",
               SubstitutionDifficulty::safe,
               ReharmStyle::quartal,
               SubstitutionFamily::extension });

        add ({ "Turn it into a ii-V",
               { chord, dominant (toPitchClass (chord.root() + 5)) },
               "If the bar is static, borrowing the second half for its V7 adds motion without "
               "changing where the progression lands.",
               SubstitutionDifficulty::safe,
               ReharmStyle::common,
               SubstitutionFamily::dominantFunction });

        add ({ "Dorian 6th",
               { minorSixth (chord.root()) },
               "Swapping the b7 for a natural 6 pins the bar to Dorian rather than Aeolian. Brighter, "
               "and it stops the chord sounding like it is waiting to become a ii-7.",
               SubstitutionDifficulty::advanced,
               ReharmStyle::modal,
               SubstitutionFamily::modalInterchange });

        add ({ "Minor-major seventh",
               { minorMajorSeventh (chord.root()) },
               "Raising the b7 to a natural 7 gives the melodic-minor sound, and sets up the line "
               "cliche: 7 to b7 to 6 as the bar repeats.",
               SubstitutionDifficulty::advanced,
               ReharmStyle::modal,
               SubstitutionFamily::modalInterchange });

        add ({ "Darken it to m7b5",
               { halfDiminished (chord.root()) },
               "Borrowing the iiø from the parallel minor. Flattening the 5th tells the listener the "
               "resolution is going to be minor, so the dominant after it can take every alteration.",
               SubstitutionDifficulty::advanced,
               ReharmStyle::modal,
               SubstitutionFamily::modalInterchange });
    }

    if (next != nullptr)
    {
        const auto secondaryDominantRoot = toPitchClass (next->root() + 7);

        if (secondaryDominantRoot != chord.root())
        {
            add ({ "Secondary dominant",
                   { chord, dominant (secondaryDominantRoot) },
                   "Take the second half of the bar with the V7 of the next chord - the strongest "
                   "way to point at where the progression is going.",
                   SubstitutionDifficulty::safe,
                   ReharmStyle::common,
                   SubstitutionFamily::dominantFunction });
        }

        add ({ "Chromatic approach dominant",
               { chord, dominant (toPitchClass (next->root() + 1)) },
               "A dominant a semitone above the target slides down into it; the same motion as a "
               "tritone substitution, used as a passing chord.",
               SubstitutionDifficulty::advanced,
               ReharmStyle::bebop,
               SubstitutionFamily::passingChord });

        add ({ "Chromatic ii-V approach",
               { minorSeventh (toPitchClass (next->root() + 1)),
                 dominant (toPitchClass (next->root() + 6)) },
               "A whole ii-V a semitone above the next chord, sliding down into it. This one replaces "
               "the bar outright, so use it where the bar was only marking time.",
               SubstitutionDifficulty::advanced,
               ReharmStyle::bebop,
               SubstitutionFamily::passingChord });

        // A diminished chord fills the gap when the roots move by a whole tone.
        if (toPitchClass (next->root() - chord.root()) == 2)
        {
            // Rising into the next chord, so it is spelled sharp: C#dim7
            // between Cmaj7 and Dm7, not Dbdim7.
            const auto passing = diminishedSeventh (toPitchClass (chord.root() + 1))
                                     .withAccidental (Accidental::sharps);
            add ({ "Diminished passing chord",
                   { chord, passing },
                   "Walks the bass chromatically into " + next->toString()
                       + ". It shares four notes with the secondary dominant of that chord, so it "
                         "pulls the same way while keeping the line stepwise.",
                   SubstitutionDifficulty::safe,
                   ReharmStyle::bebop,
                   SubstitutionFamily::passingChord });
        }
    }

    // Available whatever the quality: same harmony, different note underneath.
    add ({ "Third in the bass",
           { chord.overBass (toPitchClass (chord.root() + chord.thirdSemitones())) },
           "The same chord with its 3rd underneath. Nothing changes harmonically, but the bass line "
           "into the next bar gets shorter - often the whole point of a reharmonisation.",
           SubstitutionDifficulty::safe,
           ReharmStyle::common,
           SubstitutionFamily::bassMotion });

    // Filter by the caller's difficulty and style preferences.
    substitutions.erase (std::remove_if (substitutions.begin(), substitutions.end(),
                                         [this] (const Substitution& substitution)
                                         {
                                             if (! options.includeAdvanced
                                                 && substitution.difficulty == SubstitutionDifficulty::advanced)
                                                 return true;

                                             return options.style != ReharmStyle::common
                                                    && substitution.style != options.style
                                                    && substitution.style != ReharmStyle::common;
                                         }),
                         substitutions.end());

    // Rank by how smoothly the substitution leads into the following chord.
    for (auto& substitution : substitutions)
        substitution.voiceLeadingCost = next == nullptr || substitution.replacement.empty()
                                            ? 0
                                            : voiceLeadingCost (substitution.replacement.back(), *next);

    // Grouped by family, nearest the original harmony first, so the list reads
    // as a path from safe ground outwards rather than a flat pile of options.
    std::stable_sort (substitutions.begin(), substitutions.end(),
                      [] (const Substitution& a, const Substitution& b)
                      {
                          if (a.family != b.family)
                              return static_cast<int> (a.family) < static_cast<int> (b.family);

                          if (a.difficulty != b.difficulty)
                              return a.difficulty == SubstitutionDifficulty::safe;

                          return a.voiceLeadingCost < b.voiceLeadingCost;
                      });

    return substitutions;
}

Chart Reharmonizer::applySubstitution (const Chart& chart, int measureIndex, const Substitution& substitution)
{
    auto result = chart;

    if (measureIndex < 0 || measureIndex >= result.measureCount() || substitution.replacement.empty())
        return result;

    auto& measure = result.measures[static_cast<std::size_t> (measureIndex)];
    measure.slots.clear();

    const auto beats = result.timeSignature.numerator;
    const auto count = static_cast<int> (substitution.replacement.size());
    const auto perChord = std::max (1, beats / count);

    for (auto i = 0; i < count; ++i)
    {
        const auto isLast = i == count - 1;
        measure.slots.push_back ({ substitution.replacement[static_cast<std::size_t> (i)],
                                   isLast ? std::max (1, beats - perChord * (count - 1)) : perChord });
    }

    return result;
}

} // namespace jazz::core
