// WebAssembly bindings for the Core Engine.
//
// This is a platform shell, exactly like app/ is: it owns none of the theory,
// it only marshals between the engine and its host. The host here happens to be
// a browser rather than JUCE, which is possible precisely because the engine
// links no JUCE - the same property that keeps an AUv3/VST3 target open.
//
// Every entry point takes and returns plain strings, so the JavaScript side
// needs no knowledge of the C++ types.

#include "jazz/core/Chart.h"
#include "jazz/core/Reharmonizer.h"
#include "jazz/core/ScaleSuggester.h"
#include "jazz/core/VoicingAnalyzer.h"

#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
 #include <emscripten/emscripten.h>
 #define JAZZ_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
 #define JAZZ_EXPORT extern "C"
#endif

using namespace jazz::core;

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

    /** Each entry point owns one buffer; JavaScript copies the string out before
        the next call, which is how Emscripten's UTF8ToString works anyway.
    */
    const char* hold (std::string&& json)
    {
        static std::string buffer;
        buffer = std::move (json);
        return buffer.c_str();
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
JAZZ_EXPORT const char* jazzParseChart (const char* progressionText)
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
JAZZ_EXPORT const char* jazzScalesForChord (const char* symbol)
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
JAZZ_EXPORT const char* jazzReharmonise (const char* progressionText, int measureIndex, int includeAdvanced)
{
    auto parsed = parseProgressionText (progressionText != nullptr ? progressionText : "");

    if (! parsed.ok())
        return hold (jsonError (parsed.error));

    const Reharmonizer reharmonizer {
        Reharmonizer::Options { includeAdvanced != 0, ReharmStyle::common }
    };

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
                            + ",\"style\":" + quoted (styleName (substitution.style))
                            + ",\"voiceLeadingCost\":" + std::to_string (substitution.voiceLeadingCost)
                            + ",\"progression\":" + quoted (applied.toProgressionText()) + "}";
                   })
                 + "}");
}

/** Analyses a played voicing against a chord symbol, optionally as an exercise
    in one particular voicing shape.
*/
JAZZ_EXPORT const char* jazzAnalyseVoicing (const char* symbol, const char* midiNotesCsv,
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

/** Reads a played voicing against the substitutions available for a measure,
    so a player who stumbles onto a reharmonisation is told what they found.
*/
JAZZ_EXPORT const char* jazzRecogniseSubstitution (const char* progressionText,
                                                   int measureIndex,
                                                   const char* midiNotesCsv,
                                                   int includeAdvanced)
{
    auto parsed = parseProgressionText (progressionText != nullptr ? progressionText : "");

    if (! parsed.ok())
        return hold (jsonError (parsed.error));

    const auto voicing = Voicing::fromNotes (parseNoteList (midiNotesCsv != nullptr ? midiNotesCsv : ""));
    const Reharmonizer::Options options { includeAdvanced != 0, ReharmStyle::common };
    const auto found = recogniseSubstitution (voicing, *parsed.chart, measureIndex, options);

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

/** Idiomatic "sentence starter" voicings for a chord. */
JAZZ_EXPORT const char* jazzIdiomaticVoicings (const char* symbol, int anchorNote,
                                               const char* practiseStyle)
{
    const auto chord = ChordSymbol::parse (symbol != nullptr ? symbol : "");

    if (! chord.has_value())
        return hold (jsonError ("Not a chord symbol"));

    const std::vector<std::pair<std::string, VoicingType>> allTypes {
        { "Shell",              VoicingType::shell },
        { "Rootless left hand", VoicingType::rootlessLeftHand },
        { "Two-handed",         VoicingType::twoHandedRootless },
        { "Root position",      VoicingType::rootPosition }
    };

    // With a shape being practised, only that shape is worth showing.
    const auto wanted = practiseTypeFor (practiseStyle);
    std::vector<std::pair<std::string, VoicingType>> types;

    for (const auto& entry : allTypes)
        if (! wanted.has_value() || entry.second == *wanted)
            types.push_back (entry);

    return hold ("{\"ok\":true,\"voicings\":"
                 + jsonArray (types, [&chord, anchorNote] (const std::pair<std::string, VoicingType>& entry)
                   {
                       const auto voicings = idiomaticVoicings (*chord, entry.second, anchorNote);

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
