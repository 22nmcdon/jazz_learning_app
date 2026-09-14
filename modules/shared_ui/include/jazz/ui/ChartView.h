#pragma once

#include "jazz/ui/SizeClass.h"
#include "jazz/core/Chart.h"

#include <functional>

namespace jazz::ui
{

/** The lead-sheet grid: one cell per measure, tap or click to select.

    Measures per row follow the size class, so the same component reads as a
    phone chart, a tablet chart or a desktop chart without a second layout.
*/
class ChartView : public juce::Component
{
public:
    ChartView();

    void setChart (const core::Chart& chart);
    const core::Chart& getChart() const noexcept { return chart; }

    void setSelectedMeasure (int measureIndex);
    int getSelectedMeasure() const noexcept { return selectedMeasure; }

    /** Marks a measure as changed from the original chart, for the reharm badge. */
    void setMeasureReharmonised (int measureIndex, bool isReharmonised);
    void clearReharmonisedMarks();

    /** The measure currently sounding in playback or practice-loop mode. */
    void setPlayingMeasure (int measureIndex);

    std::function<void (int measureIndex)> onMeasureSelected;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;

private:
    int measuresPerRow() const;
    juce::Rectangle<int> boundsForMeasure (int measureIndex) const;
    int measureAt (juce::Point<int> position) const;
    void paintMeasure (juce::Graphics& g, int measureIndex, juce::Rectangle<int> area);

    core::Chart chart;
    int selectedMeasure { 0 };
    int playingMeasure { -1 };
    std::vector<bool> reharmonisedMeasures;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChartView)
};

} // namespace jazz::ui
