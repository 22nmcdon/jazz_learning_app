#include "jazz/core/Voicing.h"

#include <algorithm>
#include <cstdlib>

namespace jazz::core
{

Voicing Voicing::fromNotes (std::vector<int> notes)
{
    std::sort (notes.begin(), notes.end());
    notes.erase (std::unique (notes.begin(), notes.end()), notes.end());

    Voicing voicing;
    voicing.midiNotes = std::move (notes);
    return voicing;
}

int Voicing::lowestNote() const  { return midiNotes.empty() ? -1 : midiNotes.front(); }
int Voicing::highestNote() const { return midiNotes.empty() ? -1 : midiNotes.back(); }

int Voicing::spanInSemitones() const
{
    return midiNotes.empty() ? 0 : highestNote() - lowestNote();
}

std::uint16_t Voicing::pitchClassMask() const
{
    std::uint16_t mask = 0;

    for (auto note : midiNotes)
        mask |= static_cast<std::uint16_t> (1u << toPitchClass (note));

    return mask;
}

bool Voicing::containsPitchClass (PitchClass pitchClass) const
{
    return (pitchClassMask() & (1u << toPitchClass (pitchClass))) != 0;
}

std::vector<PitchClass> Voicing::pitchClasses() const
{
    std::vector<PitchClass> pitches;

    for (auto note : midiNotes)
    {
        const auto pitchClass = toPitchClass (note);

        if (std::find (pitches.begin(), pitches.end(), pitchClass) == pitches.end())
            pitches.push_back (pitchClass);
    }

    return pitches;
}

std::vector<int> Voicing::doubledNotes() const
{
    std::vector<int> doubled;

    for (std::size_t i = 0; i < midiNotes.size(); ++i)
        for (std::size_t j = i + 1; j < midiNotes.size(); ++j)
            if (toPitchClass (midiNotes[i]) == toPitchClass (midiNotes[j]))
                doubled.push_back (midiNotes[j]);

    return doubled;
}

std::string Voicing::describe (Accidental accidental) const
{
    std::string result;

    for (std::size_t i = 0; i < midiNotes.size(); ++i)
    {
        if (i > 0)
            result += " ";

        result += midiNoteName (midiNotes[i], accidental);
    }

    return result;
}

std::string voicingTypeName (VoicingType type)
{
    switch (type)
    {
        case VoicingType::singleNote:        return "single note";
        case VoicingType::shell:             return "shell voicing";
        case VoicingType::rootPosition:      return "root-position voicing";
        case VoicingType::rootlessLeftHand:  return "rootless left-hand voicing";
        case VoicingType::twoHandedRootless: return "two-handed rootless voicing";
        case VoicingType::solo:              return "solo voicing";
        case VoicingType::spread:            return "spread voicing";
        case VoicingType::unknown:           break;
    }

    return "voicing";
}

namespace
{
    bool has (const ChordSymbol& chord, Extension extension)
    {
        const auto& extensions = chord.extensions();
        return std::find (extensions.begin(), extensions.end(), extension) != extensions.end();
    }

    /** The chord's own ninth: altered if the symbol says so, natural otherwise. */
    int ninthOf (const ChordSymbol& chord)
    {
        if (has (chord, Extension::flatNine))  return 1;
        if (has (chord, Extension::sharpNine)) return 3;
        return 2;
    }

    /** The chord's fifth, wherever the symbol has moved it to. */
    int fifthOf (const ChordSymbol& chord)
    {
        if (has (chord, Extension::sharpFive) || chord.quality() == ChordQuality::augmented) return 8;
        if (has (chord, Extension::flatFive) || chord.quality() == ChordQuality::diminished
            || chord.quality() == ChordQuality::halfDiminished)                              return 6;
        return 7;
    }

    /** The chord's thirteenth - the tension a sixth above the root.

        A chord built on a flat fifth has no natural thirteenth to take, so it
        takes the flat thirteenth that its scale does have.
    */
    int thirteenthOf (const ChordSymbol& chord)
    {
        if (has (chord, Extension::flatThirteen) || has (chord, Extension::sharpFive)) return 8;
        if (has (chord, Extension::thirteen) || has (chord, Extension::six))           return 9;

        if (chord.quality() == ChordQuality::halfDiminished
            || chord.quality() == ChordQuality::diminished
            || chord.quality() == ChordQuality::augmented
            || has (chord, Extension::flatFive))
            return 8;

        return 9;
    }

    int thirdOf (const ChordSymbol& chord)
    {
        if (chord.quality() == ChordQuality::suspended)
            return 5;

        return chord.hasMinorThird() ? 3 : 4;
    }

    int seventhOf (const ChordSymbol& chord)
    {
        switch (chord.seventh())
        {
            case SeventhType::major:      return 11;
            case SeventhType::minor:      return 10;
            case SeventhType::diminished: return 9;
            case SeventhType::none:       return 9;  // 6th chords: the 6 takes the slot
        }

        return 10;
    }

    /** Stacks the given root-relative offsets upwards, starting at @p anchorNote. */
    Voicing stack (const ChordSymbol& chord, const std::vector<int>& offsets, int anchorNote)
    {
        std::vector<int> notes;
        auto floorNote = anchorNote;

        for (auto offset : offsets)
        {
            const auto pitchClass = toPitchClass (chord.root() + offset);
            auto note = floorNote + toPitchClass (pitchClass - toPitchClass (floorNote));

            if (! notes.empty() && note <= notes.back())
                note += semitonesPerOctave;

            notes.push_back (note);
            floorNote = note;
        }

        return Voicing::fromNotes (std::move (notes));
    }

    /** Drops offsets that would sound a note the voicing already has.

        A 6th chord is why: its "seventh" slot and its thirteenth are the same
        note, so a shape asking for both would stack one on top of the other.
        Where that leaves a shape short, @p filler tops it back up.
    */
    std::vector<int> distinctOffsets (std::vector<int> offsets, int filler, std::size_t wanted)
    {
        std::vector<int> kept;

        const auto alreadyThere = [&kept] (int offset)
        {
            return std::any_of (kept.begin(), kept.end(), [offset] (int existing)
                                { return toPitchClass (existing) == toPitchClass (offset); });
        };

        for (auto offset : offsets)
            if (! alreadyThere (offset))
                kept.push_back (offset);

        if (kept.size() < wanted && ! alreadyThere (filler))
            kept.push_back (filler);

        return kept;
    }

    /** Where the chord's root falls at or above @p floorNote. */
    int rootAtOrAbove (const ChordSymbol& chord, int floorNote)
    {
        return floorNote + toPitchClass (chord.root() - toPitchClass (floorNote));
    }

    /** Two hands: the left plays @p leftHand, the right takes the nearest notes
        above it. Starting the right hand any higher than that pushes a colour
        tone sitting just above the left hand up a whole octave.
    */
    Voicing twoHanded (const ChordSymbol& chord, const std::vector<int>& leftHand,
                       const std::vector<int>& rightHand, int anchorNote)
    {
        const auto left = stack (chord, leftHand, anchorNote);
        const auto right = stack (chord, rightHand, left.highestNote() + 2);

        auto notes = left.midiNotes;
        notes.insert (notes.end(), right.midiNotes.begin(), right.midiNotes.end());

        return Voicing::fromNotes (std::move (notes));
    }

    /** The root low, a partner the register can carry above it, and the colour
        in the right hand.
    */
    Voicing solo (const ChordSymbol& chord, int partner,
                  const std::vector<int>& rightHand, int anchorNote)
    {
        const auto root = rootAtOrAbove (chord, anchorNote);
        const auto left = root + partner;
        const auto right = stack (chord, rightHand, left + 2);

        std::vector<int> notes { root, left };
        notes.insert (notes.end(), right.midiNotes.begin(), right.midiNotes.end());

        return Voicing::fromNotes (std::move (notes));
    }

    /** One hand per octave: the root alone at the bottom, the rest well above
        it, which is what makes a voicing read as open rather than as a block.
    */
    Voicing spreadVoicing (const ChordSymbol& chord, const std::vector<int>& upper, int anchorNote)
    {
        const auto root = rootAtOrAbove (chord, anchorNote);
        const auto above = stack (chord, upper, root + semitonesPerOctave + 2);

        std::vector<int> notes { root };
        notes.insert (notes.end(), above.midiNotes.begin(), above.midiNotes.end());

        return Voicing::fromNotes (std::move (notes));
    }
}

namespace
{
    /*  The window a comping left hand lives in. Low enough to sit under a
        soloist, high enough to stay off the bass; a voicing free to wander
        outside it would climb a little with every chord that leads upwards and
        end the tune somewhere nobody comps. */
    constexpr int lowestCompAnchor = 45;    // A2
    constexpr int highestCompAnchor = 57;   // A3

    /*  How far above its anchor a two-handed voicing reaches. The anchor is the
        bottom note, so the window a caller states is about the whole voicing
        while the sweep is about its foot - and the default window has to be the
        anchors opened out by this much, or bounding the sweep by it would
        quietly narrow what this function has always returned. */
    constexpr int twoHandedReach = 24;

    /** How far the hands travel between two voicings.

        Measured both ways round - every note of each one to the nearest note of
        the other - so that a voicing is not made to look close merely by having
        fewer notes to account for.
    */
    int voiceLeadingDistance (const std::vector<int>& from, const std::vector<int>& to)
    {
        if (from.empty() || to.empty())
            return 0;

        const auto nearest = [] (const std::vector<int>& notes, int note)
        {
            auto best = std::abs (notes.front() - note);

            for (auto other : notes)
                best = std::min (best, std::abs (other - note));

            return best;
        };

        auto total = 0;

        for (auto note : to)   total += nearest (from, note);
        for (auto note : from) total += nearest (to, note);

        return total;
    }
}

Voicing compingVoicing (const ChordSymbol& chord, const std::vector<int>& previousNotes)
{
    return compingVoicing (chord, previousNotes, lowestCompAnchor,
                           highestCompAnchor + twoHandedReach);
}

namespace
{
    /** The shapes a comper reaches for, and how readily.

        Weighted rather than equal, because they are not equally ordinary. The
        plain two-handed rootless pair is what comping *is* and carries no
        penalty at all; the richer four-note pair says the same thing with the
        tensions in and is nearly as everyday; the one-hand rootless shapes are
        thinner, and a comper who played them as often as the others would
        sound like one who had run out of right hand.

        No shell. A shell puts the root at the bottom, and the root under a
        voicing is the one thing the evaluator calls a real comping fault in a
        player - the bass is already playing that note. A band playing what the
        app marks you for is the contradiction the style register exists to
        avoid, pointed at the shape instead of the octave.
    */
    struct CompShape
    {
        VoicingType type;
        VoicingDensity density;
        int weight;        ///< 100 is the house shape; lower comes up less
    };

    const std::vector<CompShape>& compShapes()
    {
        static const std::vector<CompShape> shapes {
            { VoicingType::twoHandedRootless, VoicingDensity::plain, 100 },
            { VoicingType::twoHandedRootless, VoicingDensity::rich,   80 },
            { VoicingType::rootlessLeftHand,  VoicingDensity::rich,   34 },
            { VoicingType::rootlessLeftHand,  VoicingDensity::plain,  20 }
        };

        return shapes;
    }

    /*  How far from the best a candidate may be and still be worth choosing
        between. Wide enough that a chord usually has several answers, narrow
        enough that none of them is a bad one: this picks among voicings that
        were nearly as good, never between a good one and a poor one. */
    constexpr int compSpread = 26;

    /*  How often the hands move to another part of the window instead of
        taking the nearest voicing, out of 100. Now and then rather than
        constantly - a comper who changed register on every chord would be
        harder to follow than one who never did. */
    constexpr int reachChance = 18;

    /*  And how far such a move may go. A pianist reaching for a new register
        moves a fifth or an octave; anything past that is not reaching, it is
        re-spelling the chord from scratch, which is the thing the plan is
        built in one pass to avoid. */
    constexpr int furthestReach = 12;

    /** How far the hands travel, *per voice*, scaled to keep whole numbers.

        An average rather than the sum `voiceLeadingDistance` gives, because the
        sum grows with how many notes are being counted: a three-note voicing
        compared against a four-note one has every note of the four to account
        for and only three to account for them with, so it scores worse for
        being smaller. That made the thinner shapes unreachable for a reason
        that had nothing to do with how they sound - they were never once
        chosen until this was an average.
    */
    int travelPerVoice (const std::vector<int>& from, const std::vector<int>& to)
    {
        if (from.empty() || to.empty())
            return 0;

        return voiceLeadingDistance (from, to) * 16
                 / static_cast<int> (from.size() + to.size());
    }

    /** The same mixer the comp generator uses, so one seed means one thing. */
    std::uint32_t mixSeed (std::uint32_t seed, std::uint32_t salt)
    {
        auto x = seed + 0x9e3779b9u * (salt + 1u);

        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;

        return x;
    }

    int rollOf (std::uint32_t seed, std::uint32_t salt)
    {
        return static_cast<int> (mixSeed (seed, salt) % 100u);
    }
}

namespace
{
    /** The search both overloads run, varied or not.

        `vary` is the line between two different questions. "What would a comper
        play here" has one answer and should keep having it: it is what
        `Show me a comp` shows, what a bar sounds when you land on it with no
        clock running, and the Dm7 into G7 the README walks through note by
        note. "What does the band play over this chorus" is the other, and a
        band that answered it identically every time was the thing this was
        opened up to fix.
    */
    Voicing searchCompingVoicing (const ChordSymbol& chord, const std::vector<int>& previousNotes,
                                  int lowestNote, int highestNote,
                                  bool vary, std::uint32_t seed)
    {
        const auto home = std::max (lowestNote,
                                    std::min (highestNote,
                                              naturalAnchorFor (VoicingType::twoHandedRootless)));

        // Nothing to lead from: the shape the suggester offers first, where it
        // naturally sits. That is the voicing the rest of the app would show for
        // this chord, and a tune should start on it rather than on whatever the
        // search happened to like.
        if (previousNotes.empty())
        {
            const auto opening = idiomaticVoicings (chord, VoicingType::twoHandedRootless, home);

            for (const auto& candidate : opening)
                if (! candidate.isEmpty()
                      && candidate.lowestNote() >= lowestNote
                      && candidate.highestNote() <= highestNote)
                    return candidate;

            return opening.empty() ? Voicing{} : opening.front();
        }

        /*  Now and then the hands move somewhere else in the window instead of
            taking the nearest thing - now and then rather than constantly,
            because a comper who changed register on every chord would be harder
            to follow than one who never did.

            Both halves are needed. Moving the tie-break alone moved nothing:
            the travel term is several times its size, so the nearest voicing
            went on winning and the reach never once reached. Reaching has to
            cost the voice leading its grip as well. */
        const auto reaching = vary && rollOf (seed, 7) < reachChance;
        const auto span = std::max (1, highestNote - twoHandedReach - lowestNote + 1);

        const auto pullTowards = reaching
            ? lowestNote + static_cast<int> (mixSeed (seed, 8)
                                               % static_cast<std::uint32_t> (span))
            : home;

        const auto leadWeight = reaching ? 1 : 3;

        /*  Nothing travels further than a hand reasonably reaches, reaching or
            not. Past that it is not a move, it is the chord being spelled from
            scratch - which is what planning in one pass exists to avoid. */
        const auto anchoredAt = *std::min_element (previousNotes.begin(), previousNotes.end());

        struct Candidate { Voicing voicing; int cost; int weight; };

        std::vector<Candidate> shortlist;

        Voicing best;
        Voicing bestOutside;
        auto bestCost = 0;
        auto bestOutsideCost = 0;

        /*  The sweep is the window's, not this file's - so a style that comps
            higher searches higher. It stops `twoHandedReach` below the ceiling
            because an anchor is a voicing's *bottom* note and one anchored any
            higher cannot fit under it anyway; sweeping to the ceiling would
            only generate candidates the filter below throws away. */
        for (auto anchor = lowestNote; anchor <= highestNote - twoHandedReach; ++anchor)
        {
            for (const auto& shape : compShapes())
            {
                // Unvaried, the house shape is the only shape there is.
                if (! vary && ! (shape.type == VoicingType::twoHandedRootless
                                   && shape.density == VoicingDensity::plain))
                    continue;

                for (const auto& candidate : idiomaticVoicings (chord, shape.type, anchor,
                                                                shape.density))
                {
                    if (candidate.isEmpty())
                        continue;

                    if (vary && std::abs (candidate.lowestNote() - anchoredAt) > furthestReach)
                        continue;

                    /*  The tie-break keeps a hand near where it belongs. Without
                        it two voicings the same distance away are decided by
                        loop order, and the one at the edge of the window wins as
                        often as not.

                        The shape's weight is deliberately not in here. Folded
                        into the cost it rules a shape out rather than making it
                        rarer, and what is wanted is a thin voicing now and then
                        - not one only when it is the best move on the board by a
                        distance. It decides the draw below instead. */
                    const auto cost = travelPerVoice (previousNotes, candidate.midiNotes) * leadWeight
                                        + std::abs (candidate.lowestNote() - pullTowards) * 2;

                    const auto inside = candidate.lowestNote() >= lowestNote
                                          && candidate.highestNote() <= highestNote;

                    if (inside)
                    {
                        shortlist.push_back ({ candidate, cost, shape.weight });

                        if (best.isEmpty() || cost < bestCost)
                        {
                            best = candidate;
                            bestCost = cost;
                        }
                    }
                    else if (bestOutside.isEmpty() || cost < bestOutsideCost)
                    {
                        bestOutside = candidate;
                        bestOutsideCost = cost;
                    }
                }
            }
        }

        if (best.isEmpty())
            return bestOutside;

        if (! vary)
            return best;

        /*  Among the ones that were nearly as good, never between a good one and
            a poor one: the spread is what makes this a choice rather than a
            gamble. Duplicates go first - the same notes reached from two anchors
            is one voicing offered twice, and it would weight itself. */
        std::vector<Candidate> nearlyAsGood;

        for (const auto& candidate : shortlist)
        {
            if (candidate.cost > bestCost + compSpread)
                continue;

            const auto already = std::any_of (nearlyAsGood.begin(), nearlyAsGood.end(),
                                              [&candidate] (const Candidate& taken)
                                              { return taken.voicing.midiNotes
                                                         == candidate.voicing.midiNotes; });

            if (! already)
                nearlyAsGood.push_back (candidate);
        }

        if (nearlyAsGood.empty())
            return best;

        /*  Drawn by weight, so the house shape comes up most often and a thin one
            comes up now and then. A straight draw would make every shortlisted
            shape equally likely, which is a different instrument. */
        auto total = 0;

        for (const auto& candidate : nearlyAsGood)
            total += candidate.weight;

        auto ticket = static_cast<int> (mixSeed (seed, 9)
                                          % static_cast<std::uint32_t> (std::max (1, total)));

        for (const auto& candidate : nearlyAsGood)
        {
            ticket -= candidate.weight;

            if (ticket < 0)
                return candidate.voicing;
        }

        return nearlyAsGood.front().voicing;
    }
}

Voicing compingVoicing (const ChordSymbol& chord, const std::vector<int>& previousNotes,
                        int lowestNote, int highestNote)
{
    return searchCompingVoicing (chord, previousNotes, lowestNote, highestNote, false, 0);
}

Voicing compingVoicing (const ChordSymbol& chord, const std::vector<int>& previousNotes,
                        int lowestNote, int highestNote, std::uint32_t seed)
{
    return searchCompingVoicing (chord, previousNotes, lowestNote, highestNote, true, seed);
}

int naturalAnchorFor (VoicingType type)
{
    switch (type)
    {
        case VoicingType::solo:
        case VoicingType::spread:            return 40;   // E2: the left hand holds the bass
        case VoicingType::shell:
        case VoicingType::rootPosition:
        case VoicingType::twoHandedRootless: return 48;   // C3: the root or the guide tones
        case VoicingType::rootlessLeftHand:
        case VoicingType::singleNote:
        case VoicingType::unknown:           break;
    }

    return 53;   // F3: where a rootless left hand sits under a soloist
}

int soloLeftHandPartner (const ChordSymbol& chord, int rootNote)
{
    // A root and a seventh say the whole chord by themselves, so they are the
    // first choice - but the pair turns to mud low down, where the ear wants a
    // plain consonance instead. The interval opens out as the root descends.
    if (rootNote >= 48)                     // C3 and above: the seventh sounds
        return seventhOf (chord);

    if (rootNote >= 40)                     // E2 to B2: the fifth is still clear
        return fifthOf (chord);

    return semitonesPerOctave;              // below that, only the octave
}

std::vector<Voicing> idiomaticVoicings (const ChordSymbol& chord, VoicingType type,
                                        int anchorNote, VoicingDensity density)
{
    const auto third = thirdOf (chord);
    const auto seventh = seventhOf (chord);
    const auto ninth = ninthOf (chord);
    const auto fifth = fifthOf (chord);
    const auto thirteenth = thirteenthOf (chord);
    const auto rich = density == VoicingDensity::rich;

    std::vector<Voicing> voicings;

    // A 6th chord's seventh slot and its thirteenth are the same note, so the
    // shapes that ask for both fall back on the ninth for the colour.
    const auto rootless = [&] (std::vector<int> offsets, std::size_t wanted)
    {
        return stack (chord, distinctOffsets (std::move (offsets), ninth, wanted), anchorNote);
    };

    switch (type)
    {
        case VoicingType::shell:
            // A shell is the root and the two guide tones. There is no third
            // note to add without it stopping being a shell, and no tension to
            // swap in without losing one of the two notes that make it work, so
            // this is the one shape with no richer form: asking for colour here
            // gets the same two shells back.
            voicings.push_back (stack (chord, { 0, third, seventh }, anchorNote));
            voicings.push_back (stack (chord, { 0, seventh, third }, anchorNote));
            break;

        case VoicingType::rootPosition:
            if (rich)
            {
                // Still the root in the bass with the chord above it, but the
                // tensions take the places the plain tones were holding.
                voicings.push_back (stack (chord, distinctOffsets ({ 0, third, seventh, ninth }, fifth, 4), anchorNote));
                voicings.push_back (stack (chord, { 0, fifth, seventh, third }, anchorNote));
                break;
            }

            voicings.push_back (stack (chord, { 0, third, fifth, seventh }, anchorNote));
            voicings.push_back (stack (chord, { 0, third, fifth, seventh, ninth }, anchorNote));
            break;

        case VoicingType::spread:
            if (rich)
            {
                voicings.push_back (spreadVoicing (chord, distinctOffsets ({ seventh, third, thirteenth, ninth }, fifth, 4), anchorNote));
                voicings.push_back (spreadVoicing (chord, { third, seventh, ninth, fifth }, anchorNote));
                break;
            }

            voicings.push_back (spreadVoicing (chord, { seventh, third, fifth }, anchorNote));
            voicings.push_back (spreadVoicing (chord, { third, seventh, ninth }, anchorNote));
            break;

        case VoicingType::rootlessLeftHand:
        case VoicingType::unknown:
        case VoicingType::singleNote:
            if (rich)
            {
                // A fourth note of colour, placed so the whole shape still sits
                // under one hand: the thirteenth goes below the seventh rather
                // than on top of the ninth, where it would put the voicing out
                // of reach.
                voicings.push_back (rootless ({ third, thirteenth, seventh, ninth }, 4));
                voicings.push_back (rootless ({ seventh, ninth, third, thirteenth }, 4));
                break;
            }

            // The two shapes every other rootless voicing is built from: the A
            // form up from the 3rd, the B form up from the 7th.
            voicings.push_back (rootless ({ third, seventh, ninth }, 3));
            voicings.push_back (rootless ({ seventh, third, thirteenth }, 3));
            break;

        case VoicingType::twoHandedRootless:
            // Guide tones in the left hand, colour in the right.
            if (rich)
            {
                voicings.push_back (twoHanded (chord, { third, seventh }, distinctOffsets ({ ninth, third, thirteenth }, fifth, 3), anchorNote));
                voicings.push_back (twoHanded (chord, { seventh, third }, distinctOffsets ({ thirteenth, ninth, fifth }, third, 3), anchorNote));
                break;
            }

            voicings.push_back (twoHanded (chord, { third, seventh }, distinctOffsets ({ ninth, thirteenth }, fifth, 2), anchorNote));
            voicings.push_back (twoHanded (chord, { seventh, third }, distinctOffsets ({ thirteenth, ninth }, fifth, 2), anchorNote));
            break;

        case VoicingType::solo:
        {
            // Playing alone, nobody else is holding the root down, so the left
            // hand has to - with a partner the register can carry. Then the
            // right hand says what the left could not: the guide tone it is
            // missing first, and the colour above that.
            const auto rootNote = rootAtOrAbove (chord, anchorNote);
            const auto partner = soloLeftHandPartner (chord, rootNote);
            const auto leftHandSaidTheSeventh = toPitchClass (partner) == toPitchClass (seventh);

            const auto colour = leftHandSaidTheSeventh
                              ? distinctOffsets ({ third, thirteenth, ninth }, fifth, 3)
                              : distinctOffsets ({ third, seventh, ninth }, fifth, 3);

            // The other way round: the left hand states the root as a bare
            // octave, which is safe in any register, and hands the seventh over
            // to the right along with the rest of the colour.
            const auto octaveColour = distinctOffsets ({ third, seventh, ninth }, fifth, 3);

            if (rich)
            {
                // The 3rd is the note worth doubling here: it lands a tone or
                // so above the ninth, where the right hand can still reach it,
                // and doubling it is how a solo player fills the space the
                // missing bass player leaves.
                auto fuller = colour;
                fuller.push_back (third);

                auto fullerOctave = octaveColour;
                fullerOctave.push_back (third);

                voicings.push_back (solo (chord, partner, fuller, anchorNote));
                voicings.push_back (solo (chord, semitonesPerOctave, fullerOctave, anchorNote));
                break;
            }

            voicings.push_back (solo (chord, partner, colour, anchorNote));
            voicings.push_back (solo (chord, semitonesPerOctave, octaveColour, anchorNote));
            break;
        }
    }

    return voicings;
}

} // namespace jazz::core
