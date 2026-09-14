#include "MidiDeviceInput.h"

#include "jazz/ui/MainComponent.h"

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
        if (juce::SystemStats::getEnvironmentVariable ("JAZZ_UI_TOUCH", {}).getIntValue() == 1)
            jazz::ui::setInteractionModeOverride (jazz::ui::InteractionMode::touch);

        midiInput = std::make_unique<MidiDeviceInput>();
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *midiInput);
    }

    void shutdown() override
    {
        mainWindow.reset();
        midiInput.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, MidiDeviceInput& midiInput)
            : DocumentWindow (name,
                              jazz::ui::theme::background,
                              DocumentWindow::allButtons)
        {
            auto content = std::make_unique<jazz::ui::MainComponent>();

            // Hand the shell's MIDI input to the UI as a plain note source: the
            // UI never learns that devices, drivers or Bluetooth exist.
            content->attachInputSource (midiInput);
            contentComponent = content.get();

            midiInput.onDevicesChanged = [this, &midiInput]
            {
                if (contentComponent != nullptr)
                    contentComponent->setMidiStatus (describeDevices (midiInput));
            };

            contentComponent->onConnectMidiRequested = [this, &midiInput]
            {
                MidiDeviceInput::showBluetoothPairingDialog (contentComponent);
                contentComponent->setMidiStatus (describeDevices (midiInput));
            };

            contentComponent->setMidiStatus (describeDevices (midiInput));

            setUsingNativeTitleBar (true);
            setContentOwned (content.release(), true);

           #if JUCE_IOS || JUCE_ANDROID
            setFullScreen (true);
           #else
            setResizable (true, true);
            setResizeLimits (360, 480, 4000, 3000);

            // JAZZ_UI_SIZE=380x800 opens the window at a phone size, so the
            // compact layout can be checked without a device. JAZZ_UI_TOUCH=1
            // forces the touch presentation (bottom sheets, larger targets).
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
        static juce::String describeDevices (const MidiDeviceInput& midiInput)
        {
            const auto names = midiInput.openDeviceNames();

            if (names.isEmpty())
                return "No MIDI device - use the on-screen keyboard";

            return names.joinIntoString (", ");
        }

        jazz::ui::MainComponent* contentComponent { nullptr };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<MidiDeviceInput> midiInput;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace jazz::app

START_JUCE_APPLICATION (jazz::app::JazzLearningApplication)
