#pragma once

#include "jazz/ui/SizeClass.h"
#include "jazz/core/Reharmonizer.h"

#include <functional>

namespace jazz::ui
{

/** Reharmonisation options for the selected measure.

    Each row shows what the measure becomes, why it works, and a difficulty tag
    so a learner can stay on safe substitutions until they want the advanced ones.
    The list scrolls, because a dominant chord alone offers half a dozen options.
*/
class ReharmonizationPanel : public juce::Component
{
public:
    ReharmonizationPanel();

    void setChart (const core::Chart& chart, int measureIndex);
    void setPresentation (InteractionMode mode) { interactionMode = mode; }

    /** Fires with the chart that results from applying the chosen substitution. */
    std::function<void (const core::Chart&, int measureIndex)> onSubstitutionApplied;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void refresh();
    void applySubstitution (int index);

    /** The scrolling contents: one row per substitution. */
    class SubstitutionList : public juce::Component
    {
    public:
        explicit SubstitutionList (ReharmonizationPanel& ownerPanel) : owner (ownerPanel) {}

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& event) override;

        int rowHeight() const;
        void updateHeight (int visibleWidth);

    private:
        ReharmonizationPanel& owner;
    };

    core::Chart chart;
    int measureIndex { -1 };
    std::vector<core::Substitution> substitutions;

    SubstitutionList list { *this };
    juce::Viewport viewport;
    juce::ToggleButton advancedToggle { "Include advanced" };
    InteractionMode interactionMode { InteractionMode::pointer };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReharmonizationPanel)
};

} // namespace jazz::ui
