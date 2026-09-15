#pragma once

#include "jazz/ui/Overlay.h"
#include "jazz/core/Chart.h"

namespace jazz::ui
{

/** Opening a chart that came from somewhere else, and taking this one away.

    The dialog knows the formats but not what a file is: reading bytes off a
    disk is the platform shell's job, so picking a file is a request this makes
    of whoever is hosting it, and what comes back is text.
*/
class ImportExportDialog : public OverlayPanel
{
public:
    ImportExportDialog();

    void showFor (juce::Component& parent, const core::Chart& chart);

    /** Hand back what a picked file contained, or an error to show. */
    void fileWasRead (const juce::String& text);
    void fileCouldNotBeRead (const juce::String& reason);

    /** Fires with a chart read from whatever the user pasted or opened. */
    std::function<void (const core::Chart&)> onChartRead;

    /** Asks the shell to put a file picker up. Left unset, the button hides:
        a shell with nowhere to read files from should not offer to.
    */
    std::function<void()> onOpenFileRequested;

protected:
    void contentResized (juce::Rectangle<int> area) override;
    void paintContent (juce::Graphics& g, juce::Rectangle<int> area) override;
    int preferredCardHeight (int availableHeight) const override;

private:
    struct Layout;

    Layout layout (juce::Rectangle<int> area) const;
    void readPastedText();
    void readText (const juce::String& text, const juce::String& source);
    void copyLink();
    void setStatus (const juce::String& text, bool isProblem);

    juce::TextEditor pasteBox;
    SolidButton readButton { "Read it" };
    LinkButton openFileButton { "Open a file" };
    LinkButton copyLinkButton { "Copy iReal Pro link" };

    juce::String exportLink;
    juce::String status;
    bool statusIsProblem { false };

    /** Chord symbols the reader could not understand, named rather than
        silently dropped.
    */
    juce::StringArray unreadable;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ImportExportDialog)
};

} // namespace jazz::ui
