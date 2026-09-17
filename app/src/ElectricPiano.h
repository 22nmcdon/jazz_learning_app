#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

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

    /** A recorded instrument, pitched by playing it faster or slower.

        One note per instrument rather than a sampled range: this is a practice
        app's band, not a sampler, and a single well-recorded note stretched
        across two octaves is the difference between a plausible bass and a sine
        wave. The cost is that the far ends of the range are a little short and
        a little wrong, which is why the walking line is bounded to a real
        bass's compass and the comp to a pianist's.
    */
    struct Sample
    {
        std::vector<float> audio;   ///< mono, at the rate it was recorded
        double sampleRate { 44100.0 };
        int rootNote { 60 };        ///< the note it was played at
    };

    /** Loads a sampled instrument under a name the page can ask for.

        Called once at startup with what the shell has embedded. A bank nobody
        loaded simply is not there, and asking for it falls back to the synth -
        which is a shell missing a file rather than a reason to be silent.
    */
    void addSample (const std::string& bank, Sample sample);

    bool hasSample (const std::string& bank) const;

    void noteOn (int midiNote, float velocity);

    /** Which sampled instrument the player's own keys use, by name. Empty is
        the synthesised electric piano, which is what it has always been. */
    void setPlayerBank (const std::string& bank);
    void noteOff (int midiNote);
    void allNotesOff();

    /** Holds released notes until the pedal comes up, as a damper does. */
    void setSustain (bool isDown);

    /** A metronome click, on the beat the transport says. Accented is the
        downbeat: higher and louder, matching the browser shell's click, which
        is how anyone counts a bar without being told which beat they are on.

        It borrows a voice from the same pool rather than getting a path of its
        own - it is a very short, very bright note with no attack ramp, and the
        callback is already general enough to play one. What it must not be is
        the piano: a metronome has to cut through whatever is played over it,
        and the tine's own attack is far too soft to do that.
    */
    void click (bool accented);

    /** The comping piano: one chord replacing whatever it last played.

        It is a channel of its own rather than a set of ordinary notes, because
        the player and the accompaniment share a keyboard and must not share
        voices. Comping E4 while the soloist plays E4 would otherwise hand the
        same voice to both - and then one note-off, from either of them, would
        stop a note the other one is still sounding.

        One call rather than note-on and note-off, because a comp only ever
        replaces itself: the chord before it is released here, together, which
        is also what keeps the voice pool from filling up with tails.
    */
    void compChord (const std::vector<int>& midiNotes, const std::string& bank = {});

    /** One note of the walking bass, replacing whatever it last played.

        Its own channel again, for the same reason the comp has one: three
        instruments sharing a voice pool must not share voices, or a bass note
        and a comped chord an octave apart stop each other.
    */
    void bassNote (int midiNote, const std::string& bank = {});

    void stopBass();

    /** Lets go of the comp, leaving anything the player is holding alone. */
    void stopComping();

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

        /** The accompaniment's, not the player's. Kept apart so that a key and
            a comped note at the same pitch are two voices, and so that neither
            one's note-off, pedal or panic reaches the other.
        */
        bool comping { false };

        /** The walking bass's, which is neither the player's nor the comp's. */
        bool walking { false };

        /*  Set when this voice is playing a recording rather than the synth.
            The pointer is into a bank loaded once at startup and never moved,
            so the audio thread may read it without a lock. */
        const Sample* sample { nullptr };
        double samplePosition { 0.0 };
        double sampleStep { 1.0 };
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
    void releaseComping();
    void releaseWalking();
    const Sample* sampleFor (const std::string& bank) const;
    void startVoice (Voice& voice, int midiNote, float level, const Sample* sample);

    void releaseVoice (Voice& voice);

    /*  Loaded once, never resized afterwards, so the audio thread can hold a
        pointer into one while the message thread is elsewhere. */
    std::vector<std::pair<std::string, std::unique_ptr<Sample>>> samples;

    std::string playerBank;

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
