#include "jazz/ui/VoicingFeedbackPanel.h"
#include "jazz/ui/ChartView.h"

namespace jazz::ui
{

using namespace juce;

VoicingFeedbackPanel::VoicingFeedbackPanel()
{
    // Not opaque: the dock behind it is the surface, and a second fill would
    // read as a card inside a card.
    addChildComponent (writeSpottedButton);

    writeSpottedButton.onClick = [this]
    {
        if (spotted.has_value() && onWriteSpottedIntoBar != nullptr)
            onWriteSpottedIntoBar (*spotted);
    };
}

void VoicingFeedbackPanel::setAnalysis (const core::VoicingAnalysis& newAnalysis,
                                        const core::Voicing& voicing,
                                        const core::ChordSymbol& chord)
{
    analysis = newAnalysis;
    playedVoicing = voicing;
    expectedChord = chord;
    hasAnalysis = true;

    // Single notes are not voicings; they would skew the session statistics.
    if (analysis.type != core::VoicingType::singleNote)
    {
        ++analysedCount;
        scoreTotal += analysis.score;
    }

    repaint();
}

void VoicingFeedbackPanel::setExpectedChord (const core::ChordSymbol* chord)
{
    if (chord == nullptr)
        expectedChord.reset();
    else
        expectedChord = *chord;

    repaint();
}

void VoicingFeedbackPanel::clearAnalysis()
{
    hasAnalysis = false;
    analysis = {};
    playedVoicing = {};
    spotted.reset();
    writeSpottedButton.setVisible (false);
    repaint();
}

void VoicingFeedbackPanel::setRecognisedSubstitution (std::optional<core::RecognisedSubstitution> recognised)
{
    spotted = std::move (recognised);
    writeSpottedButton.setVisible (spotted.has_value() && hasAnalysis);
    resized();
    repaint();
}

float VoicingFeedbackPanel::sessionAccuracy() const
{
    return analysedCount == 0 ? 0.0f
                              : static_cast<float> (scoreTotal) / static_cast<float> (analysedCount);
}

void VoicingFeedbackPanel::resized()
{
    if (! writeSpottedButton.isVisible())
        return;

    const auto width = jmax (130, writeSpottedButton.preferredWidth());
    writeSpottedButton.setBounds (spottedRow().removeFromRight (width)
                                      .withSizeKeepingCentre (width, 20));
}

void VoicingFeedbackPanel::paintScoreMeter (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (theme::line);
    g.fillRect (area);

    const auto fraction = static_cast<float> (analysis.score) / 100.0f;
    auto filled = area.toFloat().withWidth (area.toFloat().getWidth() * fraction);

    g.setColour (analysis.matchesChord ? theme::sage
                                       : analysis.score >= 60 ? theme::gold : theme::rust);
    g.fillRect (filled);
}

juce::Rectangle<int> VoicingFeedbackPanel::spottedRow() const
{
    // Where the spotted line lands, counted the same way paint() lays it out.
    auto area = getLocalBounds().reduced (2);

    if (! hasAnalysis)
        return {};

    area.removeFromTop (24);   // the verdict
    area.removeFromTop (18);   // what was played
    area.removeFromTop (6 + 8);// the meter and its gap

    return area.removeFromTop (22);
}

void VoicingFeedbackPanel::paint (juce::Graphics& g)
{
    // No fill: the dock is already a surface, and a card inside a card is one
    // border too many.
    auto area = getLocalBounds().reduced (2);

    if (! hasAnalysis)
    {
        g.setColour (theme::textSoft);
        g.setFont (theme::serif (theme::bodyFontSize() + 1.0f));
        g.drawText ("Play a chord on the keyboard.", area.removeFromTop (24),
                    juce::Justification::centredLeft);
        return;
    }

    g.setColour (analysis.matchesChord ? theme::sage : theme::charcoal);
    g.setFont (theme::serif (theme::bodyFontSize() + 2.0f));
    g.drawText (juce::String (analysis.summary), area.removeFromTop (24),
                juce::Justification::centredLeft, true);

    g.setColour (theme::textSoft);
    g.setFont (theme::sans (theme::bodyFontSize() - 2.0f));
    g.drawText ("You played " + juce::String (playedVoicing.describe()),
                area.removeFromTop (18), juce::Justification::centredLeft, true);

    paintScoreMeter (g, area.removeFromTop (6).withWidth (juce::jmin (area.getWidth(), 260)));
    area.removeFromTop (8);

    // A voicing that spells one of this bar's substitutions is news, not an
    // error - say so before the findings that call it a mismatch.
    if (spotted.has_value())
    {
        auto row = area.removeFromTop (22);
        row.removeFromRight (writeSpottedButton.getWidth() + 12);

        g.setColour (theme::blushDeep);
        g.setFont (theme::sans (theme::bodyFontSize() - 1.0f));
        g.drawText ("That is " + withAccidentalSigns (juce::String (spotted->chord.toString()))
                        + " - the " + juce::String (spotted->substitution.name)
                        + " for this bar.",
                    row, juce::Justification::centredLeft, true);

        area.removeFromTop (4);
    }

    for (const auto& finding : analysis.findings)
    {
        if (area.getHeight() < 18)
            break;

        const auto colour = finding.severity == core::FindingSeverity::problem    ? theme::rust
                          : finding.severity == core::FindingSeverity::suggestion ? theme::gold
                                                                                  : theme::sage;

        auto row = area.removeFromTop (18);

        g.setColour (colour);
        g.fillRect (row.removeFromLeft (2).reduced (0, 3));

        g.setColour (theme::text);
        g.setFont (theme::sans (theme::bodyFontSize() - 2.0f));
        g.drawText (juce::String (finding.message), row.reduced (8, 0),
                    juce::Justification::centredLeft, true);
    }

    for (const auto& suggestion : analysis.suggestions)
    {
        if (area.getHeight() < 16)
            break;

        g.setColour (theme::textSoft);
        g.setFont (theme::sans (theme::bodyFontSize() - 2.0f));
        g.drawText (juce::String (suggestion), area.removeFromTop (16).reduced (10, 0),
                    juce::Justification::centredLeft, true);
    }

    if (analysedCount > 0 && area.getHeight() >= 14)
    {
        g.setColour (theme::textSoft);
        g.setFont (theme::sans (10.0f));
        g.drawText ("Session: " + juce::String (analysedCount) + " voicings, average "
                        + juce::String (static_cast<int> (sessionAccuracy())) + "%",
                    area.removeFromBottom (14), juce::Justification::bottomRight);
    }
}

} // namespace jazz::ui
