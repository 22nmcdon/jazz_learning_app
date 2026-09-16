#pragma once

#include "jazz/core/ChordSymbol.h"
#include "jazz/core/ScaleSuggester.h"

#include <optional>
#include <string>
#include <vector>

namespace jazz::core
{

/** Where one note of a line sits against the chord it was played over.

    Three tiers, like the substitution difficulties - but they are not the same
    three and must not be made to share a type. Safe/advanced/risky is a
    judgement about how far a substitution goes; this is a statement about where
    a note sits. A note outside the scale is not "risky": it may be the best
    note in the line. Nothing here scores a note, and nothing should.
*/
enum class NoteColour
{
    chordTone,   ///< in the chord the bar asks for
    scaleTone,   ///< in a scale that fits the chord, but not in the chord
    outside      ///< in neither
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
};

/** How a stretch of line divided up. Counts rather than percentages: a
    percentage of nothing is not zero, it is nothing, and the difference matters
    at the start of a take.
*/
struct LineStats
{
    int chordTones {};
    int scaleTones {};
    int outside {};

    int total() const noexcept { return chordTones + scaleTones + outside; }

    /** Rounded percentages that always add up to 100 when anything was played,
        so a readout never shows three numbers making 99. */
    int percentChordTones() const noexcept;
    int percentScaleTones() const noexcept;
    int percentOutside() const noexcept;
};

/** The part of a take spent on one bar. */
struct LineBar
{
    int measureIndex {};
    std::string chordSymbol;
    LineStats stats;
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
};

/** Reads a solo line against the chart it is played over.

    This is the other half of `VoicingAnalyzer`. That one is handed a chord - a
    set of notes sounding at once - and asks whether it says what the symbol
    says. This one is handed notes one at a time and asks where each sits. They
    share no code and should not: a voicing is a thing, a line is a stream.

    Two ways to use it, and both are the real thing:

      - `read()` is pure. One note, one chord, one answer, no state anywhere.
        Every rule lives here and this is where the tests point.
      - a take is `read()` with a memory: `startTake()`, then `setTarget()` each
        time the player moves to another bar, `play()` for each note, and
        `endTake()` for the summary. Notes keep accumulating across bars, so
        walking the chart during a take is one take, not several.

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
    */
    LineNote play (int midiNote);

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

    Options options;

    struct Target
    {
        int measureIndex {};
        ChordSymbol chord;
        std::vector<ScaleSuggestion> scales;  ///< cached: one lookup per bar, not per note
    };

    std::optional<Target> target;
    std::vector<LineNote> played;
    bool taking {};
};

/** "chord tone", "scale tone", "outside" - for a UI that shows the word. */
std::string noteColourName (NoteColour colour);

} // namespace jazz::core
