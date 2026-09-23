#pragma once

#include <string>

namespace jazz::api
{

/** The engine's answers, as JSON - the one wire format both shells read.

    Every call takes plain strings and returns a JSON object as a std::string.
    On failure the object is {"ok":false,"error":"..."} rather than an
    exception or an empty string, so a caller in either shell can report what
    went wrong without knowing any C++ types.

    Nothing here decides any theory; it asks the Core Engine and writes down
    what it said. A shell adds a transport for these - Emscripten exports for
    the browser, JUCE native functions for the app - never a rule.
*/
std::string parseChart (const char* progressionText);
std::string scalesForChord (const char* symbol, const char* style);
std::string reharmonise (const char* progressionText, int measureIndex, int includeAdvanced, int includeRisky,
                         const char* styleKey);

/** Every reharmonisation vocabulary the engine offers, in menu order.

    The shells build their picker from this rather than holding a list of their
    own - which substitutions belong together is theory, and a second copy of it
    in a page is a second copy that goes stale.

    The first row is `all`, which filters nothing. It is in the engine's list
    rather than prepended by a shell, so there is one menu and one owner of it.
*/
std::string reharmStyles();

/** A played voicing read as a shape, and the quality of the chord it sits on.

    What a personal voicing library saves. The answer carries `qualities` -
    every key the engine uses - alongside the one this chord has, so a shell
    validating something out of its own store does not hold a list of eight.
    `compStyles()` sends the feels the same way and for the same reason.

    With no notes it answers the chord alone, which is what a page asks when it
    wants to know which saved shapes belong on the bar it just moved to.
*/
std::string voicingShape (const char* symbol, const char* midiNotesCsv);

/** A saved shape put back on a chord, in the register it was saved in. */
std::string voicingFromShape (const char* symbol, const char* offsetsCsv, int anchorNote);
std::string analyseVoicing (const char* symbol, const char* midiNotesCsv, const char* practiseStyle);
std::string importIRealPro (const char* text);

/** Writes the chart back out as an iReal Pro link.

    @param beats,beatUnit  the metre, because a progression text has none and
                 the chart this rebuilds would otherwise open in four. The
                 readers have always brought a time signature in; until this
                 was on the wire a waltz went out in four, which is the one way
                 a chart that left the engine and came back was not the chart
                 that left. Zero or less means "whatever the text implies",
                 which is what a shell that does not know its metre should
                 send and what an older one effectively sent.
*/
std::string exportIRealPro (const char* progressionText, const char* title, const char* composer,
                            const char* style, int beats, int beatUnit);
std::string chartFromPage (const char* tabSeparatedItems);
std::string reharmPlans (const char* progressionText);

/** Where @p symbol's guide tones are for a hand already holding @p midiNotesCsv.

    The 3rd and the 7th are what carry a progression - a chart is two lines
    moving a semitone at a time with the roots underneath - and that is the one
    thing a chord chart cannot show you. What a player needs to know about them
    is not where they are in the abstract but where they are *for the hand they
    have got*, which is why the notes go over with the chord: the answer for a
    rootless left hand at the bottom of the keyboard is an octave away from the
    answer for two hands in the middle of it, and both are right.

    Empty tones for an empty hand. See `core::voiceGuideTones`.
*/
std::string voicedGuideTones (const char* symbol, const char* midiNotesCsv);
std::string identifyChord (const char* midiNotesCsv);
std::string recogniseSubstitution (const char* progressionText, int measureIndex, const char* midiNotesCsv, int includeAdvanced);
std::string idiomaticVoicings (const char* symbol, int anchorNote, const char* practiseStyle, int rich);

/** The two-handed voicing a comping piano plays for @p symbol, having just
    played @p previousNotesCsv (empty for the first chord of a tune). */
std::string compingVoicing (const char* symbol, const char* previousNotesCsv);

/** Every comping style the engine knows, for a shell to build a menu from.

    Each one carries its whole self - the figure included - so a shell can
    draw a style and hand an edited one back. That is not a second opinion
    about placement: `compHit` and `compTake` still answer whether a hit is in
    style, and still nothing else does.

    Two fields are for a shell that stores a style rather than shows it.
    `ticksPerBeat` is the grid its ticks are counted on, so a stored style can
    tell that the grid itself moved rather than trusting a version number
    somebody has to remember to raise. `reference` is the same style as one
    line of text, ready to pass straight back as the @c styleRef below.
*/
std::string compStyles();

/** How the three calls below name a comping style.

    Either a **key** out of `compStyles()` - "charleston" - or a **description**
    of a style the engine has never seen, which is the string `compStyles()`
    hands back as `reference`:

    @verbatim
    custom:<feel>|<fewest>|<most>|<lowest>|<highest>|<variation>|<heldFor>|<slots>
        <slots> := <slot> (";" <slot>)*
        <slot>  := <beat>:<tick>:<weight>:<anticipates>:<heldFor>
    @endverbatim

    Flat text, not JSON, because that is the direction this wire runs: results
    are JSON because encoding them is the shell's business, inputs are
    delimited text because the engine has no JSON reader and must not grow one.

    `<feel>` is the word `compStyles()` writes ("eighths", "eighth-note
    triplets"). `<anticipates>` is 0 or 1, the way every other boolean on this
    wire crosses. An **empty** `<beat>` means *every* beat, which is how four
    to the bar is one slot rather than four; a negative one counts back from
    the end of the bar, which is what keeps "the and of the last beat" the
    same idea in three as in four. A `<heldFor>` of 0 on a slot means "ask the
    style". No key, name or summary: the engine needs none of the three to
    plan or to grade, so the grammar has no free text in it and needs no
    quoting. What a player calls their own style is the shell's business.

    **The two halves fail differently, on purpose.** An unknown *key* falls
    back to the first style, which is a promise `compStyleFor` makes: a shell
    asking for a style that has since been renamed should get comping in some
    style rather than silence. A malformed *description* is an error, because
    that is a broken message rather than a renamed style - falling back would
    comp four to the bar underneath someone who had just written their own
    figure, which is working-looking, wrong, and impossible to notice.
*/

/** What a comper plays over a range of bars, in a style, reproducibly.

    Positions come back as beat and tick, never as times: the shell owns the
    clock and turns one into the other.
*/
std::string compPlan (const char* progressionText, const char* styleRef,
                      int fromBar, int toBar, int seed);

/** A walking bass line over a range of bars, one note to the beat. Positions
    come back the same way a comp plan's do: a beat and a tick, never a time. */
std::string walkingBass (const char* progressionText, int fromBar, int toBar, int seed);

/** A line to play back, over the bars asked for.

    The counterpart to `walkingBass` one register up, and the thing solo
    practice could never do: read a line, yes - write one, no.

    `chosenScale` and `scaleStyle` are the same two a take is set up with
    (`soloSetBar`), and passing what the take will read against is what keeps
    the line's own colours and the reading's the same. A note comes back with
    the colour `LineAnalyzer` will give it, which is what lets a shell draw a
    written line in the same ink as a played one.
*/
std::string improvisedLine (const char* progressionText, int fromBar, int toBar,
                            const char* chosenScale, const char* scaleStyle, int seed);

/** One chord the player comped, read against the style they chose.

    Stateless, and on purpose. A comping hit has no window over it the way a
    solo note does - its slot, its register and what its notes said are all
    settled the instant it is struck, and nothing played afterwards revises any
    of them. So there is nothing here for a memory to hold, and the solo take
    stays the one stateful corner of this API.

    What that leaves the shell is a record of what was *played* - a bar, a
    position and some notes - which is the same class of thing as a list of held
    notes. Every verdict still comes from here.

    @param beat  negative when the shell has no clock and cannot say where the
                 hit fell, exactly as `soloPlayNote` means it. The notes and the
                 register still read; the placing does not.
    @param hitsAlreadyInBar  how many chords are already in this bar. A count,
                 not a judgement - the style decides what it means.
*/
std::string compHit (const char* progressionText, const char* styleRef,
                     int measureIndex, int beat, int tick,
                     const char* midiNotesCsv, int hitsAlreadyInBar);

/** A stretch of comping, read back against the style it was played in.

    @param hitsText  the hits, as "bar:beat:tick:note,note,note", separated by
                     ';' - one string because that is what this wire carries. A
                     beat of -1 is a hit with no clock behind it.
    @param fromBar,toBar  the bars the take covered, silent ones included: a bar
                     with no hits leaves no trace in @p hitsText, and a bar left
                     empty is exactly what a dense style's density reading is
                     about. A bar the take stopped part-way through belongs
                     outside this range.
*/
std::string compTake (const char* progressionText, const char* styleRef,
                      int fromBar, int toBar, const char* hitsText);

//==============================================================================
/** Solo practice: reading a line rather than a chord.

    These four are the one stateful corner of this API, and deliberately so. A
    take is a stream with a beginning and an end, and the alternative - having
    the shell send every note played so far on each new note - puts the take in
    the UI, which is where theory is not allowed to live. So the engine holds
    it: one `LineAnalyzer` for the one player this process has.

    Between them they are the whole of the mode:

      soloStartTake   arm. Anything from a previous take is dropped.
      soloSetBar      the bar being soloed over, and the scale to read against.
                      Called on arming and again on every move. Moving during a
                      take does not end it.
      soloPlayNote    one note, and whether it was struck with the one before
                      it - which is how a chord reaches the engine, one call
                      per note and no waiting for the rest of it. Read back
                      whether a take is running or not; counted only when one
                      is.
      soloEndTake     disarm, and hand back the take to read.

    A bar's own numbers come back from `soloSetBar`, because clicking a bar is
    how you ask for them.
*/
/** Every soloing vocabulary the engine offers, in menu order.

    The shells build their picker from this rather than holding a list of their
    own: which scales belong together is theory, and a second copy of it in a
    page is a second copy that goes stale.
*/
/** The grooves a tune can be played with, and the one this chart asks for.

    `forThisChart` is the key `grooveForStyleWord` reads out of the chart's
    style marking - "Medium Swing" gives swing, anything unrecognised gives
    even. A shell re-asks when the chart changes; the catalogue itself does not.

    A groove's `upbeatWhenSlow`/`upbeatWhenFast` are fractions of a beat, and
    the only non-integers on this wire. They say where a shell should *play* an
    upbeat eighth, never where the engine wrote one - the engine writes tick 12
    and always will.
*/
std::string grooves (const char* chartStyle);

std::string scaleStyles();

std::string soloStartTake();
std::string soloSetBar (int measureIndex, const char* symbol, const char* chosenScale,
                        const char* style, int beatsPerBar = 4);
/** @param beat  negative when the shell has no clock and cannot say where
                  the note fell - which is not the same as the downbeat. */
std::string soloPlayNote (int midiNote, int beat = -1, int tick = 0, int withPrevious = 0);
std::string soloEndTake();

/*  A practice record, read back.

    The engine gains no memory here and no clock. A history is data the shell
    hands over on every call - the way a chart's progression text is, and the way
    a comping style described rather than named is - and `today` is a number the
    shell computed from its own clock, like a `BarPosition`. Nothing is stored
    between calls, so both of these stay as testable as everything else here.

    The history crosses as **flat delimited text**, which is the direction this
    wire has always run: results are JSON because encoding them is the shell's
    business, inputs are text because the engine has no JSON reader and must not
    grow one. Five delimiters, none of which can occur inside a number, so the
    grammar needs no escaping and carries no free text at all:

    @verbatim
      <history> := <take> { "~" <take> }
      <take>    := <head> "|" [ <bars> ]
      <head>    := day : mode : tune : seconds : qualities : roots
                     : chordTones : scaleTones : approachTones : unresolved : outside
                     : leaps : leapsResolved : chordsPlayed
                     : onFigure : idiomatic : pushed : offStyle
      <bars>    := <bar> { ";" <bar> }
      <bar>     := index , chordTones , scaleTones , approachTones , unresolved , outside
    @endverbatim

    `mode` is 0 for soloing and 1 for comping. `tune` is a **number**, which is
    what keeps this free of quoting: what a player calls a tune is the page's
    business, exactly as what they call their own comping style is. `qualities`
    and `roots` are the bitmasks `jazz::core::qualityBit` and `rootBit` build,
    and a shell gets them from a take rather than working them out - which
    quality a symbol is is theory, and theory is the engine's.

    An empty history is a record with nothing in it, which is what a first visit
    hands over and is not an error. Anything else that does not parse **is** one:
    a shell's bug must not read back as a player who has not practised.

    Neither answer carries a score, and that is the point rather than an
    oversight. `LineStats::score()` calls itself a reading of a bar and not a
    grade for a player; the same number summed over weeks and drawn as a line is
    exactly the grade it refuses to be. The counts cross, the mark does not.
*/
std::string practiceReading (const char* history, int today);

/** One tune, read across every take over it.

    @param history  this tune's takes only. Filtering is the shell's job, since
                    a shell already knows which tune is which and an engine
                    given the whole record plus a tune number would be an engine
                    that had to be told what a tune is.
*/
std::string tuneProgress (const char* progressionText, const char* history, int today);

} // namespace jazz::api
