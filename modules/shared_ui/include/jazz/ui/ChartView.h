#pragma once

#include "jazz/ui/SizeClass.h"
#include "jazz/core/Chart.h"

#include <functional>

namespace jazz::ui
{

/** The chart as a lead sheet: systems of bars divided by barlines, chord
    symbols written where their beat falls, no staff and no melody.

    Bars per system follow the size class, so the same component reads as a
    phone chart, a tablet chart or a desktop chart without a second layout.

    Selecting and opening are separate: a click moves to a bar, a second click
    on the bar already selected opens it. Stepping along a chart to check one
    voicing after another therefore never puts a dialog over the keyboard.
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

    /** Moves the selection by @p delta bars, for arrow-key navigation. */
    void moveSelection (int delta);

    /** Fires when the selection moves to a bar. */
    std::function<void (int measureIndex)> onMeasureSelected;

    /** Fires when a bar the user is already on is chosen again - the point at
        which they are asking about it rather than moving to it.
    */
    std::function<void (int measureIndex)> onMeasureOpened;

    /** Height the chart needs to draw every system at the current width. */
    int preferredHeight() const;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseMove (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;
    bool keyPressed (const juce::KeyPress& key) override;
    void focusGained (FocusChangeType cause) override;
    void focusLost (FocusChangeType cause) override;

private:
    int barsPerSystem() const;
    int systemHeight() const;
    int systemCount() const;
    juce::Rectangle<int> boundsForMeasure (int measureIndex) const;
    juce::Rectangle<int> boundsForSystem (int systemIndex) const;
    int measureAt (juce::Point<int> position) const;
    void paintMeasure (juce::Graphics& g, int measureIndex, juce::Rectangle<int> area);
    void setHoveredMeasure (int measureIndex);

    core::Chart chart;
    int selectedMeasure { 0 };
    int playingMeasure { -1 };
    int hoveredMeasure { -1 };
    std::vector<bool> reharmonisedMeasures;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChartView)
};

/** Draws a chord symbol the way a chart writes it: real flat and sharp signs,
    and the extension raised the way "13" sits above the line after a "C".

    Shared by the sheet, the dialogs and the feedback line, so a chord reads the
    same everywhere it appears.
*/
void drawChordSymbol (juce::Graphics& g,
                      const juce::String& symbol,
                      juce::Rectangle<int> area,
                      juce::Justification justification,
                      float height,
                      juce::Colour colour);

/** Width @ref drawChordSymbol needs for @p symbol at @p height. */
float chordSymbolWidth (const juce::String& symbol, float height);

/** The symbol with "b" and "#" replaced by real accidental signs, for the
    places that draw plain text rather than a laid-out symbol.
*/
juce::String withAccidentalSigns (const juce::String& symbol);

} // namespace jazz::ui
