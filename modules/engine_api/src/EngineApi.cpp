// The engine's answers, as JSON.
//
// Both shells ask the engine the same eleven questions and both want the answer
// as text: the browser because JavaScript cannot see C++ types, and the JUCE app
// because its UI is that same page in a webview. Encoding lived in the
// WebAssembly shell until the app needed it too - and two copies of a wire
// format is how the two shells start disagreeing about what a chord looks like.
//
// This holds no theory. It asks the Core Engine and writes down what it said.
#include "jazz/api/EngineApi.h"

#include "jazz/core/Chart.h"
#include "jazz/core/ChartFormats.h"
#include "jazz/core/ChordIdentifier.h"
#include "jazz/core/Comping.h"
#include "jazz/core/LineAnalyzer.h"
#include "jazz/core/PracticeLog.h"
#include "jazz/core/Reharmonizer.h"
#include "jazz/core/ScaleSuggester.h"
#include "jazz/core/VoicingAnalyzer.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace jazz::core;
namespace core = jazz::core;

namespace
{
    //==============================================================================
    // Minimal JSON writing. The engine has no serialisation of its own and should
    // not grow any: how results are encoded is the shell's business.

    std::string quoted (const std::string& text)
    {
        std::string out = "\"";

        for (auto c : text)
        {
            switch (c)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if (static_cast<unsigned char> (c) < 0x20)
                        out += ' ';
                    else
                        out += c;
            }
        }

        return out + "\"";
    }

    template <typename Item, typename Fn>
    std::string jsonArray (const std::vector<Item>& items, Fn&& toJson)
    {
        std::string out = "[";

        for (std::size_t i = 0; i < items.size(); ++i)
        {
            if (i > 0)
                out += ",";

            out += toJson (items[i]);
        }

        return out + "]";
    }

    std::string jsonError (const std::string& message)
    {
        return "{\"ok\":false,\"error\":" + quoted (message) + "}";
    }

    /** Was a static buffer when these returned `const char*` for C. The
        answers are std::string now, so this is the identity - kept only so
        every entry point below reads exactly as it did before the move.
    */
    std::string hold (std::string&& json)
    {
        return std::move (json);
    }

    std::vector<int> parseNoteList (const std::string& csv)
    {
        std::vector<int> notes;
        std::string current;

        /*  A note that will not read is dropped, not thrown over.

            `readHit` wraps its own three conversions and then calls this one
            *outside* that try, so a hit whose notes were rubbish threw straight
            through `compHit` and out of the shell. Both shells now catch as
            well, but the reader should not be the thing that needs catching.

            Dropping is the same answer `coverageOf` gives a chord symbol it
            cannot name: the caller gets a shorter list, which every analyser
            here already handles, because an empty one is an ordinary case. */
        const auto flush = [&notes, &current]
        {
            if (! current.empty())
            {
                try
                {
                    notes.push_back (std::stoi (current));
                }
                catch (...)
                {
                    // Not a note. The list is shorter by one.
                }

                current.clear();
            }
        };

        for (auto c : csv)
        {
            if (c == ',' || c == ' ')
                flush();
            else
                current += c;
        }

        flush();
        return notes;
    }

    std::string chordToneJson (const ChordTone& tone, PitchClass root)
    {
        return "{\"label\":" + quoted (tone.label)
             + ",\"semitones\":" + std::to_string (tone.semitones)
             + ",\"pitchClass\":" + std::to_string (toPitchClass (root + tone.semitones))
             + ",\"name\":" + quoted (pitchClassName (toPitchClass (root + tone.semitones)))
             + ",\"essential\":" + (tone.essential ? "true" : "false") + "}";
    }

    std::string scaleSuggestionJson (const ScaleSuggestion& suggestion)
    {
        std::string pitches = "[";
        const auto pitchClasses = suggestion.scale.pitchClasses();

        for (std::size_t i = 0; i < pitchClasses.size(); ++i)
            pitches += (i > 0 ? "," : "") + std::to_string (pitchClasses[i]);

        pitches += "]";

        std::string avoid = "[";

        for (std::size_t i = 0; i < suggestion.avoidNotes.size(); ++i)
            avoid += (i > 0 ? "," : "") + std::to_string (suggestion.avoidNotes[i]);

        avoid += "]";

        const auto names = suggestion.scale.noteNames();

        return "{\"name\":" + quoted (suggestion.scale.name())
             + ",\"noteNames\":" + jsonArray (names, [] (const std::string& n) { return quoted (n); })
             + ",\"pitchClasses\":" + pitches
             + ",\"avoidPitchClasses\":" + avoid
             + ",\"rationale\":" + quoted (suggestion.rationale)
             + ",\"score\":" + std::to_string (suggestion.score)
             + ",\"primary\":" + (suggestion.isPrimary ? "true" : "false") + "}";
    }

    /** The families a style key names, for the suggester. An unknown key means
        every family, which is how a key from another version stays harmless. */
    std::vector<ScaleFamily> familiesForStyle (const char* key)
    {
        if (const auto* style = findScaleStyle (key != nullptr ? key : ""))
            return style->families;

        return {};
    }

    /** Maps the style key the page sends to the shape being practised. */
    std::optional<VoicingType> practiseTypeFor (const char* key)
    {
        const std::string name = key != nullptr ? key : "";

        if (name == "shell")      return VoicingType::shell;
        if (name == "root")       return VoicingType::rootPosition;
        if (name == "rootless")   return VoicingType::rootlessLeftHand;
        if (name == "twohanded")  return VoicingType::twoHandedRootless;
        if (name == "solo")       return VoicingType::solo;

        return std::nullopt;  // "any": read the chart, do not drill a shape
    }

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

    /** The harmony a take was played over, as the two masks a practice row
        stores.

        On the take rather than worked out by a shell, because which quality
        "Am7b5" is is theory and CLAUDE.md puts theory in the engine. A page
        parsing chord symbols for itself is a second chord reader, and it would
        be wrong about the first symbol somebody spells unusually.
    */
    std::string coverageJson (unsigned int qualities, unsigned int roots)
    {
        return ",\"qualities\":" + std::to_string (qualities)
             + ",\"roots\":" + std::to_string (roots);
    }

    std::string lineBarJson (int measureIndex, const std::string& symbol, const LineStats& stats,
                             bool neverLeftTheChord = false)
    {
        return "{\"index\":" + std::to_string (measureIndex)
             + ",\"chord\":" + quoted (symbol)
             + ",\"neverLeftTheChord\":" + (neverLeftTheChord ? "true" : "false")
             + "," + lineStatsJson (stats) + "}";
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

    std::string severityName (FindingSeverity severity)
    {
        switch (severity)
        {
            case FindingSeverity::problem:    return "problem";
            case FindingSeverity::suggestion: return "suggestion";
            case FindingSeverity::good:       break;
        }

        return "good";
    }
}

//==============================================================================
/** Parses progression text into a chart. */
namespace jazz::api
{

std::string parseChart (const char* progressionText)
{
    auto result = parseProgressionText (progressionText != nullptr ? progressionText : "");

    if (! result.ok())
        return hold (jsonError (result.error));

    std::string measures = "[";

    for (auto index = 0; index < result.chart->measureCount(); ++index)
    {
        if (index > 0)
            measures += ",";

        const auto& measure = result.chart->measures[static_cast<std::size_t> (index)];

        measures += "{\"index\":" + std::to_string (index) + ",\"chords\":"
                  + jsonArray (measure.slots, [] (const ChordSlot& slot)
                    {
                        return "{\"symbol\":" + quoted (slot.chord.toString())
                             + ",\"beats\":" + std::to_string (slot.beats) + "}";
                    })
                  + "}";
    }

    measures += "]";

    return hold ("{\"ok\":true,\"title\":" + quoted (result.chart->title)
                 + ",\"measures\":" + measures + "}");
}

/** Every scale that fits a chord, best first. */
std::string scalesForChord (const char* symbol, const char* style)
{
    const auto chord = ChordSymbol::parse (symbol != nullptr ? symbol : "");

    if (! chord.has_value())
        return hold (jsonError (std::string ("Not a chord symbol: ") + (symbol != nullptr ? symbol : "")));

    ScaleSuggester::Options options;
    options.families = familiesForStyle (style);

    auto suggestions = ScaleSuggester { options }.suggestionsFor (*chord);

    // A style with nothing for this chord - bebop over a diminished bar - shows
    // the whole catalogue instead, and says so, so the panel is never empty and
    // never silently pretends the style covered it.
    const auto styleHasNothing = suggestions.empty() && ! options.families.empty();

    if (styleHasNothing)
        suggestions = ScaleSuggester {}.suggestionsFor (*chord);

    const auto tones = chord->chordTones();

    return hold ("{\"ok\":true,\"chord\":" + quoted (chord->toString())
                 + ",\"root\":" + std::to_string (chord->root())
                 + ",\"styleHasNothing\":" + (styleHasNothing ? "true" : "false")
                 + ",\"tones\":" + jsonArray (tones, [&chord] (const ChordTone& tone)
                   { return chordToneJson (tone, chord->root()); })
                 + ",\"scales\":" + jsonArray (suggestions, scaleSuggestionJson) + "}");
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

std::string reharmStyles()
{
    return hold ("{\"ok\":true,\"styles\":"
                 + jsonArray (core::reharmStyles(), [] (const ReharmStyleDefinition& style)
                   {
                       return "{\"key\":" + quoted (style.key)
                            + ",\"name\":" + quoted (style.name)
                            + ",\"summary\":" + quoted (style.summary) + "}";
                   })
                 + "}");
}

/** Reharmonisation options for one measure of a progression. */
std::string reharmonise (const char* progressionText, int measureIndex,
                                         int includeAdvanced, int includeRisky,
                                         const char* styleKey)
{
    auto parsed = parseProgressionText (progressionText != nullptr ? progressionText : "");

    if (! parsed.ok())
        return hold (jsonError (parsed.error));

    Reharmonizer::Options options;
    options.includeAdvanced = includeAdvanced != 0;
    options.includeRisky = includeRisky != 0;

    /*  A key the engine does not know widens to everything rather than failing.
        A bar with no suggestions and no explanation is indistinguishable from a
        bar with no ideas, and this is a filter rather than a sound - nothing
        here can play the wrong thing under somebody. */
    options.style = core::styleFrom (styleKey != nullptr ? styleKey : "");

    const Reharmonizer reharmonizer { options };

    const auto substitutions = reharmonizer.substitutionsFor (*parsed.chart, measureIndex);
    const auto& chart = *parsed.chart;

    return hold ("{\"ok\":true,\"substitutions\":"
                 + jsonArray (substitutions, [&chart, measureIndex] (const Substitution& substitution)
                   {
                       const auto applied = Reharmonizer::applySubstitution (chart, measureIndex, substitution);

                       return "{\"name\":" + quoted (substitution.name)
                            + ",\"replacement\":" + quoted (substitution.replacementText())
                            + ",\"explanation\":" + quoted (substitution.explanation)
                            + ",\"difficulty\":" + quoted (difficultyName (substitution.difficulty))
                            + ",\"family\":" + quoted (familyName (substitution.family))
                            + ",\"risky\":"
                            + (substitution.difficulty == SubstitutionDifficulty::risky ? "true" : "false")
                            + ",\"worksHere\":"
                            + (substitution.voiceLeading.smoothHere ? "true" : "false")
                            + ",\"verdict\":" + quoted (substitution.voiceLeading.note)
                            + ",\"style\":" + quoted (styleName (substitution.style))
                            + ",\"voiceLeadingCost\":" + std::to_string (substitution.voiceLeadingCost)
                            + ",\"progression\":" + quoted (applied.toProgressionText()) + "}";
                   })
                 + "}");
}

/** Analyses a played voicing against a chord symbol, optionally as an exercise
    in one particular voicing shape.
*/
std::string analyseVoicing (const char* symbol, const char* midiNotesCsv,
                                            const char* practiseStyle)
{
    const auto chord = ChordSymbol::parse (symbol != nullptr ? symbol : "");

    if (! chord.has_value())
        return hold (jsonError (std::string ("Not a chord symbol: ") + (symbol != nullptr ? symbol : "")));

    const auto voicing = Voicing::fromNotes (parseNoteList (midiNotesCsv != nullptr ? midiNotesCsv : ""));

    VoicingAnalyzer::Options options;
    options.practiseType = practiseTypeFor (practiseStyle);

    const VoicingAnalyzer analyzer { options };
    const auto analysis = analyzer.analyse (voicing, *chord);

    return hold ("{\"ok\":true,\"summary\":" + quoted (analysis.summary)
                 + ",\"score\":" + std::to_string (analysis.score)
                 + ",\"matches\":" + (analysis.matchesChord ? "true" : "false")
                 + ",\"matchesStyle\":" + (analysis.matchesStyle ? "true" : "false")
                 + ",\"voicingType\":" + quoted (voicingTypeName (analysis.type))
                 + ",\"played\":" + quoted (voicing.describe())
                 + ",\"findings\":" + jsonArray (analysis.findings, [] (const VoicingFinding& finding)
                   {
                       return "{\"severity\":" + quoted (severityName (finding.severity))
                            + ",\"message\":" + quoted (finding.message) + "}";
                   })
                 + ",\"suggestions\":" + jsonArray (analysis.suggestions,
                                                    [] (const std::string& s) { return quoted (s); })
                 + ",\"examples\":" + jsonArray (analysis.examples, [] (const Voicing& example)
                   {
                       std::string notes = "[";

                       for (std::size_t i = 0; i < example.midiNotes.size(); ++i)
                           notes += (i > 0 ? "," : "") + std::to_string (example.midiNotes[i]);

                       return "{\"notes\":" + notes + "],\"describe\":" + quoted (example.describe()) + "}";
                   })
                 + "}");
}

namespace
{
    /** A parsed chart, handed back the way the page wants it. */
    std::string holdChart (const ChartParseResult& result)
    {
        if (! result.ok())
            return hold (jsonError (result.error));

        return hold ("{\"ok\":true,\"title\":" + quoted (result.chart->title)
                     + ",\"composer\":" + quoted (result.chart->composer)
                     + ",\"style\":" + quoted (result.chart->style)
                     // The readers have always pulled this out of an iReal Pro
                     // link and off a PDF; nothing ever asked them for it, so a
                     // waltz imported as a waltz was counted in four.
                     + ",\"beatsPerBar\":" + std::to_string (result.chart->timeSignature.numerator)
                     + ",\"beatUnit\":" + std::to_string (result.chart->timeSignature.denominator)
                     + ",\"bars\":" + std::to_string (result.chart->measureCount())
                     + ",\"unreadable\":" + jsonArray (result.unreadable, [] (const std::string& text)
                       {
                           return quoted (text);
                       })
                     + ",\"progression\":" + quoted (result.chart->toProgressionText()) + "}");
    }
}

/** Reads an iReal Pro link, or anything that looks like one. */
std::string importIRealPro (const char* text)
{
    return holdChart (core::importIRealPro (text != nullptr ? text : ""));
}

/** Writes the chart as an iReal Pro link.

    Composer and style travel with the chart so that reading a link and writing
    it back does not quietly lose who wrote the tune.
*/
std::string exportIRealPro (const char* progressionText, const char* title,
                                            const char* composer, const char* style,
                                            int beats, int beatUnit)
{
    auto parsed = parseProgressionText (progressionText != nullptr ? progressionText : "",
                                        title != nullptr ? title : "");

    if (! parsed.ok())
        return hold (jsonError (parsed.error));

    if (composer != nullptr)
        parsed.chart->composer = composer;

    if (style != nullptr && *style != '\0')
        parsed.chart->style = style;

    /*  The metre is the shell's to say. A progression text carries chords and
        barlines and nothing about how a bar is counted, so a chart rebuilt from
        one opens in four however it arrived - which is why a waltz imported and
        exported used to come back a waltz no longer.

        Nothing here is a judgement about what a good metre is: anything at or
        below zero means the caller did not know, and the text's own default
        stands. */
    if (beats > 0)
        parsed.chart->timeSignature.numerator = beats;

    if (beatUnit > 0)
        parsed.chart->timeSignature.denominator = beatUnit;

    return hold ("{\"ok\":true,\"link\":" + quoted (core::exportIRealPro (*parsed.chart)) + "}");
}

/** Rebuilds a chart from text pulled off a page.

    The page sends one run of text per line as "x<tab>y<tab>text"; pulling that
    text out of a PDF is the shell's job, and working out which of it is a chord
    chart is the engine's.
*/
std::string chartFromPage (const char* tabSeparatedItems)
{
    std::vector<PlacedText> items;
    const std::string input = tabSeparatedItems != nullptr ? tabSeparatedItems : "";

    std::size_t lineStart = 0;

    while (lineStart <= input.size())
    {
        const auto lineEnd = std::min (input.find ('\n', lineStart), input.size());
        const auto line = input.substr (lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;

        const auto firstTab = line.find ('\t');

        if (firstTab == std::string::npos)
            continue;

        const auto secondTab = line.find ('\t', firstTab + 1);

        if (secondTab == std::string::npos)
            continue;

        PlacedText item;

        try
        {
            item.x = std::stod (line.substr (0, firstTab));
            item.y = std::stod (line.substr (firstTab + 1, secondTab - firstTab - 1));
        }
        catch (...)
        {
            continue;   // a row we cannot read is a row we skip
        }

        // The width of the run is optional: a reader that knows it lets the
        // engine split words on real gaps rather than on a guessed threshold.
        const auto thirdTab = line.find ('\t', secondTab + 1);

        if (thirdTab == std::string::npos)
        {
            item.text = line.substr (secondTab + 1);
        }
        else
        {
            item.text = line.substr (secondTab + 1, thirdTab - secondTab - 1);

            try
            {
                item.width = std::stod (line.substr (thirdTab + 1));
            }
            catch (...)
            {
                item.width = 0.0;
            }
        }

        if (! item.text.empty())
            items.push_back (std::move (item));
    }

    return holdChart (chartFromPlacedText (std::move (items)));
}

/** Every whole-tune reharmonisation on offer, lightest touch first. */
std::string reharmPlans (const char* progressionText)
{
    auto parsed = parseProgressionText (progressionText != nullptr ? progressionText : "");

    if (! parsed.ok())
        return hold (jsonError (parsed.error));

    const auto plans = reharmPlansFor (*parsed.chart);

    return hold ("{\"ok\":true,\"plans\":"
                 + jsonArray (plans, [] (const ReharmPlan& plan)
                   {
                       return "{\"name\":" + quoted (plan.name)
                            + ",\"description\":" + quoted (plan.description)
                            + ",\"barsChanged\":" + std::to_string (plan.barsChanged())
                            + ",\"progression\":" + quoted (plan.chart.toProgressionText())
                            + ",\"moves\":" + jsonArray (plan.moves, [] (const PlannedMove& move)
                              {
                                  return "{\"bar\":" + std::to_string (move.measureIndex + 1)
                                       + ",\"index\":" + std::to_string (move.measureIndex)
                                       + ",\"before\":" + quoted (move.before)
                                       + ",\"after\":" + quoted (move.after)
                                       + ",\"substitution\":" + quoted (move.substitution)
                                       + ",\"family\":" + quoted (familyName (move.family)) + "}";
                              })
                            + "}";
                   })
                 + "}");
}

std::string voicedGuideTones (const char* symbol, const char* midiNotesCsv)
{
    const auto chord = ChordSymbol::parse (symbol != nullptr ? symbol : "");

    if (! chord.has_value())
        return hold (jsonError (std::string ("Not a chord symbol: ") + (symbol != nullptr ? symbol : "")));

    const auto played = parseNoteList (midiNotesCsv != nullptr ? midiNotesCsv : "");
    const auto voiced = core::voiceGuideTones (*chord, played);

    return hold ("{\"ok\":true,\"chord\":" + quoted (chord->toString())
                 + ",\"tones\":" + jsonArray (voiced, [] (const core::VoicedGuideTone& tone)
                   {
                       return "{\"note\":" + std::to_string (tone.note)
                            + ",\"label\":" + quoted (tone.label)
                            + ",\"from\":" + std::to_string (tone.from)
                            + ",\"semitones\":" + std::to_string (tone.semitones) + "}";
                   })
                 + "}");
}

/** Names the notes currently held down, with no chart and no expected chord. */
std::string identifyChord (const char* midiNotesCsv)
{
    const auto voicing = Voicing::fromNotes (parseNoteList (midiNotesCsv != nullptr ? midiNotesCsv : ""));
    const ChordIdentifier identifier;
    const auto candidates = identifier.identify (voicing);

    return hold ("{\"ok\":true,\"played\":" + quoted (voicing.describe())
                 + ",\"readings\":" + jsonArray (candidates, [] (const ChordCandidate& candidate)
                   {
                       return "{\"chord\":" + quoted (candidate.chord.toString())
                            + ",\"score\":" + std::to_string (candidate.score)
                            + ",\"rootInBass\":" + (candidate.rootInBass ? "true" : "false")
                            + ",\"rootPlayed\":" + (candidate.rootPlayed ? "true" : "false")
                            + ",\"omitted\":" + jsonArray (candidate.omittedTones,
                                                            [] (const std::string& label) { return quoted (label); })
                            + "}";
                   })
                 + "}");
}

/** Reads a played voicing against the substitutions available for a measure,
    so a player who stumbles onto a reharmonisation is told what they found.
*/
std::string recogniseSubstitution (const char* progressionText,
                                                   int measureIndex,
                                                   const char* midiNotesCsv,
                                                   int includeAdvanced)
{
    auto parsed = parseProgressionText (progressionText != nullptr ? progressionText : "");

    if (! parsed.ok())
        return hold (jsonError (parsed.error));

    const auto voicing = Voicing::fromNotes (parseNoteList (midiNotesCsv != nullptr ? midiNotesCsv : ""));
    Reharmonizer::Options options;
    options.includeAdvanced = includeAdvanced != 0;
    const auto found = core::recogniseSubstitution (voicing, *parsed.chart, measureIndex, options);

    if (! found.has_value())
        return hold ("{\"ok\":true,\"found\":false}");

    const auto applied = Reharmonizer::applySubstitution (*parsed.chart, measureIndex, found->substitution);

    return hold ("{\"ok\":true,\"found\":true"
                 + std::string (",\"chord\":") + quoted (found->chord.toString())
                 + ",\"name\":" + quoted (found->substitution.name)
                 + ",\"family\":" + quoted (familyName (found->substitution.family))
                 + ",\"difficulty\":" + quoted (difficultyName (found->substitution.difficulty))
                 + ",\"explanation\":" + quoted (found->substitution.explanation)
                 + ",\"replacement\":" + quoted (found->substitution.replacementText())
                 + ",\"progression\":" + quoted (applied.toProgressionText())
                 + ",\"score\":" + std::to_string (found->score) + "}");
}

std::string voicingShape (const char* symbol, const char* midiNotesCsv)
{
    const auto chord = ChordSymbol::parse (symbol != nullptr ? symbol : "");

    if (! chord.has_value())
        return hold (jsonError ("Not a chord symbol"));

    /*  Every key the engine knows, so a shell dropping a stored shape it can no
        longer place is checking against this rather than against a copy. */
    const auto qualities = jsonArray (core::allChordQualities(), [] (ChordQuality quality)
                                      { return quoted (qualityKey (quality)); });

    const auto voicing = Voicing::fromNotes (parseNoteList (midiNotesCsv != nullptr ? midiNotesCsv : ""));
    const auto shape = shapeOf (voicing, *chord);

    return hold ("{\"ok\":true,\"qualities\":" + qualities
                 + ",\"quality\":" + quoted (qualityKey (chord->quality()))
                 + ",\"qualityName\":" + quoted (qualityName (chord->quality()))
                 + ",\"offsets\":" + jsonArray (shape.offsets, [] (int offset)
                                                { return std::to_string (offset); })
                 + ",\"anchor\":" + std::to_string (shape.anchorNote) + "}");
}

std::string voicingFromShape (const char* symbol, const char* offsetsCsv, int anchorNote)
{
    const auto chord = ChordSymbol::parse (symbol != nullptr ? symbol : "");

    if (! chord.has_value())
        return hold (jsonError ("Not a chord symbol"));

    VoicingShape shape;
    shape.quality = chord->quality();
    shape.offsets = parseNoteList (offsetsCsv != nullptr ? offsetsCsv : "");
    shape.anchorNote = anchorNote;

    const auto voicing = core::voicingFromShape (*chord, shape);

    return hold ("{\"ok\":true,\"notes\":"
                 + jsonArray (voicing.midiNotes, [] (int note) { return std::to_string (note); })
                 + ",\"describe\":" + quoted (voicing.describe()) + "}");
}

/** Idiomatic "sentence starter" voicings for a chord.

    Each shape has a register it belongs in - a solo left hand lives an octave
    and a half below a rootless one - so the anchor comes from the type rather
    than from the caller unless the caller asks for a particular one.
*/
std::string idiomaticVoicings (const char* symbol, int anchorNote,
                                               const char* practiseStyle, int rich)
{
    const auto chord = ChordSymbol::parse (symbol != nullptr ? symbol : "");

    if (! chord.has_value())
        return hold (jsonError ("Not a chord symbol"));

    const std::vector<std::pair<std::string, VoicingType>> allTypes {
        { "Shell",              VoicingType::shell },
        { "Rootless left hand", VoicingType::rootlessLeftHand },
        { "Two-handed",         VoicingType::twoHandedRootless },
        { "Solo",               VoicingType::solo },
        { "Root position",      VoicingType::rootPosition }
    };

    const auto density = rich != 0 ? VoicingDensity::rich : VoicingDensity::plain;

    // With a shape being practised, only that shape is worth showing.
    const auto wanted = practiseTypeFor (practiseStyle);
    std::vector<std::pair<std::string, VoicingType>> types;

    for (const auto& entry : allTypes)
        if (! wanted.has_value() || entry.second == *wanted)
            types.push_back (entry);

    return hold ("{\"ok\":true,\"voicings\":"
                 + jsonArray (types, [&chord, anchorNote, density] (const std::pair<std::string, VoicingType>& entry)
                   {
                       const auto anchor = anchorNote > 0 ? anchorNote : naturalAnchorFor (entry.second);
                       const auto voicings = core::idiomaticVoicings (*chord, entry.second, anchor, density);

                       return "{\"type\":" + quoted (entry.first) + ",\"options\":"
                            + jsonArray (voicings, [] (const Voicing& voicing)
                              {
                                  std::string notes = "[";

                                  for (std::size_t i = 0; i < voicing.midiNotes.size(); ++i)
                                      notes += (i > 0 ? "," : "") + std::to_string (voicing.midiNotes[i]);

                                  notes += "]";

                                  return "{\"notes\":" + notes + ",\"describe\":" + quoted (voicing.describe()) + "}";
                              })
                            + "}";
                   })
                 + "}");
}

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

/** A style written as one line of flat text, ready to hand straight back.

    The inverse of the reader that `compPlan` and its two neighbours use, and
    the reason this crosses at all: a test that wanted to prove the two agree
    would otherwise have to hand-write four descriptions, which is a second
    copy of the catalogue living in a fixture. Emitting it means the round
    trip can be asserted against the engine's own answer.

    Flat text rather than JSON because that is the direction this wire runs.
    Results are JSON, because encoding them is the shell's business and every
    shell has a parser; inputs are delimited text, because the engine has no
    JSON reader and `EngineApi.cpp` says at the top that it should not grow
    one. `parseNoteList`, `readHit` and `readHitList` are the precedent.

    Three delimiters, no escaping needed: `|` between the header fields, `;`
    between slots, `:` inside one. Nothing here is free text - `key`, `name`
    and `summary` are deliberately **not** carried, because the engine needs
    none of the three to plan or to grade a bar. It only ever echoed them
    back. What a player calls their own style is the page's business, and
    leaving the strings out is what keeps this grammar free of quoting.
*/
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

    /** A number the engine may not have. Null rather than zero: nothing played
        is not nought out of a hundred, and a page drawing a nought would say
        exactly the thing the engine was careful not to. */
    std::string orNull (const std::optional<int>& value)
    {
        return value.has_value() ? std::to_string (*value) : std::string ("null");
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

//==============================================================================
std::string soloStartTake()
{
    soloTake().startTake();

    return hold ("{\"ok\":true,\"taking\":true," + lineStatsJson (soloTake().stats()) + "}");
}

std::string soloSetBar (int measureIndex, const char* symbol, const char* chosenScale,
                        const char* style, int beatsPerBar)
{
    const auto chord = ChordSymbol::parse (symbol != nullptr ? symbol : "");

    if (! chord.has_value())
        return hold (jsonError (std::string ("Not a chord symbol: ") + (symbol != nullptr ? symbol : "")));

    LineAnalyzer::Options options;
    options.chosenScale = chosenScale != nullptr ? chosenScale : "";
    options.style = style != nullptr ? style : "";

    // Which beats are strong is the metre's business, and the metre is the
    // chart's. Nothing reads it unless the shell also sends positions.
    options.beatsPerBar = beatsPerBar > 0 ? beatsPerBar : 4;

    auto& analyzer = soloTake();
    analyzer.setOptions (options);
    analyzer.setTarget (measureIndex, *chord);

    // Clicking a bar is how the player asks what they have done on it, so the
    // answer comes back with the move rather than needing a call of its own.
    return hold ("{\"ok\":true,\"taking\":" + std::string (analyzer.isTaking() ? "true" : "false")
                 + ",\"bar\":" + lineBarJson (measureIndex, chord->toString(),
                                              analyzer.statsForBar (measureIndex))
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
                 + ",\"bar\":" + lineBarJson (note.measureIndex, note.chordSymbol,
                                              analyzer.statsForBar (note.measureIndex))
                 + ",\"bars\":" + jsonArray (changed, [&analyzer] (const LineNote& touched)
                   { return lineBarJson (touched.measureIndex, touched.chordSymbol,
                                         analyzer.statsForBar (touched.measureIndex)); })
                 + ",\"take\":{" + lineStatsJson (analyzer.stats()) + "}}");
}

std::string soloEndTake()
{
    auto& analyzer = soloTake();
    analyzer.endTake();

    const auto take = analyzer.summary();

    std::vector<std::string> symbols;

    for (const auto& bar : take.bars)
        symbols.push_back (bar.chordSymbol);

    unsigned int qualities = 0;
    unsigned int roots = 0;
    coverageOf (symbols, qualities, roots);

    return hold ("{\"ok\":true,\"taking\":false" + coverageJson (qualities, roots)
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
                   { return lineBarJson (bar.measureIndex, bar.chordSymbol, bar.stats,
                                         bar.neverLeftTheChord); })
                 + "}");
}

} // namespace jazz::api
