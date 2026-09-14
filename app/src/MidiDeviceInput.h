#pragma once

#include "jazz/core/NoteInput.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace jazz::app
{

/** Hardware MIDI input, exposed to the rest of the app as a NoteInputSource.

    This is the only class that knows MIDI devices exist. It opens every input
    it finds and rescans periodically so a keyboard plugged in (or paired over
    Bluetooth) mid-session starts working without a restart.
*/
class MidiDeviceInput : public core::NoteInputSource,
                        private juce::MidiInputCallback,
                        private juce::Timer
{
public:
    MidiDeviceInput();
    ~MidiDeviceInput() override;

    std::string inputName() const override;

    /** Devices currently open, for the status line. */
    juce::StringArray openDeviceNames() const;

    /** Opens the platform's Bluetooth MIDI pairing UI where one exists.
        On iOS and Android this is how a wireless keyboard gets connected.
    */
    static void showBluetoothPairingDialog (juce::Component* parentForModalSheet);

    /** Called when the set of connected devices changes. */
    std::function<void()> onDevicesChanged;

private:
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;
    void timerCallback() override;
    void refreshDevices();

    juce::CriticalSection deviceLock;
    std::vector<std::unique_ptr<juce::MidiInput>> openDevices;
    juce::Array<juce::MidiDeviceInfo> knownDevices;
};

} // namespace jazz::app
