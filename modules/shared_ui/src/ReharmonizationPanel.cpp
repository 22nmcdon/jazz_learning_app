#include "jazz/ui/ReharmonizationPanel.h"

namespace jazz::ui
{

using namespace juce;

ReharmonizationPanel::ReharmonizationPanel()
{
    setOpaque (true);

    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    styleViewport (viewport);

    addAndMakeVisible (advancedToggle);
    advancedToggle.setToggleState (true, dontSendNotification);
    advancedToggle.setColour (ToggleButton::textColourId, theme::textDim);
    advancedToggle.setColour (ToggleButton::tickColourId, theme::accent);
    advancedToggle.onClick = [this] { refresh(); };

    addAndMakeVisible (riskyToggle);
    riskyToggle.setToggleState (false, dontSendNotification);
    riskyToggle.setColour (ToggleButton::textColourId, theme::textDim);
    riskyToggle.setColour (ToggleButton::tickColourId, theme::problem);
    riskyToggle.onClick = [this] { refresh(); };
}

void ReharmonizationPanel::setChart (const core::Chart& newChart, int newMeasureIndex)
{
    chart = newChart;
    measureIndex = newMeasureIndex;
    refresh();
}

void ReharmonizationPanel::refresh()
{
    core::Reharmonizer::Options options;
    options.includeAdvanced = advancedToggle.getToggleState();
    options.includeRisky = riskyToggle.getToggleState();

    const core::Reharmonizer reharmonizer { options };

    substitutions = reharmonizer.substitutionsFor (chart, measureIndex);

    list.updateHeight (viewport.getMaximumVisibleWidth());
    viewport.setViewPosition (0, 0);
    repaint();
}

void ReharmonizationPanel::applySubstitution (int index)
{
    if (index < 0 || index >= static_cast<int> (substitutions.size()))
        return;

    const auto reharmonised = core::Reharmonizer::applySubstitution (
        chart, measureIndex, substitutions[static_cast<std::size_t> (index)]);

    if (onSubstitutionApplied != nullptr)
        onSubstitutionApplied (reharmonised, measureIndex);
}

void ReharmonizationPanel::resized()
{
    auto area = getLocalBounds().reduced (12);

    area.removeFromTop (22);  // heading, drawn in paint()

    auto toggles = area.removeFromBottom (jmax (minimumTouchTarget(), 28));
    advancedToggle.setBounds (toggles.removeFromLeft (toggles.getWidth() / 2));
    riskyToggle.setBounds (toggles);
    area.removeFromBottom (4);

    viewport.setBounds (area);
    list.updateHeight (viewport.getMaximumVisibleWidth());
}

void ReharmonizationPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::surface);

    g.setColour (theme::textDim);
    g.setFont (Font (FontOptions (11.0f)));
    g.drawText ("REHARMONISE THIS MEASURE",
                getLocalBounds().reduced (12).removeFromTop (22),
                Justification::topLeft);

    if (substitutions.empty())
    {
        g.setColour (theme::textDim);
        g.setFont (Font (FontOptions (theme::bodyFontSize())));
        g.drawText ("No substitutions for this measure.", viewport.getBounds(), Justification::centredLeft);
    }
}

//==============================================================================
int ReharmonizationPanel::SubstitutionList::rowHeight() const
{
    return jmax (minimumTouchTarget() + 20, 62);
}

void ReharmonizationPanel::SubstitutionList::updateHeight (int visibleWidth)
{
    setSize (jmax (visibleWidth, 1),
             rowHeight() * static_cast<int> (owner.substitutions.size()));
    repaint();
}

void ReharmonizationPanel::SubstitutionList::mouseDown (const juce::MouseEvent& event)
{
    owner.applySubstitution (event.getPosition().getY() / jmax (1, rowHeight()));
}

void ReharmonizationPanel::SubstitutionList::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    for (const auto& substitution : owner.substitutions)
    {
        auto row = area.removeFromTop (rowHeight());

        if (! row.intersects (g.getClipBounds()))
            continue;

        const auto isSafe = substitution.difficulty == core::SubstitutionDifficulty::safe;
        const auto isRisky = substitution.difficulty == core::SubstitutionDifficulty::risky;

        g.setColour (theme::surfaceRaised);
        g.fillRoundedRectangle (row.reduced (0, 3).toFloat(), 5.0f);

        auto content = row.reduced (10, 8);
        auto topLine = content.removeFromTop (20);

        // Family and difficulty on the right, so the eye can filter down the
        // column: what kind of move this is, and how far out it goes.
        auto tagArea = topLine.removeFromRight (150);
        g.setFont (Font (FontOptions (10.0f, Font::bold)));

        auto difficultyArea = tagArea.removeFromRight (74);
        g.setColour (isRisky ? theme::problem : isSafe ? theme::good : theme::warning);
        g.drawText (String (core::difficultyName (substitution.difficulty)).toUpperCase(),
                    difficultyArea, Justification::centredRight);

        g.setColour (theme::textDim);
        g.drawText (String (core::familyName (substitution.family)).toUpperCase(),
                    tagArea, Justification::centredRight);

        g.setColour (theme::text);
        g.setFont (Font (FontOptions (theme::bodyFontSize(), Font::bold)));
        g.drawText (String (substitution.replacementText()) + "  -  " + String (substitution.name),
                    topLine, Justification::centredLeft);

        g.setColour (theme::textDim);
        g.setFont (Font (FontOptions (theme::bodyFontSize() - 2.0f)));

        // A risky row leads with whether it works in this bar; the general
        // explanation matters less than the instance.
        if (isRisky)
        {
            g.setColour (substitution.voiceLeading.smoothHere ? theme::good : theme::problem);
            g.drawFittedText (String (substitution.voiceLeading.note), content,
                              Justification::topLeft, 2);
        }
        else
        {
            g.drawFittedText (String (substitution.explanation), content, Justification::topLeft, 2);
        }
    }
}

} // namespace jazz::ui
