// A hand on the keys, read against the chord under it, as JSON.
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

#include "jazz/core/ChordSymbol.h"
// `voiceGuideTones` lives with the reharmoniser rather than with the voicings:
// it answers where the *next* chord's guide tones are for the hand holding this
// one, which is a question about harmony moving.
#include "jazz/core/Reharmonizer.h"
#include "jazz/core/Voicing.h"
#include "jazz/core/VoicingAnalyzer.h"

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

} // namespace jazz::api
