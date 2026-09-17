#include "ElectricPiano.h"

#include <cmath>

namespace jazz::app
{

namespace
{
    constexpr float masterGain = 0.22f;

    /** Seconds the modulation takes to fall away - the bark at the front. */
    constexpr double tineDecaySeconds = 0.14;

    /** Seconds to fall to a third of the peak, and to silence. */
    constexpr double bodyDecaySeconds = 0.9;
    constexpr double releaseSeconds = 0.18;

    double frequencyOf (int midiNote)
    {
        return 440.0 * std::pow (2.0, (midiNote - 69) / 12.0);
    }

    /** Per-sample multiplier that reaches @p ratio after @p seconds. */
    float decayFactor (double ratio, double seconds, double sampleRate)
    {
        if (seconds <= 0.0 || sampleRate <= 0.0)
            return 0.0f;

        return static_cast<float> (std::exp (std::log (ratio) / (seconds * sampleRate)));
    }
}

ElectricPiano::ElectricPiano() = default;

ElectricPiano::~ElectricPiano()
{
    stop();
}

bool ElectricPiano::start()
{
    if (running.load())
        return true;

    // Output only: pitch detection is deliberately out of scope, so the app
    // never asks for a microphone and never triggers that permission prompt.
    const auto error = devices.initialiseWithDefaultDevices (0, 2);

    if (error.isNotEmpty())
    {
        status = "No sound: " + error;
        return false;
    }

    if (devices.getCurrentAudioDevice() == nullptr)
    {
        status = "No sound: this machine has no audio output device.";
        return false;
    }

    devices.addAudioCallback (this);
    running = true;

    status = "Electric piano through " + devices.getCurrentAudioDevice()->getName() + ".";
    return true;
}

void ElectricPiano::stop()
{
    if (! running.load())
        return;

    devices.removeAudioCallback (this);
    devices.closeAudioDevice();
    running = false;
}

juce::String ElectricPiano::statusMessage() const
{
    return status;
}

void ElectricPiano::addSample (const std::string& bank, Sample sample)
{
    const juce::SpinLock::ScopedLockType lock (voiceLock);

    samples.emplace_back (bank, std::make_unique<Sample> (std::move (sample)));
}

bool ElectricPiano::hasSample (const std::string& bank) const
{
    return sampleFor (bank) != nullptr;
}

const ElectricPiano::Sample* ElectricPiano::sampleFor (const std::string& bank) const
{
    if (bank.empty())
        return nullptr;

    for (const auto& entry : samples)
        if (entry.first == bank && ! entry.second->audio.empty())
            return entry.second.get();

    return nullptr;
}

/** Sets a voice going, on a recording where there is one and the synth where
    there is not.

    The two share an envelope: a sample carries its own decay in the recording,
    so its amplitude is held flat and the envelope only shapes the attack and
    the release. Running the synth's decay over a recording would fade it out
    twice.
*/
void ElectricPiano::startVoice (Voice& voice, int midiNote, float level, const Sample* sample)
{
    const auto frequency = frequencyOf (midiNote);

    voice.midiNote = midiNote;
    voice.carrierPhase = 0.0;
    voice.modulatorPhase = 0.0;
    voice.sample = sample;
    voice.samplePosition = 0.0;
    voice.amplitude = 0.0f;
    voice.amplitudeTarget = level;
    voice.stage = Voice::Stage::attack;
    voice.pedalled = false;

    if (sample != nullptr)
    {
        // Faster for a higher note, slower for a lower one - the whole of what
        // pitching a recording is, and why a sample two octaves up is a quarter
        // as long as the one that was played.
        voice.sampleStep = (frequency / frequencyOf (sample->rootNote))
                             * (sample->sampleRate / sampleRate);

        voice.modulationDepth = 0.0f;
        voice.modulationDecay = 1.0f;
        voice.amplitudeDecay = 1.0f;   // the recording decays; the envelope does not
        voice.active = true;
        return;
    }

    voice.carrierDelta = juce::MathConstants<double>::twoPi * frequency / sampleRate;
    voice.modulatorDelta = voice.carrierDelta * 2.0;
    voice.modulationDepth = static_cast<float> (frequency * 5.0 * level * 2.0);
    voice.modulationDecay = decayFactor (0.05, tineDecaySeconds, sampleRate);
    voice.amplitudeDecay = decayFactor (0.3, bodyDecaySeconds, sampleRate);
    voice.active = true;
}

ElectricPiano::Voice* ElectricPiano::findVoiceFor (int midiNote)
{
    for (auto& voice : voices)
        if (voice.active.load() && voice.midiNote == midiNote
            && ! voice.comping && ! voice.walking
            && voice.stage != Voice::Stage::release)
            return &voice;

    return nullptr;
}

ElectricPiano::Voice* ElectricPiano::findFreeVoice()
{
    for (auto& voice : voices)
        if (! voice.active.load())
            return &voice;

    // Every voice is sounding: take the quietest, which is the one nearest the
    // end of its tail and so the least missed.
    Voice* quietest = &voices.front();

    for (auto& voice : voices)
        if (voice.amplitude < quietest->amplitude)
            quietest = &voice;

    return quietest;
}

void ElectricPiano::noteOn (int midiNote, float velocity)
{
    if (! running.load())
        return;

    const juce::SpinLock::ScopedLockType lock (voiceLock);

    auto* voice = findVoiceFor (midiNote);

    if (voice == nullptr)
        voice = findFreeVoice();

    if (voice == nullptr)
        return;

    const auto frequency = frequencyOf (midiNote);
    const auto level = juce::jlimit (0.08f, 1.0f, velocity);

    startVoice (*voice, midiNote, level * 0.5f, sampleFor (playerBank));

    voice->comping = false;
    voice->walking = false;
}

void ElectricPiano::setPlayerBank (const std::string& bank)
{
    const juce::SpinLock::ScopedLockType lock (voiceLock);
    playerBank = bank;
}

void ElectricPiano::releaseVoice (Voice& voice)
{
    // Not silence: a key lifted on a Rhodes still rings down.
    voice.stage = Voice::Stage::release;
    voice.amplitudeDecay = decayFactor (0.001, releaseSeconds, sampleRate);
    voice.pedalled = false;
}

void ElectricPiano::click (bool accented)
{
    if (! running.load())
        return;

    const juce::SpinLock::ScopedLockType lock (voiceLock);

    auto* voice = findFreeVoice();

    if (voice == nullptr)
        return;

    const auto frequency = accented ? 1600.0 : 1050.0;

    // Never a real note's pitch, so `findVoiceFor` can never hand this voice
    // back to a key that happens to be playing while the click rings.
    voice->midiNote = -1;
    voice->comping = false;
    voice->walking = false;
    voice->sample = nullptr;
    voice->carrierPhase = 0.0;
    voice->modulatorPhase = 0.0;
    voice->carrierDelta = juce::MathConstants<double>::twoPi * frequency / sampleRate;

    // A ratio that is not a whole number, so the partials it throws are not in
    // tune with anything - which is what makes a click read as a click rather
    // than as a very short high note.
    voice->modulatorDelta = voice->carrierDelta * 1.41;

    // Straight to full, with no attack stage: six milliseconds of rise is what
    // stops a struck tine sounding like a click, and here that is the point.
    voice->amplitudeTarget = accented ? 0.5f : 0.3f;
    voice->amplitude = voice->amplitudeTarget;
    voice->stage = Voice::Stage::decay;

    voice->modulationDepth = static_cast<float> (frequency * 1.5);
    voice->modulationDecay = decayFactor (0.001, 0.02, sampleRate);
    voice->amplitudeDecay = decayFactor (0.001, 0.055, sampleRate);
    voice->pedalled = false;

    voice->active = true;
}

void ElectricPiano::noteOff (int midiNote)
{
    const juce::SpinLock::ScopedLockType lock (voiceLock);

    for (auto& voice : voices)
    {
        if (voice.active.load() && voice.midiNote == midiNote && ! voice.comping
            && voice.stage != Voice::Stage::release)
        {
            // The damper is off the string, so the note keeps ringing until the
            // foot comes up.
            if (sustainDown)
                voice.pedalled = true;
            else
                releaseVoice (voice);
        }
    }
}

void ElectricPiano::setSustain (bool isDown)
{
    const juce::SpinLock::ScopedLockType lock (voiceLock);

    if (sustainDown == isDown)
        return;

    sustainDown = isDown;

    if (sustainDown)
        return;

    // Every release the pedal was holding back happens now, together.
    for (auto& voice : voices)
        if (voice.active.load() && voice.pedalled && ! voice.comping && ! voice.walking)
            releaseVoice (voice);
}

void ElectricPiano::allNotesOff()
{
    const juce::SpinLock::ScopedLockType lock (voiceLock);

    // Faster than a pedal release: this is "stop", not "the foot came up".
    // The comp is left alone - this is the player's panic button, and an
    // accompaniment that stopped every time the keyboard was cleared would
    // drop out in the middle of a bar for no reason the player could see.
    for (auto& voice : voices)
    {
        if (voice.comping || voice.walking)
            continue;

        voice.stage = Voice::Stage::release;
        voice.amplitudeDecay = decayFactor (0.001, 0.05, sampleRate);
        voice.pedalled = false;
    }
}

void ElectricPiano::releaseComping()
{
    // Quick, because what follows it is the next chord of the same comp: a
    // slow release would leave the tail of the last chord sounding under it.
    for (auto& voice : voices)
        if (voice.comping && voice.stage != Voice::Stage::release)
        {
            voice.stage = Voice::Stage::release;
            voice.amplitudeDecay = decayFactor (0.001, 0.18, sampleRate);
            voice.pedalled = false;
        }
}

void ElectricPiano::stopComping()
{
    const juce::SpinLock::ScopedLockType lock (voiceLock);
    releaseComping();
}

void ElectricPiano::compChord (const std::vector<int>& midiNotes, const std::string& bank)
{
    if (! running.load())
        return;

    const juce::SpinLock::ScopedLockType lock (voiceLock);

    releaseComping();

    const auto* sample = sampleFor (bank);

    for (auto midiNote : midiNotes)
    {
        auto* voice = findFreeVoice();

        if (voice == nullptr)
            return;

        // Under the soloist, not beside them: an accompaniment at the same
        // level as the line being played over it is not an accompaniment.
        startVoice (*voice, midiNote, 0.22f, sample);

        voice->comping = true;
        voice->walking = false;
    }
}

void ElectricPiano::releaseWalking()
{
    for (auto& voice : voices)
        if (voice.walking && voice.stage != Voice::Stage::release)
        {
            voice.stage = Voice::Stage::release;
            voice.amplitudeDecay = decayFactor (0.001, 0.12, sampleRate);
            voice.pedalled = false;
        }
}

void ElectricPiano::stopBass()
{
    const juce::SpinLock::ScopedLockType lock (voiceLock);
    releaseWalking();
}

void ElectricPiano::bassNote (int midiNote, const std::string& bank)
{
    if (! running.load())
        return;

    const juce::SpinLock::ScopedLockType lock (voiceLock);

    // One note at a time: a bass player has one note sounding, and letting the
    // last one ring under the next turns a walking line into a drone.
    releaseWalking();

    auto* voice = findFreeVoice();

    if (voice == nullptr)
        return;

    startVoice (*voice, midiNote, 0.5f, sampleFor (bank));

    voice->comping = false;
    voice->walking = true;
}

void ElectricPiano::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate = device->getCurrentSampleRate();

    if (sampleRate <= 0.0)
        sampleRate = 44100.0;

    for (auto& voice : voices)
        voice.active = false;
}

void ElectricPiano::audioDeviceStopped()
{
    for (auto& voice : voices)
        voice.active = false;
}

void ElectricPiano::audioDeviceIOCallbackWithContext (const float* const*,
                                                      int,
                                                      float* const* outputChannelData,
                                                      int numOutputChannels,
                                                      int numSamples,
                                                      const juce::AudioIODeviceCallbackContext&)
{
    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);

    const juce::SpinLock::ScopedTryLockType lock (voiceLock);

    if (! lock.isLocked())
        return;

    for (auto& voice : voices)
    {
        if (! voice.active.load())
            continue;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            // A six-millisecond rise, so the attack is a strike rather than a
            // click; after that the note decays on its own.
            if (voice.stage == Voice::Stage::attack)
            {
                voice.amplitude += voice.amplitudeTarget
                                   / static_cast<float> (juce::jmax (1.0, 0.006 * sampleRate));

                if (voice.amplitude >= voice.amplitudeTarget)
                {
                    voice.amplitude = voice.amplitudeTarget;
                    voice.stage = Voice::Stage::decay;
                }
            }
            else
            {
                voice.amplitude *= voice.amplitudeDecay;
            }

            float value = 0.0f;

            if (voice.sample != nullptr)
            {
                /*  Reading the recording faster or slower is the whole of the
                    pitching. Linear between the two nearest samples, which at
                    these ratios is inaudible and is a great deal cheaper than
                    anything better on the audio thread. */
                const auto& audio = voice.sample->audio;
                const auto index = static_cast<std::size_t> (voice.samplePosition);

                if (index + 1 < audio.size())
                {
                    const auto fraction = static_cast<float> (voice.samplePosition
                                                                - static_cast<double> (index));

                    value = (audio[index] * (1.0f - fraction) + audio[index + 1] * fraction)
                              * voice.amplitude * masterGain;

                    voice.samplePosition += voice.sampleStep;
                }
                else
                {
                    // The recording has run out. Let the voice go rather than
                    // looping it, which would turn a piano into an organ.
                    voice.amplitude = 0.0f;
                }
            }
            else
            {
                voice.modulationDepth *= voice.modulationDecay;

                const auto modulation = std::sin (voice.modulatorPhase) * voice.modulationDepth;

                value = static_cast<float> (std::sin (voice.carrierPhase)) * voice.amplitude
                          * masterGain;

                voice.carrierPhase += voice.carrierDelta
                                      + juce::MathConstants<double>::twoPi * modulation / sampleRate;
                voice.modulatorPhase += voice.modulatorDelta;
            }

            for (int channel = 0; channel < numOutputChannels; ++channel)
                if (outputChannelData[channel] != nullptr)
                    outputChannelData[channel][sample] += value;
        }

        // Wrap the phases so they stay accurate over a long tail.
        voice.carrierPhase = std::fmod (voice.carrierPhase, juce::MathConstants<double>::twoPi);
        voice.modulatorPhase = std::fmod (voice.modulatorPhase, juce::MathConstants<double>::twoPi);

        if (voice.amplitude < 0.0002f)
            voice.active = false;
    }
}

} // namespace jazz::app
