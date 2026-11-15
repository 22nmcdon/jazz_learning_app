// Solo practice, and the record of what was practised, as JSON.
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
#include "jazz/core/LickCatalogue.h"
#include "jazz/core/LineAnalyzer.h"
#include "jazz/core/LinePlacement.h"
#include "jazz/core/LineStyle.h"
#include "jazz/core/LineWriter.h"
#include "jazz/core/PracticeLog.h"
#include "jazz/core/Scale.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

using namespace jazz::core;
namespace core = jazz::core;
using namespace jazz::api::wire;

namespace jazz::api
{

namespace
{

std::string approachKindKey (ApproachKind kind)
{
    switch (kind)
    {
        case ApproachKind::chromatic: return "chromatic";
        case ApproachKind::passing:   return "passing";
        case ApproachKind::enclosure: return "enclosure";
        case ApproachKind::none:      break;
    }

    return "";
}

std::string noteColourKey (NoteColour colour)
{
    switch (colour)
    {
        case NoteColour::chordTone:  return "chordTone";
        case NoteColour::scaleTone:  return "scaleTone";
        case NoteColour::approach:   return "approach";
        case NoteColour::unresolved: return "unresolved";
        case NoteColour::outside:    break;
    }

    return "outside";
}

/** Counts and percentages together: the page shows both, and working the
    percentages out twice on two sides of a bridge is how they disagree. */
std::string lineStatsJson (const LineStats& stats)
{
    return "\"total\":" + std::to_string (stats.total())
         + ",\"chordTones\":" + std::to_string (stats.chordTones)
         + ",\"scaleTones\":" + std::to_string (stats.scaleTones)
         + ",\"outside\":" + std::to_string (stats.outside)
         + ",\"percentChordTones\":" + std::to_string (stats.percentChordTones())
         + ",\"percentScaleTones\":" + std::to_string (stats.percentScaleTones())
         + ",\"approachTones\":" + std::to_string (stats.approachTones)
         + ",\"unresolved\":" + std::to_string (stats.unresolved)
         + ",\"settled\":" + std::to_string (stats.settled())
         + ",\"percentApproachTones\":" + std::to_string (stats.percentApproachTones())
         + ",\"percentUnresolved\":" + std::to_string (stats.percentUnresolved())
         + ",\"percentOutside\":" + std::to_string (stats.percentOutside())
         + ",\"score\":" + std::to_string (stats.score());
}

/** One bar of a take, whole.

    It takes a `LineBar` rather than a `LineStats` and three loose
    arguments because the strong-beat counts are on the bar and nothing
    else can hold them - and because a default of zero for a count would
    have said "nothing landed on an accented beat" about a bar where
    plenty had. `LineAnalyzer::barFor` is what answers this mid-take.

    Still words and never points, here as in the engine: where a note sat
    is not a better or worse note, and none of this reaches `score`.
*/
std::string lineBarJson (const LineBar& bar, const std::string& symbol)
/*  The symbol is passed rather than read off the bar because a bar the
    player has clicked but not played over has one and the bar does not -
    `barFor` can only report the chord a note was read against. */
{
    return "{\"index\":" + std::to_string (bar.measureIndex)
         + ",\"chord\":" + quoted (symbol)
         + ",\"neverLeftTheChord\":" + (bar.neverLeftTheChord ? "true" : "false")
         + ",\"onStrongBeats\":" + std::to_string (bar.notesOnStrongBeats)
         + ",\"chordTonesOnStrongBeats\":" + std::to_string (bar.chordTonesOnStrongBeats)
         + "," + lineStatsJson (bar.stats) + "}";
}

/** Where a take put its notes, against the line style the player chose.

    Sent whole or not at all. A shell that named no style gets `null`
    rather than a reading of nothing - the standard is the player's
    choice, and without one there is nothing here to say.
*/
std::string linePlacementJson (const LinePlacementReading& placement)
{
    return "{\"style\":" + quoted (placement.styleKey)
         + ",\"styleName\":" + quoted (placement.styleName)
         + ",\"fit\":" + orNull (placement.fit)
         + ",\"grid\":" + std::to_string (placement.gridFit)
         + ",\"phrasing\":" + std::to_string (placement.phraseFit)
         + ",\"register\":" + std::to_string (placement.registerFit)
         + ",\"onsets\":" + std::to_string (placement.onsets)
         + ",\"placed\":" + std::to_string (placement.onsetsPlaced)
         + ",\"onTheGrid\":" + std::to_string (placement.onsetsOnTheGrid)
         + ",\"notes\":" + std::to_string (placement.notes)
         + ",\"inRegister\":" + std::to_string (placement.notesInRegister)
         + ",\"onStrongBeats\":" + std::to_string (placement.notesOnStrongBeats)
         + ",\"chordTonesOnStrongBeats\":" + std::to_string (placement.chordTonesOnStrongBeats)
         + ",\"chromaticsOnTheBeat\":" + std::to_string (placement.chromaticsOnTheBeat)
         + ",\"summary\":" + quoted (placement.summary)
         + ",\"phrases\":" + jsonArray (placement.phrases, [] (const LinePhrase& phrase)
           {
               return "{\"bar\":" + std::to_string (phrase.measureIndex)
                    + ",\"beat\":" + std::to_string (phrase.startsAt.beat)
                    + ",\"tick\":" + std::to_string (phrase.startsAt.tick)
                    // How a player counts it aloud, so a panel does not
                    // have to work "2 and" back out of a tick.
                    + ",\"at\":" + quoted (phrase.startsAt.describe())
                    + ",\"notes\":" + std::to_string (phrase.onsets)
                    + ",\"overBy\":" + std::to_string (phrase.overBy)
                    + ",\"onAStyleTick\":" + (phrase.startedOnAStyleTick ? "true" : "false")
                    + "}";
           })
         + ",\"observations\":" + jsonArray (placement.observations,
                                              [] (const std::string& line) { return quoted (line); })
         + "}";
}

/** A chord in the line, read as a chord.

    Sent as `voicing` rather than as `chord` because a line note already
    carries a `chord` and it is the bar's symbol - the thing this was
    played *over*. Two keys one letter apart meaning opposite halves of the
    same sentence is how a shell ends up drawing the wrong one.
*/
std::string lineChordJson (const LineChord& chord)
{
    const auto midiList = [] (const std::vector<int>& notes)
    {
        return jsonArray (notes, [] (int note) { return std::to_string (note); });
    };

    return "{\"notes\":" + midiList (chord.midiNotes)
         + ",\"names\":" + jsonArray (chord.midiNotes, [] (int note)
                                       { return quoted (midiNoteName (note)); })
         // The top note, named as well as numbered, for the same reason the
         // resolved list carries both: a shell lighting the key it was
         // played on should not have to work the pitch back out of "E4".
         + ",\"melody\":" + std::to_string (chord.melodyNote)
         + ",\"melodyName\":" + quoted (midiNoteName (chord.melodyNote))
         + ",\"melodyDegree\":" + quoted (chord.melodyDegree)
         + ",\"melodyColour\":" + quoted (noteColourKey (chord.melodyColour))
         + ",\"type\":" + quoted (chord.typeName)
         + ",\"saysTheChord\":" + (chord.saysTheChord ? "true" : "false")
         + ",\"spelled\":" + quoted (chord.spelled)
         + ",\"outside\":" + midiList (chord.outsideNotes)
         + ",\"verdict\":" + quoted (chord.verdict)
         + ",\"reading\":" + quoted (chord.reading)
         + ",\"chord\":" + quoted (chord.chordSymbol)
         + ",\"bar\":" + std::to_string (chord.measureIndex) + "}";
}

std::string lineNoteJson (const LineNote& note)
{
    return "{\"midi\":" + std::to_string (note.midiNote)
         + ",\"name\":" + quoted (midiNoteName (note.midiNote))
         + ",\"colour\":" + quoted (noteColourKey (note.colour))
         + ",\"colourName\":" + quoted (noteColourName (note.colour))
         + ",\"degree\":" + quoted (note.degree)
         + ",\"scale\":" + quoted (note.scaleName)
         + ",\"avoid\":" + (note.avoidNote ? "true" : "false")
         + ",\"resolvesTo\":" + quoted (note.resolvesTo > 0 ? midiNoteName (note.resolvesTo) : "")
         + ",\"approachKind\":" + quoted (approachKindKey (note.approachKind))
         + ",\"approachName\":" + quoted (approachKindName (note.approachKind))
         + ",\"wantsToReach\":" + quoted (note.wantsToReach > 0 ? midiNoteName (note.wantsToReach) : "")
         + ",\"wantsToReachDegree\":" + quoted (note.wantsToReachDegree)
         + ",\"wantsToReachStep\":" + std::to_string (note.wantsToReach > 0
                                                      ? note.wantsToReach - note.midiNote : 0)
         + ",\"chord\":" + quoted (note.chordSymbol)
         // Where it fell, when the shell could say. "" rather than a
         // position, so a page reading it cannot mistake a missing answer
         // for the downbeat.
         + ",\"at\":" + quoted (note.at.has_value() ? note.at->describe() : "")
         + ",\"onStrongBeat\":" + (note.onStrongBeat ? "true" : "false")
         // Struck with the note before it: the two are one chord. A shell
         // drawing a voicing finds the runs of this.
         + ",\"withPrevious\":" + (note.struckWithPrevious ? "true" : "false")
         + ",\"bar\":" + std::to_string (note.measureIndex) + "}";
}

/** The one take this process has.

    A take is a stream with a beginning and an end, so something has to
    remember it between calls. There is one player and one page, so this is
    one analyser - not a handle table pretending there might be more.
*/
LineAnalyzer& soloTake()
{
    static LineAnalyzer analyzer;
    return analyzer;
}

}

std::string lineStyles()
{
    return hold ("{\"ok\":true,\"styles\":"
                 + jsonArray (core::lineStyles(), [] (const LineStyleDefinition& style)
                   {
                       return "{\"key\":" + quoted (style.key)
                            + ",\"name\":" + quoted (style.name)
                            + ",\"summary\":" + quoted (style.summary)
                            + ",\"scaleStyle\":" + quoted (style.scaleStyle)
                            + ",\"shortestPhrase\":" + std::to_string (style.shortestPhrase)
                            + ",\"longestPhrase\":" + std::to_string (style.longestPhrase)
                            + ",\"usesApproaches\":" + (style.usesApproaches ? "true" : "false")
                            + ",\"lowestNote\":" + std::to_string (style.lowestNote)
                            + ",\"highestNote\":" + std::to_string (style.highestNote)

                            // The word, never the enumerator.
                            + ",\"feel\":" + quoted (subdivisionName (style.feel))
                            + ",\"ticksPerBeat\":" + std::to_string (ticksPerBeat) + "}";
                   })
                 + "}");
}

std::string scaleStyles()
{
    return hold ("{\"ok\":true,\"styles\":"
                 + jsonArray (core::scaleStyles(), [] (const ScaleStyle& style)
                   {
                       return "{\"key\":" + quoted (style.key)
                            + ",\"name\":" + quoted (style.name)
                            + ",\"summary\":" + quoted (style.summary) + "}";
                   })
                 + "}");
}

namespace
{
    /** What a line drew on, each named once, in the order they were played.

        Sent alongside the notes rather than worked out from them, because
        naming a lick means having its name, its attribution and how much
        weight to put on it - and a shell holding a catalogue of nineteen licks
        to look those up in is a second copy of the catalogue, which is the
        drift `scaleStyles()` and `compStyles()` are on the wire to prevent.

        Empty when the whole line was generated, which is a real answer and the
        one a shell feature-detects on: a line that quoted nothing must not be
        made to claim it quoted something.
    */
    std::string licksUsedJson (const std::vector<WrittenNote>& line)
    {
        struct Used
        {
            const LickDefinition* lick {};
            int notes {};
        };

        std::vector<Used> used;

        for (const auto& note : line)
        {
            if (note.lickKey.empty())
                continue;

            const auto already = std::find_if (used.begin(), used.end(),
                                               [&note] (const Used& seen)
                                               { return seen.lick->key == note.lickKey; });

            if (already != used.end())
            {
                ++already->notes;
                continue;
            }

            used.push_back ({ &lickFor (note.lickKey), 1 });
        }

        return jsonArray (used, [] (const Used& one)
        {
            return "{\"key\":" + quoted (one.lick->key)
                 + ",\"name\":" + quoted (one.lick->name)
                 + ",\"summary\":" + quoted (one.lick->summary)
                 // Who to credit, which is the whole reason the catalogue was
                 // built rather than more atoms added to the generator.
                 + ",\"attribution\":" + quoted (one.lick->attribution)
                 // And how solid the credit is: the research is candid that
                 // three of its entries are composites rather than quotes.
                 + ",\"source\":" + quoted (lickSourceName (one.lick->source))
                 + ",\"notes\":" + std::to_string (one.notes) + "}";
        });
    }
}

std::string improvisedLine (const char* progressionText, int fromBar, int toBar,
                            const char* chosenScale, const char* lineStyle, int seed)
{
    const auto parsed = parseProgressionText (progressionText != nullptr ? progressionText : "");

    if (! parsed.ok())
        return hold (jsonError (parsed.error));

    const auto& style = core::lineStyleFor (lineStyle != nullptr ? lineStyle : "");

    const auto line = core::improvisedLine (*parsed.chart, fromBar, toBar,
                                            chosenScale != nullptr ? chosenScale : "",
                                            style.key,
                                            static_cast<std::uint32_t> (seed));

    /*  `style` and `scaleStyle` both come back. The first is which style was
        resolved - an unknown key falls back rather than failing, so a shell
        that asked for something this version renamed should be able to see
        what it got. The second is the vocabulary a take must read this line
        against, and sending it is what stops a shell guessing: hand the reader
        a different one and half the line colours wrong. */
    return hold ("{\"ok\":true,\"ticksPerBeat\":" + std::to_string (ticksPerBeat)
                 + ",\"style\":" + quoted (style.key)
                 + ",\"scaleStyle\":" + quoted (style.scaleStyle)
                 + ",\"notes\":"
                 + jsonArray (line, [] (const WrittenNote& note)
                   {
                       return "{\"bar\":" + std::to_string (note.measureIndex)
                            + ",\"beat\":" + std::to_string (note.at.beat)
                            + ",\"tick\":" + std::to_string (note.at.tick)
                            + ",\"midi\":" + std::to_string (note.midiNote)
                            + ",\"name\":" + quoted (midiNoteName (note.midiNote))
                            + ",\"chord\":" + quoted (note.chordSymbol)
                            + ",\"colour\":" + quoted (noteColourName (note.colour))
                            // How long it sounds, which for a quoted note is
                            // the source's own rhythm rather than one step of
                            // the grid - and rhythm is half of what makes a
                            // lick that lick.
                            + ",\"length\":" + std::to_string (note.lengthTicks)
                            + ",\"lick\":" + quoted (note.lickKey) + "}";
                   })
                 + ",\"licks\":" + licksUsedJson (line)
                 + "}");
}

namespace
{
/** A practice history, read back off the wire.

    Same direction as the style reader above and for the same reason: results
    are JSON because encoding them is the shell's business, inputs are flat text
    because the engine has no JSON reader and must not grow one.

    Five delimiters, none of which can occur inside a number, so nothing needs
    escaping and the grammar carries no free text at all:

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

    The tune is a **number**, which is the whole reason this stays quote-free -
    what a player calls a tune is the page's business, exactly as what they call
    their own comping style is. `mode` is 0 for soloing and 1 for comping.

    An empty history is a record with nothing in it, which is a real state and
    not an error: it is what a first visit hands over. Anything else that does
    not parse **is** an error, the same asymmetry a described comping style
    draws - a shell's bug must not read back as a player who has not practised.
*/
std::optional<std::vector<PracticeTake>> readHistory (const std::string& text)
{
    std::vector<PracticeTake> takes;

    if (text.empty())
        return takes;

    // Wide enough for any real record and narrow enough that a broken message
    // cannot ask for arithmetic on a nonsense number.
    constexpr auto latestDay = 400000;          // about eleven centuries of days
    constexpr auto longestTake = 86400;         // one day of playing, in seconds
    constexpr auto mostOfAnything = 1000000;
    constexpr auto mostBars = 10000;

    for (const auto& one : splitOn (text, '~'))
    {
        const auto halves = splitOn (one, '|');

        if (halves.size() != 2)
            return {};

        const auto head = splitOn (halves[0], ':');

        if (head.size() != 18)
            return {};

        const auto day       = numberIn (head[0], 0, latestDay);
        const auto mode      = numberIn (head[1], 0, 1);
        const auto tune      = numberIn (head[2], 0, mostOfAnything);
        const auto seconds   = numberIn (head[3], 0, longestTake);
        const auto qualities = numberIn (head[4], 0, 255);     // eight qualities
        const auto roots     = numberIn (head[5], 0, 4095);    // twelve pitch classes

        if (! (day && mode && tune && seconds && qualities && roots))
            return {};

        PracticeTake take;
        take.day = *day;
        take.mode = *mode == 1 ? PracticeMode::comping : PracticeMode::soloing;
        take.tune = *tune;
        take.seconds = *seconds;
        take.qualities = static_cast<unsigned int> (*qualities);
        take.roots = static_cast<unsigned int> (*roots);

        int* const counts[] = {
            &take.notes.chordTones, &take.notes.scaleTones, &take.notes.approachTones,
            &take.notes.unresolved, &take.notes.outside,
            &take.leaps, &take.leapsResolved, &take.chordsPlayed,
            &take.hitsOnTheFigure, &take.hitsIdiomatic, &take.hitsPushed, &take.hitsOffStyle
        };

        for (std::size_t i = 0; i < 12; ++i)
        {
            const auto value = numberIn (head[6 + i], 0, mostOfAnything);

            if (! value)
                return {};

            *counts[i] = *value;
        }

        for (const auto& barText : splitOn (halves[1], ';'))
        {
            // An empty bar list is a take whose bars a shell did not keep, not
            // a broken message: the header says the take-wide counts are not
            // derived from the bars.
            if (barText.empty())
                continue;

            const auto parts = splitOn (barText, ',');

            if (parts.size() != 6)
                return {};

            const auto index = numberIn (parts[0], 0, mostBars);

            if (! index)
                return {};

            PracticeBar bar;
            bar.measureIndex = *index;

            int* const tiers[] = { &bar.notes.chordTones, &bar.notes.scaleTones,
                                   &bar.notes.approachTones, &bar.notes.unresolved,
                                   &bar.notes.outside };

            for (std::size_t i = 0; i < 5; ++i)
            {
                const auto value = numberIn (parts[1 + i], 0, mostOfAnything);

                if (! value)
                    return {};

                *tiers[i] = *value;
            }

            take.bars.push_back (bar);
        }

        takes.push_back (take);
    }

    return takes;
}

/** `LineStats` on the wire, with the score left off.

    Deliberately not `lineStatsJson`, which carries one. A score is a reading of
    a bar that was just played and belongs on a take; this is the same counts
    summed over weeks, where the same number would be a grade for a player.
    Leaving it out here means a page drawing this panel has nothing to plot even
    if somebody later decides it would look good as a line.
*/
std::string practiceNotesJson (const LineStats& stats)
{
    return "{\"total\":" + std::to_string (stats.total())
         + ",\"settled\":" + std::to_string (stats.settled())
         + ",\"chordTones\":" + std::to_string (stats.chordTones)
         + ",\"scaleTones\":" + std::to_string (stats.scaleTones)
         + ",\"approachTones\":" + std::to_string (stats.approachTones)
         + ",\"unresolved\":" + std::to_string (stats.unresolved)
         + ",\"outside\":" + std::to_string (stats.outside)
         + ",\"percentChordTones\":" + std::to_string (stats.percentChordTones())
         + ",\"percentScaleTones\":" + std::to_string (stats.percentScaleTones())
         + ",\"percentApproachTones\":" + std::to_string (stats.percentApproachTones())
         + ",\"percentUnresolved\":" + std::to_string (stats.percentUnresolved())
         + ",\"percentOutside\":" + std::to_string (stats.percentOutside()) + "}";
}

std::string namesJson (const std::vector<std::string>& names)
{
    return jsonArray (names, [] (const std::string& name) { return quoted (name); });
}

std::string saidJson (const std::vector<std::string>& lines)
{
    return jsonArray (lines, [] (const std::string& line) { return quoted (line); });
}
}

std::string practiceReading (const char* history, int today)
{
    const auto takes = readHistory (history != nullptr ? history : "");

    if (! takes.has_value())
        return hold (jsonError ("That practice record could not be read."));

    const auto reading = core::readPractice (*takes, today);

    return hold ("{\"ok\":true,\"takes\":" + std::to_string (reading.takes)
                 + ",\"bars\":" + std::to_string (reading.bars)
                 + ",\"minutes\":" + std::to_string (reading.minutes)
                 + ",\"daysPractised\":" + std::to_string (reading.daysPractised)
                 + ",\"span\":" + std::to_string (reading.span)
                 + ",\"daysSinceLast\":" + std::to_string (reading.daysSinceLast)
                 + ",\"soloTakes\":" + std::to_string (reading.soloTakes)
                 + ",\"compTakes\":" + std::to_string (reading.compTakes)
                 + ",\"qualitiesMet\":" + namesJson (reading.qualitiesMet)
                 + ",\"qualitiesMissing\":" + namesJson (reading.qualitiesMissing)
                 + ",\"rootsMet\":" + namesJson (reading.rootsMet)
                 + ",\"rootsMissing\":" + namesJson (reading.rootsMissing)
                 + ",\"notes\":" + practiceNotesJson (reading.notes)
                 + ",\"summary\":" + quoted (reading.summary)
                 + ",\"observations\":" + saidJson (reading.observations)
                 + "}");
}

std::string tuneProgress (const char* progressionText, const char* history, int today)
{
    auto parsed = parseProgressionText (progressionText != nullptr ? progressionText : "");

    if (! parsed.ok())
        return hold (jsonError (parsed.error));

    const auto takes = readHistory (history != nullptr ? history : "");

    if (! takes.has_value())
        return hold (jsonError ("That practice record could not be read."));

    const auto progress = core::readTuneProgress (*parsed.chart, *takes, today);

    return hold ("{\"ok\":true,\"takes\":" + std::to_string (progress.takes)
                 + ",\"daysPractised\":" + std::to_string (progress.daysPractised)
                 + ",\"daysSinceLast\":" + std::to_string (progress.daysSinceLast)
                 + ",\"barsInChart\":" + std::to_string (progress.barsInChart)
                 + ",\"bars\":" + jsonArray (progress.bars, [] (const TuneBarMemory& bar)
                   {
                       return "{\"index\":" + std::to_string (bar.measureIndex)
                            + ",\"chord\":" + quoted (bar.chordSymbol)
                            + ",\"takes\":" + std::to_string (bar.takes)
                            + ",\"notes\":" + practiceNotesJson (bar.notes) + "}";
                   })
                 + ",\"summary\":" + quoted (progress.summary)
                 + ",\"observations\":" + saidJson (progress.observations)
                 + "}");
}

//==============================================================================
std::string soloStartTake()
{
    soloTake().startTake();

    return hold ("{\"ok\":true,\"taking\":true," + lineStatsJson (soloTake().stats()) + "}");
}

namespace
{
    /** The bar's chords, read off the wire: `C6:0|A7:2`, or a bare `Dm7`.

        Refuses rather than salvages, the way every reader on this wire does:
        a bar the shell garbled must not read back as a bar with one chord on
        it, because that is the exact failure this whole change is about and
        it would be invisible. The one thing it does fix up is the first
        chord's beat, which is 0 whatever the text says - the lookup reads
        backwards and has to find something at the top of the bar.
    */
    std::optional<std::vector<BarChord>> readBarChords (const std::string& text)
    {
        std::vector<BarChord> chords;

        for (const auto& one : splitOn (text, '|'))
        {
            const auto colon = one.rfind (':');
            const auto symbol = colon == std::string::npos ? one : one.substr (0, colon);
            const auto chord = ChordSymbol::parse (symbol);

            if (! chord.has_value())
                return {};

            auto fromBeat = 0;

            if (colon != std::string::npos)
            {
                const auto beat = numberIn (one.substr (colon + 1), 0, 64);

                if (! beat.has_value())
                    return {};

                fromBeat = *beat;
            }

            chords.push_back ({ fromBeat, *chord });
        }

        if (chords.empty())
            return {};

        chords.front().fromBeat = 0;
        return chords;
    }
}

std::string soloSetBar (int measureIndex, const char* symbol, const char* chosenScale,
                        const char* style, int beatsPerBar)
{
    const std::string barText = symbol != nullptr ? symbol : "";
    const auto chords = readBarChords (barText);

    if (! chords.has_value())
        return hold (jsonError ("Not a chord symbol: " + barText));

    LineAnalyzer::Options options;
    options.chosenScale = chosenScale != nullptr ? chosenScale : "";
    options.style = style != nullptr ? style : "";

    // Which beats are strong is the metre's business, and the metre is the
    // chart's. Nothing reads it unless the shell also sends positions.
    options.beatsPerBar = beatsPerBar > 0 ? beatsPerBar : 4;

    auto& analyzer = soloTake();
    analyzer.setOptions (options);
    analyzer.setTarget (measureIndex, *chords);

    /*  The bar is named by its first chord, which is what the chart draws and
        what the player is looking at. Every note's own chord is on the note,
        so a turnaround's reading is not lost by naming the bar `C6`. */
    const auto& named = chords->front().chord;

    // Clicking a bar is how the player asks what they have done on it, so the
    // answer comes back with the move rather than needing a call of its own.
    //
    // `chords` is what a shell feature-detects on: this call's name did not
    // change, so the page's boot check cannot tell a new engine from an old
    // one and the data has to.
    return hold ("{\"ok\":true,\"taking\":" + std::string (analyzer.isTaking() ? "true" : "false")
                 + ",\"chords\":" + std::to_string (chords->size())
                 + ",\"bar\":" + lineBarJson (analyzer.barFor (measureIndex), named.toString())
                 + ",\"take\":{" + lineStatsJson (analyzer.stats()) + "}}");
}

std::string soloPlayNote (int midiNote, int beat, int tick, int withPrevious)
{
    auto& analyzer = soloTake();

    /*  Whether this note was struck with the one before it - a chord in the
        line rather than the next note of it. Known to the shell at the moment
        the note arrives, so it costs no waiting: the engine never needs to be
        told a chord is finished, only that a note joined one.  */
    const auto attack = withPrevious != 0 ? Attack::withPrevious : Attack::fresh;

    /*  A negative beat means the shell has no clock running and cannot say
        where the note fell. There is no position that means "no position", so
        it is signalled rather than encoded: a made-up downbeat would be read
        as a real one.  */
    const auto note = beat >= 0 ? analyzer.play (midiNote, BarPosition { beat, tick }, attack)
                                : analyzer.play (midiNote, attack);
    const auto& resolved = analyzer.resolvedByLastNote();
    const auto& stranded = analyzer.strandedByLastNote();
    const auto chordNow = analyzer.chordSoFar();

    /*  One note can change the reading of notes behind it, and those may be in
        an earlier bar - running chromatically into the next chord is the whole
        reason the window does not stop at the barline. So the reply carries
        every bar whose numbers moved, not only the one just played into.
        Without this, the bar before would keep a strip that stopped being true
        the moment the line landed - or failed to. */
    std::vector<LineNote> changed { note };

    const auto alsoSend = [&changed] (const std::vector<LineNote>& notes)
    {
        for (const auto& earlier : notes)
            if (std::none_of (changed.begin(), changed.end(), [&earlier] (const LineNote& seen)
                              { return seen.measureIndex == earlier.measureIndex; }))
                changed.push_back (earlier);
    };

    alsoSend (resolved);
    alsoSend (stranded);

    // Name and pitch both: a shell that wants to light the key that note was
    // played on should not have to work the pitch back out of "Db4".
    const auto names = [] (const std::vector<LineNote>& notes)
    {
        return jsonArray (notes, [] (const LineNote& earlier)
                          {
                              return "{\"name\":" + quoted (midiNoteName (earlier.midiNote))
                                   + ",\"midi\":" + std::to_string (earlier.midiNote)
                                   + ",\"kind\":" + quoted (approachKindKey (earlier.approachKind))
                                   // Where it went. Several voices of a chord
                                   // can land in one breath and land on
                                   // different notes, so "on to what" is per
                                   // note rather than one answer for the list.
                                   + ",\"to\":" + quoted (earlier.resolvesTo > 0
                                                          ? midiNoteName (earlier.resolvesTo) : "")
                                   + "}";
                          });
    };

    return hold ("{\"ok\":true,\"taking\":" + std::string (analyzer.isTaking() ? "true" : "false")
                 + ",\"note\":" + lineNoteJson (note)
                 /*  The chord this note is part of, if it is part of one, read
                     as a chord. `null` for an ordinary line, which is almost
                     every note - a shell shows this instead of the single-note
                     reading when it is there and is otherwise unaffected. */
                 + ",\"voicing\":" + (chordNow.has_value() ? lineChordJson (*chordNow) : "null")
                 + ",\"resolved\":" + names (resolved)
                 + ",\"stranded\":" + names (stranded)
                 + ",\"bar\":" + lineBarJson (analyzer.barFor (note.measureIndex), note.chordSymbol)
                 + ",\"bars\":" + jsonArray (changed, [&analyzer] (const LineNote& touched)
                   { return lineBarJson (analyzer.barFor (touched.measureIndex),
                                         touched.chordSymbol); })
                 + ",\"take\":{" + lineStatsJson (analyzer.stats()) + "}}");
}

std::string soloEndTake (const char* lineStyle)
{
    auto& analyzer = soloTake();
    analyzer.endTake();

    const auto take = analyzer.summary();

    /*  Where the take put its notes, against the style the player chose off
        the dock. Empty means they chose none, and a take with no chosen
        standard behind it gets no placement at all rather than a reading of
        the first style in the catalogue - which would be the engine inventing
        the standard and then marking somebody against it, the exact thing
        `docs/SOLO_PRACTICE.md` closed this question over.

        The metre comes off the analyser's own options rather than out of a
        new argument: the shell told it once, in `soloSetBar`, and two copies
        of the metre is two chances to disagree about which beats are strong.
    */
    const std::string styleKey = lineStyle != nullptr ? lineStyle : "";

    const auto placement = styleKey.empty()
                             ? std::string ("null")
                             : linePlacementJson (readLinePlacement (analyzer.notes(),
                                                                     lineStyleFor (styleKey),
                                                                     analyzer.currentOptions().beatsPerBar));

    std::vector<std::string> symbols;

    for (const auto& bar : take.bars)
        symbols.push_back (bar.chordSymbol);

    unsigned int qualities = 0;
    unsigned int roots = 0;
    coverageOf (symbols, qualities, roots);

    return hold ("{\"ok\":true,\"taking\":false" + coverageJson (qualities, roots)
                 + ",\"placement\":" + placement
                 + std::string (",\"summary\":") + quoted (take.summary)
                 + ",\"observations\":" + jsonArray (take.observations,
                                                     [] (const std::string& line) { return quoted (line); })
                 + ",\"take\":{" + lineStatsJson (take.overall) + "}"
                 + ",\"leaps\":" + std::to_string (take.leaps)
                 + ",\"leapsResolved\":" + std::to_string (take.leapsResolved)
                 + ",\"range\":" + std::to_string (take.rangeInSemitones())
                 + ",\"chordsPlayed\":" + std::to_string (take.chordsPlayed)
                 + ",\"chordsSpellingTheBar\":" + std::to_string (take.chordsSpellingTheBar)
                 + ",\"chordShape\":" + quoted (take.chordShape)
                 + ",\"chords\":" + jsonArray (take.chords, [] (const LineChord& chord)
                                                { return lineChordJson (chord); })
                 + ",\"bars\":" + jsonArray (take.bars, [] (const LineBar& bar)
                   { return lineBarJson (bar, bar.chordSymbol); })
                 + "}");
}

} // namespace jazz::api
