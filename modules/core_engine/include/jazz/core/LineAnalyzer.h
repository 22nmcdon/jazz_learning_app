#pragma once

#include "jazz/core/ChordSymbol.h"
#include "jazz/core/Rhythm.h"
#include "jazz/core/ScaleSuggester.h"
#include "jazz/core/Voicing.h"

#include <cstddef>
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

/** How an approach note got home.

    Not a tier and not a fifth colour: all three land, all three count the same
    in `LineStats`, and the score has no opinion about which. This is about the
    *gesture*, which is a different question from where the note sat - and it is
    worth telling apart because the three are not the same thing to play.

    An enclosure in particular is the deliberate one. Taking a note from both
    sides before playing it is a thing a player practises on purpose, and a
    reading that called it "an approach note" alongside a passing tone would be
    losing the harder thing they did.
*/
enum class ApproachKind
{
    none,        ///< not an approach note
    chromatic,   ///< a semitone into the note that followed it
    passing,     ///< stepped into, stepped out of, still going the same way
    enclosure    ///< one of a pair that took the target from both sides
};

/** "chromatic approach", "passing tone", "enclosure" - for a UI that says it. */
std::string approachKindName (ApproachKind kind);

/** Whether a note began an attack of its own or joined the one before it.

    A line is not always one note at a time. Players comp behind themselves and
    solo in block chords, and the most idiomatic thing either does is move a
    whole voicing chromatically into the next one - G7, a G7alt a semitone
    under half of it, then Cmaj7. Read as a stream of single notes that is four
    voices each missing their resolution, because the note after a chord's Ab
    is the chord's own B rather than the G it was heading for.

    So the shell says which notes were struck together, and the engine reads
    what it is given as one attack: the notes of a chord neither resolve nor
    strand one another, and the line resolves voicing to voicing, each voice
    finding its own way home.

    This is a fact the shell has whether or not it has a clock - two keys going
    down at once is not a time, it is a gesture - so it is said here rather
    than inferred from positions. `fresh` is the default and is what an
    ordinary line is made of.
*/
enum class Attack
{
    fresh,         ///< a note of its own: the next note of the line, or the first of a chord
    withPrevious   ///< struck with the note before it - the two are one chord
};

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

    /** For an `approach` note, which of the three gestures got it there. */
    ApproachKind approachKind {};

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

    /** Where in the bar this note fell, when the shell was able to say.

        Empty whenever there is no clock - practising statically, the app has
        no idea where in a bar a note landed and must not invent one. So every
        reading below it is an *extra* reading, never a replacement: a line
        played without the transport is read exactly as it always was.

        This is still not time. A `BarPosition` is a position in a bar, like
        `measureIndex` is a position in a chart; the shell owns the clock and
        turns one into the other before the engine sees it. Nothing here may
        start depending on when a note actually arrived.
    */
    std::optional<BarPosition> at;

    /** For an outside or avoid note the line did not stay on: the next note
        came within an eighth, so it was passed through rather than sat on.

        The distinction the whole grid was wanted for. An avoid note passed
        through at speed is what every bebop line does; the same note sat on is
        the one that sounds like a mistake, and until a note carried a position
        there was no way to tell the two apart. Needs the *next* note to be
        known, so it is filled in behind, like `resolvesTo`.
    */
    bool passedThrough {};

    /** The note was played on the downbeat or the bar's other strong beat. */
    bool onStrongBeat {};

    /** Struck at the same moment as the note before it: the two are one chord.

        Exactly what the shell said, kept rather than digested, so the runs of
        it are the attacks and a shell wanting to draw a voicing can find them
        again. False for the first note of a chord as well as for every note of
        an ordinary line - it describes the join, not the chord.
    */
    bool struckWithPrevious {};
};

/** One chord in a line, read as a chord.

    The reading the note-by-note one cannot give, and the reason it cannot is
    not that it lacks the notes - it has every one of them, and colours and
    counts each correctly. It is that a chord is not a fact about any of its
    notes. Four notes over Dm7 that are each a scale tone may be an Ebdim7
    passing chord or may be nothing at all, and nothing said about the Eb on
    its own distinguishes those.

    So this is *additive*, exactly like `at` above: every note of a chord is
    still read, coloured, counted and scored on its own, the numbers are the
    numbers they always were, and a line with nothing struck together never
    produces one of these. What it adds is words about the gesture, and words
    only - see the note on `LineStats::score()`.

    **The top note is the line.** A player soloing in block chords plays the
    melody in the top voice and harmonises underneath it, so that is the note
    the reading leads with and the rest is what was put under it. Read the
    other way round - as a chord that happens to have a note on top - it says
    the same thing about a voicing whichever of its notes the line was
    actually singing, which is the half a soloist came for.

    Read by `VoicingAnalyzer` and `ChordIdentifier` rather than by anything
    new here. Those already answer "what is this set of notes, and does it say
    the symbol" - `LineAnalyzer`'s own note above says a voicing is a thing and
    a line is a stream, and the way to keep that true is to hand the thing to
    the thing that reads things, not to grow a second chord reader inside the
    stream.
*/
struct LineChord
{
    std::vector<int> midiNotes;   ///< low to high, however they were struck
    int measureIndex {};

    /** The bar's chord, as written - what this was played *over*. */
    std::string chordSymbol;

    /** How the hand laid it out, as `VoicingAnalyzer` classifies it. */
    VoicingType type {};
    std::string typeName;

    /** The top note: the one the line is on, and what it is over the bar. */
    int melodyNote {};
    std::string melodyDegree;
    NoteColour melodyColour {};

    /** The notes carry the bar's chord: its guide tones are there and nothing
        is outside it. **Not a pass mark.** A block-chord soloist plays
        diminished and chromatic chords through a bar on purpose, and the
        reading below says what those *are* rather than that they are not the
        symbol. Nothing here scores a chord, and nothing should. */
    bool saysTheChord {};

    /** What the notes spell when they do not spell the bar's chord - the
        passing diminished, the chord a semitone above, the dominant being
        approached. Empty when they do, and empty when no name accounts for
        them, which is a real answer for a cluster and not a failure. */
    std::string spelled;

    /** The notes outside both the chord and its scale. */
    std::vector<int> outsideNotes;

    /** What the notes come to, as a sentence: "A rootless left-hand voicing -
        that says Dm7", "That reads as Ebdim7 over Dm7". The half of the
        reading that is about the chord rather than about the note on top. */
    std::string verdict;

    /** The whole of it - the note on top, then the verdict. Exactly
        `"<melody phrase>. " + verdict`, so a shell wanting the two halves
        separately takes `verdict` rather than splitting this one. */
    std::string reading;
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

    /** Of the notes that landed on a strong beat, how many were chord tones,
        and how many landed there at all.

        Both zero when the shell gave no positions, which is the same answer as
        "nothing landed on a strong beat" and is fine: a bar with nothing to
        say about its rhythm says nothing about it.

        Like every other shape reading, this produces words and never points -
        see the note on `score()`. Where a note sits in the bar is not a better
        or worse note, and the moment it moved a number the score would stop
        being explainable.
    */
    int notesOnStrongBeats {};
    int chordTonesOnStrongBeats {};
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

    /** Outside or avoid notes the line sat on rather than passed through, and
        how many such notes there were to sit on. Zero for a take played with
        no clock, where nothing knows how long anything lasted. */
    int notesSatOn {};
    int notesPassedThrough {};

    /** Chords in the line - attacks of two notes or more - and how many of
        their notes were outside and stepped home into the next voicing.

        Counted because chordal playing is a different thing to be doing and
        worth saying back, not because it is worth more: a note in a chord is
        counted, coloured and scored exactly like any other note. */
    int chordsPlayed {};
    int chordVoicesResolved {};

    /** Of `chordsPlayed`, the ones whose notes carried the bar's own chord.

        The rest are not mistakes and the gap between the two numbers is not an
        error rate - a chord that reads as something else over the bar is the
        whole vocabulary of block-chord playing. It is here so the summary can
        say which of the two things the player was doing, since a take of
        diminished passing chords and a take of the chart's own harmony in
        four voices are different practice and look identical in every other
        number on this struct. */
    int chordsSpellingTheBar {};

    /** The shape played most, named - "two-handed rootless voicing". Empty
        when no chords were played, and settled by a simple count: a take that
        used two shapes evenly gets whichever came first, which is the right
        amount of confidence for a sentence saying "mostly". */
    std::string chordShape;

    /** Every chord of the take, in the order they were played, each read
        against the bar's chord as it stood when it was struck. Empty for a
        line played one note at a time, which is most of them. */
    std::vector<LineChord> chords;

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

    Nothing that has settled is ever revisited, with one exception, and the
    exception is the whole of what a chord changes: a note stranded by the
    first note of a chord may still be reached by the rest of that chord. The
    window cannot tell a chord's first note from an ordinary next note until
    the second one arrives, so it judges as it always did and takes the
    judgement back when the same gesture turns out to answer it. Nothing across
    two gestures is ever revisited, which is the rule that was actually meant.

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

        /** The metre the chart is in, which decides which beats are strong.

            Only ever read when a note carries a position, so a take with no
            clock behind it is unaffected by getting this wrong. It is the
            chart's own number - see `Chart::timeSignature` - and a waltz read
            as four would call its second beat strong, which is exactly what a
            waltz does not do.
        */
        int beatsPerBar { 4 };
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
    LineNote play (int midiNote, Attack attack = Attack::fresh);

    /** The same, told where in the bar the note fell.

        A shell with a clock running knows this and a shell without one does
        not, which is why it is an overload rather than a defaulted argument:
        there is no position that means "no position", and a made-up downbeat
        would be read as a real one.
    */
    LineNote play (int midiNote, BarPosition where, Attack attack = Attack::fresh);

    /** The notes this last `play()` promoted to `approach`, in the order the
        window found them.

        Empty almost always. It exists so a shell can say "and that Db before
        it was on its way here" rather than silently improving a number the
        player is looking at.

        Cleared at the start of each *attack* rather than of each note, so the
        notes of a chord accumulate one answer between them. That is what stops
        a shell showing "left hanging" for the twenty milliseconds between a
        chord's first note and the one that actually resolved it.
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

    /** The chord being played now, when the last attack is one.

        Empty for a line played one note at a time, which is almost all of
        them: two notes have to have been struck together before there is
        anything here at all.

        It grows with the chord rather than waiting for it. A shell asking
        after each note of a four-note voicing gets the reading of two notes,
        then of three, then of four - the same way `resolvedByLastNote()`
        accumulates across an attack, and for the same reason: nothing may
        buffer a note to see what arrives next, so the reading improves in
        front of the player instead of appearing late.
    */
    std::optional<LineChord> chordSoFar() const noexcept { return currentChord; }

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

    /** The notes of one attack: a half-open range of the line. */
    struct AttackSpan
    {
        std::size_t begin {};
        std::size_t end {};
    };

    /** The line divided into attacks, oldest first. Every note belongs to
        exactly one, and a line with nothing struck together is a run of
        attacks one note long - which is why everything below reduces to what
        it did before the moment no chords are played. */
    static std::vector<AttackSpan> attacksIn (const std::vector<LineNote>& line);

    /** Says of the attack before the newest whether the line stayed on it. */
    void markPassedThrough (std::vector<LineNote>& line, const std::vector<AttackSpan>& attacks);

    /** Promotes any note of the last two attacks that the newest resolved. */
    void resolveTail (std::vector<LineNote>& line, const std::vector<AttackSpan>& attacks);

    /** Closes every open note the line can no longer reach. */
    void settleTail (std::vector<LineNote>& line, const std::vector<AttackSpan>& attacks);

    /** Whether any pattern could still promote the note at @p noteIndex. */
    static bool canStillBeReached (const std::vector<LineNote>& line,
                                   const std::vector<AttackSpan>& attacks,
                                   std::size_t attackIndex,
                                   std::size_t noteIndex);

    bool promote (std::vector<LineNote>& line, std::size_t index, int target, ApproachKind kind);

    /** Rebuilds the two public lists from the indices behind them. */
    void publishJust (const std::vector<LineNote>& line);

    /** Reads one attack as a chord. Empty for an attack of one note - a line
        is not a chord - and for a span this cannot read against a chord. */
    static std::optional<LineChord> readChord (const std::vector<LineNote>& line,
                                               AttackSpan attack,
                                               const ChordSymbol& chord);

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

    /*  Held as indices into the line rather than as copies, so a note revived
        by the rest of its chord can be taken back out of the stranded list
        instead of appearing in both. The public vectors are built from these
        at the end of every `play()`, which also means they never carry a
        reading that has since moved on. */
    std::vector<std::size_t> justResolvedAt;
    std::vector<std::size_t> justStrandedAt;

    /*  Notes stranded since the current attack began - the only notes a later
        note of that attack is allowed to promote. See the class note. */
    std::vector<std::size_t> strandedThisAttack;

    std::vector<LineNote> justResolved;
    std::vector<LineNote> justStranded;

    /*  The attack in progress, read as a chord, and every chord of the take.

        Both are worked out as the notes arrive rather than in `summary()`,
        because a chord has to be read against the chord the player was looking
        at when they struck it. Reharmonise-as-you-play moves a bar's symbol
        mid-take, and a summary re-reading the take's first chord against the
        bar's *current* symbol would be rewriting history to match a decision
        made after the fact - which is the same reason `setOptions` leaves the
        notes already played with the reading they were given.

        `currentChordBegin` is where in the line the attack being read starts,
        so a chord growing from two notes to four replaces its own entry
        instead of leaving three. */
    static constexpr std::size_t noChordInProgress = static_cast<std::size_t> (-1);

    std::optional<LineChord> currentChord;
    std::vector<LineChord> takeChords;
    std::size_t currentChordBegin { noChordInProgress };

    bool taking {};

    /*  Where the note now being played fell, for the moment it takes `play()`
        to read it. A parameter threaded through five private functions would
        have been the alternative, and all five would then have carried a
        position they had nothing to do with. */
    std::optional<BarPosition> pendingPosition;
};

/** "chord tone", "scale tone", "outside" - for a UI that shows the word. */
std::string noteColourName (NoteColour colour);

/** The scale a take would read this chord against, given these options.

    Public for one reason, and it is worth stating so nobody narrows it back:
    **`LineWriter` writes a line this analyser has to read back as written**,
    and the two cannot be allowed to disagree about which scale that is. A
    generator picking "the primary suggestion" while a take reads against the
    scale the player chose would colour half its own notes wrong - the note it
    wrote as a scale tone read as outside, the approach it was careful to make
    chromatic read as an ordinary step.

    Empty only when nothing at all fits the chord, which is what
    `LineAnalyzer`'s own fallback already treats as "read against everything".
*/
std::optional<ScaleSuggestion> readingScaleFor (const ChordSymbol& chord,
                                                const LineAnalyzer::Options& options);

} // namespace jazz::core
