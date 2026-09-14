#include "jazz/ui/ChartView.h"

namespace jazz::ui
{

using namespace juce;

ChartView::ChartView()
{
    setOpaque (true);
}

void ChartView::setChart (const core::Chart& newChart)
{
    chart = newChart;
    reharmonisedMeasures.assign (static_cast<std::size_t> (chart.measureCount()), false);
    selectedMeasure = juce::jlimit (0, juce::jmax (0, chart.measureCount() - 1), selectedMeasure);
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

int ChartView::measuresPerRow() const
{
    switch (sizeClassForWidth (getWidth()))
    {
        case SizeClass::compact:  return 2;
        case SizeClass::regular:  return 4;
        case SizeClass::expanded: return 4;
    }

    return 4;
}

juce::Rectangle<int> ChartView::boundsForMeasure (int measureIndex) const
{
    const auto perRow = measuresPerRow();
    const auto column = measureIndex % perRow;
    const auto row = measureIndex / perRow;

    const auto cellWidth = getWidth() / perRow;
    const auto cellHeight = juce::jmax (minimumTouchTarget() + 20, 64);

    return { column * cellWidth, row * cellHeight, cellWidth, cellHeight };
}

int ChartView::measureAt (juce::Point<int> position) const
{
    for (auto i = 0; i < chart.measureCount(); ++i)
        if (boundsForMeasure (i).contains (position))
            return i;

    return -1;
}

void ChartView::resized()
{
    // The grid is drawn rather than built from child components: a chart is a
    // few dozen cells, and drawing keeps scrolling smooth on mobile. The view
    // grows to fit its rows so the enclosing viewport can scroll it.
    const auto perRow = measuresPerRow();
    const auto rows = (chart.measureCount() + perRow - 1) / juce::jmax (1, perRow);
    const auto neededHeight = rows * juce::jmax (minimumTouchTarget() + 20, 64);

    if (neededHeight > getHeight())
        setSize (getWidth(), neededHeight);
}

void ChartView::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    if (chart.measureCount() == 0)
    {
        g.setColour (theme::textDim);
        g.setFont (Font (FontOptions (theme::bodyFontSize())));
        g.drawText ("No chart loaded", getLocalBounds(), Justification::centred);
        return;
    }

    for (auto i = 0; i < chart.measureCount(); ++i)
        paintMeasure (g, i, boundsForMeasure (i));
}

void ChartView::paintMeasure (juce::Graphics& g, int measureIndex, juce::Rectangle<int> area)
{
    const auto cell = area.reduced (3);
    const auto isSelected = measureIndex == selectedMeasure;
    const auto isPlaying = measureIndex == playingMeasure;
    const auto isReharmonised = measureIndex < static_cast<int> (reharmonisedMeasures.size())
                                && reharmonisedMeasures[static_cast<std::size_t> (measureIndex)];

    g.setColour (isSelected ? theme::surfaceRaised : theme::surface);
    g.fillRoundedRectangle (cell.toFloat(), 6.0f);

    g.setColour (isPlaying ? theme::accent : isSelected ? theme::accentMuted : theme::outline);
    g.drawRoundedRectangle (cell.toFloat().reduced (0.5f), 6.0f, isSelected || isPlaying ? 2.0f : 1.0f);

    // Bar number, small and out of the way.
    g.setColour (theme::textDim);
    g.setFont (Font (FontOptions (11.0f)));
    g.drawText (String (measureIndex + 1), cell.reduced (6, 4), Justification::topLeft);

    if (isReharmonised)
    {
        g.setColour (theme::accent);
        g.drawText ("reharm", cell.reduced (6, 4), Justification::topRight);
    }

    const auto& measure = chart.measures[static_cast<std::size_t> (measureIndex)];

    if (measure.isEmpty())
        return;

    // Two chords in a bar are laid out left and right, as on a lead sheet.
    auto chordArea = cell.reduced (8, 12).withTrimmedTop (6);
    const auto slotCount = static_cast<int> (measure.slots.size());
    const auto slotWidth = chordArea.getWidth() / juce::jmax (1, slotCount);

    g.setColour (theme::text);
    g.setFont (Font (FontOptions (theme::headingFontSize(), Font::bold)));

    for (auto slot = 0; slot < slotCount; ++slot)
    {
        const auto text = measure.slots[static_cast<std::size_t> (slot)].chord.toString();
        g.drawText (String (text),
                    chordArea.removeFromLeft (slotWidth),
                    slotCount == 1 ? Justification::centred : Justification::centredLeft);
    }
}

void ChartView::mouseDown (const juce::MouseEvent& event)
{
    const auto measureIndex = measureAt (event.getPosition());

    if (measureIndex < 0)
        return;

    setSelectedMeasure (measureIndex);

    if (onMeasureSelected != nullptr)
        onMeasureSelected (measureIndex);
}

} // namespace jazz::ui
