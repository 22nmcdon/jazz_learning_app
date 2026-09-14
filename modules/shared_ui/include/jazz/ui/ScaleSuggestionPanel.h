#pragma once

#include "jazz/ui/SizeClass.h"
#include "jazz/core/ScaleSuggester.h"

#include <functional>

namespace jazz::ui
{

/** Suggested scale for the selected measure, plus every valid alternative.

    One component, two presentations: the alternatives open in a popup menu when
    the user has a pointer, and in a bottom sheet when they are on touch. The
    component and its data are identical either way - only presentation differs.
*/
class ScaleSuggestionPanel : public juce::Component
{
public:
    ScaleSuggestionPanel();

    /** Shows suggestions for a chord; pass nullptr to show the empty state. */
    void setChord (const core::ChordSymbol* chord);

    void setPresentation (InteractionMode mode) { interactionMode = mode; }

    /** Called when the user picks a different scale, e.g. to light up the keys. */
    std::function<void (const core::Scale&)> onScaleChosen;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void showAlternatives();
    void chooseSuggestion (int index);

    core::ScaleSuggester suggester;
    std::vector<core::ScaleSuggestion> suggestions;
    std::optional<core::ChordSymbol> currentChord;
    int chosenIndex { 0 };

    juce::TextButton alternativesButton { "Other scales" };
    InteractionMode interactionMode { InteractionMode::pointer };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScaleSuggestionPanel)
};

/** Full-width modal sheet used instead of a popup menu on touch.

    Larger rows, a visible dismiss target and no hover states - the same list of
    options a desktop user sees in a dropdown.
*/
class BottomSheet : public juce::Component
{
public:
    struct Item
    {
        juce::String title;
        juce::String subtitle;
        bool isTicked {};
    };

    BottomSheet (juce::String sheetTitle, std::vector<Item> sheetItems);

    /** Shows the sheet over @p parent; calls back with the chosen index, or -1. */
    static void show (juce::Component& parent,
                      juce::String title,
                      std::vector<Item> items,
                      std::function<void (int)> onChoice);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;

private:
    int itemAt (juce::Point<int> position) const;
    int rowHeight() const;
    juce::Rectangle<int> sheetBounds() const;

    juce::String title;
    std::vector<Item> items;
    std::function<void (int)> onChoice;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BottomSheet)
};

} // namespace jazz::ui
