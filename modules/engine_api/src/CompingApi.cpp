// The band: what it plays, and how what you played reads against it, as JSON.
//
// One of the four files behind `jazz/api/EngineApi.h`. Both shells ask the
// engine the same questions and both want the answer as text: the browser
// because JavaScript cannot see C++ types, and the JUCE app because its UI is
// that same page in a webview. Encoding lived in the WebAssembly shell until
// the app needed it too - and two copies of a wire format is how the two shells
// start disagreeing about what a chord looks like.
//
// This holds no theory. It asks the Core Engine and writes down what it said.
// `Wire.h` holds what more than one of these four files needs.

#include "jazz/api/EngineApi.h"

#include "Wire.h"

#include "jazz/core/Chart.h"
#include "jazz/core/Comping.h"
// `coverageOf`: a comped take writes the same two harmony masks a solo take
// does, so the practice record's own header is where that lives.
#include "jazz/core/PracticeLog.h"
#include "jazz/core/Rhythm.h"
#include "jazz/core/Voicing.h"

#include <optional>
#include <string>
#include <vector>

using namespace jazz::core;
namespace core = jazz::core;
using namespace jazz::api::wire;

namespace jazz::api
{

std::string compingVoicing (const char* symbol, const char* previousNotesCsv)
{
    const auto chord = ChordSymbol::parse (symbol != nullptr ? symbol : "");

    if (! chord.has_value())
        return hold (jsonError ("Not a chord symbol"));

    // The page hands back what it last played rather than the engine keeping a
    // memory of it: the take is the one stateful corner here, and comping does
    // not need to be a second one.
    const auto previous = parseNoteList (previousNotesCsv != nullptr ? previousNotesCsv : "");
    const auto voicing = core::compingVoicing (*chord, previous);

    std::string notes = "[";

    for (std::size_t i = 0; i < voicing.midiNotes.size(); ++i)
        notes += (i > 0 ? "," : "") + std::to_string (voicing.midiNotes[i]);

    notes += "]";

    return hold ("{\"ok\":true,\"notes\":" + notes
                 + ",\"describe\":" + quoted (voicing.describe()) + "}");
}

namespace
{
    /*  Reading a style in, and writing one out. Internal: the flat grammar
        is this file's business, and `splitOn`/`numberIn` are names general
        enough that they should not be anyone else's.  */
/** The prefix that marks a style described rather than named.

    A catalogue key is a bare word, so the two can never be confused - but the
    marker is a stated one rather than a sniff at the text's shape, because
    this wire's convention for "this argument can mean a second thing" is a
    documented sentinel and not an inference. `soloPlayNote`'s beat of -1 and
    `exportIRealPro`'s `beats <= 0` are the same move.
*/
const std::string describedStylePrefix = "custom:";

/** More slots than any figure a bar could want, so a broken message cannot
    ask for a million of them. Eight beats of sixteenths is 32; this is twice
    that and still nowhere near a number that costs anything. */
constexpr int mostSlotsInAFigure = 64;

/** The longest a chord may ring, in ticks: sixteen beats, or four bars of
    four. A style that holds longer is not a style, it is a drone. */
constexpr int longestRing = ticksPerBeat * 16;

/** Splits on one character, keeping empty fields - an empty field is a value
    in this grammar, not a gap. */
std::vector<std::string> splitOn (const std::string& text, char separator)
{
    std::vector<std::string> fields { "" };

    for (auto c : text)
    {
        if (c == separator)
            fields.push_back ("");
        else
            fields.back() += c;
    }

    return fields;
}

/** A whole number inside a stated range, or nothing at all.

    Refuses rather than clamps, and refuses trailing rubbish as well as an
    out-of-range value. A tick of 900 is not a tick this reader should round
    down to 23 - it is a message that did not mean what it says, and comping
    something plausible out of it is how a shell's bug becomes a mystery about
    the band. Clamping belongs where a value is *computed*; this is where one
    is *received*.
*/
std::optional<int> numberIn (const std::string& text, int low, int high)
{
    if (text.empty())
        return {};

    try
    {
        std::size_t read = 0;
        const auto value = std::stoi (text, &read);

        if (read != text.size() || value < low || value > high)
            return {};

        return value;
    }
    catch (...)
    {
        return {};
    }
}

/** A style written out by `styleReference`, read back.

    The exact inverse, and the pair is tested by round-tripping the catalogue
    rather than by hand-written fixtures - so a field added to one side and
    not the other fails immediately instead of silently travelling as a
    default.

    False on anything malformed, and false all the way rather than partly: a
    caller gets a style or an error, never a style with one field quietly
    filled in from a default it did not ask for.
*/
bool readStyle (const std::string& text, CompStyleDefinition& into)
{
    const auto fields = splitOn (text, '|');

    if (fields.size() != 8)
        return false;

    const auto feel = subdivisionFrom (fields[0]);

    if (! feel.has_value())
        return false;

    const auto fewest    = numberIn (fields[1], 0, mostSlotsInAFigure);
    const auto most      = numberIn (fields[2], 0, mostSlotsInAFigure);
    const auto lowest    = numberIn (fields[3], 0, 127);
    const auto highest   = numberIn (fields[4], 0, 127);
    const auto variation = numberIn (fields[5], 0, 100);
    const auto heldFor   = numberIn (fields[6], 1, longestRing);

    if (! fewest || ! most || ! lowest || ! highest || ! variation || ! heldFor)
        return false;

    // A register with its ends the wrong way round is not a narrow register,
    // it is two numbers that were not meant to be these two numbers.
    if (*lowest > *highest)
        return false;

    CompStyleDefinition built;
    built.feel = *feel;
    built.fewestPerBar = *fewest;
    built.mostPerBar = *most;
    built.lowestNote = *lowest;
    built.highestNote = *highest;
    built.variation = *variation;
    built.heldFor = *heldFor;

    for (const auto& one : splitOn (fields[7], ';'))
    {
        const auto parts = splitOn (one, ':');

        if (parts.size() != 5)
            return false;

        CompSlot slot;

        /*  An empty beat is every beat, which is a value rather than a missing
            one - it is how four-to-the-bar is a single slot. Every number is
            already taken by something real, negatives included, so "no number
            at all" is the only spelling left for it. */
        if (! parts[0].empty())
        {
            const auto beat = numberIn (parts[0], -mostSlotsInAFigure, mostSlotsInAFigure);

            if (! beat)
                return false;

            slot.beat = *beat;
        }

        const auto tick   = numberIn (parts[1], 0, ticksPerBeat - 1);
        const auto weight = numberIn (parts[2], 0, 100);
        const auto pushes = numberIn (parts[3], 0, 1);
        const auto rings  = numberIn (parts[4], 0, longestRing);

        if (! tick || ! weight || ! pushes || ! rings)
            return false;

        slot.tick = *tick;
        slot.weight = *weight;
        slot.anticipates = *pushes == 1;

        // Zero is "ask the style", which is what the optional's empty case
        // means. A slot that rings for no ticks is not a thing a style says.
        if (*rings > 0)
            slot.heldFor = *rings;

        built.slots.push_back (slot);

        if (static_cast<int> (built.slots.size()) > mostSlotsInAFigure)
            return false;
    }

    // A figure with nothing in it is not a figure. The catalogue's sparsest
    // style still has three slots, and its emptiest bar comes from the density
    // window rather than from having nothing to play.
    if (built.slots.empty())
        return false;

    into = std::move (built);

    return true;
}

/** A style reference: the name of one the engine ships, or a description of
    one it has never seen.

    The two halves fail differently, and deliberately.

    An unknown **key** still falls back to the first style, because that is a
    promise `compStyleFor` makes in its own doc comment - a shell asking for a
    style that has since been renamed should get comping in some style rather
    than silence.

    A malformed **description** is an error. It is not a renamed style, it is
    a broken message, and falling back would comp four-to-the-bar underneath
    someone who had just written their own figure - working-looking, wrong,
    and impossible to notice. `readHitList` already states the principle: a
    caller should be able to refuse the lot rather than silently grade
    something shorter than what was played.

    By value, because a described style has no storage to hand out a reference
    to. The copy is a few ints and a small vector, on calls that have just
    parsed a whole chart.
*/
bool styleFrom (const std::string& reference, CompStyleDefinition& into)
{
    if (reference.rfind (describedStylePrefix, 0) != 0)
    {
        into = compStyleFor (reference);
        return true;
    }

    return readStyle (reference.substr (describedStylePrefix.size()), into);
}

/** A style written as one line of flat text, ready to hand straight back.

    The inverse of the reader that `compPlan` and its two neighbours use, and
    the reason this crosses at all: a test that wanted to prove the two agree
    would otherwise have to hand-write four descriptions, which is a second
    copy of the catalogue living in a fixture. Emitting it means the round
    trip can be asserted against the engine's own answer.

    Flat text rather than JSON because that is the direction this wire runs.
    Results are JSON, because encoding them is the shell's business and every
    shell has a parser; inputs are delimited text, because the engine has no
    JSON reader and this module says at the top that it should not grow
    one. `parseNoteList`, `readHit` and `readHitList` are the precedent.

    Three delimiters, no escaping needed: `|` between the header fields, `;`
    between slots, `:` inside one. Nothing here is free text - `key`, `name`
    and `summary` are deliberately **not** carried, because the engine needs
    none of the three to plan or to grade a bar. It only ever echoed them
    back. What a player calls their own style is the page's business, and
    leaving the strings out is what keeps this grammar free of quoting.
*/
std::string styleReference (const CompStyleDefinition& style)
{
    auto text = "custom:" + subdivisionName (style.feel)
              + "|" + std::to_string (style.fewestPerBar)
              + "|" + std::to_string (style.mostPerBar)
              + "|" + std::to_string (style.lowestNote)
              + "|" + std::to_string (style.highestNote)
              + "|" + std::to_string (style.variation)
              + "|" + std::to_string (style.heldFor)
              + "|";

    for (std::size_t i = 0; i < style.slots.size(); ++i)
    {
        const auto& slot = style.slots[i];

        if (i > 0)
            text += ";";

        /*  An empty beat is *every* beat, which is how four-to-the-bar is one
            slot rather than four and how "the and of the last beat" stays
            metre-independent. It is written as nothing at all rather than as a
            number, because every number is taken: a negative beat counts back
            from the end of the bar and is a thing three of the four shipped
            styles actually use. Same for `heldFor`, where 0 means "ask the
            style" - a slot that rings for no ticks is not a thing a style can
            mean. */
        text += (slot.beat.has_value() ? std::to_string (*slot.beat) : "")
              + ":" + std::to_string (slot.tick)
              + ":" + std::to_string (slot.weight)
              + ":" + (slot.anticipates ? "1" : "0")
              + ":" + std::to_string (slot.heldFor.value_or (0));
    }

    return text;
}

std::string styleJson (const CompStyleDefinition& style)
{
    /*  The whole definition, every field of it.

        This used to send nine of the eleven, leaving out `slots` and
        `heldFor` - which was enough for a menu that only ever picked one of
        four and not enough for anything that wants to *start from* a style.
        A figure you cannot read is a figure you cannot copy, so an editor
        could not have offered "like the Charleston, but".

        `feel` crosses as the word rather than the number, which is what
        `subdivisionFrom` exists to read back: a wire that carried the
        enumerator would break the day one is inserted in the middle.
    */
    return "{\"key\":" + quoted (style.key)
         + ",\"name\":" + quoted (style.name)
         + ",\"summary\":" + quoted (style.summary)
         + ",\"feel\":" + quoted (subdivisionName (style.feel))
         + ",\"fewestPerBar\":" + std::to_string (style.fewestPerBar)
         + ",\"mostPerBar\":" + std::to_string (style.mostPerBar)
         + ",\"variation\":" + std::to_string (style.variation)
         + ",\"lowestNote\":" + std::to_string (style.lowestNote)
         + ",\"highestNote\":" + std::to_string (style.highestNote)
         + ",\"heldFor\":" + std::to_string (style.heldFor)
         /*  The grid this style's ticks are counted on. A shell storing a
             style has to be able to tell that the grid itself changed, and
             deriving that from the engine's own answer beats stamping a
             version number that someone has to remember to raise. */
         + ",\"ticksPerBeat\":" + std::to_string (ticksPerBeat)
         /*  The same style as one line of text, ready to hand back. */
         + ",\"reference\":" + quoted (styleReference (style))
         + ",\"slots\":" + jsonArray (style.slots, [] (const CompSlot& slot)
           {
               /*  `beat` is optional and its empty case means *every* beat, so
                   it crosses as a string: "" for every beat, a number for one.
                   Not -1, which this field already uses for something real -
                   a negative beat counts back from the end of the bar, which
                   is how "the and of the last beat" stays metre-independent.

                   `heldFor` is optional too, and its empty case means "ask the
                   style". Zero says that, since a slot that rings for no ticks
                   is not a thing a style can mean. */
               return "{\"beat\":" + quoted (slot.beat.has_value()
                                             ? std::to_string (*slot.beat) : "")
                    + ",\"tick\":" + std::to_string (slot.tick)
                    + ",\"weight\":" + std::to_string (slot.weight)
                    + ",\"anticipates\":" + (slot.anticipates ? "true" : "false")
                    + ",\"heldFor\":" + std::to_string (slot.heldFor.value_or (0)) + "}";
           })
         + "}";
}
}

std::string compStyles()
{
    /*  The feels a style may be counted in, so a shell offering the choice
        does not hold its own list of four. Which subdivisions exist is the
        grid's business - `docs/RHYTHM.md` - and the grid is the engine's. */
    const auto feels = jsonArray (allSubdivisions(), [] (Subdivision subdivision)
                                  { return quoted (subdivisionName (subdivision)); });

    return hold ("{\"ok\":true,\"feels\":" + feels + ",\"styles\":"
                 + jsonArray (core::compStyles(), [] (const CompStyleDefinition& style)
                   { return styleJson (style); })
                 + "}");
}

std::string compPlan (const char* progressionText, const char* styleRef,
                      int fromBar, int toBar, int seed)
{
    const auto parsed = parseProgressionText (progressionText != nullptr ? progressionText : "");

    if (! parsed.ok())
        return hold (jsonError (parsed.error));

    CompStyleDefinition style;

    if (! styleFrom (styleRef != nullptr ? styleRef : "", style))
        return hold (jsonError ("That comping style could not be read."));
    const auto plan = core::compPlan (*parsed.chart, style, fromBar, toBar,
                                      static_cast<std::uint32_t> (seed));

    /*  Positions go over as beat and tick rather than as a time. The shell owns
        the clock and is the only thing that can turn one into the other, which
        is the same division the transport already works to - and the reason
        this plan can be handed to either shell unchanged. */
    return hold ("{\"ok\":true,\"style\":" + quoted (style.key)
                 + ",\"ticksPerBeat\":" + std::to_string (ticksPerBeat)
                 + ",\"hits\":"
                 + jsonArray (plan.hits, [] (const CompHit& hit)
                   {
                       std::string notes = "[";

                       for (std::size_t i = 0; i < hit.midiNotes.size(); ++i)
                           notes += (i > 0 ? "," : "") + std::to_string (hit.midiNotes[i]);

                       notes += "]";

                       return "{\"bar\":" + std::to_string (hit.measureIndex)
                            + ",\"beat\":" + std::to_string (hit.at.beat)
                            + ",\"tick\":" + std::to_string (hit.at.tick)
                            + ",\"at\":" + quoted (hit.at.describe())
                            + ",\"chord\":" + quoted (hit.chordSymbol)
                            + ",\"anticipation\":" + (hit.anticipation ? "true" : "false")
                            + ",\"heldFor\":" + std::to_string (hit.heldFor)
                            + ",\"notes\":" + notes + "}";
                   })
                 + "}");
}

std::string walkingBass (const char* progressionText, int fromBar, int toBar, int seed)
{
    const auto parsed = parseProgressionText (progressionText != nullptr ? progressionText : "");

    if (! parsed.ok())
        return hold (jsonError (parsed.error));

    const auto line = core::walkingBass (*parsed.chart, fromBar, toBar,
                                         static_cast<std::uint32_t> (seed));

    return hold ("{\"ok\":true,\"ticksPerBeat\":" + std::to_string (ticksPerBeat)
                 + ",\"notes\":"
                 + jsonArray (line, [] (const BassNote& note)
                   {
                       return "{\"bar\":" + std::to_string (note.measureIndex)
                            + ",\"beat\":" + std::to_string (note.at.beat)
                            + ",\"tick\":" + std::to_string (note.at.tick)
                            + ",\"midi\":" + std::to_string (note.midiNote)
                            + ",\"name\":" + quoted (midiNoteName (note.midiNote))
                            + ",\"chord\":" + quoted (note.chordSymbol)
                            + ",\"role\":" + quoted (bassRoleName (note.role)) + "}";
                   })
                 + "}");
}

//==============================================================================
namespace
{
    /** One hit, as "bar:beat:tick:note,note,note".

        Flat text because that is what this wire carries. A beat of -1 is the
        shell saying it had no clock - the same signal `soloPlayNote` uses, and
        for the same reason: there is no position that means "no position".
    */
    bool readHit (const std::string& text, PlayedHit& into)
    {
        std::vector<std::string> fields { "" };

        for (auto c : text)
        {
            if (c == ':')
                fields.push_back ("");
            else
                fields.back() += c;
        }

        if (fields.size() < 4)
            return false;

        try
        {
            into.measureIndex = std::stoi (fields[0]);

            const auto beat = std::stoi (fields[1]);
            const auto tick = std::stoi (fields[2]);

            into.at = beat >= 0 ? std::optional<BarPosition> (BarPosition { beat, tick })
                                : std::nullopt;
        }
        catch (...)
        {
            return false;
        }

        into.midiNotes = parseNoteList (fields[3]);

        return true;
    }

    /** Hits separated by ';'. Empty on anything malformed, so a caller can
        refuse the lot rather than silently grading a shorter take. */
    bool readHitList (const std::string& text, std::vector<PlayedHit>& into)
    {
        std::string current;

        const auto flush = [&current, &into]
        {
            if (current.empty())
                return true;

            PlayedHit hit;
            const auto ok = readHit (current, hit);

            current.clear();

            if (ok)
                into.push_back (hit);

            return ok;
        };

        for (auto c : text)
        {
            if (c == ';')
            {
                if (! flush())
                    return false;
            }
            else
            {
                current += c;
            }
        }

        return flush();
    }

    std::string hitReadingJson (const CompHitReading& reading)
    {
        std::string json = "{\"bar\":" + std::to_string (reading.measureIndex);

        if (reading.at.has_value())
            json += ",\"beat\":" + std::to_string (reading.at->beat)
                  + ",\"tick\":" + std::to_string (reading.at->tick)
                  + ",\"at\":" + quoted (reading.at->describe());

        return json + ",\"placement\":" + quoted (hitPlacementName (reading.placement))
                    + ",\"chord\":" + quoted (reading.chordSymbol)
                    + ",\"anticipation\":" + (reading.anticipation ? "true" : "false")
                    + ",\"inRegister\":" + (reading.inRegister ? "true" : "false")
                    + ",\"outsideBy\":" + std::to_string (reading.outsideRegisterBy)
                    + ",\"takesTheBassNote\":" + (reading.takesTheBassNote ? "true" : "false")
                    + ",\"rootAnywhere\":" + (reading.rootAnywhere ? "true" : "false")
                    + ",\"oneTooMany\":" + (reading.oneTooMany ? "true" : "false")
                    + ",\"summary\":" + quoted (reading.summary)
                    + ",\"voicing\":{\"score\":" + std::to_string (reading.voicing.score)
                    + ",\"summary\":" + quoted (reading.voicing.summary)
                    + ",\"matches\":" + (reading.voicing.matchesChord ? "true" : "false")
                    + ",\"voicingType\":" + quoted (voicingTypeName (reading.voicing.type))
                    + ",\"findings\":" + jsonArray (reading.voicing.findings,
                                                    [] (const VoicingFinding& finding)
                      {
                          return "{\"severity\":" + quoted (severityName (finding.severity))
                               + ",\"message\":" + quoted (finding.message) + "}";
                      })
                    + "}}";
    }

}

std::string compHit (const char* progressionText, const char* styleRef,
                     int measureIndex, int beat, int tick,
                     const char* midiNotesCsv, int hitsAlreadyInBar)
{
    const auto parsed = parseProgressionText (progressionText != nullptr ? progressionText : "");

    if (! parsed.ok())
        return hold (jsonError (parsed.error));

    CompStyleDefinition style;

    if (! styleFrom (styleRef != nullptr ? styleRef : "", style))
        return hold (jsonError ("That comping style could not be read."));

    PlayedHit hit;
    hit.measureIndex = measureIndex;
    hit.midiNotes = parseNoteList (midiNotesCsv != nullptr ? midiNotesCsv : "");

    if (beat >= 0)
        hit.at = BarPosition { beat, tick };

    const auto reading = readCompHit (*parsed.chart, style, hit, hitsAlreadyInBar);

    return hold ("{\"ok\":true,\"style\":" + quoted (style.key)
                 + ",\"ticksPerBeat\":" + std::to_string (ticksPerBeat)
                 + ",\"hit\":" + hitReadingJson (reading) + "}");
}

std::string compTake (const char* progressionText, const char* styleRef,
                      int fromBar, int toBar, const char* hitsText)
{
    const auto parsed = parseProgressionText (progressionText != nullptr ? progressionText : "");

    if (! parsed.ok())
        return hold (jsonError (parsed.error));

    std::vector<PlayedHit> hits;

    if (! readHitList (hitsText != nullptr ? hitsText : "", hits))
        return hold (jsonError ("Not a list of comped chords"));

    CompStyleDefinition style;

    if (! styleFrom (styleRef != nullptr ? styleRef : "", style))
        return hold (jsonError ("That comping style could not be read."));
    const auto comp = evaluateComp (*parsed.chart, style, hits, fromBar, toBar);

    unsigned int qualities = 0;
    unsigned int roots = 0;
    coverageOf (*parsed.chart, fromBar, toBar, qualities, roots);

    return hold ("{\"ok\":true" + coverageJson (qualities, roots)
                 + ",\"style\":" + quoted (style.key)
                 + ",\"styleName\":" + quoted (style.name)
                 + ",\"ticksPerBeat\":" + std::to_string (ticksPerBeat)
                 + ",\"fit\":" + orNull (comp.fit)
                 + ",\"placement\":" + std::to_string (comp.placementFit)
                 + ",\"register\":" + std::to_string (comp.registerFit)
                 + ",\"density\":" + std::to_string (comp.densityFit)
                 + ",\"voicingScore\":" + orNull (comp.voicingScore)
                 + ",\"onTheFigure\":" + std::to_string (comp.hitsOnTheFigure)
                 + ",\"idiomatic\":" + std::to_string (comp.hitsIdiomatic)
                 + ",\"pushed\":" + std::to_string (comp.hitsPushed)
                 + ",\"offStyle\":" + std::to_string (comp.hitsOffStyle)
                 + ",\"tookTheBassNote\":" + std::to_string (comp.hitsTakingTheBassNote)
                 + ",\"summary\":" + quoted (comp.summary)
                 + ",\"hits\":" + jsonArray (comp.hits, hitReadingJson)
                 + ",\"bars\":" + jsonArray (comp.bars, [] (const CompBarReading& bar)
                   {
                       return "{\"bar\":" + std::to_string (bar.measureIndex)
                            + ",\"hits\":" + std::to_string (bar.hits)
                            + ",\"fewest\":" + std::to_string (bar.fewest)
                            + ",\"most\":" + std::to_string (bar.most)
                            + ",\"tooBusy\":" + (bar.tooBusy ? "true" : "false") + "}";
                   })
                 + ",\"observations\":" + jsonArray (comp.observations,
                                                     [] (const std::string& line) { return quoted (line); })
                 + "}");
}

} // namespace jazz::api
