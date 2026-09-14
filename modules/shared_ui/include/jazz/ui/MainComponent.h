#pragma once

#include "jazz/ui/ChartView.h"
#include "jazz/ui/OnScreenKeyboard.h"
#include "jazz/ui/ReharmonizationPanel.h"
#include "jazz/ui/ScaleSuggestionPanel.h"
#include "jazz/ui/SizeClass.h"
#include "jazz/ui/VoicingFeedbackPanel.h"
#include "jazz/core/NoteInput.h"
#include "jazz/core/VoicingAnalyzer.h"

namespace jazz::ui
{

/** The whole app view: chart, reharmonisation assistant, voicing analyser and
    keyboard, laid out for the current size class.

    Below the compact breakpoint the panes collapse into tabs; above it they sit
    side by side. Same components, one layout pass - no platform branches.
*/
class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    /** Attaches an input source owned by the platform shell (hardware MIDI).
        The UI never learns what kind of device it is.
    */
    void attachInputSource (core::NoteInputSource& source);
    void detachInputSource (core::NoteInputSource& source);

    void loadChart (const core::Chart& chart);
    void loadProgressionText (const juce::String& text);

    /** Status text for the input line, e.g. the connected MIDI device. */
    void setMidiStatus (const juce::String& status);

    /** Set by the platform shell to offer device pairing or selection. The UI
        only expresses the intent; the shell decides what that means per platform.
    */
    std::function<void()> onConnectMidiRequested;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    enum class Pane { chart, scales, feedback };

    void timerCallback() override;
    void handleVoicing (const core::Voicing& voicing, core::NoteSource source);
    void measureSelected (int measureIndex);
    void showPane (Pane pane);
    void layoutCompact (juce::Rectangle<int> area);
    void layoutRegular (juce::Rectangle<int> area);
    void layoutExpanded (juce::Rectangle<int> area);
    void updateTabVisibility (bool tabbed);
    const core::ChordSymbol* selectedChord() const;

    core::Chart chart;
    core::Chart originalChart;
    int selectedMeasure { 0 };

    core::VoicingCollector collector;
    core::VoicingAnalyzer analyzer;

    ChartView chartView;
    juce::Viewport chartViewport;
    ScaleSuggestionPanel scalePanel;
    ReharmonizationPanel reharmPanel;
    VoicingFeedbackPanel feedbackPanel;
    OnScreenKeyboard keyboard;

    juce::Label titleLabel;
    juce::Label inputLabel;
    juce::TextButton connectMidiButton { "MIDI" };
    juce::TextButton chartTab { "Chart" };
    juce::TextButton scalesTab { "Scales" };
    juce::TextButton feedbackTab { "Feedback" };

    SizeClass sizeClass { SizeClass::regular };
    Pane visiblePane { Pane::chart };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace jazz::ui
