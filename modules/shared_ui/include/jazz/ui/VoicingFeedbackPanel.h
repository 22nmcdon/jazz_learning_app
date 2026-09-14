#pragma once

#include "jazz/ui/SizeClass.h"
#include "jazz/core/Reharmonizer.h"
#include "jazz/core/VoicingAnalyzer.h"

namespace jazz::ui
{

/** Feedback on the chord the user just played, against the expected symbol.

    Findings are phrased as observations rather than verdicts - the design doc is
    explicit that notes outside the target are "outside", not "wrong".
*/
class VoicingFeedbackPanel : public juce::Component
{
public:
    VoicingFeedbackPanel();

    /** @param analysis   result for the voicing just played
        @param voicing    what the player actually played
        @param chord      the chord the chart expects here
    */
    void setAnalysis (const core::VoicingAnalysis& analysis,
                      const core::Voicing& voicing,
                      const core::ChordSymbol& chord);

    void setExpectedChord (const core::ChordSymbol* chord);
    void clearAnalysis();

    /** Notes that the voicing just played spells one of this bar's
        substitutions - the player has found a reharmonisation by ear.
    */
    void setRecognisedSubstitution (std::optional<core::RecognisedSubstitution> recognised);

    /** Running accuracy across the session, for the progress-tracking module. */
    float sessionAccuracy() const;
    int voicingsAnalysed() const noexcept { return analysedCount; }

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void paintScoreMeter (juce::Graphics& g, juce::Rectangle<int> area);

    core::VoicingAnalysis analysis;
    std::optional<core::RecognisedSubstitution> spotted;
    core::Voicing playedVoicing;
    std::optional<core::ChordSymbol> expectedChord;
    bool hasAnalysis { false };

    int analysedCount { 0 };
    int scoreTotal { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoicingFeedbackPanel)
};

} // namespace jazz::ui
