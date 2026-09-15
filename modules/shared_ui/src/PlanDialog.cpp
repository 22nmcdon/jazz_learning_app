#include "jazz/ui/PlanDialog.h"
#include "jazz/ui/ChartView.h"

namespace jazz::ui
{

using namespace juce;

namespace
{
    constexpr int rowPadding = 12;
}

PlanDialog::PlanDialog() : OverlayPanel ("Reharmonise the tune")
{
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    styleViewport (viewport);
}

int PlanDialog::preferredCardWidth (int availableWidth) const
{
    return jmin (620, availableWidth - 40);
}

void PlanDialog::showFor (Component& parent, const core::Chart& chart)
{
    plans = core::reharmPlansFor (chart);
    list.updateHeight (jmax (100, viewport.getMaximumVisibleWidth()));
    viewport.setViewPosition (0, 0);
    showOver (parent);
}

void PlanDialog::contentResized (Rectangle<int> area)
{
    viewport.setBounds (area);
    list.updateHeight (area.getWidth());
}

int PlanDialog::PlanList::rowHeight() const
{
    return jmax (minimumTouchTarget() + 34, 74);
}

void PlanDialog::PlanList::updateHeight (int visibleWidth)
{
    setSize (jmax (100, visibleWidth),
             jmax (1, static_cast<int> (owner.plans.size()) * rowHeight()));
    repaint();
}

int PlanDialog::PlanList::rowAt (Point<int> position) const
{
    const auto row = position.y / jmax (1, rowHeight());
    return isPositiveAndBelow (row, static_cast<int> (owner.plans.size())) ? row : -1;
}

void PlanDialog::PlanList::paint (Graphics& g)
{
    g.fillAll (theme::paper);

    for (std::size_t i = 0; i < owner.plans.size(); ++i)
    {
        const auto& plan = owner.plans[i];
        auto row = Rectangle<int> (0, static_cast<int> (i) * rowHeight(),
                                   getWidth(), rowHeight());

        if (static_cast<int> (i) == hoveredRow)
        {
            g.setColour (theme::blush.withAlpha (0.14f));
            g.fillRect (row);
        }

        auto area = row.reduced (rowPadding, 10);

        auto titleRow = area.removeFromTop (20);

        // How many bars a plan moves is the thing that separates them, so it is
        // set beside the name rather than buried in the description.
        const auto changed = plan.barsChanged();
        const auto countText = changed == 0
                                 ? String ("leaves every bar")
                                 : String (changed) + (changed == 1 ? " bar" : " bars");

        g.setColour (theme::textSoft);
        g.setFont (theme::sans (10.0f));
        drawTracked (g, countText.toUpperCase(), titleRow, Justification::centredRight, 1.6f);

        g.setColour (theme::charcoal);
        g.setFont (theme::serif (theme::bodyFontSize() + 2.0f, true));
        g.drawText (String (plan.name), titleRow, Justification::centredLeft);

        area.removeFromTop (4);

        g.setColour (theme::textSoft);
        g.setFont (theme::sans (theme::bodyFontSize() - 1.0f));
        g.drawFittedText (String (plan.description), area, Justification::topLeft, 2);

        g.setColour (theme::line);
        g.fillRect (row.getX() + rowPadding, row.getBottom() - 1,
                    row.getWidth() - rowPadding * 2, 1);
    }
}

void PlanDialog::PlanList::mouseMove (const MouseEvent& event)
{
    const auto row = currentInteractionMode() == InteractionMode::touch
                       ? -1 : rowAt (event.getPosition());

    if (row == hoveredRow)
        return;

    hoveredRow = row;
    repaint();
}

void PlanDialog::PlanList::mouseExit (const MouseEvent&)
{
    hoveredRow = -1;
    repaint();
}

void PlanDialog::PlanList::mouseDown (const MouseEvent& event)
{
    const auto row = rowAt (event.getPosition());

    if (row < 0)
        return;

    const auto chosen = owner.plans[static_cast<std::size_t> (row)];

    if (owner.onPlanChosen != nullptr)
        owner.onPlanChosen (chosen);

    owner.dismiss();
}

} // namespace jazz::ui
