#pragma once

#include "jazz/core/Rhythm.h"

#include <string>
#include <string_view>
#include <vector>

namespace jazz::core
{

/** How a tune's eighths are played, as a ratio rather than as a place.

    `Rhythm.h` says swing is deliberately not in the grid, because "an engine
    that wrote swing into the grid would be holding a tempo-dependent feel in
    the one place that must not know about time". That rule stands and this
    file does not bend it. **The engine states a relationship; the shell
    supplies the tempo.** Nothing here moves a tick, nothing here is a
    duration, and nothing here learns what o'clock it is - a groove says where
    in a beat an upbeat *falls*, which is a dimensionless fraction, no more a
    time than `CompStyleDefinition::heldFor` is.

    **The grid stays at 24 and never learns about fives.** A beat divided into
    five has no tick - 24 / 5 is 4.8 - and needs none, because a groove whose
    upbeat sits on the fourth of five is describing a *ratio a shell plays at*
    and not a position a note is written at. That separation is what lets a
    feel like the one on Anomolie's "Velours" be described at all without the
    engine growing a second grid to write it in.

    **Why a groove and not a comping style.** The feel belongs to the tune, not
    to the piano part: the ride cymbal, the walking bass, the comp and a
    written line all have to agree about where the "and" is, and a ratio living
    on one player's figure would be three of them reading their feel out of the
    fourth's settings. `CompStyleDefinition::feel` still answers its own
    question - which grid *this figure* is counted on - and still decides
    whether an upbeat is an eighth that wants bending at all.
*/
struct GrooveDefinition
{
    std::string key;       ///< stable across versions; what a shell stores
    std::string name;      ///< "Medium swing"
    std::string summary;   ///< one line, for a menu

    /** Where the upbeat eighth falls, as a fraction of the beat.

        0.5 is even, 0.6 the fourth of five, 2/3 the middle triplet, 0.75 the
        dotted eighth. Two values rather than one because the feel slides with
        the tempo as well as with the tune: Corcoran & Frieler's 456-solo study
        found swing eighths "only slightly uneven" at speed and ratios near 2:1
        used "mostly at slow or moderate tempos". A groove whose feel does not
        slide sets both to the same number and says so in its comment.
    */
    double upbeatWhenSlow { 0.5 };
    double upbeatWhenFast { 0.5 };

    /** The tempo band the slide happens over, in beats per minute.

        At or below `slowBpm` the upbeat sits at `upbeatWhenSlow`; at or above
        `fastBpm` at `upbeatWhenFast`; between them it slides. Equal endpoints
        make the band irrelevant, which is why a flat groove may leave these at
        whatever reads best.
    */
    int slowBpm { 100 };
    int fastBpm { 240 };

    /** The subdivision this groove's upbeat is counted on.

        Only `Subdivision::eighth` is bent. A ballad written in triplets is
        already notated where it is played, and bending it again would move a
        note nobody asked to move - the same reason the page has never swung a
        triplet or a sixteenth.
    */
    Subdivision feel { Subdivision::eighth };

    /** True when this groove leaves the eighth exactly where it is written. */
    bool isEven() const noexcept;
};

/** Every groove there is, in the order a menu would show them.

    Built in code beside the comment arguing each one's case, the way
    `compStyles()` and `scaleStyles()` are - one artifact, read by the shell
    that plays it and by the menu that offers it, so the two cannot drift.
*/
const std::vector<GrooveDefinition>& grooves();

/** The groove with this key, or the first one when the key is unknown.

    Never empty, for the reason `compStyleFor` gives about a renamed style: a
    tune whose feel this version does not recognise should still be playable.

    The returned reference is into a function-local `static`, so it outlives
    every caller. `std::string_view` rather than `const std::string&` for the
    reason written at `Comping.h`'s own lookup - a temporary `std::string` at
    each literal call site sets off -Wdangling-reference on every one of them.
*/
const GrooveDefinition& grooveFor (std::string_view key);

/** The groove a chart's style marking implies: "Medium Swing" -> swing.

    **Unknown means even, and that is the important half.** A chart carrying no
    style at all, or one this version cannot read, is played straight - which
    is exactly what the page did before grooves existed, where a bare
    `/swing/i` test on the same text decided it. A fallback that guessed at
    swing would start bending the eighths of every tune that never said it
    swung.

    Matched on words rather than on the whole string, because the marking is
    prose a person wrote: "Up-tempo Swing", "Medium Swing", "Slow Blues
    Shuffle" all have to land, and none of them is a key.
*/
const GrooveDefinition& grooveForStyleWord (std::string_view chartStyle);

/** Where @p groove puts the upbeat at @p bpm, as a fraction of the beat.

    Clamped outside the groove's band and interpolated inside it. This is the
    one place the tempo meets the feel, and it is a pure function of two
    numbers the caller already has - the engine is told the tempo, it never
    asks.
*/
double upbeatAt (const GrooveDefinition& groove, int bpm);

} // namespace jazz::core
