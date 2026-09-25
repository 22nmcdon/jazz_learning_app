#include "jazz/core/LinePlacement.h"

#include "jazz/core/LickCatalogue.h"

#include <algorithm>
#include <optional>

namespace jazz::core
{

namespace
{
    /*  What a line's placement is made of.

        The grid carries twice the weight of the other two for the reason
        `Comping.cpp` gives about its own placement: the grid is what a feel
        *is*. A player phrasing in long sentences a little above the style's
        register is still playing that style; a player whose notes land
        between its subdivisions is not, whatever else is right. */
    constexpr int gridWeight     = 2;
    constexpr int phraseWeight   = 1;
    constexpr int registerWeight = 1;

    /*  A phrase a couple of notes past the style's longest is a player
        carrying on through the rest they meant to take; ten past is a line
        that never breathes, which is the pitfall the research names by name.
        Linear between the two, and steep enough that the second is not called
        the first - the same shape, and the same argument, as comping's
        `densityPointsPerExtraHit`. */
    constexpr int phrasePointsPerExtraOnset = 10;

    std::string countOf (int howMany, const char* one, const char* many)
    {
        return std::to_string (howMany) + " " + (howMany == 1 ? one : many);
    }

    /** One moment the hands struck: a single note, or every voice of a chord. */
    struct Onset
    {
        std::size_t firstNote {};
        std::size_t lastNote {};
        int measureIndex {};
        std::optional<BarPosition> at;
    };

    std::vector<Onset> onsetsIn (const std::vector<LineNote>& line)
    {
        std::vector<Onset> onsets;

        for (std::size_t i = 0; i < line.size(); ++i)
        {
            // The first note of the line starts one whatever its flag says -
            // the flag describes a join, and there is nothing behind it to
            // join to.
            if (onsets.empty() || ! line[i].struckWithPrevious)
                onsets.push_back ({ i, i, line[i].measureIndex, line[i].at });
            else
                onsets.back().lastNote = i;
        }

        return onsets;
    }
}

LinePlacementReading readLinePlacement (const std::vector<LineNote>& line,
                                        const LineStyleDefinition& style,
                                        int beatsPerBar)
{
    LinePlacementReading out;
    out.styleKey = style.key;
    out.styleName = style.name;

    const auto beats = std::max (1, beatsPerBar);
    const auto step = std::max (1, ticksFor (style.feel));

    /*  Every subdivision this style's vocabulary uses, not only its own feel.

        A style that can draw on a triplet lick has players who play triplets,
        and the app teaching a figure and then marking somebody for playing it
        is the exact failure the round trip exists to prevent. One source for
        the answer, in `subdivisionsFor`, so this and `lineFaults` cannot come
        to different conclusions about the same note.

        It does make the grid more forgiving than it was when this number
        shipped, and that is the honest cost: a style whose vocabulary is all
        eighths is unchanged, and a bebop line is now allowed its triplets. */
    const auto accepted = subdivisionsFor (style);

    const auto onAStyleGrid = [&accepted] (const BarPosition& at)
    {
        return std::any_of (accepted.begin(), accepted.end(),
                            [&at] (Subdivision feel) { return onTheGrid (at, feel); });
    };
    const auto barTicks = beats * ticksPerBeat;

    /*  The gap that reads as a rest - see `LinePhrase`. One step of the grid,
        which is the space a note of this style occupies, plus the shortest
        silence the style ever writes after a phrase. That is exactly how
        `planPhrases` lays a rest out, and comfortably longer than any gap
        inside a phrase. */
    const auto restGap = step + std::max (1, style.shortestRest);

    for (const auto& note : line)
    {
        ++out.notes;

        if (note.midiNote >= style.lowestNote && note.midiNote <= style.highestNote)
            ++out.notesInRegister;

        if (note.onStrongBeat)
        {
            ++out.notesOnStrongBeats;

            if (note.colour == NoteColour::chordTone)
                ++out.chordTonesOnStrongBeats;

            // R1, said rather than scored - the engine's rule about its own
            // generator, not something this style put its name to.
            if (note.colour == NoteColour::approach)
                ++out.chromaticsOnTheBeat;
        }
    }

    const auto onsets = onsetsIn (line);

    out.onsets = static_cast<int> (onsets.size());

    auto previousTick = 0;
    auto havePrevious = false;

    for (const auto& onset : onsets)
    {
        if (! onset.at.has_value())
        {
            out.placements.push_back (OnsetPlacement::unplaced);
            continue;
        }

        ++out.onsetsPlaced;

        const auto placed = onAStyleGrid (*onset.at);

        if (placed)
            ++out.onsetsOnTheGrid;

        out.placements.push_back (placed ? OnsetPlacement::onTheGrid
                                         : OnsetPlacement::offTheGrid);

        const auto absolute = onset.measureIndex * barTicks + onset.at->inTicks();

        /*  A new phrase on a rest, and also on a step backwards: a take that
            went round the form again arrives at a smaller tick than the note
            before it, and reading that as a gap of minus two choruses would
            join the last phrase of one chorus to the first of the next. */
        const auto breaksHere = ! havePrevious
                              || absolute < previousTick
                              || absolute - previousTick >= restGap;

        if (breaksHere)
        {
            LinePhrase phrase;
            phrase.firstNote = onset.firstNote;
            phrase.lastNote = onset.lastNote;
            phrase.measureIndex = onset.measureIndex;
            phrase.startsAt = *onset.at;
            phrase.startedOnAStyleTick =
                std::find (style.startTicks.begin(), style.startTicks.end(), onset.at->tick)
                    != style.startTicks.end();

            out.phrases.push_back (phrase);
        }

        auto& phrase = out.phrases.back();
        ++phrase.onsets;
        phrase.lastNote = onset.lastNote;

        previousTick = absolute;
        havePrevious = true;
    }

    for (auto& phrase : out.phrases)
        phrase.overBy = std::max (0, phrase.onsets - std::max (1, style.longestPhrase));

    if (out.onsetsPlaced > 0)
        out.gridFit = 100 * out.onsetsOnTheGrid / out.onsetsPlaced;

    if (out.notes > 0)
        out.registerFit = 100 * out.notesInRegister / out.notes;

    if (! out.phrases.empty())
    {
        auto total = 0;

        for (const auto& phrase : out.phrases)
            total += std::max (0, 100 - phrasePointsPerExtraOnset * phrase.overBy);

        out.phraseFit = total / static_cast<int> (out.phrases.size());
    }

    /*  Empty rather than zero. Nothing played is not nought out of a hundred,
        and neither is a line nothing was counting behind: the grid is two of
        the four parts of this and the phrasing needs positions as well, so
        without them there is no fit to report. */
    if (! line.empty() && out.onsetsPlaced > 0)
        out.fit = std::max (0, std::min (100, (out.gridFit * gridWeight
                                                 + out.phraseFit * phraseWeight
                                                 + out.registerFit * registerWeight)
                                              / (gridWeight + phraseWeight + registerWeight)));

    //==============================================================================
    if (line.empty())
    {
        out.summary = "Nothing played, so there is nothing to read.";
        return out;
    }

    if (out.onsetsPlaced == 0)
    {
        out.summary = style.name + " - read for the notes. Nothing was counting, "
                      "so there is no placing them.";

        out.observations.push_back ("Play this in time and the line gets a second reading: "
                                    "where the notes fell, against " + style.name + "'s own "
                                    + subdivisionName (style.feel) + " grid.");
        return out;
    }

    out.summary = style.name + " - " + countOf (out.onsets, "note", "notes")
                + " in " + countOf (static_cast<int> (out.phrases.size()), "phrase", "phrases") + ".";

    //  The words half.
    const auto offTheGrid = out.onsetsPlaced - out.onsetsOnTheGrid;

    if (offTheGrid > 0)
        out.observations.push_back (countOf (offTheGrid, "note", "notes") + " fell between "
                                    + style.name + "'s " + subdivisionName (style.feel)
                                    + "s and its triplets. That is a different subdivision, "
                                      "not a wrong note - but it is not this style's.");

    auto ranOn = 0;
    auto offBeatStarts = 0;

    for (const auto& phrase : out.phrases)
    {
        if (phrase.overBy > 0)                     ++ranOn;
        if (! phrase.startsAt.onTheBeat())         ++offBeatStarts;
    }

    if (ranOn > 0)
        out.observations.push_back (countOf (ranOn, "phrase", "phrases") + " ran past the "
                                    + std::to_string (style.longestPhrase) + " notes "
                                    + style.name + " writes before it takes a breath. "
                                      "Space is where the band gets heard.");

    /*  The bias half, said and never scored. A player starting everything on
        the beat is the pitfall the phrase planner was written against, and it
        is still their line to play that way. */
    if (static_cast<int> (out.phrases.size()) >= 3 && offBeatStarts == 0)
        out.observations.push_back ("Every phrase began on a beat. " + style.name
                                    + " starts most of them on an and - a pickup into the "
                                      "downbeat is the sound.");

    const auto outsideRegister = out.notes - out.notesInRegister;

    if (outsideRegister > 0)
        out.observations.push_back (countOf (outsideRegister, "note", "notes")
                                    + " sat outside the two octaves " + style.name
                                    + " lines are written in.");

    if (out.notesOnStrongBeats > 0)
        out.observations.push_back (countOf (out.chordTonesOnStrongBeats, "chord tone", "chord tones")
                                    + " on the " + countOf (out.notesOnStrongBeats, "note", "notes")
                                    + " that landed on an accented beat. Nothing here is in the "
                                      "number - it is what the line was saying where the bar "
                                      "leans.");

    if (out.chromaticsOnTheBeat > 0)
        out.observations.push_back (countOf (out.chromaticsOnTheBeat, "approach note", "approach notes")
                                    + " landed on an accented beat. Off the beat a chromatic is "
                                      "colour; on it, it is the note the bar leans on.");

    return out;
}

} // namespace jazz::core
