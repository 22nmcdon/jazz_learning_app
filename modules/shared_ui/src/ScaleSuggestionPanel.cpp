#include "jazz/ui/ScaleSuggestionPanel.h"

namespace jazz::ui
{

using namespace juce;

ScaleSuggestionPanel::ScaleSuggestionPanel()
{
    setOpaque (true);

    addAndMakeVisible (alternativesButton);
    alternativesButton.onClick = [this] { showAlternatives(); };
    alternativesButton.setColour (TextButton::buttonColourId, theme::surfaceRaised);
    alternativesButton.setColour (TextButton::textColourOffId, theme::text);
}

void ScaleSuggestionPanel::setChord (const core::ChordSymbol* chord)
{
    if (chord == nullptr)
    {
        currentChord.reset();
        suggestions.clear();
        chosenIndex = 0;
        repaint();
        return;
    }

    currentChord = *chord;
    suggestions = suggester.suggestionsFor (*chord);
    chosenIndex = 0;
    alternativesButton.setButtonText ("Other scales (" + String (suggestions.size()) + ")");
    repaint();
}

void ScaleSuggestionPanel::chooseSuggestion (int index)
{
    if (index < 0 || index >= static_cast<int> (suggestions.size()))
        return;

    chosenIndex = index;
    repaint();

    if (onScaleChosen != nullptr)
        onScaleChosen (suggestions[static_cast<std::size_t> (index)].scale);
}

void ScaleSuggestionPanel::showAlternatives()
{
    if (suggestions.empty())
        return;

    // Same options, two presentations: a popup under the pointer, or a sheet
    // with tap-sized rows that the thumb can reach.
    if (interactionMode == InteractionMode::touch)
    {
        std::vector<BottomSheet::Item> items;

        for (std::size_t i = 0; i < suggestions.size(); ++i)
            items.push_back ({ String (suggestions[i].scale.name()),
                               String (suggestions[i].rationale),
                               static_cast<int> (i) == chosenIndex });

        BottomSheet::show (*getTopLevelComponent(),
                           "Scales for " + String (currentChord ? currentChord->toString() : ""),
                           std::move (items),
                           [this] (int index) { chooseSuggestion (index); });
        return;
    }

    PopupMenu menu;

    for (std::size_t i = 0; i < suggestions.size(); ++i)
    {
        PopupMenu::Item item;
        item.itemID = static_cast<int> (i) + 1;
        item.text = String (suggestions[i].scale.name());
        item.isTicked = static_cast<int> (i) == chosenIndex;
        menu.addItem (item);
    }

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (alternativesButton),
                        [this] (int result) { chooseSuggestion (result - 1); });
}

void ScaleSuggestionPanel::resized()
{
    auto area = getLocalBounds().reduced (12);
    alternativesButton.setBounds (area.removeFromBottom (jmax (minimumTouchTarget(), 32)));
}

void ScaleSuggestionPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::surface);

    auto area = getLocalBounds().reduced (12);
    area.removeFromBottom (jmax (minimumTouchTarget(), 32) + 8);

    g.setColour (theme::textDim);
    g.setFont (Font (FontOptions (11.0f)));
    g.drawText ("SCALE FOR THIS MEASURE", area.removeFromTop (16), Justification::topLeft);

    if (! currentChord.has_value() || suggestions.empty())
    {
        g.setColour (theme::textDim);
        g.setFont (Font (FontOptions (theme::bodyFontSize())));
        g.drawText ("Select a measure to see which scales fit it.", area, Justification::centredLeft);
        return;
    }

    const auto& suggestion = suggestions[static_cast<std::size_t> (chosenIndex)];

    area.removeFromTop (6);
    g.setColour (theme::accent);
    g.setFont (Font (FontOptions (theme::headingFontSize(), Font::bold)));
    g.drawText (String (suggestion.scale.name()), area.removeFromTop (26), Justification::topLeft);

    // The notes of the scale, spelled against the chord.
    String notes;

    for (const auto& noteName : suggestion.scale.noteNames())
        notes += String (noteName) + " ";

    g.setColour (theme::text);
    g.setFont (Font (FontOptions (theme::bodyFontSize())));
    g.drawText (notes.trim(), area.removeFromTop (22), Justification::topLeft);

    // The fixed-height rows are laid out first; the rationale takes what is
    // left, so it stays readable when the pane is short.
    if (! suggestion.avoidNotes.empty())
    {
        String avoid;

        for (auto pitchClass : suggestion.avoidNotes)
            avoid += String (core::pitchClassName (pitchClass)) + " ";

        g.setColour (theme::warning);
        g.setFont (Font (FontOptions (theme::bodyFontSize() - 1.0f)));
        g.drawText ("Handle with care: " + avoid.trim(), area.removeFromBottom (20), Justification::bottomLeft);
    }

    area.removeFromTop (4);
    g.setColour (theme::textDim);
    g.setFont (Font (FontOptions (theme::bodyFontSize() - 1.0f)));
    g.drawFittedText (String (suggestion.rationale), area, Justification::topLeft,
                      jmax (1, area.getHeight() / 16));
}

//==============================================================================
BottomSheet::BottomSheet (juce::String sheetTitle, std::vector<Item> sheetItems)
    : title (std::move (sheetTitle)), items (std::move (sheetItems))
{
    setOpaque (false);
    setAlwaysOnTop (true);
}

void BottomSheet::show (juce::Component& parent,
                        juce::String title,
                        std::vector<Item> items,
                        std::function<void (int)> onChoice)
{
    auto* sheet = new BottomSheet (std::move (title), std::move (items));
    sheet->onChoice = std::move (onChoice);

    parent.addAndMakeVisible (sheet);
    sheet->setBounds (parent.getLocalBounds());
    sheet->toFront (true);
}

int BottomSheet::rowHeight() const
{
    return jmax (minimumTouchTarget() + 16, 56);
}

juce::Rectangle<int> BottomSheet::sheetBounds() const
{
    const auto height = jmin (getHeight() - 40,
                              40 + rowHeight() * static_cast<int> (items.size()));

    return getLocalBounds().removeFromBottom (height);
}

void BottomSheet::paint (juce::Graphics& g)
{
    // Scrim: tapping outside the sheet dismisses it.
    g.fillAll (Colours::black.withAlpha (0.55f));

    auto sheet = sheetBounds();
    g.setColour (theme::surface);
    g.fillRoundedRectangle (sheet.toFloat(), 14.0f);

    auto area = sheet.reduced (16, 10);

    g.setColour (theme::textDim);
    g.setFont (Font (FontOptions (12.0f)));
    g.drawText (title, area.removeFromTop (26), Justification::centredLeft);

    for (const auto& item : items)
    {
        auto row = area.removeFromTop (rowHeight());

        if (row.getBottom() > sheet.getBottom())
            break;

        g.setColour (item.isTicked ? theme::accent : theme::text);
        g.setFont (Font (FontOptions (theme::bodyFontSize() + 1.0f, Font::bold)));
        g.drawText (item.title, row.removeFromTop (24), Justification::centredLeft);

        g.setColour (theme::textDim);
        g.setFont (Font (FontOptions (theme::bodyFontSize() - 1.0f)));
        g.drawFittedText (item.subtitle, row, Justification::topLeft, 2);

        g.setColour (theme::outline);
        g.fillRect (row.removeFromBottom (1));
    }
}

void BottomSheet::resized()
{
    repaint();
}

int BottomSheet::itemAt (juce::Point<int> position) const
{
    auto area = sheetBounds().reduced (16, 10);
    area.removeFromTop (26);

    for (std::size_t i = 0; i < items.size(); ++i)
        if (area.removeFromTop (rowHeight()).contains (position))
            return static_cast<int> (i);

    return -1;
}

void BottomSheet::mouseDown (const juce::MouseEvent& event)
{
    const auto index = itemAt (event.getPosition());
    const auto tappedScrim = ! sheetBounds().contains (event.getPosition());

    if (index < 0 && ! tappedScrim)
        return;  // tapped the sheet itself, between rows

    // The sheet owns itself, so it is torn down after the mouse event has been
    // fully dispatched rather than from inside it.
    Component::SafePointer<BottomSheet> self { this };
    auto callback = onChoice;

    MessageManager::callAsync ([self, callback, index]
    {
        if (self == nullptr)
            return;

        std::unique_ptr<BottomSheet> owner { self.getComponent() };

        if (auto* parent = owner->getParentComponent())
            parent->removeChildComponent (owner.get());

        if (callback != nullptr && index >= 0)
            callback (index);
    });
}

} // namespace jazz::ui
