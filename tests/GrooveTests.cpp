#include "TestFramework.h"

#include "jazz/core/Groove.h"

#include <cmath>
#include <set>

using namespace jazz::core;

namespace
{
    bool near (double a, double b, double tolerance = 0.0005)
    {
        return std::abs (a - b) < tolerance;
    }
}

TEST ("every groove puts the upbeat somewhere an eighth could be")
{
    /*  Half the beat is even and three quarters is the dotted eighth. Outside
        that pair is not a feel, it is a typo: below 0.5 the upbeat would be
        early, which nothing in this idiom does, and above 0.75 it stops being a
        swung eighth and becomes two notes with a rest between them. */
    for (const auto& groove : grooves())
    {
        CHECK (! groove.key.empty());
        CHECK (! groove.name.empty());
        CHECK (! groove.summary.empty());

        CHECK (groove.upbeatWhenSlow >= 0.5);
        CHECK (groove.upbeatWhenSlow <= 0.75);
        CHECK (groove.upbeatWhenFast >= 0.5);
        CHECK (groove.upbeatWhenFast <= 0.75);

        // A groove that swung harder the faster it went would be a sign
        // slow and fast had been filled in the wrong way round.
        CHECK (groove.upbeatWhenSlow >= groove.upbeatWhenFast);

        CHECK (groove.slowBpm > 0);
        CHECK (groove.fastBpm > 0);
    }
}

TEST ("the grooves on offer are genuinely different from one another")
{
    /*  The shape CompingTests uses on the comping styles, for the same reason:
        a catalogue fitted to one entry passes every test written about that
        entry. Checked at both ends of the tempo band, because two grooves that
        differ at a ballad tempo and agree at speed are one groove with a
        curve. */
    std::set<std::string> keys;

    for (const auto& groove : grooves())
        keys.insert (groove.key);

    CHECK_EQ (keys.size(), grooves().size());

    const auto& even = grooveFor ("straight");
    const auto& swing = grooveFor ("swing");
    const auto& light = grooveFor ("light");
    const auto& shuffle = grooveFor ("shuffle");

    for (auto bpm : { 80, 140, 280 })
    {
        CHECK (! near (upbeatAt (even, bpm), upbeatAt (light, bpm)));
        CHECK (! near (upbeatAt (light, bpm), upbeatAt (shuffle, bpm)));
        CHECK (! near (upbeatAt (even, bpm), upbeatAt (shuffle, bpm)));
    }

    // And the one that is meant to slide really does, or the tempo band is
    // decoration.
    CHECK (! near (upbeatAt (swing, 80), upbeatAt (swing, 280)));
    CHECK (upbeatAt (swing, 80) > upbeatAt (swing, 280));
}

TEST ("an even groove leaves the eighth exactly where it is written")
{
    CHECK (grooveFor ("straight").isEven());
    CHECK (near (upbeatAt (grooveFor ("straight"), 60), 0.5));
    CHECK (near (upbeatAt (grooveFor ("straight"), 300), 0.5));

    // The negative half: nothing else here is even, or `isEven` would be a
    // function that always says yes.
    CHECK (! grooveFor ("swing").isEven());
    CHECK (! grooveFor ("light").isEven());
    CHECK (! grooveFor ("ballad").isEven());
    CHECK (! grooveFor ("shuffle").isEven());
}

TEST ("the fourth of five is the fourth of five")
{
    /*  The groove that proves the grid did not need changing: 24 ticks do not
        divide by five, and this feel needs no tick of its own because it is a
        ratio a shell plays at. */
    CHECK (near (upbeatAt (grooveFor ("light"), 120), 0.6));
    CHECK (near (upbeatAt (grooveFor ("shuffle"), 120), 0.75));
    CHECK (near (upbeatAt (grooveFor ("swing"), 100), 2.0 / 3.0));
}

TEST ("the tempo curve is clamped outside its band and slides inside it")
{
    const auto& swing = grooveFor ("swing");

    CHECK (near (upbeatAt (swing, swing.slowBpm), swing.upbeatWhenSlow));
    CHECK (near (upbeatAt (swing, swing.fastBpm), swing.upbeatWhenFast));

    // Clamped, not extrapolated - a tune at 40bpm does not swing harder than
    // the triplet, and one at 300 does not pass through even and come out early.
    CHECK (near (upbeatAt (swing, 40), swing.upbeatWhenSlow));
    CHECK (near (upbeatAt (swing, 300), swing.upbeatWhenFast));

    // Halfway along the band is halfway between the two ends.
    const auto middle = (swing.slowBpm + swing.fastBpm) / 2;
    const auto expected = (swing.upbeatWhenSlow + swing.upbeatWhenFast) / 2.0;
    CHECK (near (upbeatAt (swing, middle), expected, 0.005));

    // Monotone the whole way, so there is no tempo at which speeding up swings
    // you harder.
    auto last = upbeatAt (swing, 40);

    for (auto bpm = 41; bpm <= 300; ++bpm)
    {
        const auto now = upbeatAt (swing, bpm);
        CHECK (now <= last + 0.0005);
        last = now;
    }
}

TEST ("a groove nobody has heard of is still playable")
{
    // The promise `compStyleFor` makes, for the same reason: a tune whose feel
    // this version cannot name should still play.
    CHECK_EQ (grooveFor ("no-such-groove").key, grooves().front().key);
    CHECK_EQ (grooveFor ("").key, grooves().front().key);

    // And the fallback is the even one, so an unknown feel is played as
    // written rather than bent into a swing nobody asked for.
    CHECK (grooveFor ("no-such-groove").isEven());
}

TEST ("a chart's style marking picks the feel, and silence picks none")
{
    CHECK_EQ (grooveForStyleWord ("Medium Swing").key, std::string ("swing"));
    CHECK_EQ (grooveForStyleWord ("Up-tempo Swing").key, std::string ("swing"));
    CHECK_EQ (grooveForStyleWord ("SWING").key, std::string ("swing"));
    CHECK_EQ (grooveForStyleWord ("Bebop").key, std::string ("swing"));

    CHECK_EQ (grooveForStyleWord ("Ballad").key, std::string ("ballad"));
    CHECK_EQ (grooveForStyleWord ("Slow Blues Shuffle").key, std::string ("shuffle"));

    /*  The important half: anything unrecognised is even. This is what the page
        did before grooves existed - a bare /swing/i test on the same text - and
        a fallback that guessed at swing would start bending the eighths of
        every tune that never said it swung. */
    CHECK (grooveForStyleWord ("").isEven());
    CHECK (grooveForStyleWord ("Bossa Nova").isEven());
    CHECK (grooveForStyleWord ("Samba").isEven());
    CHECK (grooveForStyleWord ("Even 8ths").isEven());
    CHECK (grooveForStyleWord ("Funk").isEven());

    // The negative control on the matcher: a word that merely contains "swing"
    // is not the word, or every tune by a band called the Swingers would swing.
    CHECK (grooveForStyleWord ("The Swingers").isEven());
}

TEST ("a marking that names two feels is read as the narrower one")
{
    /*  These markings stack, and the order the matcher tries them in is the
        whole of the behaviour: "Slow Blues Shuffle" says shuffle and says
        nothing about swing, while "Medium Up Swing" says swing and a tempo. */
    CHECK_EQ (grooveForStyleWord ("Swing Shuffle").key, std::string ("shuffle"));
    CHECK_EQ (grooveForStyleWord ("Swing Ballad").key, std::string ("ballad"));
    CHECK_EQ (grooveForStyleWord ("Medium Up Swing").key, std::string ("swing"));
}
