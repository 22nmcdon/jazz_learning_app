#pragma once

#include "jazz/core/Chart.h"
#include "jazz/core/ChordSymbol.h"
#include "jazz/core/Voicing.h"

#include <optional>
#include <string>
#include <vector>

namespace jazz::core
{

/** How far outside the original harmony a substitution takes the player.
    Surfaced in the UI so learners can stay on safe ground while they build up.
*/
enum class SubstitutionDifficulty
{
    safe,      ///< diatonic, common, hard to get wrong
    advanced   ///< tritone subs, altered dominants, chromatic approaches
};

std::string difficultyName (SubstitutionDifficulty difficulty);

/** A vocabulary the substitution belongs to, for the style-preset filter. */
enum class ReharmStyle
{
    common,
    bebop,
    modal,
    quartal,
    brazilian
};

std::string styleName (ReharmStyle style);

/** What kind of move a substitution is.

    Ordered from the ones closest to the original harmony to the ones furthest
    from it, so a list grouped by family reads as a path a learner can walk
    down rather than a flat pile of options.
*/
enum class SubstitutionFamily
{
    extension,        ///< same chord, richer colour
    diatonic,         ///< another chord from the same key
    dominantFunction, ///< re-routing the pull to the next chord
    modalInterchange, ///< borrowed from the parallel minor or another mode
    chromaticMediant, ///< a third away, sharing tones but not a key
    passingChord,     ///< something inserted on the way to the next chord
    bassMotion        ///< same harmony, different note underneath
};

std::string familyName (SubstitutionFamily family);

/** One offered reharmonisation of a single measure. */
struct Substitution
{
    std::string name;                     ///< "Tritone substitution"
    std::vector<ChordSymbol> replacement; ///< chords that replace the measure, in order
    std::string explanation;              ///< why it works, in the user's language
    SubstitutionDifficulty difficulty { SubstitutionDifficulty::safe };
    ReharmStyle style { ReharmStyle::common };
    SubstitutionFamily family { SubstitutionFamily::extension };

    /** Total guide-tone movement into the following chord, in semitones. Lower
        is smoother; used to rank substitutions and to drive the voice-leading
        visualiser.
    */
    int voiceLeadingCost {};

    /** "Db7#11" or "Am7 D7" - the replacement as it would be written. */
    std::string replacementText() const;
};

/** A substitution the player has landed on by ear rather than by choosing it.

    When someone tries a voicing over a bar and what they play spells one of the
    substitutions available for that bar, that is worth telling them: they have
    found a reharmonisation, not made a mistake.
*/
struct RecognisedSubstitution
{
    Substitution substitution;  ///< the substitution the voicing belongs to
    ChordSymbol chord;          ///< which of its chords was played
    int score {};               ///< how cleanly the voicing spells that chord, 0-100
    int writtenChordScore {};   ///< how the same voicing reads against the written chord
};

/** Movement of one guide tone (3rd or 7th) between two chords. */
struct GuideToneMotion
{
    int fromNote {};        ///< MIDI note in the source chord
    int toNote {};          ///< MIDI note it resolves to
    std::string fromLabel;  ///< "3", "b7"
    std::string toLabel;
    int semitones {};       ///< signed movement
};

/** Voice-leads the 3rd and 7th of @p from into @p to, choosing the closest
    available target for each. The reference octave only affects note numbers.
*/
std::vector<GuideToneMotion> guideToneMotion (const ChordSymbol& from,
                                              const ChordSymbol& to,
                                              int referenceNote = 60);

/** Total absolute guide-tone movement between two chords. */
int voiceLeadingCost (const ChordSymbol& from, const ChordSymbol& to);

/** Rule-based reharmonisation suggestions for one measure of a chart.

    Every rule is a music-theory heuristic with an explanation attached; nothing
    here is data-driven. The design doc leaves rule-based vs. data-informed
    ranking open, so the rules are kept separable and each carries its own
    difficulty and style tag for filtering.
*/
class Reharmonizer
{
public:
    struct Options
    {
        bool includeAdvanced { true };

        /** When set to anything but `common`, only substitutions tagged with
            this style (or tagged `common`) are returned.
        */
        ReharmStyle style { ReharmStyle::common };
    };

    Reharmonizer() = default;
    explicit Reharmonizer (Options optionsToUse) : options (optionsToUse) {}

    /** Substitutions for the measure at @p measureIndex, best voice-leading first. */
    std::vector<Substitution> substitutionsFor (const Chart& chart, int measureIndex) const;

    /** Applies a substitution, returning the resulting chart. */
    static Chart applySubstitution (const Chart& chart, int measureIndex, const Substitution& substitution);

private:
    Options options;
};

/** Reads a voicing against every substitution available for a measure.

    Returns the substitution the voicing spells, if it spells one clearly better
    than it spells the chord actually written in the bar. Voicings that simply
    are the written chord, or that are too small to be conclusive, return
    nothing - silence is the common case and a false positive is worse than none.

    @param minimumImprovement  how far the reading has to beat the written chord
                               before it is worth mentioning
*/
std::optional<RecognisedSubstitution> recogniseSubstitution (const Voicing& voicing,
                                                             const Chart& chart,
                                                             int measureIndex,
                                                             Reharmonizer::Options options = {},
                                                             int minimumImprovement = 10);

} // namespace jazz::core
