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
    advanced,  ///< tritone subs, altered dominants, chromatic approaches
    risky      ///< works in the right instance and nowhere else - see VoiceLeadingVerdict
};

std::string difficultyName (SubstitutionDifficulty difficulty);

/** A vocabulary the substitution belongs to, for the style-preset filter.

    **Only ever a tag on a rule.** `common` means "belongs to no particular
    vocabulary, offer it whatever the player asked for" - it does not mean
    "no filter". Not filtering is `Options::style` holding nothing; see there.

    **Two values were deleted rather than shipped, and that is the lesson this
    comment exists for.** There used to be a `quartal` and a `brazilian` here.
    Counted against the rules that actually carried them: quartal was **one**
    rule, reachable only on a minor bar, and brazilian was **none at all**. A
    menu offering a Brazilian reharmonisation and answering with the five
    generic rules is worse than a menu that does not offer one - so a style
    lives here only while at least one rule is tagged with it, and
    `ReharmonizerTests` asserts exactly that. Adding `brazilian` back means
    writing the bossa rules first, not the enumerator first.
*/
enum class ReharmStyle
{
    common,
    bebop,
    modal
};

/** The display name, as it appears beside a substitution. */
std::string styleName (ReharmStyle style);

/** One row of the style menu, as the shells build it.

    The shells hold no list of their own - which vocabularies exist is theory,
    and a second copy of it in a page is a second copy that goes stale. Same
    reason `scaleStyles()` and `compStyles()` are shaped this way.

    **The "no filter" row is in here too**, as the `all` key, so the engine owns
    the whole menu rather than a page inventing its first entry.
*/
struct ReharmStyleDefinition
{
    std::string key;      ///< stable, lowercase, what crosses the wire
    std::string name;     ///< what a player reads
    std::string summary;  ///< one line saying what it is for
};

/** Every style the menu offers, in menu order, the `all` row first. */
const std::vector<ReharmStyleDefinition>& reharmStyles();

/** The style a key names, or nothing for `all` - and for anything unknown.

    An unrecognised key falls back to offering everything rather than to an
    error. A renamed or dropped style should still reharmonise; the asymmetry
    with a malformed comping style is deliberate, because there the broken
    message would put a sound under a player and here it only widens a list.
*/
std::optional<ReharmStyle> styleFrom (const std::string& key);

/** Whether a substitution actually works where it is being offered.

    The adventurous substitutions are not wrong in general and right in general:
    a chord a tritone away lands beautifully into one progression and falls flat
    in the next. Rather than warn in the abstract, the engine measures this
    instance - what the guide tones have to do to get in and out of the chord,
    and what it keeps from the chord it replaces - and says whether it works
    here.
*/
struct VoiceLeadingVerdict
{
    int approachCost {};        ///< guide-tone movement from the previous chord into this one
    int departureCost {};       ///< ... and out of it into the next chord
    int sharedWithOriginal {};  ///< notes in common with the chord being replaced
    bool smoothHere {};         ///< the verdict for this instance
    std::string note;           ///< why, naming the notes involved

    int totalCost() const { return approachCost + departureCost; }
};

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

    /** How this substitution behaves in the bar it was offered for. Filled in
        for every substitution; it is what makes a risky one usable.
    */
    VoiceLeadingVerdict voiceLeading {};

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

/** One guide tone of a chord, voiced where the hand already playing can reach it. */
struct VoicedGuideTone
{
    int note {};            ///< the MIDI note to play
    std::string label;      ///< "3", "b7", "6"
    int from {};            ///< the note under the hand it leads from
    int semitones {};       ///< signed movement from that note
};

/** Voices the guide tones of @p chord for a hand already on @p playedNotes.

    `guideToneMotion` answers this for two chord *symbols*: the 3rd and 7th of
    an idealised voicing of one resolving into an idealised voicing of the next.
    This answers it for the notes actually under a player's hands, which is a
    different question whenever what they played is not what the engine would
    have played - a rootless voicing, an inversion, a left hand on its own, or
    in solo practice a single note.

    Each guide tone is placed in the octave nearest the note it leads from, and
    the notes are paired to the tones so that the hand as a whole moves least -
    not tone by tone, which lets both of them claim the same finger and leaves
    the other voice stranded. Two voices that would swap places are a swap the
    hand does not need, so an assignment that crosses loses a tie.

    The played notes are used up before any is used twice, unless there are
    fewer of them than there are guide tones - one note can lead into both, and
    in solo practice usually has to.

    Empty in, empty out: a hand playing nothing is not leading anywhere, and a
    guide tone voiced for it would be voiced for a reference octave this has no
    business inventing.
*/
std::vector<VoicedGuideTone> voiceGuideTones (const ChordSymbol& chord,
                                              const std::vector<int>& playedNotes);

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

        /** Offer the substitutions that only work in the right instance. They
            arrive with a verdict on whether this is one of those instances.
        */
        bool includeRisky { false };

        /** Hold a style and only substitutions tagged with it - or tagged
            `common` - are returned. Hold nothing and nothing is filtered.

            **This used to be a plain `ReharmStyle` defaulting to `common`, and
            that made `common` do two jobs.** As a tag on a rule it means
            "generic"; as a value here it meant "do not filter" - so a style
            menu could not offer Common as a peer of the others without it
            silently meaning All. An empty optional says "no filter" without
            borrowing a word that already means something else.
        */
        std::optional<ReharmStyle> style;
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

//==============================================================================
/** A named way of reharmonising a whole tune rather than one bar. */
enum class ReharmPlanKind
{
    minimalTouch,    ///< colour on the bars that were only marking time
    recommended,     ///< safe moves, spaced out, the tune still recognisable
    adventurous,     ///< borrowed chords and mediants, every bar in play
    cycleOfFifths,   ///< ii-Vs and secondary dominants: keep it moving
    modalColour,     ///< borrow from the parallel minor throughout
    outThere         ///< risky moves, but only in the bars where they land
};

std::string planName (ReharmPlanKind kind);

/** One bar the plan rewrites, kept so the UI can show its working. */
struct PlannedMove
{
    int measureIndex {};
    std::string before;          ///< the bar as written, e.g. "G7"
    std::string after;           ///< what it becomes, e.g. "Dm7 G7"
    std::string substitution;    ///< the name of the move
    SubstitutionFamily family { SubstitutionFamily::extension };
};

/** A whole-tune reharmonisation: the resulting chart and how it got there. */
struct ReharmPlan
{
    ReharmPlanKind kind { ReharmPlanKind::recommended };
    std::string name;
    std::string description;     ///< what this plan does, in the user's language
    Chart chart;                 ///< the tune with the plan applied
    std::vector<PlannedMove> moves;

    int barsChanged() const { return static_cast<int> (moves.size()); }
};

/** Applies @p kind across the whole of @p chart.

    Each bar is decided against the chart as it stands, so a bar sees what the
    bar before it became. The pass is deterministic: the same chart and plan
    give the same result every time.
*/
ReharmPlan makeReharmPlan (const Chart& chart, ReharmPlanKind kind);

/** Every plan, in the order they should be offered - lightest touch first. */
std::vector<ReharmPlan> reharmPlansFor (const Chart& chart);

//==============================================================================
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
