#pragma once

#include "jazz/ui/Overlay.h"
#include "jazz/ui/ReharmonizationPanel.h"
#include "jazz/ui/ScaleSuggestionPanel.h"
#include "jazz/core/Chart.h"

namespace jazz::ui
{

/** What one bar has to say: its scales, and what else could be played there.

    Opened by choosing a bar that is already selected, so moving along a chart
    to check voicings never puts this in front of the keyboard.
*/
class ChordDialog : public OverlayPanel
{
public:
    ChordDialog();

    void showFor (juce::Component& parent, const core::Chart& chart, int measureIndex);

    /** Forwarded from the reharmonisation list. */
    std::function<void (const core::Chart&, int measureIndex)> onSubstitutionApplied;

    /** Forwarded from the scale list, to light the scale up on the keyboard. */
    std::function<void (const core::Scale&)> onScaleChosen;

protected:
    void contentResized (juce::Rectangle<int> area) override;
    void paintContent (juce::Graphics& g, juce::Rectangle<int> area) override;
    int preferredCardWidth (int availableWidth) const override;
    int preferredCardHeight (int availableHeight) const override;

private:
    enum class Tab { scales, reharmonise };

    void showTab (Tab tab);

    ScaleSuggestionPanel scalePanel;
    ReharmonizationPanel reharmPanel;

    LinkButton scalesTab { "Scales" };
    LinkButton reharmTab { "Reharmonise" };

    Tab currentTab { Tab::scales };
    juce::String barLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordDialog)
};

} // namespace jazz::ui
