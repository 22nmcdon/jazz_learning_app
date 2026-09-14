#include "MidiDeviceInput.h"

namespace jazz::app
{

MidiDeviceInput::MidiDeviceInput()
{
    refreshDevices();

    // Hot-plug and Bluetooth pairing both show up as a device list change.
    startTimer (2000);
}

MidiDeviceInput::~MidiDeviceInput()
{
    stopTimer();

    const juce::ScopedLock lock (deviceLock);

    for (auto& device : openDevices)
        device->stop();

    openDevices.clear();
}

std::string MidiDeviceInput::inputName() const
{
    return "MIDI keyboard";
}

juce::StringArray MidiDeviceInput::openDeviceNames() const
{
    const juce::ScopedLock lock (deviceLock);
    juce::StringArray names;

    for (const auto& device : openDevices)
        names.add (device->getName());

    return names;
}

void MidiDeviceInput::refreshDevices()
{
    const auto available = juce::MidiInput::getAvailableDevices();

    if (available == knownDevices)
        return;

    knownDevices = available;

    {
        const juce::ScopedLock lock (deviceLock);

        for (auto& device : openDevices)
            device->stop();

        openDevices.clear();

        // Open everything: a player may have several controllers connected, and
        // asking them to pick one before they can play is friction the POC does
        // not need.
        for (const auto& info : available)
        {
            if (auto device = juce::MidiInput::openDevice (info.identifier, this))
            {
                device->start();
                openDevices.push_back (std::move (device));
            }
        }
    }

    if (onDevicesChanged != nullptr)
        onDevicesChanged();
}

void MidiDeviceInput::timerCallback()
{
    refreshDevices();
}

void MidiDeviceInput::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    if (! message.isNoteOnOrOff())
        return;

    // This callback runs on the MIDI thread; hop to the message thread so the
    // engine and UI only ever see notes from one thread.
    const core::NoteEvent event {
        message.getNoteNumber(),
        message.getFloatVelocity(),
        message.isNoteOn() && message.getVelocity() > 0,
        core::NoteSource::hardwareMidi,
        juce::Time::getMillisecondCounterHiRes() * 0.001
    };

    juce::MessageManager::callAsync ([this, event] { broadcast (event); });
}

void MidiDeviceInput::showBluetoothPairingDialog (juce::Component* parentForModalSheet)
{
   #if JUCE_IOS || JUCE_ANDROID || JUCE_MAC
    if (juce::BluetoothMidiDevicePairingDialogue::isAvailable())
    {
        juce::BluetoothMidiDevicePairingDialogue::open (nullptr,
                                                        parentForModalSheet != nullptr
                                                            ? parentForModalSheet->getScreenBounds()
                                                            : juce::Rectangle<int> {});
        return;
    }
   #else
    juce::ignoreUnused (parentForModalSheet);
   #endif

    // Desktop: USB devices are picked up by the rescan, so there is nothing to pair.
}

} // namespace jazz::app
