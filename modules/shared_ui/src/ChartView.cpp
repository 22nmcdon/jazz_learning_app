#include "jazz/ui/ChartView.h"

namespace jazz::ui
{

using namespace juce;

namespace
{
    // Real signs, not the "b" and "#" the engine spells chords with. No chord
    // quality the engine writes contains either character - m, maj, min, dim,
    // aug, sus, alt - so both are always accidentals wherever they appear.
    const String flatSign  { CharPointer_UTF8 ("\xe2\x99\xad") };
    const String sharpSign { CharPointer_UTF8 ("\xe2\x99\xaf") };

    constexpr float accidentalScale = 0.82f;

    /** One run of a chord symbol: the glyphs plus how they are set. */
    struct SymbolRun
    {
        String text;
        bool isAccidental { false };
    };

    /** Splits a symbol into the runs a chart would write it in.

        Only the accidentals are set apart, and only because handwriting faces
        have no flat or sharp glyph - they borrow the serif's. Nothing is
        raised: a chart writes "C13" on one line, and raising the trailing
        digits would also raise the 5 of an "Am7b5", which is an alteration
        rather than an extension.
    */
    std::vector<SymbolRun> runsFor (const String& symbol)
    {
        std::vector<SymbolRun> runs;
        String pending;

        const auto flush = [&runs, &pending]
        {
            if (pending.isNotEmpty())
            {
                runs.push_back ({ pending, false });
                pending.clear();
            }
        };

        for (int i = 0; i < symbol.length(); ++i)
        {
            const auto character = symbol[i];

            // No chord quality the engine writes contains a 'b' or a '#' - m,
            // maj, min, dim, aug, sus, alt - so both are always accidentals.
            if (character == 'b' || character == '#')
            {
                flush();
                runs.push_back ({ character == '#' ? sharpSign : flatSign, true });
                continue;
            }

            pending += String::charToString (character);
        }

        flush();
        return runs;
    }

    Font fontForRun (const SymbolRun& run, float height)
    {
        return run.isAccidental ? theme::serif (height * accidentalScale, false)
                                : theme::hand (height);
    }
}

String withAccidentalSigns (const String& symbol)
{
    return symbol.replaceCharacter ('b', flatSign[0]).replaceCharacter ('#', sharpSign[0]);
}

float chordSymbolWidth (const String& symbol, float height)
{
    float width = 0.0f;

    for (const auto& run : runsFor (symbol))
        width += GlyphArrangement::getStringWidth (fontForRun (run, height), run.text);

    return width;
}

void drawChordSymbol (Graphics& g,
                      const String& symbol,
                      Rectangle<int> area,
                      Justification justification,
                      float height,
                      Colour colour)
{
    const auto runs = runsFor (symbol);
    const auto total = chordSymbolWidth (symbol, height);

    auto x = static_cast<float> (area.getX());

    if (justification.testFlags (Justification::horizontallyCentred))
        x = static_cast<float> (area.getCentreX()) - total * 0.5f;
    else if (justification.testFlags (Justification::right))
        x = static_cast<float> (area.getRight()) - total;

    const auto baseline = static_cast<float> (area.getCentreY()) + height * 0.35f;

    g.setColour (colour);

    for (const auto& run : runs)
    {
        const auto font = fontForRun (run, height);
        g.setFont (font);
        g.drawSingleLineText (run.text, roundToInt (x), roundToInt (baseline));
        x += GlyphArrangement::getStringWidth (font, run.text);
    }
}

ChartView::ChartView()
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
}

void ChartView::setChart (const core::Chart& newChart)
{
    chart = newChart;
    reharmonisedMeasures.assign (static_cast<std::size_t> (chart.measureCount()), false);
    selectedMeasure = jlimit (0, jmax (0, chart.measureCount() - 1), selectedMeasure);
    hoveredMeasure = -1;
    resized();
    repaint();
}

void ChartView::setSelectedMeasure (int measureIndex)
{
    if (measureIndex == selectedMeasure || measureIndex < 0 || measureIndex >= chart.measureCount())
        return;

    selectedMeasure = measureIndex;
    repaint();
}

void ChartView::moveSelection (int delta)
{
    if (chart.measureCount() == 0)
        return;

    const auto target = jlimit (0, chart.measureCount() - 1, selectedMeasure + delta);

    if (target == selectedMeasure)
        return;

    setSelectedMeasure (target);

    if (onMeasureSelected != nullptr)
        onMeasureSelected (target);
}

void ChartView::setMeasureReharmonised (int measureIndex, bool isReharmonised)
{
    if (measureIndex < 0 || measureIndex >= static_cast<int> (reharmonisedMeasures.size()))
        return;

    reharmonisedMeasures[static_cast<std::size_t> (measureIndex)] = isReharmonised;
    repaint();
}

void ChartView::clearReharmonisedMarks()
{
    reharmonisedMeasures.assign (static_cast<std::size_t> (chart.measureCount()), false);
    repaint();
}

void ChartView::setPlayingMeasure (int measureIndex)
{
    if (playingMeasure == measureIndex)
        return;

    playingMeasure = measureIndex;
    repaint();
}

int ChartView::barsPerSystem() const
{
    // A phone cannot fit four bars of "Am7b5" across and stay readable.
    return sizeClassForWidth (getWidth()) == SizeClass::compact ? 2 : 4;
}

int ChartView::systemHeight() const
{
    return jmax (minimumTouchTarget() + 42, 86);
}

int ChartView::systemCount() const
{
    const auto perSystem = jmax (1, barsPerSystem());
    return (chart.measureCount() + perSystem - 1) / perSystem;
}

int ChartView::preferredHeight() const
{
    return jmax (systemHeight(), systemCount() * (systemHeight() + 4));
}

Rectangle<int> ChartView::boundsForSystem (int systemIndex) const
{
    const auto height = systemHeight();
    return { 0, systemIndex * (height + 4), getWidth(), height };
}

Rectangle<int> ChartView::boundsForMeasure (int measureIndex) const
{
    const auto perSystem = jmax (1, barsPerSystem());
    const auto column = measureIndex % perSystem;
    const auto system = boundsForSystem (measureIndex / perSystem);

    const auto barWidth = system.getWidth() / perSystem;

    // The last bar takes the rounding, so the closing barline sits flush right.
    const auto width = column == perSystem - 1 ? system.getWidth() - barWidth * column : barWidth;

    return { column * barWidth, system.getY(), width, system.getHeight() };
}

int ChartView::measureAt (Point<int> position) const
{
    for (auto i = 0; i < chart.measureCount(); ++i)
        if (boundsForMeasure (i).contains (position))
            return i;

    return -1;
}

void ChartView::resized()
{
    const auto needed = preferredHeight();

    if (needed != getHeight())
        setSize (getWidth(), needed);
}

void ChartView::paint (Graphics& g)
{
    g.fillAll (theme::paper);

    if (chart.measureCount() == 0)
    {
        g.setColour (theme::textSoft);
        g.setFont (theme::serif (theme::bodyFontSize(), false, true));
        g.drawText ("No chart loaded", getLocalBounds(), Justification::centred);
        return;
    }

    for (auto i = 0; i < chart.measureCount(); ++i)
        paintMeasure (g, i, boundsForMeasure (i));

    // Barlines are drawn over the bars so a fill never covers one: an opening
    // line down the left of every bar, and a closing line at each system's end
    // that doubles on the last, the way a chart closes.
    const auto perSystem = jmax (1, barsPerSystem());

    for (auto system = 0; system < systemCount(); ++system)
    {
        const auto area = boundsForSystem (system);
        g.setColour (theme::charcoal);

        for (auto column = 0; column < perSystem; ++column)
        {
            const auto index = system * perSystem + column;

            if (index >= chart.measureCount())
                break;

            const auto bar = boundsForMeasure (index);
            g.fillRect (bar.getX(), area.getY(), 2, area.getHeight());
        }

        const auto isLast = system == systemCount() - 1;
        const auto right = area.getRight();

        if (isLast)
        {
            g.fillRect (right - 5, area.getY(), 2, area.getHeight());
            g.fillRect (right - 2, area.getY(), 2, area.getHeight());
        }
        else
        {
            g.fillRect (right - 2, area.getY(), 2, area.getHeight());
        }
    }
}

void ChartView::paintMeasure (Graphics& g, int measureIndex, Rectangle<int> area)
{
    const auto isSelected = measureIndex == selectedMeasure;
    const auto isHovered = measureIndex == hoveredMeasure;
    const auto isPlaying = measureIndex == playingMeasure;
    const auto isReharmonised = measureIndex < static_cast<int> (reharmonisedMeasures.size())
                                && reharmonisedMeasures[static_cast<std::size_t> (measureIndex)];

    // The selection is a fill. The pointer adds an outline, because a bar has
    // nothing else to say the mouse is over this one; the arrow keys get no
    // outline, since the selection they move is already drawn.
    if (isSelected)
        g.setColour (theme::blush.withAlpha (0.26f));
    else if (isHovered)
        g.setColour (theme::blush.withAlpha (0.16f));

    if (isSelected || isHovered)
        g.fillRect (area);

    if (isHovered)
    {
        g.setColour (theme::blushDeep);
        g.drawRect (area, 2);
    }

    if (isPlaying)
    {
        g.setColour (theme::gold);
        g.drawRect (area, 2);
    }

    const auto& measure = chart.measures[static_cast<std::size_t> (measureIndex)];

    // The bar number sits at the head of each system, as on a chart, rather
    // than on every bar.
    if (measureIndex % jmax (1, barsPerSystem()) == 0)
    {
        g.setColour (theme::gold);
        g.setFont (theme::sans (11.0f));
        g.drawText (String (measureIndex + 1),
                    area.reduced (8, 5).withHeight (14),
                    Justification::topLeft);
    }

    if (isReharmonised)
    {
        g.setColour (theme::blushDeep);
        g.setFont (theme::sans (9.0f));
        g.drawText ("REHARM", area.reduced (8, 6).withHeight (12), Justification::topRight);
    }

    if (measure.isEmpty())
        return;

    // Chords sit where their beat falls, and two in a bar are written smaller -
    // as they are on a real chart, and as they must be for "Amaj7 F#maj9/B" not
    // to run into the bar after it.
    const auto shared = measure.slots.size() > 1;
    const auto height = shared ? jmin (19.0f, theme::headingFontSize() * 1.05f)
                               : jmin (28.0f, theme::headingFontSize() * 1.5f);

    double totalBeats = 0.0;

    for (const auto& slot : measure.slots)
        totalBeats += slot.beats;

    if (totalBeats <= 0.0)
        totalBeats = 4.0;

    const auto writingArea = area.withTrimmedTop (22).withTrimmedBottom (8);
    double beatsSoFar = 0.0;

    for (const auto& slot : measure.slots)
    {
        const auto left = area.getX() + static_cast<int> (beatsSoFar / totalBeats * area.getWidth())
                          + (shared ? 8 : 12);

        drawChordSymbol (g,
                         String (slot.chord.toString()),
                         writingArea.withX (left).withWidth (area.getRight() - left),
                         Justification::centredLeft,
                         height,
                         theme::charcoal);

        beatsSoFar += slot.beats;
    }
}

void ChartView::setHoveredMeasure (int measureIndex)
{
    if (hoveredMeasure == measureIndex)
        return;

    hoveredMeasure = measureIndex;
    repaint();
}

void ChartView::mouseMove (const MouseEvent& event)
{
    // Touch has no hover, and a lingering outline after a tap would be a lie
    // about where the finger is.
    setHoveredMeasure (currentInteractionMode() == InteractionMode::touch
                           ? -1
                           : measureAt (event.getPosition()));
}

void ChartView::mouseExit (const MouseEvent&)
{
    setHoveredMeasure (-1);
}

void ChartView::mouseDown (const MouseEvent& event)
{
    const auto measureIndex = measureAt (event.getPosition());

    if (measureIndex < 0)
        return;

    grabKeyboardFocus();

    const auto wasSelected = measureIndex == selectedMeasure;

    if (! wasSelected)
    {
        setSelectedMeasure (measureIndex);

        if (onMeasureSelected != nullptr)
            onMeasureSelected (measureIndex);

        return;
    }

    if (onMeasureOpened != nullptr)
        onMeasureOpened (measureIndex);
}

bool ChartView::keyPressed (const KeyPress& key)
{
    const auto perSystem = jmax (1, barsPerSystem());

    if (key == KeyPress::leftKey)   { moveSelection (-1); return true; }
    if (key == KeyPress::rightKey)  { moveSelection (1); return true; }
    if (key == KeyPress::upKey)     { moveSelection (-perSystem); return true; }
    if (key == KeyPress::downKey)   { moveSelection (perSystem); return true; }
    if (key == KeyPress::homeKey)   { moveSelection (-chart.measureCount()); return true; }
    if (key == KeyPress::endKey)    { moveSelection (chart.measureCount()); return true; }

    if (key == KeyPress::returnKey || key == KeyPress::spaceKey)
    {
        if (onMeasureOpened != nullptr && chart.measureCount() > 0)
            onMeasureOpened (selectedMeasure);

        return true;
    }

    return false;
}

void ChartView::focusGained (FocusChangeType)   { repaint(); }
void ChartView::focusLost (FocusChangeType)     { setHoveredMeasure (-1); }

} // namespace jazz::ui
