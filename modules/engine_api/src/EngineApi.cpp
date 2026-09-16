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
#include "jazz/core/Reharmonizer.h"
#include "jazz/core/ScaleSuggester.h"
#include "jazz/core/VoicingAnalyzer.h"

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
std::string scalesForChord (const char* symbol)
{
    const auto chord = ChordSymbol::parse (symbol != nullptr ? symbol : "");

    if (! chord.has_value())
        return hold (jsonError (std::string ("Not a chord symbol: ") + (symbol != nullptr ? symbol : "")));

    const ScaleSuggester suggester;
    const auto suggestions = suggester.suggestionsFor (*chord);
    const auto tones = chord->chordTones();

    return hold ("{\"ok\":true,\"chord\":" + quoted (chord->toString())
                 + ",\"root\":" + std::to_string (chord->root())
                 + ",\"tones\":" + jsonArray (tones, [&chord] (const ChordTone& tone)
                   { return chordToneJson (tone, chord->root()); })
                 + ",\"scales\":" + jsonArray (suggestions, scaleSuggestionJson) + "}");
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

} // namespace jazz::api
