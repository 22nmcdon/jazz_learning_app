#pragma once

#include "jazz/ui/Overlay.h"
#include "jazz/core/Voicing.h"

#include <functional>

namespace jazz::ui
{

/** A radio row in the practice menu: a ring, a label and an optional hint. */
class OptionButton : public juce::Button
{
public:
    OptionButton (const juce::String& buttonText, juce::String optionHint = {});

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

private:
    juce::String hint;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptionButton)
};

/** The session's settings, gathered where the page keeps them.

    None of these belong to a single bar, which is why they are in a menu rather
    than beside a chord: the shape being practised applies to everything played,
    and the sound applies to the whole session.
*/
class PracticeMenu : public juce::Component
{
public:
    PracticeMenu();

    void showOver (juce::Component& parent);
    void dismiss();

    /** The shape every voicing is checked against; nullopt means "any shape". */
    std::optional<core::VoicingType> practiseType() const;
    core::VoicingDensity density() const;
    bool soundEnabled() const;

    void setMidiStatus (const juce::String& status);
    void setSoundNote (const juce::String& note);

    std::function<void()> onPractiseStyleChanged;
    std::function<void()> onDensityChanged;
    std::function<void()> onSoundChanged;
    std::function<void()> onConnectMidiRequested;
    std::function<void()> onImportExportRequested;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    struct Layout;

    Layout layout() const;
    juce::Rectangle<int> panelBounds() const;
    void selectStyle (int index);
    void selectDensity (int index);
    void selectSound (int index);

    juce::OwnedArray<OptionButton> styleOptions;
    juce::OwnedArray<OptionButton> densityOptions;
    juce::OwnedArray<OptionButton> soundOptions;

    LinkButton importExportButton { "Import / export" };
    LinkButton connectMidiButton { "Connect a keyboard" };

    int styleIndex { 0 };
    int densityIndex { 0 };
    int soundIndex { 0 };

    juce::String midiStatus { "Not connected. The on-screen keyboard works either way." };
    juce::String soundNote { "The electric piano sounds every key you press, and Play chord sounds them together." };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PracticeMenu)
};

} // namespace jazz::ui
