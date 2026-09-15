#include "jazz/ui/ImportExportDialog.h"
#include "jazz/core/ChartFormats.h"

namespace jazz::ui
{

using namespace juce;

namespace
{
    constexpr int pasteBoxHeight = 96;
    constexpr int rowHeight = 30;
}

ImportExportDialog::ImportExportDialog() : OverlayPanel ("Import / export")
{
    addAndMakeVisible (pasteBox);
    pasteBox.setMultiLine (true, true);
    pasteBox.setReturnKeyStartsNewLine (true);
    pasteBox.setFont (theme::mono (13.0f));
    pasteBox.setColour (TextEditor::backgroundColourId, theme::cream);
    pasteBox.setColour (TextEditor::textColourId, theme::text);
    pasteBox.setColour (TextEditor::outlineColourId, theme::line);
    pasteBox.setColour (TextEditor::focusedOutlineColourId, theme::blush);
    pasteBox.setColour (TextEditor::highlightColourId, theme::blush.withAlpha (0.4f));
    pasteBox.setTextToShowWhenEmpty ("irealbook://...  or  | Dm7 | G7 | Cmaj7 |",
                                     theme::textSoft.withAlpha (0.7f));

    addAndMakeVisible (readButton);
    addAndMakeVisible (openFileButton);
    addAndMakeVisible (copyLinkButton);

    readButton.onClick = [this] { readPastedText(); };
    copyLinkButton.onClick = [this] { copyLink(); };

    openFileButton.onClick = [this]
    {
        if (onOpenFileRequested != nullptr)
            onOpenFileRequested();
    };
}

int ImportExportDialog::preferredCardHeight (int availableHeight) const
{
    return jmin (470, availableHeight - 40);
}

void ImportExportDialog::showFor (Component& parent, const core::Chart& chart)
{
    exportLink = String (core::exportIRealPro (chart));
    status.clear();
    statusIsProblem = false;
    unreadable.clear();

    openFileButton.setVisible (onOpenFileRequested != nullptr);

    showOver (parent);
    pasteBox.grabKeyboardFocus();
}

void ImportExportDialog::setStatus (const String& text, bool isProblem)
{
    status = text;
    statusIsProblem = isProblem;
    repaint();
}

void ImportExportDialog::readPastedText()
{
    const auto text = pasteBox.getText().trim();

    if (text.isEmpty())
    {
        setStatus ("Paste an iReal Pro link, or a progression like | Dm7 | G7 | Cmaj7 |.", true);
        return;
    }

    readText (text, "what you pasted");
}

void ImportExportDialog::fileWasRead (const String& text)
{
    readText (text, "that file");
}

void ImportExportDialog::fileCouldNotBeRead (const String& reason)
{
    setStatus (reason, true);
}

void ImportExportDialog::readText (const String& text, const String& source)
{
    const auto raw = text.toStdString();

    // An iReal Pro link and a typed progression are told apart by looking, not
    // by asking the user which they have.
    auto result = core::looksLikeIRealPro (raw) ? core::importIRealPro (raw)
                                                : core::parseProgressionText (raw);

    unreadable.clear();

    for (const auto& symbol : result.unreadable)
        unreadable.add (String (symbol));

    if (! result.ok())
    {
        setStatus ("Could not read " + source + ": " + String (result.error), true);
        return;
    }

    const auto bars = result.chart->measureCount();
    auto message = "Read " + String (bars) + (bars == 1 ? " bar" : " bars")
                   + " from " + source + ".";

    // A chart that arrives with a chord the engine cannot read says so and
    // names it, rather than looking complete and being short a chord.
    if (! unreadable.isEmpty())
        message += " Could not read: " + unreadable.joinIntoString (", ") + ".";

    setStatus (message, ! unreadable.isEmpty());

    if (onChartRead != nullptr)
        onChartRead (*result.chart);

    exportLink = String (core::exportIRealPro (*result.chart));
    repaint();
}

void ImportExportDialog::copyLink()
{
    SystemClipboard::copyTextToClipboard (exportLink);
    setStatus ("iReal Pro link copied - paste it anywhere iReal Pro can open a link.", false);
}

/** Where each part of the dialog sits.

    The painted headings and the laid-out controls interleave, so the positions
    are worked out once and both read them rather than each counting the other's
    heights.
*/
struct ImportExportDialog::Layout
{
    juce::Rectangle<int> openHeading;
    juce::Rectangle<int> openBlurb;
    juce::Rectangle<int> paste;
    juce::Rectangle<int> readRow;
    juce::Rectangle<int> exportHeading;
    juce::Rectangle<int> link;
    juce::Rectangle<int> copyRow;
    juce::Rectangle<int> status;
};

ImportExportDialog::Layout ImportExportDialog::layout (Rectangle<int> area) const
{
    Layout out;

    out.openHeading = area.removeFromTop (16);
    area.removeFromTop (4);
    out.openBlurb = area.removeFromTop (32);
    out.paste = area.removeFromTop (pasteBoxHeight);
    area.removeFromTop (8);
    out.readRow = area.removeFromTop (rowHeight);
    area.removeFromTop (18);

    out.exportHeading = area.removeFromTop (16);
    area.removeFromTop (6);
    out.link = area.removeFromTop (34);
    area.removeFromTop (6);
    out.copyRow = area.removeFromTop (rowHeight);
    area.removeFromTop (10);
    out.status = area;

    return out;
}

void ImportExportDialog::paintContent (Graphics& g, Rectangle<int> area)
{
    const auto placed = layout (area);

    drawEyebrow (g, "Open a chart", placed.openHeading,
                 Justification::centredLeft, theme::charcoal, 10.0f);

    g.setColour (theme::textSoft);
    g.setFont (theme::sans (theme::bodyFontSize() - 1.0f));
    g.drawFittedText ("Paste an iReal Pro link, the HTML iReal Pro shares, or a progression "
                      "written as | Dm7 | G7 | Cmaj7 |.",
                      placed.openBlurb, Justification::topLeft, 2);

    drawEyebrow (g, "Take this chart away", placed.exportHeading,
                 Justification::centredLeft, theme::charcoal, 10.0f);

    // The link itself is shown, not just copied: it is the export, and seeing
    // it is how you know there is one.
    g.setColour (theme::cream);
    g.fillRect (placed.link);
    g.setColour (theme::textSoft);
    g.setFont (theme::mono (11.0f));
    g.drawText (exportLink, placed.link.reduced (8, 0), Justification::centredLeft, false);

    if (status.isNotEmpty())
    {
        g.setColour (statusIsProblem ? theme::rust : theme::sage);
        g.setFont (theme::sans (theme::bodyFontSize() - 1.0f));
        g.drawFittedText (status, placed.status, Justification::topLeft, 3);
    }
}

void ImportExportDialog::contentResized (Rectangle<int> area)
{
    const auto placed = layout (area);

    pasteBox.setBounds (placed.paste);

    auto readRow = placed.readRow;
    const auto readWidth = jmax (90, readButton.preferredWidth());
    readButton.setBounds (readRow.removeFromLeft (readWidth).withSizeKeepingCentre (readWidth, 28));

    readRow.removeFromLeft (18);
    const auto openWidth = jmax (80, openFileButton.preferredWidth());
    openFileButton.setBounds (readRow.removeFromLeft (openWidth).withSizeKeepingCentre (openWidth, 22));

    const auto copyWidth = jmax (140, copyLinkButton.preferredWidth());
    copyLinkButton.setBounds (placed.copyRow.withWidth (copyWidth).withSizeKeepingCentre (copyWidth, 22));
}

} // namespace jazz::ui
