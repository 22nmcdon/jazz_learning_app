#pragma once

#include "jazz/ui/Overlay.h"
#include "jazz/core/Reharmonizer.h"

namespace jazz::ui
{

/** Reharmonising the whole tune at once: every plan, lightest touch first.

    Each plan says what it would do and how many bars it would take, because
    "Adventurous" means nothing until you can see it moves eleven of twelve.
*/
class PlanDialog : public OverlayPanel
{
public:
    PlanDialog();

    void showFor (juce::Component& parent, const core::Chart& chart);

    /** Fires with the reharmonised chart when a plan is chosen. */
    std::function<void (const core::ReharmPlan&)> onPlanChosen;

protected:
    void contentResized (juce::Rectangle<int> area) override;
    int preferredCardWidth (int availableWidth) const override;

private:
    /** The scrolling contents: one row per plan. */
    class PlanList : public juce::Component
    {
    public:
        explicit PlanList (PlanDialog& ownerDialog) : owner (ownerDialog) {}

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& event) override;
        void mouseMove (const juce::MouseEvent& event) override;
        void mouseExit (const juce::MouseEvent& event) override;

        int rowHeight() const;
        void updateHeight (int visibleWidth);

    private:
        int rowAt (juce::Point<int> position) const;

        PlanDialog& owner;
        int hoveredRow { -1 };
    };

    std::vector<core::ReharmPlan> plans;
    PlanList list { *this };
    juce::Viewport viewport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlanDialog)
};

} // namespace jazz::ui
