#include "jazz/core/LickCatalogue.h"

#include <algorithm>

namespace jazz::core
{

namespace
{
    /** One note, written the way the catalogue below reads best.

        The argument order is the order the source states them in - where, over
        which chord, which degree, which octave, what it is doing - so an entry
        can be checked against the research line by line without counting
        commas.
    */
    LickNote note (int tick, int chordIndex, int degree, int octave, LickRole role,
                   int lengthTicks = ticksPerBeat / 2,
                   LickOrnament ornament = LickOrnament::none)
    {
        LickNote written;
        written.tick = tick;
        written.lengthTicks = lengthTicks;
        written.degree = degree;
        written.chordIndex = chordIndex;
        written.octave = octave;
        written.role = role;
        written.ornament = ornament;
        return written;
    }

    LickChord over (ChordQuality quality, int rootOffset, int startTick, int lengthTicks)
    {
        return LickChord { quality, rootOffset, startTick, lengthTicks };
    }

    constexpr int aBar = 4 * ticksPerBeat;      // 96, in four
    constexpr int halfABar = 2 * ticksPerBeat;  // two beats, for a turnaround
    constexpr int quarter = ticksPerBeat;
    constexpr int eighth = ticksPerBeat / 2;
    constexpr int triplet = ticksPerBeat / 3;
    constexpr int graceLength = 2;

    /*  What a source is worth, as a draw weight.

        The research is candid that its entries are not all the same kind of
        thing, and throwing that away would lose the most useful thing it said.
        A documented device is a practice somebody teaches; a teaching site's
        worked example is one person's reading of a recording; a composite was
        assembled out of devices rather than quoted from anybody. Weight
        decides the draw and never the cost - a composite is rarer where it
        fits, not worse. */
    constexpr int documented = 100;
    constexpr int fromALesson = 65;
    constexpr int assembled = 45;

    /*  "The Lick" is in the research's own list of pitfalls - "overusing
        cliche cells such as The Lick" - and its note on the entry says to tag
        it low-weight or as humour. It is in the catalogue because a player
        should meet it; it is down here because meeting it twice a chorus is
        the joke wearing out. */
    constexpr int aMeme = 20;
}

//==============================================================================
std::string lickRoleName (LickRole role)
{
    switch (role)
    {
        case LickRole::chordTone:  return "chord tone";
        case LickRole::colourTone: return "colour tone";
        case LickRole::scaleTone:  return "scale tone";
        case LickRole::approach:   return "approach";
        case LickRole::enclosure:  return "enclosure";
        case LickRole::outside:    break;
    }

    return "outside";
}

std::string lickOrnamentName (LickOrnament ornament)
{
    switch (ornament)
    {
        case LickOrnament::grace: return "grace";
        case LickOrnament::crush: return "crush";
        case LickOrnament::none:  break;
    }

    return "none";
}

std::string lickSourceName (LickSource source)
{
    switch (source)
    {
        case LickSource::documentedDevice: return "documented device";
        case LickSource::transcription:    return "transcription";
        case LickSource::teachingSite:     return "teaching site";
        case LickSource::composite:        break;
    }

    return "composite";
}

int LickDefinition::spanInTicks() const noexcept
{
    if (notes.empty())
        return 0;

    auto first = notes.front().tick;
    auto last = notes.front().tick + notes.front().lengthTicks;

    for (const auto& written : notes)
    {
        first = std::min (first, written.tick);
        last = std::max (last, written.tick + written.lengthTicks);
    }

    return last - first;
}

int LickDefinition::midiFor (const LickNote& written, int rootOfFirstChord) const noexcept
{
    const auto offset = written.chordIndex >= 0
                     && written.chordIndex < static_cast<int> (chords.size())
                          ? chords[static_cast<std::size_t> (written.chordIndex)].rootOffset
                          : 0;

    return rootOfFirstChord + offset + written.degree + 12 * written.octave;
}

//==============================================================================
const std::vector<LickDefinition>& licks()
{
    static const std::vector<LickDefinition> catalogue = []
    {
        std::vector<LickDefinition> built;

        //======================================================================
        //  B1. Major ii-V-I.
        {
            /*  Bar 1 is a 1-2-3-5 digital pattern and then the arpeggio above
                it, which puts a chord tone on every beat. Bar 2 opens with a
                Bdim7 - the "3 to b9" arpeggio, common bebop vocabulary - and
                the C across the barline is b7 resolving to 3. The F-D-E into
                the last bar is an enclosure, and that F is the V's 7th falling
                to the I's 3rd.

                Written in C: D E F A C E D C | B D F Ab G F D F | E.

                A composite. The research builds it from a documented Parker
                cliche ("a 3 to b9 leap followed by a chromatic run to the 5 of
                Cmaj7") rather than quoting one solo, and says so. */
            LickDefinition lick;
            lick.key = "L01";
            lick.name = "Digital ii, 3-to-b9 V";
            lick.summary = "A 1-2-3-5 cell over the ii, a diminished arpeggio over the V, "
                           "and an enclosure into the I.";
            lick.attribution = "a Parker cliche";
            lick.source = LickSource::composite;
            lick.weight = assembled;
            lick.styles = { "bebop" };
            lick.chords = { over (ChordQuality::minor, 0, 0, aBar),
                            over (ChordQuality::dominant, 5, aBar, aBar),
                            over (ChordQuality::major, 10, 2 * aBar, aBar) };
            lick.notes = {
                note (  0, 0,  0, 0, LickRole::chordTone),   // D
                note ( 12, 0,  2, 0, LickRole::colourTone),  // E
                note ( 24, 0,  3, 0, LickRole::chordTone),   // F
                note ( 36, 0,  7, 0, LickRole::chordTone),   // A
                note ( 48, 0, 10, 0, LickRole::chordTone),   // C
                note ( 60, 0,  2, 1, LickRole::colourTone),  // E
                note ( 72, 0,  0, 1, LickRole::chordTone),   // D
                note ( 84, 0, 10, 0, LickRole::chordTone),   // C - b7 into the V's 3
                note ( 96, 1,  4, 0, LickRole::chordTone),   // B
                note (108, 1,  7, 0, LickRole::chordTone),   // D
                note (120, 1, 10, 0, LickRole::chordTone),   // F
                note (132, 1,  1, 1, LickRole::colourTone),  // Ab - the b9
                note (144, 1,  0, 1, LickRole::chordTone),   // G
                note (156, 1, 10, 0, LickRole::chordTone),   // F
                note (168, 1,  7, 0, LickRole::enclosure),   // D
                note (180, 1, 10, 0, LickRole::enclosure),   // F
                note (192, 2,  4, 0, LickRole::chordTone, quarter) };  // E
            built.push_back (lick);
        }

        {
            /*  Arpeggios superimposed from the 3rd: Fmaj7 over Dm7 and Bm7b5
                over G7, which is the standard bebop substitution rather than
                anybody's particular line. Ends on the 7-to-3 again.

                Written in C: F A C E D C A C | B D F A G F D F | E. */
            LickDefinition lick;
            lick.key = "L02";
            lick.name = "Arpeggio from the 3rd";
            lick.summary = "Fmaj7 over the ii and Bm7b5 over the V - the arpeggio a third "
                           "up, which is the chord with its colour on top.";
            lick.attribution = "standard bebop substitution";
            lick.source = LickSource::documentedDevice;
            lick.weight = documented;
            lick.styles = { "bebop" };
            lick.chords = { over (ChordQuality::minor, 0, 0, aBar),
                            over (ChordQuality::dominant, 5, aBar, aBar),
                            over (ChordQuality::major, 10, 2 * aBar, aBar) };
            lick.notes = {
                note (  0, 0,  3, 0, LickRole::chordTone),   // F
                note ( 12, 0,  7, 0, LickRole::chordTone),   // A
                note ( 24, 0, 10, 0, LickRole::chordTone),   // C
                note ( 36, 0,  2, 1, LickRole::colourTone),  // E
                note ( 48, 0,  0, 1, LickRole::chordTone),   // D
                note ( 60, 0, 10, 0, LickRole::chordTone),   // C
                note ( 72, 0,  7, 0, LickRole::chordTone),   // A
                note ( 84, 0, 10, 0, LickRole::chordTone),   // C
                note ( 96, 1,  4, 0, LickRole::chordTone),   // B
                note (108, 1,  7, 0, LickRole::chordTone),   // D
                note (120, 1, 10, 0, LickRole::chordTone),   // F
                note (132, 1,  2, 1, LickRole::colourTone),  // A
                note (144, 1,  0, 1, LickRole::chordTone),   // G
                note (156, 1, 10, 0, LickRole::chordTone),   // F
                note (168, 1,  7, 0, LickRole::chordTone),   // D
                note (180, 1, 10, 0, LickRole::chordTone),   // F
                note (192, 2,  4, 0, LickRole::chordTone, quarter) };  // E
            built.push_back (lick);
        }

        {
            /*  The side-slip, and the reason an authored lick is allowed to
                fail `lineFaults`. Over the ii it is plain D Dorian, 8-6-5-4,
                answered by silence. Over the V it plays the same 1-2-3-5 shape
                a half step up - an Ab cell over G7 - so every note is outside
                the scale a take reads against and the first of them lands on
                beat one. `lineFaults` would call that `chromaticOnTheBeat` and
                a take reads those notes `outside`, and both are correct: it is
                a documented Coltrane device, and "that Eb was outside, and it
                landed on Ab" is exactly what a player wants told.

                Written in C: D B A G (rest) | Eb C Bb Ab | G. */
            LickDefinition lick;
            lick.key = "L03";
            lick.name = "Coltrane cell a half step up";
            lick.summary = "Plain over the ii, then the same shape side-slipped a half step "
                           "over the V, resolving down into the I.";
            lick.attribution = "a Coltrane side-slip";
            lick.source = LickSource::teachingSite;
            lick.weight = fromALesson;
            lick.styles = { "bebop" };
            lick.chords = { over (ChordQuality::minor, 0, 0, aBar),
                            over (ChordQuality::dominant, 5, aBar, aBar),
                            over (ChordQuality::major, 10, 2 * aBar, aBar) };
            lick.notes = {
                note (  0, 0,  0, 1, LickRole::chordTone),   // D - the octave
                note ( 12, 0,  9, 0, LickRole::scaleTone),   // B - the 6, which is what makes it Dorian
                note ( 24, 0,  7, 0, LickRole::chordTone),   // A
                note ( 36, 0,  5, 0, LickRole::scaleTone, quarter),  // G, then silence
                note ( 96, 1,  8, 0, LickRole::outside),     // Eb - b13, on the downbeat
                note (108, 1,  5, 0, LickRole::scaleTone),   // C - the 11
                note (120, 1,  3, 0, LickRole::outside),     // Bb - #9
                note (132, 1,  1, 0, LickRole::outside),     // Ab - b9, and the note that resolves
                note (192, 2,  7, -1, LickRole::chordTone, quarter) };  // G - the 5 of the I
            built.push_back (lick);
        }

        {
            /*  The tritone sub, played as what it is - a Db7 line over the G7 -
                and landing on the 9 of the I rather than on a chord tone,
                which is the whole character of it. Two chords rather than
                three, so it fits any V-I and not only a full ii-V-I.

                Written in C: Db F Ab B Bb Ab F Eb | D. */
            LickDefinition lick;
            lick.key = "L04";
            lick.name = "Tritone sub resolving to the 9";
            lick.summary = "A Db7 line over the G7, falling chromatically onto the 9 of "
                           "the tonic instead of its root.";
            lick.attribution = "tritone substitution";
            lick.source = LickSource::documentedDevice;
            lick.weight = documented;
            lick.styles = { "bebop" };
            lick.chords = { over (ChordQuality::dominant, 0, 0, aBar),
                            over (ChordQuality::major, 5, aBar, aBar) };
            lick.notes = {
                note (  0, 0,  6, 0, LickRole::outside),     // Db - b5
                note ( 12, 0, 10, 0, LickRole::chordTone),   // F - b7
                note ( 24, 0,  1, 1, LickRole::colourTone),  // Ab - b9
                note ( 36, 0,  4, 1, LickRole::chordTone),   // B - the 3
                note ( 48, 0,  3, 1, LickRole::colourTone),  // Bb - #9
                note ( 60, 0,  1, 1, LickRole::colourTone),  // Ab - b9
                note ( 72, 0, 10, 0, LickRole::chordTone),   // F
                note ( 84, 0,  8, 0, LickRole::colourTone),  // Eb - b13, a half step over the target
                note ( 96, 1,  2, 0, LickRole::colourTone, quarter) };  // D - the 9
            built.push_back (lick);
        }

        //======================================================================
        //  B2. Minor ii-V-i, two beats a chord.
        {
            /*  Barry Harris' Bb7 scale taken from its 7th down to B, which
                lands a chord tone on every beat across both halves of the bar
                and encloses the minor tonic on the way out. The chromatic B is
                what does the work.

                Written in C minor: Ab G F Eb D C B D | C. */
            LickDefinition lick;
            lick.key = "L05";
            lick.name = "Bb7 scale from its 7th down";
            lick.summary = "One scale run covering both halves of a minor ii-V, with a "
                           "chord tone on every beat and an enclosure into the i.";
            lick.attribution = "Barry Harris";
            lick.source = LickSource::documentedDevice;
            lick.weight = documented;
            lick.styles = { "bebop" };
            lick.chords = { over (ChordQuality::halfDiminished, 0, 0, halfABar),
                            over (ChordQuality::dominant, 5, halfABar, halfABar),
                            over (ChordQuality::minor, 10, aBar, aBar) };
            lick.notes = {
                note (  0, 0,  6,  0, LickRole::chordTone),   // Ab - the b5
                note ( 12, 0,  5,  0, LickRole::scaleTone),   // G - the 11
                note ( 24, 0,  3,  0, LickRole::chordTone),   // F - the b3
                note ( 36, 0,  1,  0, LickRole::colourTone),  // Eb - the b9
                note ( 48, 1,  7, -1, LickRole::chordTone),   // D - the 5 of the V
                note ( 60, 1,  5, -1, LickRole::scaleTone),   // C - the 11
                note ( 72, 1,  4, -1, LickRole::enclosure),   // B - the 3, chromatic
                note ( 84, 1,  7, -1, LickRole::enclosure),   // D
                note ( 96, 2,  0, -1, LickRole::chordTone, quarter) };  // C
            built.push_back (lick);
        }

        {
            /*  Wynton Kelly's way through a minor ii-V, as the lesson that
                analyses it describes: a chromatic pickup into the b3, an Fm
                arpeggio over the m7b5 (the minor arpeggio a minor third up),
                harmonic minor over the V with C-to-B as b7-to-3, and an
                enclosure into the tonic. A composite - the research is explicit
                that it is built from the devices rather than quoted note for
                note.

                Written in C minor, with a pickup:
                  (G F#) | F Ab C Eb D C Ab C | B C D F Ab G F D | Eb. */
            LickDefinition lick;
            lick.key = "L06";
            lick.name = "Kelly-style minor ii-V";
            lick.summary = "A chromatic pickup, an arpeggio from the b3 over the m7b5, and "
                           "harmonic minor over the V.";
            lick.attribution = "after Wynton Kelly";
            lick.source = LickSource::composite;
            lick.weight = assembled;
            lick.styles = { "bebop" };
            lick.startsOnAPickup = true;
            lick.chords = { over (ChordQuality::halfDiminished, 0, 0, aBar),
                            over (ChordQuality::dominant, 5, aBar, aBar),
                            over (ChordQuality::minor, 10, 2 * aBar, aBar) };
            lick.notes = {
                note (-24, 0,  5, 0, LickRole::scaleTone),   // G  - the pickup
                note (-12, 0,  4, 0, LickRole::outside),     // F# - chromatic into the b3
                note (  0, 0,  3, 0, LickRole::chordTone),   // F
                note ( 12, 0,  6, 0, LickRole::chordTone),   // Ab
                note ( 24, 0, 10, 0, LickRole::chordTone),   // C
                note ( 36, 0,  1, 1, LickRole::colourTone),  // Eb
                note ( 48, 0,  0, 1, LickRole::chordTone),   // D
                note ( 60, 0, 10, 0, LickRole::chordTone),   // C
                note ( 72, 0,  6, 0, LickRole::chordTone),   // Ab
                note ( 84, 0, 10, 0, LickRole::chordTone),   // C - b7 into the V's 3
                note ( 96, 1,  4, 0, LickRole::chordTone),   // B
                note (108, 1,  5, 0, LickRole::scaleTone),   // C
                note (120, 1,  7, 0, LickRole::chordTone),   // D
                note (132, 1, 10, 0, LickRole::chordTone),   // F
                note (144, 1,  1, 1, LickRole::colourTone),  // Ab - the b9
                note (156, 1,  0, 1, LickRole::chordTone),   // G
                note (168, 1, 10, 0, LickRole::enclosure),   // F
                note (180, 1,  7, 0, LickRole::enclosure),   // D
                note (192, 2,  3, 0, LickRole::chordTone, quarter) };  // Eb
            built.push_back (lick);
        }

        //======================================================================
        /*  B2 continued. The "Cry Me a River" motif, which is one shape -
            9-1-5-b3-9-1-7 of a minor add9 arpeggio - and three settings.

            Three entries rather than one, and that is a decision worth stating.
            The shape is the same in all three; what changes is which chord it
            is superimposed over, and the degrees are therefore completely
            different each time. One entry carrying three degree tables would
            be a second kind of lick in the same struct, and the matcher would
            have to learn to choose between them. Three entries cost the
            catalogue two lines of repetition and keep both simple. Greg
            Fishman calls this the "Rosetta Stone" of licks and applies it to
            eight chord types, so three is a beginning rather than the set.

            The rhythm is the research's own "rhythm suggested", and its tick
            list is internally inconsistent - it says the phrase starts at 12
            with eighths through 48, then calls the *fifth* note the one held
            from 48. Four eighths from 12 put the fifth note at 60, so that is
            where the held note goes, and the two after it follow at 84 and at
            the downbeat of the next bar. Only the literal numbers move; the
            figure - four eighths, a held note, two eighths, a landing - is the
            one described. */
        const auto cmarRhythm = [] (int index)
        {
            constexpr int ticks[] = { 12, 24, 36, 48, 60, 84, 96 };
            return ticks[index];
        };

        {
            LickDefinition lick;
            lick.key = "L07a";
            lick.name = "Cry Me a River motif, over the i";
            lick.summary = "The minor add9 shape at home: 9-1-5-b3, then down to the 7.";
            lick.attribution = "the Cry Me a River motif";
            lick.source = LickSource::teachingSite;
            lick.weight = fromALesson;
            lick.styles = { "bebop", "blues" };
            lick.chords = { over (ChordQuality::minor, 0, 0, aBar + aBar) };
            lick.notes = {
                note (cmarRhythm (0), 0,  2, 1, LickRole::colourTone),  // D - the 9
                note (cmarRhythm (1), 0,  0, 1, LickRole::chordTone),   // C
                note (cmarRhythm (2), 0,  7, 0, LickRole::chordTone),   // G
                note (cmarRhythm (3), 0,  3, 0, LickRole::chordTone),   // Eb
                note (cmarRhythm (4), 0,  2, 0, LickRole::colourTone, quarter),  // D
                note (cmarRhythm (5), 0,  0, 0, LickRole::chordTone),   // C
                note (cmarRhythm (6), 0, 11, -1, LickRole::scaleTone, quarter) };  // B
            built.push_back (lick);
        }

        {
            /*  The same shape as an Abm(add9) over G7alt, which spells #9 b9
                b13 3 - every altered tension the chord has, in one gesture. */
            LickDefinition lick;
            lick.key = "L07b";
            lick.name = "Cry Me a River motif, over an altered V";
            lick.summary = "The same shape a half step above the root, which spells the "
                           "altered dominant's own tensions.";
            lick.attribution = "the Cry Me a River motif";
            lick.source = LickSource::teachingSite;
            lick.weight = fromALesson;
            lick.styles = { "bebop" };
            lick.chords = { over (ChordQuality::dominant, 0, 0, aBar + aBar) };
            lick.notes = {
                note (cmarRhythm (0), 0, 3, 1, LickRole::colourTone),  // Bb - #9
                note (cmarRhythm (1), 0, 1, 1, LickRole::colourTone),  // Ab - b9
                note (cmarRhythm (2), 0, 8, 0, LickRole::colourTone),  // Eb - b13
                note (cmarRhythm (3), 0, 4, 0, LickRole::chordTone),   // Cb/B - the 3
                note (cmarRhythm (4), 0, 3, 0, LickRole::colourTone, quarter),  // Bb
                note (cmarRhythm (5), 0, 1, 0, LickRole::colourTone),  // Ab
                note (cmarRhythm (6), 0, 0, 0, LickRole::chordTone, quarter) };  // G
            built.push_back (lick);
        }

        {
            /*  And as an Fm(add9) from the b3 over a m7b5, which is the
                half-diminished chord with its 11 and b5 on show. */
            LickDefinition lick;
            lick.key = "L07c";
            lick.name = "Cry Me a River motif, over a m7b5";
            lick.summary = "The same shape from the b3, which lands the 11 and the b5 where "
                           "the half-diminished chord wants them.";
            lick.attribution = "the Cry Me a River motif";
            lick.source = LickSource::teachingSite;
            lick.weight = fromALesson;
            lick.styles = { "bebop" };
            lick.chords = { over (ChordQuality::halfDiminished, 0, 0, aBar + aBar) };
            lick.notes = {
                note (cmarRhythm (0), 0,  5, 1, LickRole::scaleTone),   // G - the 11
                note (cmarRhythm (1), 0,  3, 1, LickRole::chordTone),   // F - the b3
                note (cmarRhythm (2), 0, 10, 0, LickRole::chordTone),   // C - the b7
                note (cmarRhythm (3), 0,  6, 0, LickRole::chordTone),   // Ab - the b5
                note (cmarRhythm (4), 0,  5, 0, LickRole::scaleTone, quarter),  // G
                note (cmarRhythm (5), 0,  3, 0, LickRole::chordTone),   // F
                note (cmarRhythm (6), 0,  2, 0, LickRole::colourTone, quarter) };  // E - the 9
            built.push_back (lick);
        }

        //======================================================================
        //  B3. Iconic cells.
        {
            /*  "The Lick". 1-2-b3-4-2-b7-1 over a minor chord, with the fifth
                note held. It is in the catalogue because a player will hear it
                everywhere and should know what it is; it is weighted near the
                floor because the research lists overusing it among the
                pitfalls, and because it is now a meme rather than a line.

                Written in D minor: D E F G E C D. */
            LickDefinition lick;
            lick.key = "L08";
            lick.name = "The Lick";
            lick.summary = "The one everybody knows - up the minor scale to the 4, back "
                           "down through the b7. Play it swung, and sparingly.";
            lick.attribution = "The Lick, as everybody plays it";
            lick.source = LickSource::teachingSite;
            lick.weight = aMeme;
            lick.styles = { "bebop", "blues" };
            lick.chords = { over (ChordQuality::minor, 0, 0, aBar) };
            lick.notes = {
                note ( 0, 0,  0,  0, LickRole::chordTone),   // D
                note (12, 0,  2,  0, LickRole::colourTone),  // E
                note (24, 0,  3,  0, LickRole::chordTone),   // F
                note (36, 0,  5,  0, LickRole::scaleTone),   // G
                note (48, 0,  2,  0, LickRole::colourTone, quarter),  // E, held
                note (72, 0, 10, -1, LickRole::chordTone),   // C
                note (84, 0,  0,  0, LickRole::chordTone) }; // D
            built.push_back (lick);
        }

        {
            /*  A phrase ending rather than a phrase: land the target on beat
                one and drop to a lower tension on the "and". Two notes, which
                is why it exists - a generator that only knows how to keep
                going has no way to stop. */
            LickDefinition lick;
            lick.key = "L09";
            lick.name = "Resolve then drop";
            lick.summary = "A bebop ending: the target on the beat, then down to the 9 on "
                           "the and of it.";
            lick.attribution = "Jens Larsen";
            lick.source = LickSource::documentedDevice;
            lick.weight = documented;
            lick.styles = { "bebop" };
            lick.chords = { over (ChordQuality::major, 0, 0, aBar) };
            lick.notes = {
                note ( 0, 0, 7, 0, LickRole::chordTone),    // G - the 5
                note (12, 0, 2, 0, LickRole::colourTone) }; // D - the 9 below it
            built.push_back (lick);
        }

        //======================================================================
        //  B4. Dominant licks - a static V7 or a secondary dominant.
        {
            /*  The Mixolydian bebop scale descending from the b7, which is
                Harris' first rule: the added half step puts a chord tone on
                every downbeat. Then back up the arpeggio to the 9, ending on
                the b7 that carries into the next dominant.

                Written on E7: D C# B A G# F# E D# | E G# B D F# D B D. */
            LickDefinition lick;
            lick.key = "L10";
            lick.name = "Mixolydian bebop descent from the b7";
            lick.summary = "The bebop scale down from the b7 - the added half step is what "
                           "keeps a chord tone on every downbeat - then the arpeggio back up.";
            lick.attribution = "Barry Harris' first rule";
            lick.source = LickSource::documentedDevice;
            lick.weight = documented;
            lick.styles = { "bebop" };
            lick.chords = { over (ChordQuality::dominant, 0, 0, 2 * aBar) };
            lick.notes = {
                note (  0, 0, 10,  0, LickRole::chordTone),   // D  - the b7
                note ( 12, 0,  9,  0, LickRole::scaleTone),   // C#
                note ( 24, 0,  7,  0, LickRole::chordTone),   // B  - the 5
                note ( 36, 0,  5,  0, LickRole::scaleTone),   // A
                note ( 48, 0,  4,  0, LickRole::chordTone),   // G# - the 3
                note ( 60, 0,  2,  0, LickRole::scaleTone),   // F#
                note ( 72, 0,  0,  0, LickRole::chordTone),   // E  - the 1
                note ( 84, 0, 11, -1, LickRole::outside),     // D# - the added half step
                note ( 96, 0,  0,  0, LickRole::chordTone),   // E
                note (108, 0,  4,  0, LickRole::chordTone),   // G#
                note (120, 0,  7,  0, LickRole::chordTone),   // B
                note (132, 0, 10,  0, LickRole::chordTone),   // D
                note (144, 0,  2,  1, LickRole::colourTone),  // F# - the 9
                note (156, 0, 10,  0, LickRole::chordTone),   // D
                note (168, 0,  7,  0, LickRole::chordTone),   // B
                note (180, 0, 10,  0, LickRole::chordTone) }; // D - b7 into the next dominant
            built.push_back (lick);
        }

        {
            /*  Four descending major triads a minor third apart - E, Db, Bb, G
                - which are all in G's half-whole diminished scale, played in
                triplets. The one lick in the catalogue that is not written in
                eighths, and the reason `LickDefinition` carries a feel of its
                own: every line style's is the eighth, and a bebop player plays
                triplets.

                Written on G7(b9,13): B G# E | Ab F Db | F D Bb | D B G. */
            LickDefinition lick;
            lick.key = "L11";
            lick.name = "Diminished triad cascade";
            lick.summary = "Major triads a minor third apart, falling in triplets - all of "
                           "them inside the half-whole scale.";
            lick.attribution = "the half-whole triad cascade";
            lick.source = LickSource::documentedDevice;
            lick.weight = documented;
            lick.styles = { "bebop" };
            lick.feel = Subdivision::tripletEighth;
            lick.chords = { over (ChordQuality::dominant, 0, 0, aBar) };
            lick.notes = {
                note ( 0, 0,  4, 1, LickRole::chordTone,  triplet),   // B
                note ( 8, 0,  1, 1, LickRole::colourTone, triplet),   // G#/Ab - b9
                note (16, 0,  9, 0, LickRole::colourTone, triplet),   // E  - the 13
                note (24, 0,  1, 1, LickRole::colourTone, triplet),   // Ab
                note (32, 0, 10, 0, LickRole::chordTone,  triplet),   // F
                note (40, 0,  6, 0, LickRole::outside,    triplet),   // Db
                note (48, 0, 10, 0, LickRole::chordTone,  triplet),   // F
                note (56, 0,  7, 0, LickRole::chordTone,  triplet),   // D
                note (64, 0,  3, 0, LickRole::colourTone, triplet),   // Bb - #9
                note (72, 0,  7, 0, LickRole::chordTone,  triplet),   // D
                note (80, 0,  4, 0, LickRole::chordTone,  triplet),   // B
                note (88, 0,  0, 0, LickRole::chordTone,  triplet) }; // G
            built.push_back (lick);
        }

        //======================================================================
        //  B5. Blues, over a I7 or a IV7.
        {
            /*  The blues b3 leaning into the 3, then 3-5-8 and a syncopated
                b7 held across the beat. The pickup b3 can be played as a
                grace note; written here as a real eighth, because a shell that
                wants it crushed has the ornament to say so.

                Written on C7: (Eb) E G C Bb. */
            LickDefinition lick;
            lick.key = "L12";
            lick.name = "Blues b3 pickup into 3-5-8";
            lick.summary = "The b3 leaning into the 3, up to the octave, and a b7 held "
                           "across the beat.";
            lick.attribution = "Piano-ology blues lick 009";
            lick.source = LickSource::teachingSite;
            lick.weight = fromALesson;
            lick.styles = { "blues" };
            lick.startsOnAPickup = true;
            lick.chords = { over (ChordQuality::dominant, 0, 0, aBar) };
            lick.notes = {
                note (-12, 0,  3, 0, LickRole::colourTone),  // Eb - the b3, on the and of four
                note (  0, 0,  4, 0, LickRole::chordTone),   // E  - the 3
                note ( 12, 0,  7, 0, LickRole::chordTone),   // G  - the 5
                note ( 24, 0,  0, 1, LickRole::chordTone),   // C  - the octave
                note ( 36, 0, 10, 0, LickRole::chordTone, quarter + eighth) };  // Bb, held
            built.push_back (lick);
        }

        {
            /*  Two crushes - the b3 into the 3 and the b5 into the 5 - around
                a major-blues shape. The crush is the point: the blue note and
                its target are one physical impulse, not two notes.

                Written on C7: (Eb)E G A G (Gb)G C.

                Both crushes are written **on** their targets' ticks, not
                before them - see the note on `LickOrnament`. How far ahead of
                the beat a crush is actually struck is a rendering question,
                the same kind of question as how late a swung eighth falls, and
                the grid answers neither. */
            LickDefinition lick;
            lick.key = "L13";
            lick.name = "Grace-note blues crush";
            lick.summary = "The b3 and the b5 crushed into their targets, around the major "
                           "blues scale.";
            lick.attribution = "Red Garland and Wynton Kelly's blues language";
            lick.source = LickSource::teachingSite;
            lick.weight = fromALesson;
            lick.styles = { "blues" };
            lick.chords = { over (ChordQuality::dominant, 0, 0, aBar) };
            lick.notes = {
                note ( 0, 0,  3, 0, LickRole::colourTone, graceLength, LickOrnament::crush),  // Eb
                note ( 0, 0,  4, 0, LickRole::chordTone),   // E
                note (12, 0,  7, 0, LickRole::chordTone),   // G
                note (24, 0,  9, 0, LickRole::colourTone),  // A - the 6
                note (36, 0,  7, 0, LickRole::chordTone),   // G
                note (48, 0,  6, 0, LickRole::colourTone, graceLength, LickOrnament::crush),  // Gb
                note (48, 0,  7, 0, LickRole::chordTone),   // G
                note (72, 0,  0, 1, LickRole::chordTone, quarter) };  // C
            built.push_back (lick);
        }

        //======================================================================
        //  B6. Minor 7 / modal.
        {
            /*  McCoy Tyner's pentatonic-a-whole-step-up: E minor pentatonic
                over Dm7 gives 9 11 5 6 1, which is the Dorian sound with its
                third left out. Four-note groups climbing, then a leap back
                down - the registral return the research lists among the things
                that separate a line from a scale exercise.

                Written on Dm7: E G A B | A B D E | B D E G | D. */
            LickDefinition lick;
            lick.key = "L15";
            lick.name = "Pentatonic fourths over a Dorian minor";
            lick.summary = "The minor pentatonic a whole step up, in four-note groups - the "
                           "Dorian colour with the third left out.";
            lick.attribution = "McCoy Tyner";
            lick.source = LickSource::documentedDevice;
            lick.weight = documented;
            /*  Modal alone, though it is a pentatonic device: thirteen notes
                is one more than the pentatonic style's longest phrase, so that
                style could not phrase it and tagging it there would be a tag
                nothing could ever draw. */
            lick.styles = { "modal" };
            lick.chords = { over (ChordQuality::minor, 0, 0, 2 * aBar) };
            lick.notes = {
                note (  0, 0, 2, 0, LickRole::colourTone),  // E - the 9
                note ( 12, 0, 5, 0, LickRole::scaleTone),   // G - the 11
                note ( 24, 0, 7, 0, LickRole::chordTone),   // A - the 5
                note ( 36, 0, 9, 0, LickRole::scaleTone),   // B - the 6, the Dorian note
                note ( 48, 0, 7, 0, LickRole::chordTone),   // A
                note ( 60, 0, 9, 0, LickRole::scaleTone),   // B
                note ( 72, 0, 0, 1, LickRole::chordTone),   // D
                note ( 84, 0, 2, 1, LickRole::colourTone),  // E
                note ( 96, 0, 9, 0, LickRole::scaleTone),   // B
                note (108, 0, 0, 1, LickRole::chordTone),   // D
                note (120, 0, 2, 1, LickRole::colourTone),  // E
                note (132, 0, 5, 1, LickRole::scaleTone),   // G
                note (144, 0, 0, 1, LickRole::chordTone, quarter) };  // D - the leap back down
            built.push_back (lick);
        }

        //======================================================================
        //  B7. Major 7.
        {
            /*  The major bebop scale down from the root - the Baker/Harris half
                step between the 5 and the 6 - which lands 1, 6, 5, 3 on the
                downbeats. Then Em7 from the 3rd going back up, which is the
                major chord with its 9 on top.

                Written on Cmaj7: C B A Ab G F E D | E G B D. */
            LickDefinition lick;
            lick.key = "L16";
            lick.name = "Major bebop descent, then Em7 from the 3rd";
            lick.summary = "Down the major bebop scale - the half step between 5 and 6 puts "
                           "1 6 5 3 on the beats - then the arpeggio from the 3rd.";
            lick.attribution = "the Baker/Harris major bebop scale";
            lick.source = LickSource::documentedDevice;
            lick.weight = documented;
            lick.styles = { "bebop" };
            lick.chords = { over (ChordQuality::major, 0, 0, 2 * aBar) };
            lick.notes = {
                note (  0, 0,  0, 1, LickRole::chordTone),   // C
                note ( 12, 0, 11, 0, LickRole::chordTone),   // B - the 7
                note ( 24, 0,  9, 0, LickRole::chordTone),   // A - the 6
                note ( 36, 0,  8, 0, LickRole::outside),     // Ab - the added half step
                note ( 48, 0,  7, 0, LickRole::chordTone),   // G
                note ( 60, 0,  5, 0, LickRole::scaleTone),   // F
                note ( 72, 0,  4, 0, LickRole::chordTone),   // E
                note ( 84, 0,  2, 0, LickRole::colourTone),  // D - the 9
                note ( 96, 0,  4, 0, LickRole::chordTone),   // E
                note (108, 0,  7, 0, LickRole::chordTone),   // G
                note (120, 0, 11, 0, LickRole::chordTone),   // B
                note (132, 0,  2, 1, LickRole::colourTone, quarter) };  // D
            built.push_back (lick);
        }

        //======================================================================
        //  B8. Turnaround, two beats a chord.
        {
            /*  Every beat carries a chord tone and every change resolves by
                step, which is what a guide-tone line is for: D to C# is a
                chromatic approach from above, E to F one from below, C to B is
                b7 to 3, and F-D-E encloses the 3 of the tonic at the end.

                Written in C: E G A D | C# E G E | F E D C | B D F D | E.

                A composite, the research says: the devices are documented and
                this particular assembly of them is not anybody's solo. */
            LickDefinition lick;
            lick.key = "L17";
            lick.name = "Guide-tone turnaround";
            lick.summary = "A chord tone on every beat of a I-VI-ii-V, with every change "
                           "resolved by step.";
            lick.attribution = "the guide-tone turnaround";
            lick.source = LickSource::composite;
            lick.weight = assembled;
            lick.styles = { "bebop" };
            lick.chords = { over (ChordQuality::major, 0, 0, halfABar),
                            over (ChordQuality::dominant, 9, halfABar, halfABar),
                            over (ChordQuality::minor, 2, aBar, halfABar),
                            over (ChordQuality::dominant, 7, aBar + halfABar, halfABar),
                            over (ChordQuality::major, 0, 2 * aBar, aBar) };
            lick.notes = {
                note (  0, 0,  4, 0, LickRole::chordTone),   // E
                note ( 12, 0,  7, 0, LickRole::chordTone),   // G
                note ( 24, 0,  9, 0, LickRole::chordTone),   // A - the 6
                note ( 36, 0,  2, 1, LickRole::approach),    // D - down a half step into C#
                note ( 48, 1,  4, 0, LickRole::chordTone),   // C# - the 3 of the VI
                note ( 60, 1,  7, 0, LickRole::chordTone),   // E
                note ( 72, 1, 10, 0, LickRole::chordTone),   // G
                note ( 84, 1,  7, 0, LickRole::approach),    // E - up a half step into F
                note ( 96, 2,  3, 1, LickRole::chordTone),   // F - the b3 of the ii
                note (108, 2,  2, 1, LickRole::colourTone),  // E
                note (120, 2,  0, 1, LickRole::chordTone),   // D
                note (132, 2, 10, 0, LickRole::chordTone),   // C - b7 into the V's 3
                note (144, 3,  4, 0, LickRole::chordTone),   // B
                note (156, 3,  7, 0, LickRole::chordTone),   // D
                note (168, 3, 10, 0, LickRole::enclosure),   // F
                note (180, 3,  7, 0, LickRole::enclosure),   // D
                note (192, 4,  4, 1, LickRole::chordTone, quarter) };  // E
            built.push_back (lick);
        }

        return built;
    }();

    return catalogue;
}

//==============================================================================
namespace
{
    /** One chord of the chart, with where it starts and how long it holds.

        Consecutive slots of the same chord are merged on the way in, which is
        what lets a lick written across two bars of one chord find two bars of
        one chord. Without it the chart offers a Dm7 and then another Dm7, and
        a two-bar lick matches neither.
    */
    struct ChartChord
    {
        ChordSymbol chord;
        int startTick {};
        int lengthTicks {};

        /** Where each slot this run swallowed began.

            A run is the *chord*, and these are the places a lick may start
            inside it. Four bars of C7 are one chord - which is what lets a
            two-bar lick match them - and also four places a blues figure could
            begin, which merging alone would have thrown away: with one start
            per run a lick with a pickup could never be played at all over a
            blues that opens on the tonic.
        */
        std::vector<int> slotStarts;
    };

    std::vector<ChartChord> chordRunsIn (const Chart& chart, int fromBar, int toBar)
    {
        std::vector<ChartChord> runs;

        const auto beatsPerBar = std::max (1, chart.timeSignature.numerator);
        auto at = 0;

        for (auto bar = fromBar; bar <= toBar; ++bar)
        {
            const auto& measure = chart.measures[static_cast<std::size_t> (bar)];

            if (measure.isEmpty())
            {
                /*  A bar with nothing in it is a hole rather than a chord, and
                    a lick may not be laid across one - so the run before it
                    ends here and the next one starts after. */
                at += beatsPerBar * ticksPerBeat;
                runs.push_back ({});   // a marker, dropped below
                continue;
            }

            for (const auto& slot : measure.slots)
            {
                const auto length = std::max (1, slot.beats) * ticksPerBeat;

                if (! runs.empty()
                    && runs.back().lengthTicks > 0
                    && runs.back().chord.toString() == slot.chord.toString())
                {
                    runs.back().lengthTicks += length;
                    runs.back().slotStarts.push_back (at);
                }
                else
                {
                    runs.push_back ({ slot.chord, at, length, { at } });
                }

                at += length;
            }
        }

        // The hole markers have done their job of breaking the runs up.
        runs.erase (std::remove_if (runs.begin(), runs.end(),
                                    [] (const ChartChord& run) { return run.lengthTicks == 0; }),
                    runs.end());

        return runs;
    }

    bool playsThisStyle (const LickDefinition& lick, const std::string& styleKey)
    {
        return std::find (lick.styles.begin(), lick.styles.end(), styleKey) != lick.styles.end();
    }

    /** Semitones from @p from up to @p to, 0-11. */
    int offsetBetween (PitchClass from, PitchClass to)
    {
        return ((to - from) % 12 + 12) % 12;
    }

    /** Whether the lick's chords line up with the chart's from @p at.

        @param firstLength  how much of `runs[at]` is left from where the lick
                            starts. A lick beginning part-way through a long
                            run gets the remainder rather than the whole thing,
                            which is what stops a figure written over one bar
                            of C7 claiming to fit the four bars of it.
    */
    bool fitsFrom (const LickDefinition& lick, const std::vector<ChartChord>& runs,
                   std::size_t at, int firstLength)
    {
        if (at + lick.chords.size() > runs.size())
            return false;

        const auto firstRoot = runs[at].chord.root();

        for (std::size_t i = 0; i < lick.chords.size(); ++i)
        {
            const auto& wanted = lick.chords[i];
            const auto& found = runs[at + i];

            /*  Contiguous, which has to be asked rather than assumed: a bar
                with nothing in it breaks the runs up and is then dropped, so
                two runs either side of a hole sit next to each other in this
                vector while being a bar apart in the music. A lick laid across
                that would have its second half a bar late. */
            if (i > 0 && found.startTick != runs[at + i - 1].startTick
                                          + runs[at + i - 1].lengthTicks)
                return false;

            if (found.chord.quality() != wanted.quality)
                return false;

            if (offsetBetween (firstRoot, found.chord.root()) != ((wanted.rootOffset % 12) + 12) % 12)
                return false;

            /*  The boundaries, and the one place the last chord is treated
                differently: a chart that holds the tonic on after the lick has
                landed is still the tonic it landed on, but a chord in the
                middle that outlasts what the lick expects would put every note
                after it in the wrong place. */
            const auto isLast = i + 1 == lick.chords.size();
            const auto holdsFor = i == 0 ? firstLength : found.lengthTicks;

            if (isLast ? holdsFor < wanted.lengthTicks
                       : holdsFor != wanted.lengthTicks)
                return false;
        }

        return true;
    }
}

std::vector<LickMatch> licksFitting (const Chart& chart, const LineStyleDefinition& style,
                                     int fromBar, int toBar)
{
    std::vector<LickMatch> found;

    if (fromBar < 0 || toBar < fromBar || chart.measureCount() == 0)
        return found;

    toBar = std::min (toBar, chart.measureCount() - 1);

    const auto runs = chordRunsIn (chart, fromBar, toBar);

    for (std::size_t at = 0; at < runs.size(); ++at)
        for (const auto startTick : runs[at].slotStarts)
            for (const auto& lick : licks())
            {
                if (! playsThisStyle (lick, style.key))
                    continue;

                /*  A pickup reaches back before the first chord, so a lick
                    that has one cannot start at the very top of the range -
                    there is no bar in front of it to lead in from. */
                if (lick.startsOnAPickup && startTick < ticksPerBeat)
                    continue;

                const auto leftOfThisChord = runs[at].startTick + runs[at].lengthTicks - startTick;

                if (! fitsFrom (lick, runs, at, leftOfThisChord))
                    continue;

                found.push_back ({ &lick, startTick, runs[at].chord.root() });
            }

    /*  In the order they occur, which the loops above do not give: they walk
        run by run and then inside each run, so a lick starting late in one run
        can be listed before one starting early in the next. */
    std::stable_sort (found.begin(), found.end(),
                      [] (const LickMatch& a, const LickMatch& b)
                      { return a.startTick < b.startTick; });

    return found;
}

std::vector<Subdivision> subdivisionsFor (const LineStyleDefinition& style)
{
    std::vector<Subdivision> accepted { style.feel };

    for (const auto& lick : licks())
        if (playsThisStyle (lick, style.key)
            && std::find (accepted.begin(), accepted.end(), lick.feel) == accepted.end())
            accepted.push_back (lick.feel);

    return accepted;
}

const LickDefinition& lickFor (std::string_view key)
{
    const auto& catalogue = licks();

    for (const auto& lick : catalogue)
        if (lick.key == key)
            return lick;

    return catalogue.front();
}

} // namespace jazz::core
