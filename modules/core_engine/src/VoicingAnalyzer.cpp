#include "jazz/core/VoicingAnalyzer.h"

#include <algorithm>

namespace jazz::core
{

namespace
{
    bool isRootless (VoicingType type)
    {
        return type == VoicingType::rootlessLeftHand || type == VoicingType::twoHandedRootless;
    }

    std::string noteList (const std::vector<int>& midiNotes)
    {
        std::string result;

        for (std::size_t i = 0; i < midiNotes.size(); ++i)
        {
            if (i > 0)
                result += ", ";

            result += midiNoteName (midiNotes[i]);
        }

        return result;
    }
}

VoicingType VoicingAnalyzer::classify (const Voicing& voicing, const ChordSymbol& chord)
{
    if (voicing.isEmpty())
        return VoicingType::unknown;

    if (voicing.size() == 1)
        return VoicingType::singleNote;

    const auto rootPresent = voicing.containsPitchClass (chord.root());
    const auto bassPitchClass = toPitchClass (voicing.lowestNote());
    const auto bassIsRoot = bassPitchClass == toPitchClass (chord.bass().value_or (chord.root()));

    if (! rootPresent)
    {
        // Two hands show up as a gap wider than a hand can span, or as a
        // voicing too wide for one hand - not merely as crossing middle C.
        auto widestGap = 0;

        for (std::size_t i = 1; i < voicing.midiNotes.size(); ++i)
            widestGap = std::max (widestGap, voicing.midiNotes[i] - voicing.midiNotes[i - 1]);

        if (voicing.size() >= 4 && (widestGap >= 7 || voicing.spanInSemitones() > 16))
            return VoicingType::twoHandedRootless;

        return VoicingType::rootlessLeftHand;
    }

    if (voicing.size() == 3 && bassIsRoot)
    {
        // Root plus two notes that are both guide tones is the classic shell.
        const auto guides = chord.guideTones();
        const auto isGuide = [&] (int note)
        {
            return std::any_of (guides.begin(), guides.end(), [&] (const ChordTone& tone)
                                { return toPitchClass (chord.root() + tone.semitones) == toPitchClass (note); });
        };

        if (isGuide (voicing.midiNotes[1]) && isGuide (voicing.midiNotes[2]))
            return VoicingType::shell;
    }

    if (voicing.spanInSemitones() > 19)
        return VoicingType::spread;

    return bassIsRoot ? VoicingType::rootPosition : VoicingType::spread;
}

VoicingAnalysis VoicingAnalyzer::analyse (const Voicing& voicing, const ChordSymbol& chord) const
{
    VoicingAnalysis analysis;
    analysis.type = classify (voicing, chord);

    if (voicing.isEmpty())
    {
        analysis.summary = "Nothing played yet.";
        return analysis;
    }

    const auto rootless = isRootless (analysis.type);
    const auto primaryScale = suggester.primarySuggestionFor (chord);
    const auto scaleMask = primaryScale.scale.definition != nullptr ? primaryScale.scale.pitchClassMask() : 0;
    const auto avoidNotes = primaryScale.avoidNotes;

    auto score = 100;
    auto missingGuideTone = false;
    auto hasOutsideNote = false;
    auto hasClash = false;

    // --- tones the symbol asks for that are not in the voicing ---------------
    for (const auto& tone : chord.essentialTones())
    {
        const auto pitchClass = toPitchClass (chord.root() + tone.semitones);

        if (voicing.containsPitchClass (pitchClass))
            continue;

        if (tone.role == ChordToneRole::root && rootless && ! options.requireRoot)
        {
            analysis.findings.push_back ({ FindingSeverity::good,
                                           "No root - correct for a " + voicingTypeName (analysis.type)
                                               + "; the bass covers it.",
                                           {} });
            continue;
        }

        analysis.missingTones.push_back (tone);

        if (tone.role == ChordToneRole::third || tone.role == ChordToneRole::seventh
            || tone.role == ChordToneRole::sixth)
        {
            missingGuideTone = true;
            score -= 35;
            analysis.findings.push_back ({ FindingSeverity::problem,
                                           "Missing the " + tone.label + " (" + pitchClassName (pitchClass)
                                               + ") - that note is what makes this chord "
                                               + chord.toString() + ".",
                                           {} });
        }
        else if (tone.role == ChordToneRole::root)
        {
            score -= 15;
            analysis.findings.push_back ({ FindingSeverity::suggestion,
                                           "No root (" + pitchClassName (pitchClass)
                                               + ") - fine with a bass player, thin without one.",
                                           {} });
        }
        else
        {
            score -= 20;
            analysis.findings.push_back ({ FindingSeverity::problem,
                                           "Missing the " + tone.label + " ("
                                               + pitchClassName (pitchClass) + ").",
                                           {} });
        }
    }

    // --- notes in the voicing that the chord did not ask for ------------------
    for (auto note : voicing.midiNotes)
    {
        const auto pitchClass = toPitchClass (note);

        if (chord.containsPitchClass (pitchClass))
            continue;

        const auto label = intervalLabel (ascendingInterval (chord.root(), pitchClass), chord.hasMinorThird());
        const auto inScale = (scaleMask & (1u << pitchClass)) != 0;
        const auto isAvoid = std::find (avoidNotes.begin(), avoidNotes.end(), pitchClass) != avoidNotes.end();

        if (! inScale)
        {
            hasOutsideNote = true;
            score -= 30;
            analysis.outsideNotes.push_back (note);
            analysis.findings.push_back ({ FindingSeverity::problem,
                                           midiNoteName (note) + " (" + label + ") is outside "
                                               + chord.toString() + " and outside "
                                               + primaryScale.scale.name() + ".",
                                           { note } });
        }
        else if (isAvoid)
        {
            hasClash = true;
            score -= 20;
            analysis.findings.push_back ({ FindingSeverity::problem,
                                           midiNoteName (note) + " (" + label
                                               + ") sits a semitone above a chord tone - it clashes when held.",
                                           { note } });
        }
        else
        {
            // An unnamed tension that fits the scale is colour, not an error.
            analysis.findings.push_back ({ FindingSeverity::good,
                                           midiNoteName (note) + " adds the " + label + " - good colour.",
                                           { note } });
        }
    }

    // --- register and spacing -------------------------------------------------
    for (std::size_t i = 1; i < voicing.midiNotes.size(); ++i)
    {
        const auto lower = voicing.midiNotes[i - 1];
        const auto interval = voicing.midiNotes[i] - lower;

        // The low-interval limit widens as the interval narrows: a major 3rd is
        // playable lower than a minor 3rd, which is playable lower than a 2nd.
        const auto limitForInterval = interval <= 2 ? options.lowIntervalLimit + 4
                                    : interval == 3 ? options.lowIntervalLimit
                                                    : options.lowIntervalLimit - 2;

        if (interval > 0 && interval <= 4 && lower < limitForInterval)
        {
            score -= 8;
            analysis.findings.push_back ({ FindingSeverity::suggestion,
                                           "The " + std::to_string (interval) + "-semitone gap at "
                                               + midiNoteName (lower)
                                               + " is below the low-interval limit - it will sound muddy. "
                                                 "Open it up or move the voicing up an octave.",
                                           { lower, voicing.midiNotes[i] } });
            break;
        }
    }

    if (const auto doubled = voicing.doubledNotes(); ! doubled.empty() && rootless)
    {
        score -= 5;
        analysis.findings.push_back ({ FindingSeverity::suggestion,
                                       "Doubling " + noteList (doubled)
                                           + " - that finger could carry a tension instead.",
                                       doubled });
    }

    // --- colour tones the symbol names and the player left out ---------------
    for (auto extension : chord.extensions())
    {
        const auto pitchClass = toPitchClass (chord.root() + semitonesAboveRoot (extension));

        if (! voicing.containsPitchClass (pitchClass)
            && std::none_of (analysis.missingTones.begin(), analysis.missingTones.end(),
                             [&] (const ChordTone& t) { return toPitchClass (chord.root() + t.semitones) == pitchClass; }))
        {
            analysis.suggestions.push_back ("Try adding the " + extensionLabel (extension) + " ("
                                            + pitchClassName (pitchClass) + ") - the symbol asks for it.");
        }
    }

    // Problems first: a feedback panel that runs out of room must never drop
    // the note that actually needs fixing in favour of a confirmation.
    std::stable_sort (analysis.findings.begin(), analysis.findings.end(),
                      [] (const VoicingFinding& a, const VoicingFinding& b)
                      { return static_cast<int> (a.severity) > static_cast<int> (b.severity); });

    analysis.matchesChord = ! missingGuideTone && ! hasOutsideNote && ! hasClash;
    analysis.score = std::clamp (score, 0, 100);

    if (analysis.type == VoicingType::singleNote)
        analysis.summary = "One note - play a full voicing to get feedback on it.";
    else if (analysis.matchesChord)
        analysis.summary = "That reads as " + chord.toString() + " - "
                           + voicingTypeName (analysis.type) + ".";
    else if (missingGuideTone)
        analysis.summary = "Close, but this does not yet say " + chord.toString() + ".";
    else
        analysis.summary = "Recognisable as " + chord.toString() + ", with notes to clean up.";

    if (options.includeExamples && analysis.score < 85)
    {
        const auto anchor = voicing.lowestNote() > 0 ? voicing.lowestNote() : 53;
        analysis.examples = idiomaticVoicings (chord, analysis.type, anchor);

        for (const auto& example : analysis.examples)
            analysis.suggestions.push_back ("Try: " + example.describe());
    }

    return analysis;
}

} // namespace jazz::core
