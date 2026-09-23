#include "jazz/core/Groove.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace jazz::core
{

namespace
{
    /*  An even eighth, and the tolerance for calling one even.

        A groove is "even" when its upbeat is written where it is played. The
        tolerance exists because these are doubles written by hand, not because
        0.5000001 is a feel anybody meant. */
    constexpr double evenUpbeat = 0.5;
    constexpr double evenEnough = 0.001;

    /** Lower-cased, with everything that is not a letter turned into a space.

        A style marking is prose somebody typed - "Up-tempo Swing", "Medium
        Swing (Basie)", "Slow 12/8 Blues". Reducing it to words is what lets
        this match on "swing" without matching "swingers" and without caring
        which punctuation the writer reached for.
    */
    std::string wordsOf (std::string_view text)
    {
        std::string out;
        out.reserve (text.size() + 2);
        out.push_back (' ');

        for (auto character : text)
        {
            const auto raw = static_cast<unsigned char> (character);
            out.push_back (std::isalpha (raw) != 0
                               ? static_cast<char> (std::tolower (raw))
                               : ' ');
        }

        out.push_back (' ');
        return out;
    }

    bool saysWord (const std::string& words, std::string_view word)
    {
        std::string padded;
        padded.reserve (word.size() + 2);
        padded.push_back (' ');
        padded.append (word);
        padded.push_back (' ');

        return words.find (padded) != std::string::npos;
    }
}

bool GrooveDefinition::isEven() const noexcept
{
    return std::abs (upbeatWhenSlow - evenUpbeat) < evenEnough
        && std::abs (upbeatWhenFast - evenUpbeat) < evenEnough;
}

const std::vector<GrooveDefinition>& grooves()
{
    static const std::vector<GrooveDefinition> catalogue = []
    {
        std::vector<GrooveDefinition> built;

        {
            /*  Even eighths, and the one every unrecognised tune gets.

                First in the list on purpose: `grooveFor` falls back to the
                front, and the safe fallback for "I do not know this feel" is
                to play what is written rather than to bend it. Bossa, samba
                and most latin feels are genuinely this - the eighth is even
                and the swing people hear is in the accents, not the placement. */
            GrooveDefinition even;
            even.key = "straight";
            even.name = "Even eighths";
            even.summary = "The eighth falls where it is written - bossa, samba, "
                           "latin, and anything that never said it swung.";
            even.upbeatWhenSlow = 0.5;
            even.upbeatWhenFast = 0.5;
            even.feel = Subdivision::eighth;
            built.push_back (even);
        }

        {
            /*  The default swing, and the only groove here whose feel slides
                with the tempo.

                Corcoran & Frieler measured 456 Weimar Jazz Database solos and
                found swing eighths "only slightly uneven", BUR 1.3:1 - which is
                0.565 of the beat, not the 0.667 everybody writes down - with
                ratios near 2:1 used "only occasionally, mostly at slow or
                moderate tempos". So the curve runs from the triplet at ballad
                tempo to very nearly even at speed, which is also what happens
                to a player's hands. The 2:1 this replaces was a single constant
                applied at every tempo, and it was the fast end it was most
                wrong about. */
            GrooveDefinition swing;
            swing.key = "swing";
            swing.name = "Medium swing";
            swing.summary = "The upbeat late and easing towards even as the tempo "
                            "climbs - the triplet at a ballad, barely uneven up top.";
            swing.upbeatWhenSlow = 2.0 / 3.0;
            swing.upbeatWhenFast = 0.57;
            swing.slowBpm = 100;
            swing.fastBpm = 240;
            swing.feel = Subdivision::eighth;
            built.push_back (swing);
        }

        {
            /*  The fourth of five.

                A beat in five with the upbeat on the fourth part is 0.6, which
                sits between even and the triplet and is a feel of its own
                rather than a weak swing - the one on Anomolie's "Velours". It
                is also, coincidentally, where the Jazz Trio Database's piano
                soloists average out (1.53:1), so it doubles as the "modern
                straight-ahead" feel. Flat, because it is a placement somebody
                chose rather than a ratio that relaxes with speed.

                This is the groove that proves the grid did not need changing:
                24 ticks do not divide by five, and nothing here needed them to. */
            GrooveDefinition light;
            light.key = "light";
            light.name = "Fourth of five";
            light.summary = "The upbeat on the fourth of five - between even and "
                            "the triplet, and a feel in its own right.";
            light.upbeatWhenSlow = 0.6;
            light.upbeatWhenFast = 0.6;
            light.feel = Subdivision::eighth;
            built.push_back (light);
        }

        {
            /*  Nearer the dotted eighth than the triplet.

                A ballad has room to place the upbeat late and does, which is
                the opposite direction from the tempo curve above - so this is
                its own groove rather than the swing curve read at a slow
                tempo. Flat across its band because a ballad that sped up would
                stop being one. */
            GrooveDefinition ballad;
            ballad.key = "ballad";
            ballad.name = "Ballad";
            ballad.summary = "Late, nearer a dotted eighth than a triplet - the "
                             "room a slow tune leaves.";
            ballad.upbeatWhenSlow = 0.72;
            ballad.upbeatWhenFast = 0.72;
            ballad.feel = Subdivision::eighth;
            built.push_back (ballad);
        }

        {
            /*  The dotted eighth itself: a blues shuffle, played hard.

                0.75 is the widest thing in this catalogue and the one people
                actually notate, as a dotted eighth and a sixteenth. Anything
                later stops being a swung eighth and becomes two notes with a
                rest between them. */
            GrooveDefinition shuffle;
            shuffle.key = "shuffle";
            shuffle.name = "Shuffle";
            shuffle.summary = "The dotted eighth - a blues shuffle, the hardest "
                              "swing here.";
            shuffle.upbeatWhenSlow = 0.75;
            shuffle.upbeatWhenFast = 0.75;
            shuffle.feel = Subdivision::eighth;
            built.push_back (shuffle);
        }

        return built;
    }();

    return catalogue;
}

const GrooveDefinition& grooveFor (std::string_view key)
{
    const auto& catalogue = grooves();

    for (const auto& groove : catalogue)
        if (groove.key == key)
            return groove;

    return catalogue.front();
}

const GrooveDefinition& grooveForStyleWord (std::string_view chartStyle)
{
    const auto words = wordsOf (chartStyle);

    /*  Most particular first, because these markings stack: "Slow Blues
        Shuffle" says both shuffle and blues, and "Medium Up Swing" says both
        swing and a tempo. Whichever word names the narrowest feel wins.

        Nothing here matches "funk". A funk groove lays back on the *sixteenth*
        and this model bends only the eighth, so mapping it to any of these
        would be claiming a feel the page cannot play - see the note in
        docs/RHYTHM.md. It reads as even, which is what an even-eighth funk
        actually is. */
    if (saysWord (words, "shuffle"))
        return grooveFor ("shuffle");

    if (saysWord (words, "ballad"))
        return grooveFor ("ballad");

    if (saysWord (words, "swing") || saysWord (words, "bebop"))
        return grooveFor ("swing");

    return grooveFor ("straight");
}

double upbeatAt (const GrooveDefinition& groove, int bpm)
{
    const auto slow = groove.slowBpm;
    const auto fast = groove.fastBpm;

    // A band that is not one - reversed, or a single tempo wide - has no middle
    // to interpolate across, so the slow end is the whole answer.
    if (fast <= slow)
        return groove.upbeatWhenSlow;

    if (bpm <= slow) return groove.upbeatWhenSlow;
    if (bpm >= fast) return groove.upbeatWhenFast;

    const auto through = static_cast<double> (bpm - slow)
                       / static_cast<double> (fast - slow);

    return groove.upbeatWhenSlow
         + (groove.upbeatWhenFast - groove.upbeatWhenSlow) * through;
}

} // namespace jazz::core
