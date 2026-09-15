#include "jazz/ui/ChordDialog.h"
#include "jazz/ui/ChartView.h"

namespace jazz::ui
{

using namespace juce;

namespace
{
    constexpr int tabRowHeight = 34;
}

ChordDialog::ChordDialog() : OverlayPanel ("Bar")
{
    addAndMakeVisible (scalePanel);
    addAndMakeVisible (reharmPanel);

    for (auto* tab : { &scalesTab, &reharmTab })
        addAndMakeVisible (*tab);

    scalesTab.onClick = [this] { showTab (Tab::scales); };
    reharmTab.onClick = [this] { showTab (Tab::reharmonise); };

    scalePanel.onScaleChosen = [this] (const core::Scale& scale)
    {
        if (onScaleChosen != nullptr)
            onScaleChosen (scale);
    };

    reharmPanel.onSubstitutionApplied = [this] (const core::Chart& chart, int measureIndex)
    {
        if (onSubstitutionApplied != nullptr)
            onSubstitutionApplied (chart, measureIndex);

        dismiss();
    };

    showTab (Tab::scales);
}

int ChordDialog::preferredCardWidth (int availableWidth) const
{
    return jmin (680, availableWidth - 40);
}

int ChordDialog::preferredCardHeight (int availableHeight) const
{
    return jmin (600, availableHeight - 40);
}

void ChordDialog::showFor (Component& parent, const core::Chart& chart, int measureIndex)
{
    const auto* chord = chart.chordAt (measureIndex);

    barLabel = "Bar " + String (measureIndex + 1);

    if (chord != nullptr)
        barLabel += "  " + withAccidentalSigns (String (chord->toString()));

    setPanelTitle (barLabel);

    scalePanel.setChord (chord);
    reharmPanel.setChart (chart, measureIndex);

    const auto mode = currentInteractionMode();
    scalePanel.setPresentation (mode);
    reharmPanel.setPresentation (mode);

    showTab (currentTab);
    showOver (parent);
}

void ChordDialog::showTab (Tab tab)
{
    currentTab = tab;

    scalePanel.setVisible (tab == Tab::scales);
    reharmPanel.setVisible (tab == Tab::reharmonise);

    scalesTab.setSelected (tab == Tab::scales);
    reharmTab.setSelected (tab == Tab::reharmonise);

    resized();
    repaint();
}

void ChordDialog::paintContent (Graphics& g, Rectangle<int> area)
{
    // A rule under the tab row, with the active tab's underline left showing.
    g.setColour (theme::line);
    g.fillRect (area.getX(), area.getY() + tabRowHeight - 1, area.getWidth(), 1);
}

void ChordDialog::contentResized (Rectangle<int> area)
{
    auto tabRow = area.removeFromTop (tabRowHeight);

    scalesTab.setBounds (tabRow.removeFromLeft (jmax (70, scalesTab.preferredWidth())));
    tabRow.removeFromLeft (18);
    reharmTab.setBounds (tabRow.removeFromLeft (jmax (90, reharmTab.preferredWidth())));

    area.removeFromTop (10);

    scalePanel.setBounds (area);
    reharmPanel.setBounds (area);
}

} // namespace jazz::ui
