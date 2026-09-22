#pragma once

#include "ElectricPiano.h"
#include "MidiDeviceInput.h"

#include "jazz/core/NoteInput.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace jazz::app
{

/** The app's user interface: the same page the website serves, in a webview.

    There is one UI in this project and this is how the desktop app gets it.
    What the page cannot do for itself - open a MIDI device, own an audio
    device, read a file off a disk - the shell still does, exactly as it did
    when the UI was JUCE components. The page draws; this owns the hardware.

    Two channels carry that, both of them plain events:

      - the page asks for the engine, for sound, or for a file;
      - the shell pushes MIDI notes, the sustain pedal and device status back.

    The engine itself is the native C++ already in this process, reached
    through jazz::api - not a second copy compiled to WebAssembly. That is why
    building this app needs no Emscripten.
*/
class WebUi : public juce::Component
{
public:
    WebUi (MidiDeviceInput& midiInput, ElectricPiano& piano);
    ~WebUi() override;

    /** Called by the shell when the set of MIDI devices changes. */
    void midiDevicesChanged();

    void resized() override;

private:
    /** Lays the page and its siblings down where the webview can load them. */
    juce::String pageUrl();

    /** The page asking the engine a question; the answer goes back by id. */
    void handleEngineCall (const juce::var& request);
    void handleSound (const juce::var& request);
    void handleFileOpen (const juce::var& request);

    void pushStatus();
    void sendToPage (const juce::Identifier& event, const juce::var& payload);

    /** Relays hardware notes and the pedal into the page. */
    class DeviceRelay : public core::NoteInputListener
    {
    public:
        explicit DeviceRelay (WebUi& ownerUi) : owner (ownerUi) {}

        void noteEventReceived (const core::NoteEvent& event) override;
        void sustainChanged (bool isDown) override;

    private:
        WebUi& owner;
    };

    MidiDeviceInput& midi;
    ElectricPiano& sound;
    DeviceRelay relay { *this };

    std::unique_ptr<juce::WebBrowserComponent> browser;
    /** The unguessable temp folder holding index.html and assets/. */
    juce::File pageFolder;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebUi)
};

} // namespace jazz::app
