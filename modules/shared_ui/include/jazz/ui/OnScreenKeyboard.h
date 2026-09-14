#pragma once

#include "jazz/ui/SizeClass.h"
#include "jazz/core/NoteInput.h"

namespace jazz::ui
{

/** A multi-touch piano keyboard.

    This is a first-class input method, not a fallback for people without a MIDI
    keyboard. Several fingers landing together are reported as several note-on
    events with the same timestamp, which VoicingCollector then groups into one
    voicing - exactly what hardware MIDI produces for a played chord.

    The visible range follows the size class: one or two octaves on a phone,
    shifted with the octave buttons, more on a desktop window.
*/
class OnScreenKeyboard : public juce::Component,
                         public core::NoteInputSource
{
public:
    OnScreenKeyboard();

    std::string inputName() const override { return "On-screen keyboard"; }

    /** Number of octaves shown. Clamped to what the width can usefully draw. */
    void setVisibleOctaves (int octaves);
    int getVisibleOctaves() const noexcept { return visibleOctaves; }

    void setLowestOctaveNote (int midiNote);
    int getLowestOctaveNote() const noexcept { return lowestNote; }

    void shiftOctave (int direction);

    /** Latch mode: keys stay down until clicked again.

        A finger per note works on touch, but a mouse has one pointer, so
        without latching a desktop user could never enter a chord. Defaults to
        on for pointer input and off for touch - the affordance follows the
        input type, not the platform.
    */
    void setLatchEnabled (bool shouldLatch);
    bool isLatchEnabled() const noexcept { return latchEnabled; }

    /** Releases every latched note. */
    void clearHeldNotes();

    /** Highlights notes coming from elsewhere - hardware MIDI, or a suggested
        voicing the user is being shown.
    */
    void setHighlightedNotes (const std::vector<int>& midiNotes);
    void setSuggestedNotes (const std::vector<int>& midiNotes);

    /** Chooses a sensible octave count for the size class. */
    void applySizeClass (SizeClass sizeClass);

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;

private:
    struct Key
    {
        int midiNote {};
        bool isBlack {};
        juce::Rectangle<float> area;
    };

    void rebuildKeys();
    const Key* keyAt (juce::Point<float> position) const;
    void noteDown (int midiNote, juce::MouseInputSource source);
    void noteUp (juce::MouseInputSource source);
    void toggleLatchedNote (int midiNote);
    double now() const;
    bool isHeld (int midiNote) const;

    std::vector<Key> keys;
    juce::Rectangle<int> keyboardArea;
    juce::TextButton octaveDownButton { "-" };
    juce::TextButton octaveUpButton { "+" };
    juce::TextButton clearButton { "Clear" };
    juce::ToggleButton latchButton { "Hold" };

    bool latchEnabled { true };
    int visibleOctaves { 2 };
    int lowestNote { 48 };  // C3

    /** Which note each active touch or mouse is holding, keyed by source index. */
    std::map<int, int> notesByTouchSource;
    std::vector<int> latchedNotes;
    std::vector<int> highlightedNotes;
    std::vector<int> suggestedNotes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OnScreenKeyboard)
};

} // namespace jazz::ui
