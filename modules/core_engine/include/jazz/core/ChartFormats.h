#pragma once

#include "jazz/core/Chart.h"

#include <string>
#include <string_view>
#include <vector>

namespace jazz::core
{

//==============================================================================
// iReal Pro
//
// The format is a URL: irealbook://Title=Composer=Style=Key=n=BODY, where the
// body is chord text with bar lines, section markers and repeat signs. Chord
// symbols use iReal Pro's own shorthand - ^ for major 7, - for minor, h for
// half-diminished, o for diminished - which this engine's parser already reads.

/** A chord symbol written the way iReal Pro writes it: Cmaj7 -> C^7. */
std::string toIRealProSymbol (const ChordSymbol& chord);

/** The whole chart as an irealbook:// URL, ready to hand to iReal Pro. */
std::string exportIRealPro (const Chart& chart);

/** Reads an irealbook:// URL, or a bare iReal Pro body.

    The newer irealb:// links carry a scrambled body; those are detected and
    reported rather than half-read, because a wrong chart is worse than none.
*/
ChartParseResult importIRealPro (std::string_view text);

/** True when the text looks like an iReal Pro link or body rather than plain
    progression text.
*/
bool looksLikeIRealPro (std::string_view text);

//==============================================================================
// Charts recovered from a page
//
// A PDF is a page of positioned text, so recovering a chart from one splits in
// two: pulling the text and its positions out of the file, which is the shell's
// job and needs a PDF library, and working out which of that text is a chord
// chart, which is this engine's job and needs none.

/** One run of text on a page.

    Note the y convention: distance from the top of the page, increasing
    downward, which is reading order rather than PDF order. A reader working in
    PDF user space, where y grows upward from the bottom, converts with
    pageHeight - y before handing runs over. One convention, converted at the
    edge, beats a flag every caller has to get right.


    A PDF rarely hands over a chord symbol in one piece: "Dm7" usually arrives
    as "D", "m" and "7", three runs a few points apart. Runs are stitched back
    into words before anything is read as a chord.
*/
struct PlacedText
{
    double x {};       ///< distance from the left edge
    double y {};       ///< distance from the TOP of the page, increasing downward
    std::string text;
    double width {};   ///< advance width of the run, if the reader knows it
};

struct PageReadingOptions
{
    /** Text within this much of the same y sits on the same line. */
    double lineTolerance { 6.0 };

    /** Two chords closer than this fraction of the line's usual spacing are
        taken to share a bar rather than to start a new one.
    */
    double sameBarRatio { 0.6 };

    /** Runs whose ends are within this many points belong to the same word.
        Only used when the reader supplies run widths.
    */
    double wordGap { 2.5 };
};

/** Rebuilds a chart from the text on a page.

    Anything that does not read as a chord symbol is ignored, so titles, tempo
    markings, bar numbers and page furniture fall away and the chords are left.
    Bars are inferred from the spacing between chords, because bar lines in a
    lead sheet are drawn as lines rather than written as text.
*/
ChartParseResult chartFromPlacedText (std::vector<PlacedText> items,
                                      PageReadingOptions options = {});

} // namespace jazz::core
