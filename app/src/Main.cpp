#include "ElectricPiano.h"
#include "MidiDeviceInput.h"
#include "WebUi.h"

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
    const juce::String getApplicationName() override    { return "Jazz Learning App"; }
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
