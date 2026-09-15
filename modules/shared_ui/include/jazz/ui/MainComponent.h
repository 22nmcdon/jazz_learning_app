#pragma once

#include "jazz/ui/ChartView.h"
#include "jazz/ui/ChordDialog.h"
#include "jazz/ui/ImportExportDialog.h"
#include "jazz/ui/OnScreenKeyboard.h"
#include "jazz/ui/Overlay.h"
#include "jazz/ui/PlanDialog.h"
#include "jazz/ui/PracticeMenu.h"
#include "jazz/ui/VoicingFeedbackPanel.h"
#include "jazz/core/ChordIdentifier.h"
#include "jazz/core/NoteInput.h"
#include "jazz/core/VoicingAnalyzer.h"

namespace jazz::ui
{

/** The whole app view: a lead sheet, with the keyboard and its feedback docked
    under it, laid out for the current size class.

    The chart is the page; everything that speaks about one bar arrives in a
    dialog over it, and everything that belongs to the session sits in the
    practice menu. Below the compact breakpoint the sheet and the dock share the
    height differently - same components, one layout pass, no platform branches.
*/
class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    /** Attaches an input source owned by the platform shell (hardware MIDI).
        The UI never learns what kind of device it is.
    */
    void attachInputSource (core::NoteInputSource& source);
    void detachInputSource (core::NoteInputSource& source);

    void loadChart (const core::Chart& chart);
    void loadProgressionText (const juce::String& text);

    /** Status text for the practice menu's MIDI line. */
    void setMidiStatus (const juce::String& status);

    /** Set by the platform shell to offer device pairing or selection. The UI
        only expresses the intent; the shell decides what that means per platform.
    */
    std::function<void()> onConnectMidiRequested;

    /** Sounding what is played is the shell's job - it owns the audio device,
        the way it owns the MIDI ones. The UI only says which notes and when.
    */
    std::function<void (int midiNote, bool isOn)> onSoundNote;
    std::function<void (const std::vector<int>& midiNotes)> onSoundChord;
    std::function<void()> onSilenceRequested;

    /** Told by the shell whether there is any audio device to sound through. */
    void setSoundAvailable (bool available, const juce::String& note);

    /** Set by the shell to offer a file picker for importing a chart. Left
        unset, the button is not offered: reading bytes off a disk is the
        shell's job, and a shell that cannot should not pretend otherwise.
    */
    std::function<void()> onOpenChartFileRequested;

    /** Handed back by the shell once a picked file has been read. */
    void chartFileWasRead (const juce::String& text);
    void chartFileCouldNotBeRead (const juce::String& reason);

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    void timerCallback() override;
    void handleVoicing (const core::Voicing& voicing, core::NoteSource source);
    void measureSelected (int measureIndex);
    void openSelectedMeasure();
    void adoptChart (const core::Chart& chart, bool becomesTheOriginal);
    void applyPractiseStyle();
    void applyEditedProgression();
    void showEditor (bool shouldShow);
    void refreshChartHead();
    void nameWhatIsPlayed();
    void showAnotherVoicing();
    void updateVoicingControls();
    void playHeldNotes();
    const core::ChordSymbol* selectedChord() const;

    /** Sounds what is played, whatever it was played on.

        Both the on-screen keys and a hardware keyboard arrive here, so the app
        speaks with one voice and the UI still holds no audio code - it says
        which note and when, and the shell owns the device.
    */
    class SoundRelay : public core::NoteInputListener
    {
    public:
        explicit SoundRelay (MainComponent& ownerComponent) : owner (ownerComponent) {}
        void noteEventReceived (const core::NoteEvent& event) override;

    private:
        MainComponent& owner;
    };

    /** The three bands of the window, worked out once so the painted headings
        and the laid-out controls cannot disagree about where they are.
    */
    struct Frame
    {
        juce::Rectangle<int> header;
        juce::Rectangle<int> sheet;
        juce::Rectangle<int> dock;
    };

    /** The sheet card's parts, on the same principle. */
    struct SheetLayout
    {
        juce::Rectangle<int> card;
        juce::Rectangle<int> head;
        juce::Rectangle<int> hint;
        juce::Rectangle<int> tools;
        juce::Rectangle<int> editor;
        juce::Rectangle<int> systems;
    };

    /** The dock's parts. On a narrow window the practising line drops onto a
        row of its own rather than sharing one with the buttons.
    */
    struct DockLayout
    {
        juce::Rectangle<int> status;
        juce::Rectangle<int> feedback;
        juce::Rectangle<int> keys;
        juce::Rectangle<int> practising;
        juce::Rectangle<int> controls;
    };

    Frame frame() const;
    SheetLayout sheetLayout (juce::Rectangle<int> area) const;
    DockLayout dockLayout (juce::Rectangle<int> area) const;

    core::Chart chart;
    core::Chart originalChart;
    int selectedMeasure { 0 };

    core::VoicingCollector collector;
    SoundRelay soundRelay { *this };
    core::VoicingAnalyzer analyzer;
    core::ChordIdentifier identifier;

    ChartView chartView;
    juce::Viewport chartViewport;
    VoicingFeedbackPanel feedbackPanel;
    OnScreenKeyboard keyboard;

    PracticeMenu practiceMenu;
    ChordDialog chordDialog;
    PlanDialog planDialog;
    ImportExportDialog ioDialog;

    LinkButton menuButton { "Practice" };
    LinkButton planButton { "Reharmonise the tune" };
    LinkButton editButton { "Edit chart" };
    LinkButton restoreButton { "Restore original" };

    juce::TextEditor progressionEditor;
    SolidButton applyEditButton { "Read it" };
    juce::String parseError;
    bool editorOpen { false };

    SolidButton playChordButton { "Play chord" };
    LinkButton nameItButton { "Name it" };
    LinkButton showVoicingButton { "Show me one" };
    LinkButton clearKeysButton { "Clear keys" };

    /** What "Show me one" is currently walking through, so pressing it again
        moves on to the next shape rather than repeating the last one.
    */
    juce::String shownFor;
    int shownIndex { 0 };

    juce::String namedText;
    juce::String midiStatus { "Not connected. The on-screen keyboard works either way." };
    bool soundAvailable { false };

    SizeClass sizeClass { SizeClass::regular };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace jazz::ui
