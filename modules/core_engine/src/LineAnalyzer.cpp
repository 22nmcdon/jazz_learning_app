#include "jazz/core/LineAnalyzer.h"

#include "jazz/core/Pitch.h"

#include <algorithm>
#include <array>
#include <numeric>

namespace jazz::core
{

namespace
{
    /** Rounded percentages of @p counts that add up to exactly 100.

        Rounding each share on its own gives three numbers that make 99 or 101
        often enough to be noticed - 0.505 and 0.495 both round up. So: take
        every whole percent, then hand the points left over to whichever shares
        were cut shortest. Standard largest-remainder, and the only reason it is
        here is that a panel reading "34% / 52% / 15%" looks like a bug.
    */
    std::array<int, 3> sharesOfOneHundred (const std::array<int, 3>& counts)
    {
        const auto total = counts[0] + counts[1] + counts[2];

        if (total <= 0)
            return { 0, 0, 0 };

        std::array<int, 3> whole {};
        std::array<int, 3> remainder {};

        for (std::size_t i = 0; i < counts.size(); ++i)
        {
            whole[i] = counts[i] * 100 / total;
            remainder[i] = counts[i] * 100 % total;
        }

        auto spare = 100 - (whole[0] + whole[1] + whole[2]);

        while (spare > 0)
        {
            const auto largest = static_cast<std::size_t> (
                std::distance (remainder.begin(), std::max_element (remainder.begin(), remainder.end())));

            whole[largest] += 1;
            remainder[largest] = -1;   // taken: do not let it win twice
            --spare;
        }

        return whole;
    }

    /** Reads one note against a chord and the scales that fit it.

        The scales arrive ordered best-first, so the first one accounting for a
        note is the best reading of it - with one preference on top: a scale
        where the note is not an avoid note is named ahead of one where it is.
        The note is a scale tone either way; this only decides which scale gets
        the credit, and "the 9th of C Lydian" is more use than "the 4th you
        should not sit on" when a perfectly good reading exists.
    */
    LineNote readAgainst (int midiNote,
                          int measureIndex,
                          const ChordSymbol& chord,
                          const std::vector<ScaleSuggestion>& scales)
    {
        LineNote note;
        note.midiNote = midiNote;
        note.measureIndex = measureIndex;
        note.chordSymbol = chord.toString();

        const auto pitch = toPitchClass (midiNote);
        note.degree = intervalLabel (ascendingInterval (chord.root(), pitch), chord.hasMinorThird());

        if (chord.containsPitchClass (pitch))
        {
            note.colour = NoteColour::chordTone;
            return note;
        }

        const ScaleSuggestion* fallback = nullptr;

        for (const auto& suggestion : scales)
        {
            if (! suggestion.scale.contains (pitch))
                continue;

            const auto& avoid = suggestion.avoidNotes;
            const auto isAvoid = std::find (avoid.begin(), avoid.end(), pitch) != avoid.end();

            if (isAvoid)
            {
                if (fallback == nullptr)
                    fallback = &suggestion;

                continue;
            }

            note.colour = NoteColour::scaleTone;
            note.scaleName = suggestion.scale.name (chord.accidental());
            return note;
        }

        if (fallback != nullptr)
        {
            note.colour = NoteColour::scaleTone;
            note.scaleName = fallback->scale.name (chord.accidental());
            note.avoidNote = true;
            return note;
        }

        note.colour = NoteColour::outside;
        return note;
    }

    /** The scales a note will be read against, best-first. */
    std::vector<ScaleSuggestion> scalesFor (const ChordSymbol& chord,
                                            const LineAnalyzer::Options& options)
    {
        const ScaleSuggester suggester;
        auto suggestions = suggester.suggestionsFor (chord);

        if (options.acceptAnyValidScale || suggestions.empty())
            return suggestions;

        if (! options.chosenScale.empty())
        {
            for (const auto& suggestion : suggestions)
                if (suggestion.scale.name() == options.chosenScale
                    || suggestion.scale.name (Accidental::sharps) == options.chosenScale)
                    return { suggestion };
        }

        // No choice made, or a name from some other chord's list: the engine's
        // own first answer, which is what the panel shows by default anyway.
        return { suggestions.front() };
    }

    std::string plural (int count, const std::string& singular, const std::string& many)
    {
        return std::to_string (count) + " " + (count == 1 ? singular : many);
    }
}

//==============================================================================
int LineStats::percentChordTones() const noexcept
{
    return sharesOfOneHundred ({ chordTones, scaleTones, outside })[0];
}

int LineStats::percentScaleTones() const noexcept
{
    return sharesOfOneHundred ({ chordTones, scaleTones, outside })[1];
}

int LineStats::percentOutside() const noexcept
{
    return sharesOfOneHundred ({ chordTones, scaleTones, outside })[2];
}

std::string noteColourName (NoteColour colour)
{
    switch (colour)
    {
        case NoteColour::chordTone: return "chord tone";
        case NoteColour::scaleTone: return "scale tone";
        case NoteColour::outside:   break;
    }

    return "outside";
}

//==============================================================================
LineNote LineAnalyzer::read (int midiNote, const ChordSymbol& chord, Options options)
{
    return readAgainst (midiNote, 0, chord, scalesFor (chord, options));
}

void LineAnalyzer::setOptions (Options newOptions)
{
    options = std::move (newOptions);

    // The bar in front of the player is read the new way immediately; the notes
    // behind them keep the reading they were given.
    if (target.has_value())
        setTarget (target->measureIndex, target->chord);
}

void LineAnalyzer::startTake()
{
    played.clear();
    taking = true;
}

void LineAnalyzer::endTake()
{
    // The notes stay. Disarming freezes a summary to read, so throwing the take
    // away at exactly the moment it becomes worth looking at would be perverse.
    taking = false;
}

void LineAnalyzer::setTarget (int measureIndex, const ChordSymbol& chord)
{
    Target next;
    next.measureIndex = measureIndex;
    next.chord = chord;
    next.scales = scalesFor (chord, options);

    target = std::move (next);
}

LineNote LineAnalyzer::play (int midiNote)
{
    auto note = readAgainstTarget (midiNote);

    if (taking)
        played.push_back (note);

    return note;
}

LineNote LineAnalyzer::readAgainstTarget (int midiNote) const
{
    if (! target.has_value())
    {
        LineNote note;
        note.midiNote = midiNote;
        note.measureIndex = -1;
        note.colour = NoteColour::outside;
        return note;
    }

    return readAgainst (midiNote, target->measureIndex, target->chord, target->scales);
}

//==============================================================================
namespace
{
    void count (LineStats& stats, NoteColour colour)
    {
        switch (colour)
        {
            case NoteColour::chordTone: ++stats.chordTones; break;
            case NoteColour::scaleTone: ++stats.scaleTones; break;
            case NoteColour::outside:   ++stats.outside;    break;
        }
    }
}

LineStats LineAnalyzer::statsForBar (int measureIndex) const
{
    LineStats stats;

    for (const auto& note : played)
        if (note.measureIndex == measureIndex)
            count (stats, note.colour);

    return stats;
}

LineStats LineAnalyzer::stats() const
{
    LineStats stats;

    for (const auto& note : played)
        count (stats, note.colour);

    return stats;
}

TakeSummary LineAnalyzer::summary() const
{
    TakeSummary take;
    take.overall = stats();

    for (const auto& note : played)
    {
        auto existing = std::find_if (take.bars.begin(), take.bars.end(),
                                      [&note] (const LineBar& bar)
                                      { return bar.measureIndex == note.measureIndex; });

        if (existing == take.bars.end())
        {
            take.bars.push_back ({ note.measureIndex, note.chordSymbol, {} });
            existing = std::prev (take.bars.end());
        }

        count (existing->stats, note.colour);
    }

    if (take.overall.total() == 0)
    {
        take.summary = "Nothing played yet.";
        return take;
    }

    take.summary = plural (take.overall.total(), "note", "notes")
                 + " over " + plural (static_cast<int> (take.bars.size()), "bar", "bars")
                 + " - " + std::to_string (take.overall.percentChordTones()) + "% chord tones, "
                 + std::to_string (take.overall.percentScaleTones()) + "% scale tones, "
                 + std::to_string (take.overall.percentOutside()) + "% outside.";

    // Problems first, the way the voicing analyser orders its findings - and in
    // the same voice: a note outside the scale is outside the scale, not wrong.
    // These describe the take; none of them scores it.
    const auto outside = take.overall.percentOutside();
    const auto chordTones = take.overall.percentChordTones();

    if (outside >= 35)
        take.observations.push_back (
            "A good deal of the line sat outside the scales these bars take. Landing on a chord "
            "tone as each bar arrives is the quickest way to bring it back in.");

    // Only worth naming once there is enough of a bar to be talking about, and
    // only when it stands apart from the take around it.
    if (take.bars.size() > 1)
    {
        const auto worst = std::max_element (take.bars.begin(), take.bars.end(),
                                             [] (const LineBar& a, const LineBar& b)
                                             { return a.stats.percentOutside() < b.stats.percentOutside(); });

        if (worst->stats.total() >= 4 && worst->stats.percentOutside() >= outside + 25)
            take.observations.push_back (
                "Bar " + std::to_string (worst->measureIndex + 1) + " (" + worst->chordSymbol
                + ") was the one that pulled away: " + std::to_string (worst->stats.percentOutside())
                + "% of it sat outside.");
    }

    if (chordTones >= 75)
        take.observations.push_back (
            "Nearly all of it was chord tones. That is safe ground, and a little plain - the "
            "scale tones in between are where a line starts to sound like one.");
    else if (outside < 20 && chordTones >= 25)
        take.observations.push_back (
            "A good spread: chord tones anchoring it, scale tones colouring it.");

    return take;
}

} // namespace jazz::core
