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
#include "jazz/core/LineAnalyzer.h"
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

        const auto flush = [&notes, &current]
        {
            if (! current.empty())
            {
                notes.push_back (std::stoi (current));
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

    std::string lineBarJson (int measureIndex, const std::string& symbol, const LineStats& stats,
                             bool neverLeftTheChord = false)
    {
        return "{\"index\":" + std::to_string (measureIndex)
             + ",\"chord\":" + quoted (symbol)
             + ",\"neverLeftTheChord\":" + (neverLeftTheChord ? "true" : "false")
             + "," + lineStatsJson (stats) + "}";
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
             + ",\"wantsToReach\":" + quoted (note.wantsToReach > 0 ? midiNoteName (note.wantsToReach) : "")
             + ",\"wantsToReachDegree\":" + quoted (note.wantsToReachDegree)
             + ",\"wantsToReachStep\":" + std::to_string (note.wantsToReach > 0
                                                          ? note.wantsToReach - note.midiNote : 0)
             + ",\"chord\":" + quoted (note.chordSymbol)
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

/** Reharmonisation options for one measure of a progression. */
std::string reharmonise (const char* progressionText, int measureIndex,
                                         int includeAdvanced, int includeRisky)
{
    auto parsed = parseProgressionText (progressionText != nullptr ? progressionText : "");

    if (! parsed.ok())
        return hold (jsonError (parsed.error));

    Reharmonizer::Options options;
    options.includeAdvanced = includeAdvanced != 0;
    options.includeRisky = includeRisky != 0;

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
                                            const char* composer, const char* style)
{
    auto parsed = parseProgressionText (progressionText != nullptr ? progressionText : "",
                                        title != nullptr ? title : "");

    if (! parsed.ok())
        return hold (jsonError (parsed.error));

    if (composer != nullptr)
        parsed.chart->composer = composer;

    if (style != nullptr && *style != '\0')
        parsed.chart->style = style;

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

//==============================================================================
std::string soloStartTake()
{
    soloTake().startTake();

    return hold ("{\"ok\":true,\"taking\":true," + lineStatsJson (soloTake().stats()) + "}");
}

std::string soloSetBar (int measureIndex, const char* symbol, const char* chosenScale,
                        const char* style)
{
    const auto chord = ChordSymbol::parse (symbol != nullptr ? symbol : "");

    if (! chord.has_value())
        return hold (jsonError (std::string ("Not a chord symbol: ") + (symbol != nullptr ? symbol : "")));

    LineAnalyzer::Options options;
    options.chosenScale = chosenScale != nullptr ? chosenScale : "";
    options.style = style != nullptr ? style : "";

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

std::string soloPlayNote (int midiNote)
{
    auto& analyzer = soloTake();
    const auto note = analyzer.play (midiNote);
    const auto& resolved = analyzer.resolvedByLastNote();
    const auto& stranded = analyzer.strandedByLastNote();

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
                                   + ",\"midi\":" + std::to_string (earlier.midiNote) + "}";
                          });
    };

    return hold ("{\"ok\":true,\"taking\":" + std::string (analyzer.isTaking() ? "true" : "false")
                 + ",\"note\":" + lineNoteJson (note)
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

    return hold ("{\"ok\":true,\"taking\":false"
                 + std::string (",\"summary\":") + quoted (take.summary)
                 + ",\"observations\":" + jsonArray (take.observations,
                                                     [] (const std::string& line) { return quoted (line); })
                 + ",\"take\":{" + lineStatsJson (take.overall) + "}"
                 + ",\"leaps\":" + std::to_string (take.leaps)
                 + ",\"leapsResolved\":" + std::to_string (take.leapsResolved)
                 + ",\"range\":" + std::to_string (take.rangeInSemitones())
                 + ",\"bars\":" + jsonArray (take.bars, [] (const LineBar& bar)
                   { return lineBarJson (bar.measureIndex, bar.chordSymbol, bar.stats,
                                         bar.neverLeftTheChord); })
                 + "}");
}

} // namespace jazz::api
