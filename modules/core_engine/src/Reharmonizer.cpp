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
               ReharmStyle::bebop });

        add ({ "Add the related ii-7",
               { minorSeventh (toPitchClass (chord.root() + 7)), chord },
               "Splitting the bar into ii-V gives the line somewhere to walk from and is the most "
               "common way to fill a static dominant bar.",
               SubstitutionDifficulty::safe,
               ReharmStyle::bebop });

        add ({ "Tritone ii-V",
               { minorSeventh (toPitchClass (tritoneRoot + 7)), dominant (tritoneRoot) },
               "The tritone substitution with its own ii-7 in front, so the whole ii-V slides down "
               "a semitone into the target chord.",
               SubstitutionDifficulty::advanced,
               ReharmStyle::bebop });

        add ({ "Altered dominant",
               { dominant (chord.root(), { Extension::flatNine, Extension::sharpNine,
                                           Extension::sharpEleven, Extension::flatThirteen }) },
               "Same function, more tension. Play the altered scale (melodic minor a semitone above "
               "the root) over it.",
               SubstitutionDifficulty::advanced,
               ReharmStyle::bebop });

        add ({ "Suspend the dominant",
               { ChordSymbol::build (chord.root(), ChordQuality::suspended, SeventhType::minor,
                                     { Extension::nine }) },
               "Delays the 3rd, softening the pull to the tonic - the modal/quartal sound.",
               SubstitutionDifficulty::safe,
               ReharmStyle::modal });

        if (next != nullptr && isMajorish (*next)
            && toPitchClass (next->root() - chord.root()) == 5)
        {
            add ({ "Backdoor ii-V",
                   { minorSeventh (toPitchClass (next->root() + 5)),
                     dominant (toPitchClass (next->root() + 10)) },
                   "Approaches the target from a whole tone below instead of from its own V. The b7 "
                   "of the backdoor dominant falls to the 3rd of the target chord.",
                   SubstitutionDifficulty::advanced,
                   ReharmStyle::bebop });
        }
    }

    if (isMajorish (chord))
    {
        add ({ "Diatonic substitution (iii-7)",
               { minorSeventh (toPitchClass (chord.root() + 4)) },
               "The iii-7 shares three notes with the I chord, so the harmony barely moves while the "
               "bass note changes.",
               SubstitutionDifficulty::safe,
               ReharmStyle::common });

        add ({ "Diatonic substitution (vi-7)",
               { minorSeventh (toPitchClass (chord.root() + 9)) },
               "The relative minor: same three upper notes, darker colour.",
               SubstitutionDifficulty::safe,
               ReharmStyle::common });

        add ({ "Lydian colour",
               { ChordSymbol::build (chord.root(), ChordQuality::major, SeventhType::major,
                                     { Extension::sharpEleven }) },
               "Raising the 4th removes the one avoid note over a major 7th chord.",
               SubstitutionDifficulty::safe,
               ReharmStyle::modal });
    }

    if (chord.quality() == ChordQuality::minor)
    {
        add ({ "Extend to m11",
               { minorSeventh (chord.root(), { Extension::nine, Extension::eleven }) },
               "Stacked fourths over a minor 7th - the quartal sound, and safe because every added "
               "note is diatonic to Dorian.",
               SubstitutionDifficulty::safe,
               ReharmStyle::quartal });

        add ({ "Turn it into a ii-V",
               { chord, dominant (toPitchClass (chord.root() + 5)) },
               "If the bar is static, borrowing the second half for its V7 adds motion without "
               "changing where the progression lands.",
               SubstitutionDifficulty::safe,
               ReharmStyle::common });
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
                   ReharmStyle::common });
        }

        add ({ "Chromatic approach dominant",
               { chord, dominant (toPitchClass (next->root() + 1)) },
               "A dominant a semitone above the target slides down into it; the same motion as a "
               "tritone substitution, used as a passing chord.",
               SubstitutionDifficulty::advanced,
               ReharmStyle::bebop });
    }

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

    std::stable_sort (substitutions.begin(), substitutions.end(),
                      [] (const Substitution& a, const Substitution& b)
                      {
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
