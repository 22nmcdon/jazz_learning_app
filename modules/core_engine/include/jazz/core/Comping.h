#pragma once

#include "jazz/core/Chart.h"
#include "jazz/core/Rhythm.h"
#include "jazz/core/Voicing.h"
#include "jazz/core/VoicingAnalyzer.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace jazz::core
{

/** One place in a bar a style is willing to put a hit, and how often it does.

    A slot is written the way a player would describe one - "every beat", "the
    and of four" - rather than as an absolute tick, because a style has to
    survive being played in a metre it was not written for. `beat` says which
    beat, counting from zero, or from the end of the bar when it is negative,
    so "the and of the last beat" is one slot that means the same thing in four
    and in three. Left empty it means every beat, which is how four-to-the-bar
    is one slot rather than four.
*/
struct CompSlot
{
    std::optional<int> beat;      ///< empty = every beat; negative counts back from the end
    int tick { 0 };               ///< where within that beat, 0 to ticksPerBeat - 1
    int weight { 50 };            ///< 0-100: how often this style takes the slot

    /** This hit belongs to the *next* bar's chord, played early.

        A comper pushing the chord change across the barline is the single most
        characteristic thing about the idiom, and it is a property of the slot
        rather than of the moment: the and of four anticipates, the downbeat
        does not.
    */
    bool anticipates { false };
};

/** Everything a comping style is, as data.

    One artifact, three readers: the generator samples from it, the evaluator
    scores a player against it, and the menu is built from the same list. A
    style described in one place and re-described in another is how the two
    drift, which is the same reason `scaleStyles()` lives in the engine.

    What a style still cannot say is **how long a hit lasts**. A voicing rings
    until the next one stops it and a shell decides that, so nothing here tells
    a Basie punch from a ballad's sustain - which is a real difference between
    them and the one bullet of this shape with no field. The evaluator is held
    to the same silence: it reads placement, register, density and the notes,
    and marks nobody for holding a chord, because a comper holding every chord
    through a four-to-the-bar is reading a style that does not say not to. That
    is the first thing to add if this shape is reopened.
*/
struct CompStyleDefinition
{
    std::string key;           ///< "four", "basie" - what the wire and the page use
    std::string name;          ///< "Four to the bar"
    std::string summary;       ///< one line, for the menu

    /** The subdivision the style is counted in. Nothing enforces it - it is
        what a shell needs in order to say "swung eighths" and mean it, and
        what tells a reader at a glance whether a style is a triplet feel. */
    Subdivision feel { Subdivision::eighth };

    std::vector<CompSlot> slots;

    /** Hits per bar, counted where they were *played*.

        The generator samples slots by weight and then trims or tops up to land
        inside this, which is what stops a run of unlucky rolls producing a
        silent bar or a wall - and it counts an anticipation in the bar it falls
        in rather than in the bar whose chord it voices. The evaluator has to
        count the same way or it marks the generator's own comp out of its own
        density: a pushed hit is a hit in the bar the hands played it.
    */
    int fewestPerBar { 1 };
    int mostPerBar { 4 };

    /** The register the style's voicings sit in, as MIDI notes. Comping under
        a soloist and comping alone are different registers, and this is where
        that difference is written down. */
    int lowestNote { 45 };
    int highestNote { 84 };
};

/** Every style the engine knows, in menu order. */
std::vector<CompStyleDefinition> compStyles();

/** The style with this key, or the first one when the key is unknown.

    Never empty: a shell asking for a style that has been renamed should get
    comping in some style rather than silence.
*/
const CompStyleDefinition& compStyleFor (const std::string& key);

/** The positions a slot resolves to in a bar of @p beatsPerBar beats.

    Empty when the slot names a beat the metre does not have - a style written
    around the fourth beat simply has less to say in three, which is more
    honest than folding it onto a beat that does exist.
*/
std::vector<BarPosition> slotPositions (const CompSlot& slot, int beatsPerBar);

/** The slot this position belongs to in a bar of @p beatsPerBar, or nullptr.

    The one answer to "which slot is this", so the generator, `fitsStyle` and
    the evaluator cannot disagree about it. The first match wins: two slots
    landing on one position would be one slot written twice.
*/
const CompSlot* slotAt (BarPosition at, const CompStyleDefinition& style, int beatsPerBar);

/** One chord, struck once. */
struct CompHit
{
    int measureIndex {};
    BarPosition at {};

    /** The voicing to play, already led from the hit before it. */
    std::vector<int> midiNotes;

    std::string chordSymbol;   ///< what this hit is voicing
    bool anticipation {};      ///< sounds the next bar's chord, early
};

/** What a comper plays over a range of bars. */
struct CompPlan
{
    std::vector<CompHit> hits;

    bool isEmpty() const noexcept { return hits.empty(); }
};

/** Plans the comping for a range of bars, in a style, reproducibly.

    Planned ahead rather than decided per beat, for two reasons that are really
    one: a hit that anticipates the next bar has to know what the next chord is,
    and a voicing has to be led from the one before it. Both need more than the
    moment they are played in, which a reactive comper does not have.

    @param seed  the same seed gives the same plan, note for note. Bar index is
                 mixed into it, so a loop that comes round again is planned the
                 same way it was the first time rather than drifting.
*/
CompPlan compPlan (const Chart& chart,
                   const CompStyleDefinition& style,
                   int fromBar,
                   int toBar,
                   std::uint32_t seed);

/** What a walking note is doing in the line.

    Not a tier and not a grade - a bass line is not marked. It is here because
    the roles are what the rules are written in terms of, and because a shell
    that wants to draw or explain a line needs to know which note is the root.
*/
enum class BassRole
{
    root,        ///< states the chord, on the beat the chord arrives
    chordTone,   ///< filling, from the chord under it
    scaleTone,   ///< filling, from the scale when the chord has run out of notes
    approach     ///< the last beat before a change, leading into the next root
};

std::string bassRoleName (BassRole role);

/** One note of a walking line. Always on a beat: walking is what the name says. */
struct BassNote
{
    int measureIndex {};
    BarPosition at {};
    int midiNote {};
    std::string chordSymbol;
    BassRole role {};
};

/** A walking bass line over a range of bars, one note to the beat.

    The rules are the ones every bass player is taught, in this order:

      - **The root on the beat the chord arrives.** That is what states the
        harmony, and it is the one note the line is not free about.
      - **An approach into the next root** on the beat before a change: a
        semitone either side, or the fifth above. Chromatic approaches are what
        make a line sound like walking rather than like an arpeggio.
      - **Chord tones in between**, moving towards that approach note rather
        than wandering, and stepping where a step is available.

    Bounded to a bass's own register throughout, because a line free to follow
    the voice leading upwards climbs out of the instrument inside a chorus.

    Seeded the same way `compPlan` is, and for the same reason: a loop that
    came round differently every time would be a different bass player.
*/
std::vector<BassNote> walkingBass (const Chart& chart, int fromBar, int toBar,
                                   std::uint32_t seed);

/** The lowest and highest note a walking line may use. */
constexpr int lowestBassNote = 28;    // E1, the bottom of a double bass
constexpr int highestBassNote = 55;   // G3, where a walking line stops walking

/** Whether a hit is one this style could have played.

    The slot half of the evaluator, and the half the generator is held to: a
    style that generates what it would then mark as out of style is broken in
    one of the two directions, and this is what catches it. The rest of the
    evaluator - register, density and what the notes said - is `readCompHit`
    and `evaluateComp` below, and the invariant now runs through all of it.

    This one stays about a *generated* hit, deliberately. A `CompHit` carries
    two things it already knows: which chord it voices and whether it pushed.
    For a hit somebody played, both of those are answers rather than inputs,
    which is why `PlayedHit` is a type of its own and why this signature did
    not grow to serve them both.
*/
bool fitsStyle (const CompHit& hit, const CompStyleDefinition& style, int beatsPerBar);

//==============================================================================
/** Comping as an exercise: what the player put where, read against the style.

    The third reader of `CompStyleDefinition`, and the one that finally reads
    all of it - `lowestNote`/`highestNote` and `fewestPerBar`/`mostPerBar` were
    written down when the styles were and had no reader at all until this.

    **Where a note falls does not score a solo, and here it scores a comp.**
    That is not a change of mind; it is a different question with a different
    standard behind it. A solo line has no stated standard for placement - the
    chart says which chord and never says where a note belongs in the bar, so a
    number for placement would be the engine inventing a standard and then
    marking a player against it, which is why `LineAnalyzer`'s score reads
    nothing rhythmic and why a line's shape produces words.

    A comp has one, and the player picked it off a menu. A style is a written
    statement of where the hits go, how many of them there are and what
    register they sit in, and the generator is already held to it in both
    directions. Scoring a player against the same slots the band is held to is
    the same act as `VoicingAnalyzer` scoring a voicing against the symbol the
    chart wrote, or `LineAnalyzer::Options::chosenScale` holding a player to the
    scale they chose out of the panel. Take the style away and there is nothing
    here to score, which is why a verdict always names one.

    And the line holds on the other side: everything a style does *not* pin
    down still produces words. How long a chord rang, whether the comp left
    room for the line, whether it varied, whether the root was doubled - none of
    them is in the definition, so none of them is in the number.
*/

/** One chord the player struck, as the shell saw it and no more.

    Deliberately not a `CompHit`. That is what the band plays, and it carries
    two things a page cannot honestly know: which chord the hit voices, and
    whether it was a push. Here both are the evaluator's answers - a player does
    not declare an anticipation, they play the next chord early, and the notes
    are the only evidence there is.
*/
struct PlayedHit
{
    int measureIndex {};

    /** Where in the bar it landed, when the shell had a clock to say.

        Empty is a real answer rather than a placeholder - the same thing
        `soloPlayNote` signals with a negative beat. There is no position that
        means "no position", and a made-up downbeat would be read as a real one.
        Without one the register and the voicing still read; the placement does
        not, and the fit is not scored at all.
    */
    std::optional<BarPosition> at;

    std::vector<int> midiNotes;   ///< as played, any order; sorted on the way in
};

/** Where a hit sits against the style. Settled the moment it is played. */
enum class HitPlacement
{
    inStyle,    ///< the style offers this position
    pushed,     ///< an anticipating slot, and the notes say the next bar's chord
    offStyle,   ///< no slot here
    unplaced    ///< no clock behind it, so there is no placement to read
};

std::string hitPlacementName (HitPlacement placement);

/** One played hit, read against the chart and the style. */
struct CompHitReading
{
    int measureIndex {};
    std::optional<BarPosition> at;

    HitPlacement placement { HitPlacement::unplaced };
    std::string chordSymbol;      ///< the chord it was judged against, as written
    bool anticipation {};         ///< it read as the next bar's chord, early

    bool inRegister { true };
    int outsideRegisterBy {};     ///< semitones past the nearer edge, 0 when inside

    /** The root, played under a bass player already playing it.

        Not a shape rule and not `VoicingAnalyzer`'s business: a rootless left
        hand and a shell voicing are both perfectly good comping, so the fault
        is not "wrong shape", it is *that is the bass player's note*.
        `takesTheBassNote` is the root at the bottom of the voicing, which is
        where it actually collides; `rootAnywhere` is the softer case, worth a
        word and not a fault. Neither costs the fit anything - the style says
        nothing about either, so neither is in the number.
    */
    bool takesTheBassNote {};
    bool rootAnywhere {};

    /** The analyser's own answer about the notes, unchanged and unweighted. */
    VoicingAnalysis voicing;

    /** One line, for a dock that has to say something this instant. */
    std::string summary;

    /** One more than the style ever plays in a bar.

        The busy direction only. A bar left too empty cannot be known until the
        bar is over, which is why the other direction lives on the take - the
        same shape as solo practice's bad news arriving at the earliest moment
        it is honestly available.
    */
    bool oneTooMany {};
};

/** A hit the shell placed just outside its bar, put where it belongs.

    A page quantises a moment onto the grid, so a chord struck a hair before the
    downbeat comes over as beat 4 of a bar of four, and `BarPosition::fromTicks`
    floors, so one read back from a tick count comes over as beat -1. Both are
    the neighbouring bar, and without this a comper pushing the and of four a
    few milliseconds late is marked off style at a position no slot has ever
    offered. Still position arithmetic: the engine learns nothing about when.
*/
PlayedHit inItsOwnBar (PlayedHit hit, int beatsPerBar);

/** Reads one hit, with no memory of any other.

    @param hitsAlreadyInBar  how many chords the player has already put in this
                             bar. The only thing here that is not a fact about
                             this hit alone, and it is a count rather than a
                             judgement: the style decides what the count means.
*/
CompHitReading readCompHit (const Chart& chart,
                            const CompStyleDefinition& style,
                            const PlayedHit& hit,
                            int hitsAlreadyInBar = 0);

/** How one bar's density read. */
struct CompBarReading
{
    int measureIndex {};
    int hits {};
    /** What the style asks for *in this metre*, copied so a shell can say it.

        Not always `fewestPerBar`/`mostPerBar`: a count does not survive a
        change of metre the way a slot does, so the window is clamped to the
        number of places the style has to put a chord in this bar. Four to the
        bar is four chords in four and three in three.
    */
    int fewest {};
    int most {};
    bool tooBusy {};
    bool tooSparse {};
};

/** A stretch of comping, read back against the style it was played in. */
struct CompEvaluation
{
    /** 0-100: placement, register and density.

        Empty when nothing was played, and empty when nothing carried a
        position - not zero. A nought out of a hundred for a player who has not
        played is a mark rather than a reading, which is the distinction
        `LineStats` draws between nothing and zero.
    */
    std::optional<int> fit;

    int placementFit {};   ///< the three parts, so a number can be explained
    int registerFit {};
    int densityFit {};

    /** The analyser's mean over the hits, kept apart from `fit` on purpose.

        Two different standards: the fit is read against the style the player
        chose, the voicing against the symbol the chart wrote. One number out of
        two questions stops being explainable the first time somebody asks which
        half of it moved.
    */
    std::optional<int> voicingScore;

    std::vector<CompHitReading> hits;   ///< in the order they were played
    std::vector<CompBarReading> bars;   ///< every bar covered, silent ones included

    int hitsInStyle {};
    int hitsPushed {};
    int hitsOffStyle {};
    int hitsTakingTheBassNote {};

    std::string summary;

    /** The words half: everything true of the take that is not in the number. */
    std::vector<std::string> observations;
};

/** Reads a stretch of comping against a style.

    Takes what `compPlan` takes and consumes what it produces - the same chart,
    the same style, the same range of bars. That symmetry is the invariant:
    everything the generator plays for a style has to come back out of here as a
    comp in that style, which is the same trap `idiomaticVoicings` and
    `VoicingAnalyzer` are held out of.

    @param fromBar,toBar  the bars the take covered, silent ones included. A bar
                          with no hits leaves no trace in @p hits, and a bar
                          left empty is exactly what a dense style's density
                          reading is about, so the range has to be said rather
                          than inferred. A bar the take stopped part-way through
                          belongs outside it: it was not played to the end, and
                          counting it marks every four-to-the-bar take sparse in
                          its last bar.
*/
CompEvaluation evaluateComp (const Chart& chart,
                             const CompStyleDefinition& style,
                             const std::vector<PlayedHit>& hits,
                             int fromBar, int toBar);

} // namespace jazz::core
