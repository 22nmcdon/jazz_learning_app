#include "ElectricPiano.h"
#include "MidiDeviceInput.h"
#include "WebUi.h"

#include <BinaryData.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <juce_gui_basics/juce_gui_basics.h>

namespace jazz::app
{

/** The standalone application target.

    Everything platform-specific lives here: the window (or full-screen view on
    mobile), MIDI device handling and app lifecycle. The UI and the engine below
    it are identical on every platform.
*/
class JazzLearningApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        midiInput = std::make_unique<MidiDeviceInput>();
        piano = std::make_unique<ElectricPiano>();
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *midiInput, *piano);
    }

    void shutdown() override
    {
        mainWindow.reset();
        piano.reset();
        midiInput.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    /** Reads one recorded instrument out of the binary the app was built with.

        Mono, because that is what these were prepared as and what the voice
        pool plays; a stereo recording would be folded down here rather than
        doubling every voice for a band nobody pans.
    */
    static ElectricPiano::Sample readSample (const void* data, int size, int rootNote)
    {
        ElectricPiano::Sample sample;
        sample.rootNote = rootNote;

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (
            wav.createReaderFor (new juce::MemoryInputStream (data, static_cast<std::size_t> (size), false),
                                 true));

        if (reader == nullptr || reader->lengthInSamples <= 0)
            return sample;

        juce::AudioBuffer<float> buffer (static_cast<int> (reader->numChannels),
                                         static_cast<int> (reader->lengthInSamples));
        reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);

        sample.sampleRate = reader->sampleRate;
        sample.audio.resize (static_cast<std::size_t> (buffer.getNumSamples()));

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            auto value = 0.0f;

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                value += buffer.getSample (channel, i);

            sample.audio[static_cast<std::size_t> (i)] = value / static_cast<float> (buffer.getNumChannels());
        }

        return sample;
    }

    /** The band's instruments, under the names the page asks for them by.

        The root notes are what was played into the microphone, and getting one
        wrong transposes a whole instrument - so they are written down here next
        to the files rather than guessed from a name.
    */
    static void loadSamples (ElectricPiano& piano)
    {
        piano.addSample ("upright",  readSample (BinaryData::bassupright_wav,
                                                 BinaryData::bassupright_wavSize, 36));   // C2
        piano.addSample ("electric", readSample (BinaryData::basselectric_wav,
                                                 BinaryData::basselectric_wavSize, 36));  // C2
        piano.addSample ("grand",    readSample (BinaryData::pianogrand_wav,
                                                 BinaryData::pianogrand_wavSize, 65));    // F4
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, MidiDeviceInput& midiInput, ElectricPiano& piano)
            : DocumentWindow (name,
                              juce::Colour (0xfffaf6f0),
                              DocumentWindow::allButtons)
        {
            // Sound first: the page asks whether it has any as soon as it loads.
            piano.start();
            loadSamples (piano);

            auto content = std::make_unique<WebUi> (midiInput, piano);
            contentComponent = content.get();

            midiInput.onDevicesChanged = [this]
            {
                if (contentComponent != nullptr)
                    contentComponent->midiDevicesChanged();
            };

            setUsingNativeTitleBar (true);
            setContentOwned (content.release(), true);

           #if JUCE_IOS || JUCE_ANDROID
            setFullScreen (true);
           #else
            setResizable (true, true);
            setResizeLimits (360, 480, 4000, 3000);

            // JAZZ_UI_SIZE=380x800 opens the window at a phone size, so the
            // page's narrow layout can be checked without a device - the page
            // responds to the window the way it responds to a browser window.
            const auto requested = juce::SystemStats::getEnvironmentVariable ("JAZZ_UI_SIZE", {});
            const auto dimensions = juce::StringArray::fromTokens (requested, "x", {});

            if (dimensions.size() == 2 && dimensions[0].getIntValue() > 0)
                centreWithSize (dimensions[0].getIntValue(), dimensions[1].getIntValue());
            else
                centreWithSize (1100, 760);
           #endif

            setVisible (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        WebUi* contentComponent { nullptr };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<MidiDeviceInput> midiInput;
    std::unique_ptr<ElectricPiano> piano;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace jazz::app

START_JUCE_APPLICATION (jazz::app::JazzLearningApplication)
