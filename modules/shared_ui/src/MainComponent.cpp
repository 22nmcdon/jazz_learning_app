#include "jazz/ui/MainComponent.h"

namespace jazz::ui
{

using namespace juce;

namespace
{
    /** The chart the app opens with, so the POC has something to play against. */
    constexpr const char* defaultProgression = "| Dm7 | G7 | Cmaj7 | Cmaj7 | "
                                               "| Cm7 | F7 | Bbmaj7 | Bbmaj7 | "
                                               "| Am7b5 | D7alt | Gm7 | Gm7 |";
}

MainComponent::MainComponent()
{
    addAndMakeVisible (titleLabel);
    addAndMakeVisible (inputLabel);
    addAndMakeVisible (chartViewport);
    chartViewport.setViewedComponent (&chartView, false);
    chartViewport.setScrollBarsShown (true, false);
    chartViewport.setColour (juce::ScrollBar::thumbColourId, theme::outline);
    addAndMakeVisible (scalePanel);
    addAndMakeVisible (reharmPanel);
    addAndMakeVisible (feedbackPanel);
    addAndMakeVisible (keyboard);

    for (auto* tab : { &chartTab, &scalesTab, &feedbackTab })
    {
        addChildComponent (*tab);
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (1);
        tab->setColour (TextButton::buttonColourId, theme::surface);
        tab->setColour (TextButton::buttonOnColourId, theme::accentMuted);
        tab->setColour (TextButton::textColourOffId, theme::textDim);
        tab->setColour (TextButton::textColourOnId, theme::text);
    }

    chartTab.onClick    = [this] { showPane (Pane::chart); };
    scalesTab.onClick   = [this] { showPane (Pane::scales); };
    feedbackTab.onClick = [this] { showPane (Pane::feedback); };

    titleLabel.setColour (Label::textColourId, theme::text);
    titleLabel.setFont (Font (FontOptions (theme::headingFontSize(), Font::bold)));

    inputLabel.setColour (Label::textColourId, theme::textDim);
    inputLabel.setFont (Font (FontOptions (11.0f)));
    inputLabel.setJustificationType (Justification::centredRight);
    inputLabel.setText ("On-screen keyboard", dontSendNotification);

    // Practising one shape is a session-wide choice, not a per-bar one, so it
    // lives in the header rather than beside any single chord.
    addAndMakeVisible (styleSelector);
    styleSelector.addItem ("Any shape", 1);
    styleSelector.addItem ("Root position", 2);
    styleSelector.addItem ("Shell", 3);
    styleSelector.addItem ("Rootless, left hand", 4);
    styleSelector.addItem ("Two-handed rootless", 5);
    styleSelector.setSelectedId (1, dontSendNotification);
    styleSelector.setColour (ComboBox::backgroundColourId, theme::surfaceRaised);
    styleSelector.setColour (ComboBox::textColourId, theme::text);
    styleSelector.setColour (ComboBox::outlineColourId, theme::outline);
    styleSelector.setColour (ComboBox::arrowColourId, theme::textDim);
    styleSelector.onChange = [this] { applyPractiseStyle(); };

    addAndMakeVisible (connectMidiButton);
    connectMidiButton.setColour (TextButton::buttonColourId, theme::surfaceRaised);
    connectMidiButton.setColour (TextButton::textColourOffId, theme::text);
    connectMidiButton.onClick = [this]
    {
        if (onConnectMidiRequested != nullptr)
            onConnectMidiRequested();
    };

    chartView.onMeasureSelected = [this] (int measureIndex) { measureSelected (measureIndex); };

    scalePanel.onScaleChosen = [this] (const core::Scale& scale)
    {
        // Light up the chosen scale on the keyboard, across its visible range.
        std::vector<int> notes;

        for (auto note = keyboard.getLowestOctaveNote();
             note < keyboard.getLowestOctaveNote() + keyboard.getVisibleOctaves() * core::semitonesPerOctave;
             ++note)
            if (scale.contains (note))
                notes.push_back (note);

        keyboard.setSuggestedNotes (notes);
    };

    reharmPanel.onSubstitutionApplied = [this] (const core::Chart& reharmonised, int measureIndex)
    {
        chart = reharmonised;
        chartView.setChart (chart);
        chartView.setMeasureReharmonised (measureIndex, true);
        chartView.setSelectedMeasure (measureIndex);
        measureSelected (measureIndex);
    };

    // The keyboard is just another input source as far as the analyser is
    // concerned - the same path hardware MIDI takes.
    attachInputSource (keyboard);

    collector.onVoicing = [this] (const core::Voicing& voicing, core::NoteSource source)
    {
        handleVoicing (voicing, source);
    };

    collector.onHeldNotesChanged = [this] (const core::Voicing& voicing)
    {
        keyboard.setHighlightedNotes (voicing.midiNotes);
    };

    loadProgressionText (defaultProgression);
    showPane (Pane::chart);

    // Drives the chord-settling window; the collector has no clock of its own.
    startTimerHz (30);
    setSize (1100, 760);
}

MainComponent::~MainComponent()
{
    stopTimer();
    detachInputSource (keyboard);
}

void MainComponent::attachInputSource (core::NoteInputSource& source)
{
    source.addListener (&collector);
}

void MainComponent::detachInputSource (core::NoteInputSource& source)
{
    source.removeListener (&collector);
}

void MainComponent::applyPractiseStyle()
{
    core::VoicingAnalyzer::Options options;

    switch (styleSelector.getSelectedId())
    {
        case 2:  options.practiseType = core::VoicingType::rootPosition; break;
        case 3:  options.practiseType = core::VoicingType::shell; break;
        case 4:  options.practiseType = core::VoicingType::rootlessLeftHand; break;
        case 5:  options.practiseType = core::VoicingType::twoHandedRootless; break;
        default: break;  // "Any shape": read the chart, do not drill a shape
    }

    analyzer = core::VoicingAnalyzer { options };

    // Re-read whatever is still under the hands against the new expectation.
    const auto held = collector.heldNotes();

    if (! held.isEmpty())
        handleVoicing (held, core::NoteSource::onScreenKeyboard);
}

void MainComponent::setMidiStatus (const juce::String& status)
{
    inputLabel.setText (status, dontSendNotification);
}

void MainComponent::loadChart (const core::Chart& newChart)
{
    chart = newChart;
    originalChart = newChart;
    selectedMeasure = 0;

    chartView.setChart (chart);
    chartView.clearReharmonisedMarks();
    chartView.setSelectedMeasure (0);
    titleLabel.setText (String (chart.title), dontSendNotification);
    measureSelected (0);
}

void MainComponent::loadProgressionText (const juce::String& text)
{
    auto result = core::parseProgressionText (text.toStdString(), "Practice chart");

    if (! result.ok())
    {
        titleLabel.setText ("Could not read chart: " + String (result.error), dontSendNotification);
        return;
    }

    loadChart (*result.chart);
}

const core::ChordSymbol* MainComponent::selectedChord() const
{
    return chart.chordAt (selectedMeasure);
}

void MainComponent::measureSelected (int measureIndex)
{
    selectedMeasure = measureIndex;

    const auto* chord = selectedChord();

    scalePanel.setChord (chord);
    reharmPanel.setChart (chart, measureIndex);
    feedbackPanel.setExpectedChord (chord);
    feedbackPanel.clearAnalysis();
    keyboard.setSuggestedNotes ({});
}

void MainComponent::handleVoicing (const core::Voicing& voicing, core::NoteSource source)
{
    // Deliberately unused: analysis must not differ between a chord played on a
    // MIDI keyboard and the same chord tapped on the on-screen keyboard.
    juce::ignoreUnused (source);

    const auto* chord = selectedChord();

    if (chord == nullptr)
        return;

    feedbackPanel.setAnalysis (analyzer.analyse (voicing, *chord), voicing, *chord);
    feedbackPanel.setRecognisedSubstitution (
        core::recogniseSubstitution (voicing, chart, selectedMeasure));

    // On compact layouts the feedback pane is not visible while playing, so
    // bring it forward as soon as there is something to say.
    if (sizeClass == SizeClass::compact && visiblePane != Pane::feedback)
        showPane (Pane::feedback);
}

void MainComponent::timerCallback()
{
    collector.advanceTime (Time::getMillisecondCounterHiRes() * 0.001);
}

void MainComponent::showPane (Pane pane)
{
    visiblePane = pane;

    chartTab.setToggleState (pane == Pane::chart, dontSendNotification);
    scalesTab.setToggleState (pane == Pane::scales, dontSendNotification);
    feedbackTab.setToggleState (pane == Pane::feedback, dontSendNotification);

    if (sizeClass == SizeClass::compact)
    {
        chartViewport.setVisible (pane == Pane::chart);
        scalePanel.setVisible (pane == Pane::scales);
        reharmPanel.setVisible (pane == Pane::scales);
        feedbackPanel.setVisible (pane == Pane::feedback);
    }

    resized();
}

void MainComponent::updateTabVisibility (bool tabbed)
{
    for (auto* tab : { &chartTab, &scalesTab, &feedbackTab })
        tab->setVisible (tabbed);

    if (! tabbed)
    {
        chartViewport.setVisible (true);
        scalePanel.setVisible (true);
        reharmPanel.setVisible (true);
        feedbackPanel.setVisible (true);
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void MainComponent::resized()
{
    sizeClass = sizeClassForWidth (getWidth());

    const auto mode = currentInteractionMode();
    scalePanel.setPresentation (mode);
    reharmPanel.setPresentation (mode);
    keyboard.applySizeClass (sizeClass);

    auto area = getLocalBounds().reduced (8);

    auto header = area.removeFromTop (jmax (minimumTouchTarget(), 28));
    connectMidiButton.setBounds (header.removeFromRight (jmax (minimumTouchTarget() + 20, 64)).reduced (2));
    styleSelector.setBounds (header.removeFromRight (jmin (190, header.getWidth() / 3)).reduced (2));

    // The device line is the first thing to go when the header runs out of room.
    if (sizeClass == SizeClass::compact)
        inputLabel.setBounds ({});
    else
        inputLabel.setBounds (header.removeFromRight (jmin (200, header.getWidth() / 2)));

    titleLabel.setBounds (header);

    area.removeFromTop (6);

    switch (sizeClass)
    {
        case SizeClass::compact:  layoutCompact (area); break;
        case SizeClass::regular:  layoutRegular (area); break;
        case SizeClass::expanded: layoutExpanded (area); break;
    }
}

void MainComponent::layoutCompact (juce::Rectangle<int> area)
{
    updateTabVisibility (true);

    // Tabs across the top, keyboard docked at the bottom, one pane between.
    auto tabRow = area.removeFromTop (jmax (minimumTouchTarget(), 34));
    const auto tabWidth = tabRow.getWidth() / 3;

    chartTab.setBounds (tabRow.removeFromLeft (tabWidth).reduced (2));
    scalesTab.setBounds (tabRow.removeFromLeft (tabWidth).reduced (2));
    feedbackTab.setBounds (tabRow.reduced (2));

    area.removeFromTop (6);
    keyboard.setBounds (area.removeFromBottom (jmax (150, area.getHeight() / 3)));
    area.removeFromBottom (6);

    chartViewport.setVisible (visiblePane == Pane::chart);
    scalePanel.setVisible (visiblePane == Pane::scales);
    reharmPanel.setVisible (visiblePane == Pane::scales);
    feedbackPanel.setVisible (visiblePane == Pane::feedback);

    switch (visiblePane)
    {
        case Pane::chart:
            chartViewport.setBounds (area);
            chartView.setSize (area.getWidth(), area.getHeight());
            break;

        case Pane::scales:
        {
            // Scales above, reharmonisations below: stacked, never side by side.
            scalePanel.setBounds (area.removeFromTop (area.getHeight() / 2).reduced (0, 0));
            reharmPanel.setBounds (area.withTrimmedTop (6));
            break;
        }

        case Pane::feedback:
            feedbackPanel.setBounds (area);
            break;
    }
}

void MainComponent::layoutRegular (juce::Rectangle<int> area)
{
    updateTabVisibility (false);

    keyboard.setBounds (area.removeFromBottom (jmax (160, area.getHeight() / 4)));
    area.removeFromBottom (8);

    // Chart on top, the two panels side by side underneath.
    const auto chartArea = area.removeFromTop (area.getHeight() / 2);
    chartViewport.setBounds (chartArea);
    chartView.setSize (chartArea.getWidth(), chartArea.getHeight());
    area.removeFromTop (8);

    FlexBox panels;
    panels.flexDirection = FlexBox::Direction::row;
    panels.items.add (FlexItem (scalePanel).withFlex (1.0f).withMargin ({ 0, 4, 0, 0 }));
    panels.items.add (FlexItem (feedbackPanel).withFlex (1.0f).withMargin ({ 0, 0, 0, 4 }));
    panels.performLayout (area.removeFromTop (area.getHeight()));

    // The reharmonisation list shares the scale pane's column on this size class.
    auto scaleArea = scalePanel.getBounds();
    scalePanel.setBounds (scaleArea.removeFromTop (scaleArea.getHeight() / 2));
    reharmPanel.setBounds (scaleArea.withTrimmedTop (8));
}

void MainComponent::layoutExpanded (juce::Rectangle<int> area)
{
    updateTabVisibility (false);

    keyboard.setBounds (area.removeFromBottom (jmax (170, area.getHeight() / 4)));
    area.removeFromBottom (8);

    FlexBox columns;
    columns.flexDirection = FlexBox::Direction::row;

    FlexBox rightColumn;
    rightColumn.flexDirection = FlexBox::Direction::column;
    rightColumn.items.add (FlexItem (scalePanel).withFlex (1.2f).withMinHeight (170.0f).withMargin ({ 0, 0, 4, 0 }));
    rightColumn.items.add (FlexItem (reharmPanel).withFlex (1.3f).withMinHeight (150.0f).withMargin ({ 4, 0, 4, 0 }));
    rightColumn.items.add (FlexItem (feedbackPanel).withFlex (1.4f).withMinHeight (150.0f).withMargin ({ 4, 0, 0, 0 }));

    // Chart takes the left half; the assistant panels stack down the right.
    auto chartArea = area.removeFromLeft (static_cast<int> (area.getWidth() * 0.55f)).withTrimmedRight (8);
    chartViewport.setBounds (chartArea);
    chartView.setSize (chartArea.getWidth(), chartArea.getHeight());
    rightColumn.performLayout (area);
}

} // namespace jazz::ui
