#pragma once

#include "jazz/core/ChordSymbol.h"
#include "jazz/core/Voicing.h"

#include <string>
#include <vector>

namespace jazz::core
{

/** One reading of a set of notes: a chord symbol that accounts for them. */
struct ChordCandidate
{
    ChordSymbol chord;                    ///< the symbol, with a slash bass where the bass is not the root
    int score {};                         ///< 0-100, how well the notes fit this name
    bool rootInBass {};                   ///< the lowest note is the chord's root
    bool rootPlayed {};                   ///< the root is somewhere in the voicing
    std::vector<std::string> omittedTones; ///< tones the name implies that were not played, e.g. "5"
};

/** Names a set of notes.

    This answers "what did I just play", with no chart and no expected chord -
    the question a player asks about a shape they found under their hands. Every
    note has to be accounted for: a name that cannot explain one of the notes is
    not offered, so a cluster with no reading returns nothing rather than the
    least bad guess.
*/
class ChordIdentifier
{
public:
    struct Options
    {
        /** How many readings to return, best first. */
        int maxCandidates { 4 };

        /** Offer names whose root is not in the voicing - the rootless
            voicings a piano player leaves to the bass.
        */
        bool includeRootlessReadings { true };
    };

    ChordIdentifier() = default;
    explicit ChordIdentifier (Options optionsToUse) : options (optionsToUse) {}

    /** Readings of @p voicing, best first. Empty when nothing explains it. */
    std::vector<ChordCandidate> identify (const Voicing& voicing) const;

private:
    Options options;
};

} // namespace jazz::core
