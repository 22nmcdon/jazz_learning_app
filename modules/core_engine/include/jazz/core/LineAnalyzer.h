#pragma once

#include "jazz/core/ChordSymbol.h"
#include "jazz/core/ScaleSuggester.h"

#include <optional>
#include <string>
#include <vector>

namespace jazz::core
{

/** Where one note of a line sits against the chord it was played over.

    Not the same tiers as the substitution difficulties, and they must not be
    made to share a type. Safe/advanced/risky is a judgement about how far a
    substitution goes; this is a statement about where a note sits. A note
    outside the scale is not "risky": it may be the best note in the line.
    Nothing here scores a note, and nothing should.

    The last three are the reason this enum grew, and the reason it has an
    order. The first two can be decided from one note and one chord. The others
    cannot be decided from a note at all: a chromatic approach, an enclosure and
    a passing tone are outside by pitch and *are the line working*, and what
    separates them from a note that simply did not land is where the line goes
    next.

    So a note outside the harmony starts `unresolved`, which is a real answer
    and not a placeholder: it says the line has opened something and not yet
    closed it. Two notes later the window has had every chance it will get, and
    the note becomes `approach` or `outside` - for good, in both directions.

    `read()`, which is pure and sees one note, returns `outside` for a note
    outside the harmony, because with one note there is no line to be waiting
    on. Only a take produces `unresolved` or `approach`.
*/
enum class NoteColour
{
    chordTone,   ///< in the chord the bar asks for
    scaleTone,   ///< in a scale that fits the chord, but not in the chord
    approach,    ///< outside by pitch, and resolved by step into one of the above
    unresolved,  ///< outside by pitch, and the line has not said yet
    outside      ///< outside by pitch, and the line went somewhere else
};

/** Whether a colour is outside the harmony by pitch alone.

    True for all three of `approach`, `unresolved` and `outside`, because none
    of them changed pitch - only what the line did with it. Anything asking "was
    that note in the scale" wants this; anything asking "did that note work"
    wants the colour itself.
*/
bool isOutsideByPitch (NoteColour colour) noexcept;

/** Whether a colour is final, or still waiting on the notes after it. */
bool isSettled (NoteColour colour) noexcept;

/** One note of a line, read against the bar it landed in. */
struct LineNote
{
    int midiNote {};
    int measureIndex {};
    NoteColour colour {};

    /** Against the chord's root: "R", "b3", "9", "#11". Written for every note,
        outside ones included - "a b9 over Dm7" says more than "outside". */
    std::string degree;

    /** The scale that accounts for a scale tone; empty for the other two. */
    std::string scaleName;

    /** A scale tone a semitone above a chord tone - the 4th over a major
        seventh, and its relatives. It is in the scale, so it is a scale tone
        and stays one: landing on it is what sounds wrong, and passing through
        it is what every line does. Named rather than marked down. */
    bool avoidNote {};

    /** The chord this note was read against, as written. */
    std::string chordSymbol;

    /** For an `approach` note, the note it resolved into - the thing that made
        it an approach rather than a miss. Zero otherwise. */
    int resolvesTo {};

    /** For an `unresolved` note, the nearest note that would close it: a step
        away, semitones before tones, and a chord tone before a scale tone at
        the same distance - the order a player thinks in.

        This is why "outside" is not the first thing said about a note. "That
        was wrong" is a guess at this point and a third of the time it is the
        wrong guess; "a semitone up lands on D" is true whatever happens next,
        and it is the thing that would have helped. Zero, with an empty degree,
        when nothing within a step lands - rare, and honest when it happens.
    */
    int wantsToReach {};
    std::string wantsToReachDegree;
};

/** How a stretch of line divided up. Counts rather than percentages: a
    percentage of nothing is not zero, it is nothing, and the difference matters
    at the start of a take.
*/
struct LineStats
{
    int chordTones {};
    int scaleTones {};
    int approachTones {};
    int unresolved {};
    int outside {};

    int total() const noexcept
    {
        return chordTones + scaleTones + approachTones + unresolved + outside;
    }

    /** Everything that worked: the two inside tiers and the approach notes,
        which are outside by pitch and inside by intent. */
    int landed() const noexcept { return chordTones + scaleTones + approachTones; }

    /** The notes whose reading is final. A note the line has opened and not yet
        closed is not a note that went wrong, so nothing judges it until it is
        one or the other. */
    int settled() const noexcept { return total() - unresolved; }

    /** Rounded percentages that always add up to 100 when anything was played,
        so a readout never shows a row of numbers making 99. */
    int percentChordTones() const noexcept;
    int percentScaleTones() const noexcept;
    int percentApproachTones() const noexcept;
    int percentUnresolved() const noexcept;
    int percentOutside() const noexcept;

    /** How this stretch of line went, 0 to 100. Zero for nothing played.

        The one number here that is a judgement rather than a count, so it is
        worth being plain about what it judges.

        Read over `settled()` notes alone. A note the line has opened and not
        yet closed is not a note that went wrong, and grading it as one - even
        for the two notes before the window decides - meant a score that dipped
        every time a player reached for a chromatic approach and climbed back
        once they landed it. Nothing is judged until there is something to
        judge.

        Of what has settled: chord tones, scale tones and approach notes all
        count as landing, the difference between them being colour rather than
        correctness. What is left as `outside` counts a quarter rather than
        nothing, because a note that resolved into nothing may still have been
        the best note in the line.

        What is left is balance, and it costs fifteen points at the very most.
        A line is chord tones anchoring it and everything else colouring it,
        so a bar that leans all the way onto the chord and one that never
        touches it are both one-sided, and are marked the same. Approach notes
        count as colour here: they are the opposite of never leaving the chord. Anywhere from about a
        third to about two thirds chord tones gives up nothing at all. And the
        allowance fades in with the length of the bar, because two notes are
        not unbalanced, they are two notes.

        It is a reading of a bar, not a grade for a player. Nothing calls a
        note wrong, here or anywhere else in this file.
    */
    int score() const noexcept;
};

/** The part of a take spent on one bar. */
struct LineBar
{
    int measureIndex {};
    std::string chordSymbol;
    LineStats stats;

    /** The line went over this bar without ever leaving the chord.

        Not a mistake and not scored as one - it is the "add some colour" flag,
        and it is deliberately about the *bar* rather than about a run of N
        notes. The bar is a boundary the player already feels and the take
        already tracks, where any note count would be a number pulled out of
        the air. An approach note does not clear it: the flag asks whether the
        line found anything to say over this chord, and a note on its way
        somewhere else has not answered that.
    */
    bool neverLeftTheChord {};
};

/** A finished take, read back. */
struct TakeSummary
{
    LineStats overall;

    /** Every bar played over, in the order it was first reached. Walking back
        to a bar adds to the entry that is already there rather than making a
        second one - the summary is about bars, not about visits. */
    std::vector<LineBar> bars;

    /** One line for the panel header. */
    std::string summary;

    /** A few constructive readings of the numbers above, most useful first.
        Framed the way the voicing analyser frames things: a note outside the
        scale is outside the scale, never wrong. */
    std::vector<std::string> observations;

    /** Shape, rather than content: what the line did as a line, regardless of
        which chords it was over. These feed the observations above and are
        left on the summary because a caller may want to draw them.

        None of them touches the score. They are advice about how a line moves,
        the score is a reading of where its notes sat, and mixing the two would
        make a number nobody could explain out of one that can be. */
    int leaps {};             ///< intervals of a fourth or more between consecutive notes
    int leapsResolved {};     ///< of those, the ones the next note stepped away from
    int lowestNote {};        ///< 0 when nothing was played
    int highestNote {};

    int rangeInSemitones() const noexcept
    {
        return highestNote > 0 ? highestNote - lowestNote : 0;
    }
};

/** Reads a solo line against the chart it is played over.

    This is the other half of `VoicingAnalyzer`. That one is handed a chord - a
    set of notes sounding at once - and asks whether it says what the symbol
    says. This one is handed notes one at a time and asks where each sits. They
    share no code and should not: a voicing is a thing, a line is a stream.

    Two ways to use it, and both are the real thing:

      - `read()` is pure. One note, one chord, one answer, no state anywhere.
        Every rule that can be decided from a single note lives here.
      - a take is `read()` with a memory, and with a *window*: `startTake()`,
        then `setTarget()` each time the player moves to another bar, `play()`
        for each note, and `endTake()` for the summary. Notes keep accumulating
        across bars, so walking the chart during a take is one take, not
        several.

    The window is the part worth knowing about. A chromatic approach, an
    enclosure and a passing tone are all outside by pitch, and all three are
    the line working rather than failing - but none of them can be told apart
    from a note that simply did not land until the note *after* it arrives.

    So a note outside the harmony is not read as `outside` when it is played.
    It is read as `unresolved`, and stays that way for exactly as long as the
    window can still reach it - which is usually only the next note. A note
    that lands somewhere else settles the one before it immediately: the step
    patterns have been tried, and an enclosure needs that note to be outside as
    well. Only a second outside note with room for a target between the two
    holds the verdict back a further note. Then it becomes `approach` or
    `outside`, once, and never changes again.

    That is a statement about *when* a thing is decided, and it matters twice
    over. The counts do not judge an unresolved note - the score reads
    `LineStats::settled()` - so a score no longer dips every time a player
    reaches for a chromatic approach and climbs back when they land it. And a
    shell has something true to say at the moment of playing that is not "that
    was wrong": the line has opened something, and here is what would close it.

    Nothing that has settled is ever revisited. A note that landed stays
    landed, and a note left hanging stays hanging.

    There is no clock. A take is bounded by the player arming and disarming it,
    not by a transport, which is what makes it testable with no time in it at
    all. A tempo-driven version would drive `setTarget()` from a clock and need
    nothing else from here.
*/
class LineAnalyzer
{
public:
    struct Options
    {
        /** Read a note against every scale that fits the chord at once.

            Off, and this is the one default here worth arguing about. Reading
            against all of them sounds like the forgiving choice - a player who
            picked a different valid scale has not made a mistake - but the
            engine offers up to twelve scales per chord and between them they
            cover nearly the whole octave. Counted over the twelve pitch
            classes:

                Cmaj7    4 chord tones,  8 scale tones,  0 outside
                G7       4 chord tones,  8 scale tones,  0 outside
                Dm7      4 chord tones,  7 scale tones,  1 outside
                Bbmaj7   4 chord tones,  8 scale tones,  0 outside

            Nothing is outside anything. The three tiers collapse into two and
            the feature stops saying anything. Against one scale the same
            chords read 4 / 3 / 5, which is a line a player can actually learn
            from.

            Forgiveness belongs in `chosenScale` instead: let the player say
            which scale they are playing, and hold them to that one.
        */
        bool acceptAnyValidScale { false };

        /** The scale to read against, by name - "D Dorian", "G Altered".

            This is the player's own choice out of the Scales panel, and it is
            what makes reading against a single scale forgiving rather than
            merely strict: the app is not insisting on the scale it suggested,
            it is holding you to the one you chose. Unknown or empty falls back
            to the engine's first suggestion. Ignored when every scale is being
            accepted anyway.
        */
        std::string chosenScale;

        /** The soloing vocabulary in play, by `ScaleStyle::key`.

            Empty, or a key this version does not know, means every scale -
            which is the right way round for a key stored by an older or newer
            build: an unfamiliar style widens the answer rather than emptying
            it. A style that has nothing to offer for a chord falls back the
            same way, because a bar nothing can be read against is worse than
            a bar read against the wrong vocabulary for one chord.
        */
        std::string style;
    };

    LineAnalyzer() = default;
    explicit LineAnalyzer (Options optionsToUse) : options (optionsToUse) {}

    /** Reads one note against one chord. Pure - no take needed, and the same
        note over the same chord always reads the same way.

        Two overloads rather than a default argument: GCC will not take `= {}`
        for a nested type whose members have default initialisers, because the
        enclosing class is not complete where the default sits.
    */
    static LineNote read (int midiNote, const ChordSymbol& chord, Options options);
    static LineNote read (int midiNote, const ChordSymbol& chord)
    {
        return read (midiNote, chord, Options {});
    }

    /** Changes how notes are read from here on.

        Notes already in the take keep the reading they were given. That is the
        honest thing rather than the tidy one: they were played against what the
        player was looking at at the time, and re-reading them later would
        rewrite history to match a decision made after the fact.
    */
    void setOptions (Options newOptions);

    //==============================================================================
    void startTake();
    void endTake();
    bool isTaking() const noexcept { return taking; }

    /** The bar the player is soloing over now.

        Called on arming and again whenever they move, during a take or not.
        Moving during a take does not end it: the target changes and the notes
        carry on into the same take.
    */
    void setTarget (int measureIndex, const ChordSymbol& chord);

    bool hasTarget() const noexcept { return target.has_value(); }

    /** Reads a note against the current bar, keeping it when a take is running.

        With no target set the note reads as outside with no degree, which is
        the honest answer to "how does this sit against nothing".

        A note outside the harmony comes back `unresolved` rather than
        `outside`: the line has opened something, and at this instant nothing
        can know whether it will close it. What a caller can say is what the
        note now needs.

        This also resolves and settles the notes behind it - see the class note
        above - so the returned note is the new one, and `notes()` may have
        changed further back than the end. It happens with or without a take
        running: with one the readings land in the counts, without one they
        still show up in `resolvedByLastNote()` and `strandedByLastNote()`,
        because both are worth saying to someone who has not armed anything.
    */
    LineNote play (int midiNote);

    /** The notes this last `play()` promoted to `approach`, in the order the
        window found them.

        Empty almost always. It exists so a shell can say "and that Db before
        it was on its way here" rather than silently improving a number the
        player is looking at.
    */
    const std::vector<LineNote>& resolvedByLastNote() const noexcept { return justResolved; }

    /** The notes this last `play()` settled as `outside` - ones the window has
        now passed without the line closing them.

        The other half of the pair, and the one carrying the bad news. It
        arrives late by construction: nothing can know a note was left hanging
        until the notes that could have saved it have been played. A shell that
        reports the first list and not this one tells a player only what they
        got right.
    */
    const std::vector<LineNote>& strandedByLastNote() const noexcept { return justStranded; }

    const std::vector<LineNote>& notes() const noexcept { return played; }

    /** What the take has done on one bar so far. Zeroed stats for a bar that
        has not been played over, which is a real answer rather than an error. */
    LineStats statsForBar (int measureIndex) const;

    /** What the take has done overall so far. */
    LineStats stats() const;

    /** The take, read back. Works during a take as well as after one. */
    TakeSummary summary() const;

private:
    LineNote readAgainstTarget (int midiNote) const;

    /** Promotes any of the last few notes of @p line that the newest resolved. */
    void resolveTail (std::vector<LineNote>& line);

    /** Closes every open note the line can no longer reach. */
    void settleTail (std::vector<LineNote>& line);

    bool promote (std::vector<LineNote>& line, std::size_t index, int target);

    Options options;

    struct Target
    {
        int measureIndex {};
        ChordSymbol chord;
        std::vector<ScaleSuggestion> scales;  ///< cached: one lookup per bar, not per note
    };

    std::optional<Target> target;
    std::vector<LineNote> played;

    /*  The last few notes when no take is running, so the window still works
        for someone trying things out. A player who has not armed anything is
        the one most likely to be experimenting with chromatic notes, and
        telling them those were misses is the exact lesson this is here to
        stop. Kept separate from `played` because these notes are not counted:
        a reading is not a tally. Cleared at both edges of a take, because a
        take starts and ends clean. */
    std::vector<LineNote> recent;

    std::vector<LineNote> justResolved;
    std::vector<LineNote> justStranded;
    bool taking {};
};

/** "chord tone", "scale tone", "outside" - for a UI that shows the word. */
std::string noteColourName (NoteColour colour);

} // namespace jazz::core
