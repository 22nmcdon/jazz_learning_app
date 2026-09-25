#include "jazz/core/LineStyle.h"

namespace jazz::core
{

const std::vector<LineStyleDefinition>& lineStyles()
{
    static const std::vector<LineStyleDefinition> catalogue = []
    {
        std::vector<LineStyleDefinition> built;

        {
            /*  Long running eighths, chromatic voice leading, and not much
                silence - the sound the whole of Part A of the research is
                describing. The Weimar corpus puts a "line" at about 19 notes,
                so the long end is there rather than at a round number, and the
                short end leaves room for a lick inside a bebop chorus.

                First in the list, so it is also what an unknown key falls back
                to - the style a player who has not chosen one most likely
                meant, and the one the analyser's own vocabulary was built to
                read. */
            LineStyleDefinition bebop;
            bebop.key = "bebop";
            bebop.name = "Bebop";
            bebop.summary = "Running eighths, chromatic approaches, and phrases that "
                            "start off the beat.";
            bebop.scaleStyle = "bebop";
            bebop.shortestPhrase = 6;
            bebop.longestPhrase = 19;
            bebop.shortestRest = ticksPerBeat / 2;
            bebop.longestRest = ticksPerBeat * 2;
            bebop.startTicks = { ticksPerBeat / 2, 0 };
            bebop.feel = Subdivision::eighth;
            bebop.descending = 54;
            /*  The middle of the research's range. Bebop is the most quotable
                of the four - most of the catalogue is written for it - and it
                is also the style where a line made entirely of quotes would
                stop being a line. */
            bebop.lickShare = 60;
            bebop.usesApproaches = true;
            built.push_back (bebop);
        }

        {
            /*  Short and answered by silence. The research puts a "lick" at
                about 8 notes against a line's 19, and calls licks
                "rhythmically more varied with more rests" - so this is the
                same vocabulary played in shorter sentences, which is most of
                what makes blues phrasing sound like blues rather than like
                bebop over a blues.

                Pentatonic scales rather than bebop ones, and it keeps
                approaches: the b3 sliding into the 3 is the sound. */
            LineStyleDefinition blues;
            blues.key = "blues";
            blues.name = "Blues";
            blues.summary = "Short phrases with room between them, out of the blues "
                            "and pentatonic scales.";
            blues.scaleStyle = "pentatonic";
            blues.shortestPhrase = 3;
            blues.longestPhrase = 8;
            blues.shortestRest = ticksPerBeat;
            blues.longestRest = ticksPerBeat * 4;
            blues.startTicks = { ticksPerBeat / 2, 0 };
            blues.feel = Subdivision::eighth;
            blues.descending = 55;
            /*  The top of the range. Blues phrasing *is* its figures: the b3
                leaning into the 3 and the crush into the 5 are not decoration
                on a line, they are what a player is there to learn. */
            blues.lickShare = 70;
            blues.usesApproaches = true;
            built.push_back (blues);
        }

        {
            /*  The scale is the sound, so nothing chromatic goes in.

                `usesApproaches = false` is the whole difference and it is a
                musical claim rather than a simplification: in a modal line a
                note from outside the mode is not colour on the way somewhere,
                it is a wrong note. Phrases run medium-long and start on the
                beat as often as off it, because there is no ii-V to push
                against. */
            LineStyleDefinition modal;
            modal.key = "modal";
            modal.name = "Modal";
            modal.summary = "Inside the mode and staying there - long phrases, no "
                            "chromatics, nothing to resolve.";
            modal.scaleStyle = "modes";
            modal.shortestPhrase = 5;
            modal.longestPhrase = 14;
            modal.shortestRest = ticksPerBeat;
            modal.longestRest = ticksPerBeat * 3;
            modal.startTicks = { 0, ticksPerBeat / 2 };
            modal.feel = Subdivision::eighth;
            modal.descending = 50;
            /*  Low, and not because modal playing quotes less - because the
                catalogue has one lick for it. Asking for more than there is
                costs nothing (the phrase is generated instead), but saying 60
                here would be claiming a vocabulary that does not exist. */
            modal.lickShare = 25;
            modal.usesApproaches = false;
            built.push_back (modal);
        }

        {
            /*  Five notes and the leaps they make.

                A pentatonic has no half steps in it, so a line built from one
                moves in fourths and minor thirds where a diatonic line would
                step - which is the sound, and is also why this one is given a
                little less descending bias than the rest: a pentatonic line
                that always fell would run out of register in a bar. */
            LineStyleDefinition pentatonic;
            pentatonic.key = "pentatonic";
            pentatonic.name = "Pentatonic";
            pentatonic.summary = "Five notes and the leaps between them, with no half "
                                 "steps to lean on.";
            pentatonic.scaleStyle = "pentatonic";
            pentatonic.shortestPhrase = 4;
            pentatonic.longestPhrase = 12;
            pentatonic.shortestRest = ticksPerBeat / 2;
            pentatonic.longestRest = ticksPerBeat * 3;
            pentatonic.startTicks = { ticksPerBeat / 2, 0 };
            pentatonic.feel = Subdivision::eighth;
            pentatonic.descending = 46;
            /*  This was **nothing** for as long as there was nothing here it
                could reach, and the entry that changed it is L20 - an
                eight-note minor pentatonic cell over one ordinary minor bar.
                Before it, the catalogue's only pentatonic device was L15 at
                thirteen notes against this style's longest phrase of twelve,
                so no number here could have had any effect at all: a style
                asking to quote what it cannot reach is the shape of bug
                `brazilian` was, sat in `ReharmStyle` tagged on no rule.

                Below the research's 50-70 rather than inside it, because two
                figures is not a vocabulary - L20 over a minor bar and L19 over
                a sus one. Measured at this number: 12% of a line over a sus vamp, 20% on a
                standard, 33% on a modal minor vamp. Raise it as the
                pentatonic side of the catalogue grows; the test that kept the
                zero honest is the same one that keeps this honest, since it
                asks what a style can *reach* rather than what it is tagged
                for. */
            pentatonic.lickShare = 45;
            pentatonic.usesApproaches = false;
            built.push_back (pentatonic);
        }

        return built;
    }();

    return catalogue;
}

const LineStyleDefinition& lineStyleFor (std::string_view key)
{
    const auto& catalogue = lineStyles();

    for (const auto& style : catalogue)
        if (style.key == key)
            return style;

    return catalogue.front();
}

} // namespace jazz::core
