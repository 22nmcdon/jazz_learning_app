#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>

namespace jazz::app
{

/** The app's voice: an electric piano, synthesised rather than sampled.

    This lives in the platform shell for the same reason MIDI does - it owns a
    device. The UI says which note and when; nothing above this file knows the
    sample rate exists.

    The tone is the browser shell's, so the two sound alike: one sine ringing
    another, with the modulation dying away faster than the note, which is what
    makes the attack bark and the tail settle into something near a sine.
*/
class ElectricPiano : private juce::AudioIODeviceCallback
{
public:
    ElectricPiano();
    ~ElectricPiano() override;

    /** Opens an output device. False when the machine has none to open, which
        is not an error - it is a headless machine, or one with audio in use.
    */
    bool start();
    void stop();

    bool isRunning() const noexcept { return running.load(); }

    /** What to tell the player about the sound, device and all. */
    juce::String statusMessage() const;

    void noteOn (int midiNote, float velocity);
    void noteOff (int midiNote);
    void allNotesOff();

    /** Holds released notes until the pedal comes up, as a damper does. */
    void setSustain (bool isDown);

private:
    /** One sounding note. Voices are a fixed pool: a chord is ten notes at
        most, and allocating on the audio thread is not allowed.
    */
    struct Voice
    {
        std::atomic<bool> active { false };
        int midiNote { -1 };

        double carrierPhase { 0.0 };
        double modulatorPhase { 0.0 };
        double carrierDelta { 0.0 };
        double modulatorDelta { 0.0 };

        float amplitude { 0.0f };
        float amplitudeTarget { 0.0f };
        float modulationDepth { 0.0f };

        /** Per-sample decay factors, so the audio thread only multiplies. */
        float amplitudeDecay { 0.9999f };
        float modulationDecay { 0.999f };

        /** Which part of the envelope this voice is in. Without this, a note
            that had just reached its peak would read as "not yet at peak" on
            the next sample and attack again, and so never decay.
        */
        enum class Stage { attack, decay, release };

        Stage stage { Stage::attack };

        /** The key is up but the pedal is holding this note down. Kept apart
            from the stage, because such a note is still decaying normally - the
            pedal defers its release rather than changing how it rings.
        */
        bool pedalled { false };
    };

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    Voice* findVoiceFor (int midiNote);
    Voice* findFreeVoice();

    void releaseVoice (Voice& voice);

    juce::AudioDeviceManager devices;
    std::array<Voice, 16> voices;
    bool sustainDown { false };

    double sampleRate { 44100.0 };
    std::atomic<bool> running { false };
    juce::String status { "No audio device opened yet." };

    /** Guards voice allocation between the message thread and the audio one. */
    juce::SpinLock voiceLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ElectricPiano)
};

} // namespace jazz::app
