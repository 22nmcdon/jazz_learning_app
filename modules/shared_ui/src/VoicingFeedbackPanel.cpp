#include "jazz/ui/VoicingFeedbackPanel.h"

namespace jazz::ui
{

using namespace juce;

VoicingFeedbackPanel::VoicingFeedbackPanel()
{
    setOpaque (true);
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
    repaint();
}

void VoicingFeedbackPanel::setRecognisedSubstitution (std::optional<core::RecognisedSubstitution> recognised)
{
    spotted = std::move (recognised);
    repaint();
}

float VoicingFeedbackPanel::sessionAccuracy() const
{
    return analysedCount == 0 ? 0.0f
                              : static_cast<float> (scoreTotal) / static_cast<float> (analysedCount);
}

void VoicingFeedbackPanel::resized() {}

void VoicingFeedbackPanel::paintScoreMeter (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (theme::surfaceRaised);
    g.fillRoundedRectangle (area.toFloat(), 3.0f);

    const auto fraction = static_cast<float> (analysis.score) / 100.0f;
    auto filled = area.toFloat().withWidth (area.toFloat().getWidth() * fraction);

    g.setColour (analysis.matchesChord ? theme::good
                                       : analysis.score >= 60 ? theme::warning : theme::problem);
    g.fillRoundedRectangle (filled, 3.0f);
}

void VoicingFeedbackPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::surface);

    auto area = getLocalBounds().reduced (12);

    g.setColour (theme::textDim);
    g.setFont (Font (FontOptions (11.0f)));

    const auto expectedText = expectedChord.has_value()
                                  ? "VOICING FEEDBACK - EXPECTING " + String (expectedChord->toString()).toUpperCase()
                                  : String ("VOICING FEEDBACK");

    g.drawText (expectedText, area.removeFromTop (16), Justification::topLeft);

    if (! hasAnalysis)
    {
        g.setColour (theme::textDim);
        g.setFont (Font (FontOptions (theme::bodyFontSize())));
        g.drawFittedText ("Play the chord for the selected measure on a MIDI keyboard or the "
                          "on-screen keyboard, and the voicing will be analysed here.",
                          area, Justification::centredLeft, 3);
        return;
    }

    area.removeFromTop (8);
    g.setColour (analysis.matchesChord ? theme::good : theme::text);
    g.setFont (Font (FontOptions (theme::headingFontSize(), Font::bold)));
    g.drawFittedText (String (analysis.summary), area.removeFromTop (46), Justification::topLeft, 2);

    g.setColour (theme::textDim);
    g.setFont (Font (FontOptions (theme::bodyFontSize() - 1.0f)));
    g.drawText ("You played: " + String (playedVoicing.describe()),
                area.removeFromTop (20), Justification::topLeft);

    paintScoreMeter (g, area.removeFromTop (6).withSizeKeepingCentre (area.getWidth(), 6));
    area.removeFromTop (10);

    // A voicing that spells one of this bar's substitutions is news, not an
    // error - say so before the findings that call it a mismatch.
    if (spotted.has_value())
    {
        auto row = area.removeFromTop (34);

        g.setColour (theme::accent);
        g.fillRoundedRectangle (row.removeFromLeft (3).reduced (0, 2).toFloat(), 1.5f);

        g.setColour (theme::accent);
        g.setFont (Font (FontOptions (theme::bodyFontSize() - 1.0f, Font::bold)));
        g.drawFittedText ("That is " + String (spotted->chord.toString()) + " - the "
                              + String (spotted->substitution.name) + " substitution for this bar.",
                          row.reduced (8, 0), Justification::centredLeft, 2);

        area.removeFromTop (4);
    }

    for (const auto& finding : analysis.findings)
    {
        if (area.getHeight() < 34)
            break;

        const auto colour = finding.severity == core::FindingSeverity::problem    ? theme::problem
                          : finding.severity == core::FindingSeverity::suggestion ? theme::warning
                                                                                  : theme::good;

        auto row = area.removeFromTop (32);

        g.setColour (colour);
        g.fillRoundedRectangle (row.removeFromLeft (3).reduced (0, 2).toFloat(), 1.5f);

        g.setColour (theme::text);
        g.setFont (Font (FontOptions (theme::bodyFontSize() - 1.0f)));
        g.drawFittedText (String (finding.message), row.reduced (8, 0), Justification::centredLeft, 2);
    }

    for (const auto& suggestion : analysis.suggestions)
    {
        if (area.getHeight() < 24)
            break;

        g.setColour (theme::accent);
        g.setFont (Font (FontOptions (theme::bodyFontSize() - 1.0f)));
        g.drawFittedText (String (suggestion), area.removeFromTop (22).reduced (11, 0),
                          Justification::centredLeft, 1);
    }

    if (analysedCount > 0 && area.getHeight() >= 18)
    {
        g.setColour (theme::textDim);
        g.setFont (Font (FontOptions (11.0f)));
        g.drawText ("Session: " + String (analysedCount) + " voicings, average "
                        + String (static_cast<int> (sessionAccuracy())) + "%",
                    area.removeFromBottom (16), Justification::bottomLeft);
    }
}

} // namespace jazz::ui
