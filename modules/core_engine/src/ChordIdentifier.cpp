#include "jazz/core/ChordIdentifier.h"

#include <algorithm>

namespace jazz::core
{

namespace
{
    /** The vocabulary the identifier can name, written the way a player would.

        Held as suffixes and parsed by the chord parser rather than built by
        hand, so the identifier can only ever name chords the rest of the engine
        also understands.

        The dominant tensions are generated rather than listed. A hand-written
        list kept missing real chords - a 7b9#9b13 is what an altered dominant
        is when the player leaves the #11 out, which is most of the time - and
        the reason is that the family is combinatorial: a ninth, a sharp
        eleventh and a thirteenth, each with its own choices. Thirty of them
        written out by hand will always be missing the thirty-first.
    */
    const std::vector<std::string>& templateSuffixes()
    {
        static const std::vector<std::string> suffixes = []
        {
            std::vector<std::string> built {
                // Triads first: the simplest name that explains the notes should win.
                "", "m", "dim", "+", "sus4", "sus2",
                "6", "m6", "6/9", "add9", "madd9",
                "maj7", "maj9", "maj11", "maj13", "maj7#11", "maj9#11", "maj13#11",
                "maj7#5",
                "m7", "m9", "m11", "m13",
                "mMaj7", "mMaj9", "mMaj11",
                "m7b5", "m9b5", "m11b5", "dim7", "dimMaj7", "m6/9",
                // The sus family goes all the way up: a 13sus4 is a sound in its own
                // right, not a 9sus4 with a note left over.
                "7sus4", "9sus4", "13sus4", "7sus4b9",
                // An eleventh chord keeps its natural 11, which the generated
                // tensions below never use.
                "11"
            };

            // Every dominant the tensions can spell: the ninth in each of its
            // forms, with or without the sharp eleventh, with or without a
            // thirteenth. "7b9#9#11b13" comes back out of the parser as "alt".
            for (const auto* ninth : { "", "9", "b9", "#9", "b9#9" })
                for (const auto* eleventh : { "", "#11" })
                    for (const auto* thirteenth : { "", "13", "b13" })
                        built.push_back (std::string ("7") + ninth + eleventh + thirteenth);

            // A moved fifth is its own chord, but only the plain forms earn a
            // place: every name in here competes with every other, and one that
            // nobody writes still wins whenever it happens to account for all
            // the notes. A complete Asus4b9 beat a rootless C13 to the answer
            // while this list was being written, which is the whole argument
            // for keeping the vocabulary to chords players actually put on
            // charts rather than to everything the parser will accept.
            built.push_back ("7b5");
            built.push_back ("7#5");

            return built;
        }();

        return suffixes;
    }

    /** Every template in every key, built once. */
    const std::vector<ChordSymbol>& allTemplates()
    {
        static const std::vector<ChordSymbol> templates = []
        {
            std::vector<ChordSymbol> built;
            std::vector<std::string> seen;

            // Different suffixes can spell the same chord - "7#11 13" and
            // "79#11 13" are both a 13#11 - and a name offered twice reads as
            // two different answers to the same question.
            for (PitchClass root = 0; root < semitonesPerOctave; ++root)
            {
                for (const auto& suffix : templateSuffixes())
                {
                    const auto chord = ChordSymbol::parse (pitchClassName (root) + suffix);

                    if (! chord.has_value())
                        continue;

                    // Keyed on the notes as well as the name: two spellings are
                    // the same template only when they sound the same, so a name
                    // that failed to say everything about its chord can never
                    // quietly drop a different chord from the vocabulary.
                    const auto key = chord->toString() + "/" + std::to_string (chord->pitchClassMask());

                    if (std::find (seen.begin(), seen.end(), key) != seen.end())
                        continue;

                    seen.push_back (key);
                    built.push_back (*chord);
                }
            }

            return built;
        }();

        return templates;
    }
}

std::vector<ChordCandidate> ChordIdentifier::identify (const Voicing& voicing) const
{
    // Two notes are an interval; naming one would be a guess dressed up as an answer.
    if (voicing.size() < 3)
        return {};

    const auto playedMask = voicing.pitchClassMask();
    const auto bass = toPitchClass (voicing.lowestNote());

    std::vector<ChordCandidate> candidates;

    for (const auto& candidateChord : allTemplates())
    {
        const auto tones = candidateChord.chordTones();

        std::uint16_t toneMask = 0;
        auto missingEssential = false;
        auto rootPlayed = false;
        auto omissionCost = 0;
        std::vector<std::string> omitted;

        for (const auto& tone : tones)
        {
            const auto pitchClass = toPitchClass (candidateChord.root() + tone.semitones);
            toneMask |= static_cast<std::uint16_t> (1u << pitchClass);

            const auto played = (playedMask & (1u << pitchClass)) != 0;

            if (tone.role == ChordToneRole::root)
            {
                rootPlayed = played;

                if (! played)
                {
                    omitted.push_back ("root");
                    omissionCost += 30;
                }

                continue;
            }

            if (played)
                continue;

            // A name that promises a 3rd or a 7th and does not deliver one is
            // the wrong name, whatever else fits.
            if (tone.essential)
            {
                missingEssential = true;
                break;
            }

            omitted.push_back (tone.label);

            // Nobody misses a 5th. A missing 9th or 13th means the name is
            // promising colour that was never played, which is worse.
            omissionCost += tone.role == ChordToneRole::fifth ? 4 : 14;
        }

        if (missingEssential)
            continue;

        // Every note has to be explained, or this is not what was played.
        if ((playedMask & ~toneMask) != 0)
            continue;

        if (! rootPlayed && ! options.includeRootlessReadings)
            continue;

        // A name has to be mostly present to be the name: three notes are not a
        // thirteenth chord with half of it left out.
        if (omitted.size() >= voicing.pitchClasses().size())
            continue;

        const auto rootInBass = bass == candidateChord.root();
        const auto bassIsChordTone = (toneMask & (1u << bass)) != 0;

        auto score = 100 - omissionCost;

        // Bass disagreement is a penalty rather than root-in-bass being a
        // bonus, so a good bass can never mask missing notes.
        if (rootPlayed && ! rootInBass)
            score -= 12;

        if (! bassIsChordTone)
            score -= 12;

        ChordCandidate candidate;
        candidate.chord = rootInBass ? candidateChord : candidateChord.overBass (bass);
        candidate.score = std::clamp (score, 0, 100);
        candidate.rootInBass = rootInBass;
        candidate.rootPlayed = rootPlayed;
        candidate.omittedTones = std::move (omitted);

        candidates.push_back (std::move (candidate));
    }

    std::stable_sort (candidates.begin(), candidates.end(),
                      [] (const ChordCandidate& a, const ChordCandidate& b)
                      {
                          if (a.score != b.score)
                              return a.score > b.score;

                          // Same fit: the name with fewer notes in it wins.
                          return a.chord.chordTones().size() < b.chord.chordTones().size();
                      });

    if (options.maxCandidates > 0
        && candidates.size() > static_cast<std::size_t> (options.maxCandidates))
        candidates.resize (static_cast<std::size_t> (options.maxCandidates));

    return candidates;
}

} // namespace jazz::core
