#include "jazz/ui/PracticeMenu.h"

namespace jazz::ui
{

using namespace juce;

namespace
{
    constexpr int optionHeight = 26;
    constexpr int groupGap = 16;
    constexpr int panelPadding = 20;
    constexpr int panelWidth = 300;
}

//==============================================================================
OptionButton::OptionButton (const String& buttonText, String optionHint)
    : Button (buttonText), hint (std::move (optionHint))
{
    setClickingTogglesState (false);
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void OptionButton::paintButton (Graphics& g, bool highlighted, bool)
{
    auto area = getLocalBounds();
    const auto on = getToggleState();

    auto ring = area.removeFromLeft (22).withSizeKeepingCentre (12, 12).toFloat();

    g.setColour (on ? theme::blushDeep : theme::line.darker (0.1f));
    g.drawEllipse (ring, 1.4f);

    if (on)
    {
        g.setColour (theme::blushDeep);
        g.fillEllipse (ring.reduced (3.2f));
    }

    area.removeFromLeft (6);

    g.setColour (highlighted ? theme::blushDeep : theme::text);
    g.setFont (theme::serif (theme::bodyFontSize()));

    const auto labelWidth = GlyphArrangement::getStringWidth (g.getCurrentFont(), getButtonText());
    g.drawText (getButtonText(), area.removeFromLeft (roundToInt (labelWidth) + 4),
                Justification::centredLeft);

    if (hint.isNotEmpty())
    {
        area.removeFromLeft (8);
        g.setColour (theme::textSoft);
        g.setFont (theme::sans (11.0f));
        g.drawText (hint, area, Justification::centredLeft);
    }
}

//==============================================================================
PracticeMenu::PracticeMenu()
{
    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);

    const auto addOption = [this] (OwnedArray<OptionButton>& group,
                                   const char* text,
                                   const char* hint,
                                   std::function<void (int)> onChosen)
    {
        auto* option = group.add (new OptionButton (text, hint));
        addAndMakeVisible (option);
        const auto index = group.size() - 1;
        option->onClick = [onChosen, index] { onChosen (index); };
    };

    const auto chooseStyle   = [this] (int index) { selectStyle (index); };
    const auto chooseDensity = [this] (int index) { selectDensity (index); };
    const auto chooseSound   = [this] (int index) { selectSound (index); };

    addOption (styleOptions, "Any shape", "", chooseStyle);
    addOption (styleOptions, "Root position", "", chooseStyle);
    addOption (styleOptions, "Shell", "root, 3rd, 7th", chooseStyle);
    addOption (styleOptions, "Rootless, left hand", "", chooseStyle);
    addOption (styleOptions, "Two-handed rootless", "", chooseStyle);
    addOption (styleOptions, "Solo", "root in the left hand", chooseStyle);

    addOption (densityOptions, "The base shape", "", chooseDensity);
    addOption (densityOptions, "With the tensions", "", chooseDensity);

    addOption (soundOptions, "Electric piano", "", chooseSound);
    addOption (soundOptions, "Silent", "", chooseSound);

    addAndMakeVisible (importExportButton);
    addAndMakeVisible (connectMidiButton);

    importExportButton.onClick = [this]
    {
        dismiss();

        if (onImportExportRequested != nullptr)
            onImportExportRequested();
    };

    connectMidiButton.onClick = [this]
    {
        if (onConnectMidiRequested != nullptr)
            onConnectMidiRequested();
    };

    selectStyle (0);
    selectDensity (0);
    selectSound (0);
}

std::optional<core::VoicingType> PracticeMenu::practiseType() const
{
    switch (styleIndex)
    {
        case 1:  return core::VoicingType::rootPosition;
        case 2:  return core::VoicingType::shell;
        case 3:  return core::VoicingType::rootlessLeftHand;
        case 4:  return core::VoicingType::twoHandedRootless;
        case 5:  return core::VoicingType::solo;
        default: return std::nullopt;   // "Any shape": read the chart, drill nothing
    }
}

core::VoicingDensity PracticeMenu::density() const
{
    return densityIndex == 1 ? core::VoicingDensity::rich : core::VoicingDensity::plain;
}

bool PracticeMenu::soundEnabled() const
{
    return soundIndex == 0;
}

void PracticeMenu::selectStyle (int index)
{
    styleIndex = index;

    for (int i = 0; i < styleOptions.size(); ++i)
        styleOptions[i]->setToggleState (i == index, dontSendNotification);

    repaint();

    if (onPractiseStyleChanged != nullptr)
        onPractiseStyleChanged();
}

void PracticeMenu::selectDensity (int index)
{
    densityIndex = index;

    for (int i = 0; i < densityOptions.size(); ++i)
        densityOptions[i]->setToggleState (i == index, dontSendNotification);

    repaint();

    if (onDensityChanged != nullptr)
        onDensityChanged();
}

void PracticeMenu::selectSound (int index)
{
    soundIndex = index;

    for (int i = 0; i < soundOptions.size(); ++i)
        soundOptions[i]->setToggleState (i == index, dontSendNotification);

    repaint();

    if (onSoundChanged != nullptr)
        onSoundChanged();
}

void PracticeMenu::setMidiStatus (const String& status)
{
    midiStatus = status;
    repaint();
}

void PracticeMenu::setSoundNote (const String& note)
{
    soundNote = note;
    repaint();
}

void PracticeMenu::showOver (Component& parent)
{
    if (getParentComponent() != &parent)
        parent.addAndMakeVisible (this);
    else
        setVisible (true);

    toFront (true);
    setBounds (parent.getLocalBounds());
    grabKeyboardFocus();
}

void PracticeMenu::dismiss()
{
    setVisible (false);

    if (auto* parent = getParentComponent())
        parent->grabKeyboardFocus();
}

Rectangle<int> PracticeMenu::panelBounds() const
{
    // Tall enough for its groups, and never taller than the window.
    const auto height = jmin (getHeight() - 24,
                              panelPadding * 2
                              + 5 * groupGap
                              + 22 * 5
                              + optionHeight * (styleOptions.size() + densityOptions.size()
                                                + soundOptions.size())
                              + 150);

    const auto width = jmin (panelWidth, getWidth() - 24);

    return { getWidth() - width - 12, 12, width, height };
}

/** Where every row of the menu sits.

    Painted headings and laid-out buttons have to agree about this, and two
    copies of the arithmetic would not stay in step, so it is worked out once
    and both read it.
*/
struct PracticeMenu::Layout
{
    struct Group
    {
        juce::Rectangle<int> heading;
        std::vector<juce::Rectangle<int>> rows;
        juce::Rectangle<int> note;
        juce::String noteText;
        const char* title {};
    };

    std::vector<Group> groups;
};

PracticeMenu::Layout PracticeMenu::layout() const
{
    Layout out;
    auto area = panelBounds().reduced (panelPadding);

    const auto noteLines = [&area] (const String& text)
    {
        const auto font = theme::sans (11.0f);
        const auto width = jmax (1.0f, static_cast<float> (area.getWidth()));
        return jlimit (1, 4, roundToInt (std::ceil (
            GlyphArrangement::getStringWidth (font, text) / width)));
    };

    const auto group = [&] (const char* title, int rowCount, String noteText)
    {
        Layout::Group g;
        g.title = title;
        g.heading = area.removeFromTop (18);
        area.removeFromTop (6);

        for (int i = 0; i < rowCount; ++i)
            g.rows.push_back (area.removeFromTop (optionHeight));

        g.noteText = std::move (noteText);
        g.note = area.removeFromTop (14 * noteLines (g.noteText));
        area.removeFromTop (groupGap);

        out.groups.push_back (std::move (g));
    };

    group ("Chart", 1, "Open a chart from iReal Pro, or take this one away.");
    group ("Voicing style", styleOptions.size(),
           "Everything you play is checked against this shape, on every bar.");
    group ("Show me", densityOptions.size(),
           "Show me one keeps going: press it again for the next shape.");
    group ("Sound", soundOptions.size(), soundNote);
    group ("MIDI keyboard", 1, midiStatus);

    return out;
}

void PracticeMenu::paint (Graphics& g)
{
    g.fillAll (theme::charcoal.withAlpha (0.22f));

    const auto panel = panelBounds();

    g.setColour (theme::paper);
    g.fillRect (panel);
    g.setColour (theme::line);
    g.drawRect (panel, 1);

    // Headings and notes are painted rather than built from labels: they never
    // change, and a dozen more child components would buy nothing.
    for (const auto& group : layout().groups)
    {
        drawEyebrow (g, group.title, group.heading, Justification::centredLeft,
                     theme::charcoal, 10.0f);

        g.setColour (theme::textSoft);
        g.setFont (theme::sans (11.0f));
        g.drawFittedText (group.noteText, group.note, Justification::topLeft,
                          jmax (1, group.note.getHeight() / 14));
    }
}

void PracticeMenu::resized()
{
    const auto placed = layout();

    if (placed.groups.size() < 5)
        return;

    const auto place = [] (Component& component, Rectangle<int> row)
    {
        component.setBounds (row.withHeight (jmax (18, row.getHeight() - 4)));
    };

    place (importExportButton, placed.groups[0].rows.front());

    for (int i = 0; i < styleOptions.size(); ++i)
        styleOptions[i]->setBounds (placed.groups[1].rows[static_cast<std::size_t> (i)]);

    for (int i = 0; i < densityOptions.size(); ++i)
        densityOptions[i]->setBounds (placed.groups[2].rows[static_cast<std::size_t> (i)]);

    for (int i = 0; i < soundOptions.size(); ++i)
        soundOptions[i]->setBounds (placed.groups[3].rows[static_cast<std::size_t> (i)]);

    place (connectMidiButton, placed.groups[4].rows.front());
}

void PracticeMenu::mouseDown (const MouseEvent& event)
{
    if (! panelBounds().contains (event.getPosition()))
        dismiss();
}

bool PracticeMenu::keyPressed (const KeyPress& key)
{
    if (key == KeyPress::escapeKey)
    {
        dismiss();
        return true;
    }

    return false;
}

} // namespace jazz::ui
