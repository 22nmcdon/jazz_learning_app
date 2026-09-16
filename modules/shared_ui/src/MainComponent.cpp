#include "jazz/ui/MainComponent.h"
#include "jazz/core/ChartFormats.h"

namespace jazz::ui
{

using namespace juce;

namespace
{
    /** The chart the app opens with, so the POC has something to play against. */
    constexpr const char* defaultProgression = "| Dm7 | G7 | Cmaj7 | Cmaj7 | "
                                               "| Cm7 | F7 | Bbmaj7 | Bbmaj7 | "
                                               "| Am7b5 | D7alt | Gm7 | Gm7 |";

    constexpr int cardPadding = 24;
    constexpr int headHeight = 46;
    constexpr int hintHeight = 20;
    constexpr int toolsHeight = 30;
    constexpr int editorHeight = 104;

    String describeStyle (const std::optional<core::VoicingType>& type)
    {
        if (! type.has_value())
            return "any shape";

        // Every type is named rather than defaulted, so adding one to the
        // engine shows up here as a compile error instead of quietly becoming
        // "any shape" in the one place the player reads it.
        switch (*type)
        {
            case core::VoicingType::rootPosition:      return "root position";
            case core::VoicingType::shell:             return "shell voicings";
            case core::VoicingType::rootlessLeftHand:  return "rootless, left hand";
            case core::VoicingType::twoHandedRootless: return "two-handed rootless";
            case core::VoicingType::solo:              return "solo voicings";
            case core::VoicingType::spread:            return "spread voicings";
            case core::VoicingType::singleNote:        return "single notes";
            case core::VoicingType::unknown:           break;
        }

        return "any shape";
    }
}

MainComponent::MainComponent()
{
    addAndMakeVisible (chartViewport);
    chartViewport.setViewedComponent (&chartView, false);
    chartViewport.setScrollBarsShown (true, false);
    styleViewport (chartViewport);

    addAndMakeVisible (feedbackPanel);
    addAndMakeVisible (keyboard);

    for (auto* button : { &menuButton, &planButton, &editButton, &restoreButton,
                          &nameItButton, &showVoicingButton, &clearKeysButton })
        addAndMakeVisible (*button);

    addAndMakeVisible (playChordButton);

    addChildComponent (progressionEditor);
    addChildComponent (applyEditButton);

    progressionEditor.setMultiLine (true, true);
    progressionEditor.setReturnKeyStartsNewLine (true);
    progressionEditor.setFont (theme::mono (13.0f));
    progressionEditor.setColour (TextEditor::backgroundColourId, theme::cream);
    progressionEditor.setColour (TextEditor::textColourId, theme::text);
    progressionEditor.setColour (TextEditor::outlineColourId, theme::line);
    progressionEditor.setColour (TextEditor::focusedOutlineColourId, theme::blush);
    progressionEditor.setColour (TextEditor::highlightColourId, theme::blush.withAlpha (0.4f));

    applyEditButton.onClick = [this] { applyEditedProgression(); };

    menuButton.onClick   = [this] { practiceMenu.showOver (*this); };
    planButton.onClick   = [this] { planDialog.showFor (*this, chart); };
    editButton.onClick   = [this] { showEditor (! editorOpen); };
    restoreButton.onClick = [this]
    {
        // Back to the chart as it arrived - which, after an import, is the tune
        // that was brought in rather than the one the app happened to open with.
        adoptChart (originalChart, false);
    };

    nameItButton.onClick       = [this] { nameWhatIsPlayed(); };
    showVoicingButton.onClick  = [this] { showAnotherVoicing(); };
    clearKeysButton.onClick    = [this]
    {
        keyboard.clearHeldNotes();
        keyboard.setHighlightedNotes ({});

        if (onSilenceRequested != nullptr)
            onSilenceRequested();

        namedText.clear();
        repaint();
    };

    playChordButton.onClick = [this] { playHeldNotes(); };

    chartView.onMeasureSelected = [this] (int measureIndex) { measureSelected (measureIndex); };
    chartView.onMeasureOpened   = [this] (int) { openSelectedMeasure(); };

    chordDialog.onScaleChosen = [this] (const core::Scale& scale)
    {
        // Light up the chosen scale on the keyboard, across its visible range.
        std::vector<int> notes;

        for (auto note = keyboard.getLowestOctaveNote();
             note < keyboard.getLowestOctaveNote()
                    + keyboard.getVisibleOctaves() * core::semitonesPerOctave;
             ++note)
            if (scale.contains (note))
                notes.push_back (note);

        keyboard.setSuggestedNotes (notes);
    };

    chordDialog.onSubstitutionApplied = [this] (const core::Chart& reharmonised, int measureIndex)
    {
        chart = reharmonised;
        chartView.setChart (chart);
        chartView.setMeasureReharmonised (measureIndex, true);
        chartView.setSelectedMeasure (measureIndex);
        measureSelected (measureIndex);
    };

    planDialog.onPlanChosen = [this] (const core::ReharmPlan& plan)
    {
        chart = plan.chart;
        chartView.setChart (chart);
        chartView.clearReharmonisedMarks();

        for (const auto& move : plan.moves)
            chartView.setMeasureReharmonised (move.measureIndex, true);

        refreshChartHead();
        measureSelected (jlimit (0, jmax (0, chart.measureCount() - 1), selectedMeasure));
        repaint();
    };

    ioDialog.onChartRead = [this] (const core::Chart& read)
    {
        // An imported chart becomes the chart, so Restore original returns the
        // tune that was brought in.
        adoptChart (read, true);
    };

    feedbackPanel.onWriteSpottedIntoBar = [this] (const core::RecognisedSubstitution& spotted)
    {
        // A reharmonisation found by ear is kept the same way one chosen from
        // the list is: the bar becomes the substitution's chords.
        if (selectedMeasure < 0 || selectedMeasure >= chart.measureCount())
            return;

        auto& measure = chart.measures[static_cast<std::size_t> (selectedMeasure)];

        auto beats = 0;

        for (const auto& slot : measure.slots)
            beats += slot.beats;

        if (beats <= 0)
            beats = chart.timeSignature.numerator;

        const auto& chords = spotted.substitution.replacement;

        if (chords.empty())
            return;

        measure.slots.clear();

        // The bar's beats split evenly between the substitution's chords, with
        // the first taking any remainder - two chords in a four-beat bar are
        // two and two, three are two, one and one.
        const auto share = jmax (1, beats / static_cast<int> (chords.size()));

        for (std::size_t i = 0; i < chords.size(); ++i)
            measure.slots.push_back ({ chords[i],
                                       i == 0 ? beats - share * static_cast<int> (chords.size() - 1)
                                              : share });

        chartView.setChart (chart);
        chartView.setMeasureReharmonised (selectedMeasure, true);
        measureSelected (selectedMeasure);
    };

    practiceMenu.onPractiseStyleChanged = [this] { applyPractiseStyle(); };
    practiceMenu.onDensityChanged = [this]
    {
        shownFor.clear();   // a different colour is a different set of shapes
        updateVoicingControls();
    };
    practiceMenu.onSoundChanged = [this]
    {
        if (! practiceMenu.soundEnabled() && onSilenceRequested != nullptr)
            onSilenceRequested();

        updateVoicingControls();
    };
    practiceMenu.onConnectMidiRequested = [this]
    {
        if (onConnectMidiRequested != nullptr)
            onConnectMidiRequested();
    };
    practiceMenu.onImportExportRequested = [this]
    {
        // Only offer to open a file if the shell hosting us can read one.
        ioDialog.onOpenFileRequested = onOpenChartFileRequested == nullptr
                                         ? std::function<void()>()
                                         : [this] { onOpenChartFileRequested(); };

        ioDialog.showFor (*this, chart);
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
        keyboard.ensureNotesVisible (voicing.midiNotes);
        keyboard.setHighlightedNotes (voicing.midiNotes);
        updateVoicingControls();
    };

    loadProgressionText (defaultProgression);
    applyPractiseStyle();
    updateVoicingControls();

    // Drives the chord-settling window; the collector has no clock of its own.
    startTimerHz (30);
    setSize (1100, 800);
}

MainComponent::~MainComponent()
{
    stopTimer();
    detachInputSource (keyboard);
}

void MainComponent::attachInputSource (core::NoteInputSource& source)
{
    source.addListener (&collector);
    source.addListener (&soundRelay);
}

void MainComponent::detachInputSource (core::NoteInputSource& source)
{
    source.removeListener (&collector);
    source.removeListener (&soundRelay);
}

void MainComponent::SoundRelay::noteEventReceived (const core::NoteEvent& event)
{
    if (! owner.practiceMenu.soundEnabled() || owner.onSoundNote == nullptr)
        return;

    owner.onSoundNote (event.midiNote, event.isNoteOn);
}

void MainComponent::SoundRelay::sustainChanged (bool isDown)
{
    // A hardware pedal and the on-screen one are the same event by the time
    // they reach here, so the control shows the pedal's state either way.
    owner.keyboard.showSustainPedal (isDown);

    if (owner.onSustainChanged != nullptr)
        owner.onSustainChanged (isDown);
}

void MainComponent::applyPractiseStyle()
{
    core::VoicingAnalyzer::Options options;

    if (const auto type = practiceMenu.practiseType())
        options.practiseType = *type;

    analyzer = core::VoicingAnalyzer { options };

    shownFor.clear();   // a different shape is a different set of suggestions
    updateVoicingControls();

    // Re-read whatever is still under the hands against the new expectation.
    const auto held = collector.heldNotes();

    if (! held.isEmpty())
        handleVoicing (held, core::NoteSource::onScreenKeyboard);
}

void MainComponent::setMidiStatus (const String& status)
{
    midiStatus = status;
    practiceMenu.setMidiStatus (status);
    repaint();
}

void MainComponent::chartFileWasRead (const String& text)
{
    ioDialog.fileWasRead (text);
}

void MainComponent::chartFileCouldNotBeRead (const String& reason)
{
    ioDialog.fileCouldNotBeRead (reason);
}

void MainComponent::setSoundAvailable (bool available, const String& note)
{
    soundAvailable = available;
    practiceMenu.setSoundNote (note);
    updateVoicingControls();
}

void MainComponent::loadChart (const core::Chart& newChart)
{
    adoptChart (newChart, true);
}

void MainComponent::adoptChart (const core::Chart& newChart, bool becomesTheOriginal)
{
    chart = newChart;

    if (becomesTheOriginal)
        originalChart = newChart;

    selectedMeasure = 0;

    chartView.setChart (chart);
    chartView.clearReharmonisedMarks();
    chartView.setSelectedMeasure (0);

    refreshChartHead();
    measureSelected (0);
    resized();
    repaint();
}

void MainComponent::refreshChartHead()
{
    // The editor shows the chart as text, so it has to follow the chart.
    String text;

    for (const auto& measure : chart.measures)
    {
        text += "| ";

        for (const auto& slot : measure.slots)
            text += String (slot.chord.toString()) + " ";
    }

    if (text.isNotEmpty())
        text += "|";

    progressionEditor.setText (text.trim(), dontSendNotification);
}

void MainComponent::loadProgressionText (const String& text)
{
    auto result = core::parseProgressionText (text.toStdString(), "Practice chart");

    if (! result.ok())
    {
        parseError = "Could not read chart: " + String (result.error);
        repaint();
        return;
    }

    parseError.clear();
    adoptChart (*result.chart, true);
}

void MainComponent::applyEditedProgression()
{
    auto result = core::parseProgressionText (progressionEditor.getText().toStdString(),
                                              chart.title);

    if (! result.ok())
    {
        parseError = String (result.error);
        repaint();
        return;
    }

    if (! result.unreadable.empty())
    {
        StringArray names;

        for (const auto& symbol : result.unreadable)
            names.add (String (symbol));

        parseError = "Could not read: " + names.joinIntoString (", ");
    }
    else
    {
        parseError.clear();
    }

    // Keeping what the chart came with, since the editor only carries chords.
    auto edited = *result.chart;
    edited.title = chart.title;
    edited.style = chart.style;
    edited.composer = chart.composer;

    chart = edited;
    chartView.setChart (chart);
    chartView.clearReharmonisedMarks();
    measureSelected (jlimit (0, jmax (0, chart.measureCount() - 1), selectedMeasure));
    repaint();
}

void MainComponent::showEditor (bool shouldShow)
{
    editorOpen = shouldShow;
    progressionEditor.setVisible (shouldShow);
    applyEditButton.setVisible (shouldShow);

    if (shouldShow)
    {
        refreshChartHead();
        progressionEditor.grabKeyboardFocus();
    }
    else
    {
        parseError.clear();
    }

    resized();
    repaint();
}

const core::ChordSymbol* MainComponent::selectedChord() const
{
    return chart.chordAt (selectedMeasure);
}

void MainComponent::measureSelected (int measureIndex)
{
    selectedMeasure = measureIndex;

    const auto* chord = selectedChord();

    feedbackPanel.setExpectedChord (chord);
    feedbackPanel.clearAnalysis();
    keyboard.setSuggestedNotes ({});

    shownFor.clear();
    namedText.clear();
    updateVoicingControls();
    repaint();
}

void MainComponent::openSelectedMeasure()
{
    if (chart.measureCount() == 0)
        return;

    chordDialog.showFor (*this, chart, selectedMeasure);
}

void MainComponent::handleVoicing (const core::Voicing& voicing, core::NoteSource source)
{
    // Deliberately unused: analysis must not differ between a chord played on a
    // MIDI keyboard and the same chord tapped on the on-screen keyboard.
    ignoreUnused (source);

    const auto* chord = selectedChord();

    if (chord == nullptr)
        return;

    feedbackPanel.setAnalysis (analyzer.analyse (voicing, *chord), voicing, *chord);
    feedbackPanel.setRecognisedSubstitution (
        core::recogniseSubstitution (voicing, chart, selectedMeasure));

    namedText.clear();
    updateVoicingControls();
    repaint();
}

void MainComponent::nameWhatIsPlayed()
{
    const auto held = collector.heldNotes();

    if (held.isEmpty())
        return;

    const auto readings = identifier.identify (held);

    if (readings.empty())
    {
        namedText = "No chord accounts for all of those notes.";
        repaint();
        return;
    }

    // The best reading, then the others, the way the page offers them.
    const auto& best = readings.front();
    namedText = "These notes are " + withAccidentalSigns (String (best.chord.toString()));

    if (! best.omittedTones.empty())
    {
        StringArray omitted;

        for (const auto& tone : best.omittedTones)
            omitted.add (String (tone));

        namedText += " (no " + omitted.joinIntoString (", no ") + ")";
    }

    if (readings.size() > 1)
    {
        StringArray others;

        for (std::size_t i = 1; i < readings.size(); ++i)
            others.add (withAccidentalSigns (String (readings[i].chord.toString())));

        namedText += "  -  also reads as " + others.joinIntoString (", ");
    }

    repaint();
}

void MainComponent::showAnotherVoicing()
{
    const auto* chord = selectedChord();

    if (chord == nullptr)
        return;

    const auto type = practiceMenu.practiseType().value_or (core::VoicingType::rootlessLeftHand);
    const auto density = practiceMenu.density();

    const auto key = String (chord->toString()) + "/" + String (static_cast<int> (type))
                     + "/" + String (static_cast<int> (density));

    // Pressing the button again walks on to the next shape rather than
    // repeating the last one.
    if (key != shownFor)
    {
        shownFor = key;
        shownIndex = 0;
    }

    const auto voicings = core::idiomaticVoicings (*chord, type,
                                                   core::naturalAnchorFor (type), density);

    if (voicings.empty())
        return;

    const auto& voicing = voicings[static_cast<std::size_t> (shownIndex) % voicings.size()];
    shownIndex = (shownIndex + 1) % static_cast<int> (voicings.size());

    // Put it under the hands rather than merely drawing it: the shape then
    // sounds, gets analysed and can be named, exactly as if it had been played.
    keyboard.holdNotes (voicing.midiNotes);
    repaint();
}

void MainComponent::playHeldNotes()
{
    const auto held = collector.heldNotes();

    if (held.isEmpty() || ! practiceMenu.soundEnabled() || onSoundChord == nullptr)
        return;

    onSoundChord (held.midiNotes);
}

void MainComponent::updateVoicingControls()
{
    const auto held = collector.heldNotes();

    nameItButton.setMuted (held.isEmpty());
    playChordButton.setEnabled (! held.isEmpty() && practiceMenu.soundEnabled() && soundAvailable);
    showVoicingButton.setMuted (selectedChord() == nullptr);
    clearKeysButton.setMuted (held.isEmpty());
}

void MainComponent::timerCallback()
{
    collector.advanceTime (Time::getMillisecondCounterHiRes() * 0.001);
}

bool MainComponent::keyPressed (const KeyPress& key)
{
    // The space bar sounds what is under the hands, as it does on the page -
    // but not while the chart is being typed into.
    if (key == KeyPress::spaceKey && ! editorOpen)
    {
        playHeldNotes();
        return true;
    }

    return false;
}

//==============================================================================
MainComponent::Frame MainComponent::frame() const
{
    Frame out;
    auto area = getLocalBounds().reduced (16);

    out.header = area.removeFromTop (34);

    // The dock is measured off the bottom first - the keyboard has to stay
    // reachable - and the sheet takes what is left.
    // The keyboard is the point of the dock, so on a narrow window the dock
    // takes more of the height rather than less - a chart you cannot play
    // against is just a picture.
    const auto dockHeight = jlimit (170,
                                    jmax (170, area.getHeight() - 140),
                                    sizeClass == SizeClass::compact ? 330 : 290);

    out.dock = area.removeFromBottom (dockHeight);
    area.removeFromBottom (12);
    out.sheet = area;

    return out;
}

MainComponent::SheetLayout MainComponent::sheetLayout (Rectangle<int> area) const
{
    SheetLayout out;
    out.card = area;

    auto inside = area.reduced (cardPadding);

    out.head = inside.removeFromTop (headHeight);
    inside.removeFromTop (10);
    out.hint = inside.removeFromTop (hintHeight);
    inside.removeFromTop (4);
    // On a narrow window the three chart links will not sit on one row, so
    // the row becomes two and they wrap onto it.
    out.tools = inside.removeFromTop (sizeClass == SizeClass::compact ? toolsHeight * 2
                                                                      : toolsHeight);
    inside.removeFromTop (14);

    if (editorOpen)
    {
        out.editor = inside.removeFromTop (editorHeight);
        inside.removeFromTop (parseError.isNotEmpty() ? 26 : 12);
    }

    out.systems = inside;
    return out;
}

MainComponent::DockLayout MainComponent::dockLayout (Rectangle<int> area) const
{
    DockLayout out;
    auto docked = area.reduced (cardPadding, 12);

    out.status = docked.removeFromTop (20);
    docked.removeFromTop (8);

    out.controls = docked.removeFromBottom (30);

    if (sizeClass == SizeClass::compact)
    {
        out.practising = docked.removeFromBottom (18);
    }
    else
    {
        // Wide enough for both on one row: the line sits at its left end.
        out.practising = out.controls.withWidth (190);
    }

    docked.removeFromBottom (10);

    const auto feedbackHeight = jmin (jmax (0, docked.getHeight() / 2),
                                      sizeClass == SizeClass::compact ? 40 : 74);
    out.feedback = docked.removeFromTop (feedbackHeight);
    docked.removeFromTop (8);

    out.keys = docked;
    return out;
}

void MainComponent::paint (Graphics& g)
{
    g.fillAll (theme::cream);

    const auto bands = frame();

    drawEyebrow (g, "Jazz Learning App",
                 bands.header.withWidth (bands.header.getWidth() / 2),
                 Justification::centredLeft, theme::blushDeep, 11.0f);

    const auto sheet = sheetLayout (bands.sheet);

    g.setColour (theme::paper);
    g.fillRect (sheet.card);
    g.setColour (theme::line);
    g.drawRect (sheet.card, 1);

    // The head a lead sheet writes: the feel top left, the title in the middle,
    // who wrote it on the right. A narrow window has room for the title alone,
    // and three marks fighting over one line is worse than two of them waiting
    // for a wider window.
    auto head = sheet.head;

    if (sizeClass != SizeClass::compact)
    {
        const auto sideWidth = head.getWidth() / 4;

        drawEyebrow (g, chart.style.empty() ? String ("Medium Swing") : String (chart.style),
                     head.removeFromLeft (sideWidth).withTrimmedTop (8),
                     Justification::topLeft, theme::textSoft, 11.0f);

        auto credit = head.removeFromRight (sideWidth).withTrimmedTop (6);
        g.setColour (theme::textSoft);
        g.setFont (theme::serif (16.0f, false, true));
        g.drawText (chart.composer.empty() ? String ("chord chart") : String (chart.composer),
                    credit, Justification::topRight);
    }

    // Shrink the title until it fits rather than letting it run into whatever
    // is beside it - a long tune name is normal, not an edge case.
    const auto title = String (chart.title).toUpperCase();
    auto titleSize = jmin (30.0f, theme::headingFontSize() * 1.55f);

    while (titleSize > 13.0f
           && trackedWidth (theme::serif (titleSize, false), title, 1.6f)
              > static_cast<float> (head.getWidth()))
        titleSize -= 1.0f;

    g.setColour (theme::charcoal);
    g.setFont (theme::serif (titleSize, false));
    drawTracked (g, title, head, Justification::centred, 1.6f);

    g.setColour (theme::textSoft);
    g.setFont (theme::sans (theme::bodyFontSize()));
    g.drawText ("Click a bar to work on it - arrow keys move along. Click it again for its "
                "scales and reharmonisations.",
                sheet.hint, Justification::centredLeft, true);

    g.setColour (theme::line);
    g.fillRect (sheet.tools.getX(), sheet.tools.getBottom() + 6, sheet.tools.getWidth(), 1);

    if (editorOpen && parseError.isNotEmpty())
    {
        g.setColour (theme::rust);
        g.setFont (theme::sans (theme::bodyFontSize() - 1.0f));
        g.drawText (parseError,
                    Rectangle<int> (sheet.editor.getX(), sheet.editor.getBottom() + 4,
                                    sheet.editor.getWidth(), 20),
                    Justification::centredLeft);
    }

    // --- the dock -------------------------------------------------------
    const auto dock = dockLayout (bands.dock);

    g.setColour (theme::creamDeep);
    g.fillRect (bands.dock);
    g.setColour (theme::line);
    g.fillRect (bands.dock.getX(), bands.dock.getY(), bands.dock.getWidth(), 1);

    auto statusRow = dock.status;
    const auto* chord = selectedChord();

    if (chord != nullptr)
    {
        const auto labelFont = theme::sans (11.0f);
        const auto symbolFont = theme::serif (14.0f, true);
        const String expecting { "Expecting " };
        const auto symbol = withAccidentalSigns (String (chord->toString()));

        const auto labelWidth = GlyphArrangement::getStringWidth (labelFont, expecting);
        const auto symbolWidth = GlyphArrangement::getStringWidth (symbolFont, symbol);

        auto area = statusRow.removeFromRight (roundToInt (labelWidth + symbolWidth) + 8);

        g.setColour (theme::textSoft);
        g.setFont (labelFont);
        g.drawText (expecting, area.removeFromLeft (roundToInt (labelWidth)),
                    Justification::centredLeft);

        g.setColour (theme::blushDeep);
        g.setFont (symbolFont);
        g.drawText (symbol, area, Justification::centredLeft);
    }

    // What "Name it" found, when it has been asked.
    if (namedText.isNotEmpty())
    {
        g.setColour (theme::charcoal);
        g.setFont (theme::serif (theme::bodyFontSize() + 1.0f));
        g.drawText (namedText, statusRow, Justification::centredLeft, true);
    }

    g.setColour (theme::textSoft);
    g.setFont (theme::sans (11.0f));
    g.drawText ("Practising " + describeStyle (practiceMenu.practiseType()),
                dock.practising, Justification::centredLeft, true);
}

void MainComponent::resized()
{
    sizeClass = sizeClassForWidth (getWidth());
    keyboard.applySizeClass (sizeClass);

    auto bands = frame();

    const auto menuWidth = jmax (90, menuButton.preferredWidth());
    menuButton.setBounds (bands.header.removeFromRight (menuWidth)
                              .withSizeKeepingCentre (menuWidth, 22));

    const auto sheet = sheetLayout (bands.sheet);

    // The chart toolbar, right-aligned as the page sets it - and wrapped onto a
    // second row when one will not hold it.
    auto tools = sheet.tools;

    if (sizeClass == SizeClass::compact)
    {
        auto row = tools.removeFromTop (toolsHeight);

        for (auto* button : { &planButton, &editButton, &restoreButton })
        {
            const auto width = jmax (80, button->preferredWidth());

            if (width > row.getWidth())
                row = tools.removeFromTop (toolsHeight);

            button->setBounds (row.removeFromLeft (width).withSizeKeepingCentre (width, 22));
            row.removeFromLeft (14);
        }
    }
    else
    {
        for (auto* button : { &restoreButton, &editButton, &planButton })
        {
            const auto width = jmax (80, button->preferredWidth());
            button->setBounds (tools.removeFromRight (width).withSizeKeepingCentre (width, 22));
            tools.removeFromRight (18);
        }
    }

    if (editorOpen)
    {
        auto editorArea = sheet.editor;
        const auto applyWidth = jmax (90, applyEditButton.preferredWidth());
        applyEditButton.setBounds (editorArea.removeFromBottom (28).withWidth (applyWidth));
        editorArea.removeFromBottom (6);
        progressionEditor.setBounds (editorArea);
    }

    chartViewport.setBounds (sheet.systems);
    chartView.setSize (sheet.systems.getWidth(),
                       jmax (sheet.systems.getHeight(), chartView.preferredHeight()));

    // --- the dock -------------------------------------------------------
    const auto dock = dockLayout (bands.dock);

    feedbackPanel.setBounds (dock.feedback);
    keyboard.setBounds (dock.keys);

    // Practising ... | Play chord  Name it  Show me one ......... Clear keys
    auto controls = dock.controls;

    if (sizeClass != SizeClass::compact)
        controls.removeFromLeft (200);

    const auto place = [&controls] (Component& button, int width, int height)
    {
        button.setBounds (controls.removeFromLeft (width).withSizeKeepingCentre (width, height));
        controls.removeFromLeft (16);
    };

    place (playChordButton, jmax (100, playChordButton.preferredWidth()), 26);
    place (nameItButton, jmax (70, nameItButton.preferredWidth()), 22);
    place (showVoicingButton, jmax (100, showVoicingButton.preferredWidth()), 22);

    // The keyboard carries its own Clear, so on a narrow window this one goes
    // rather than being drawn over the button beside it.
    const auto clearWidth = jmax (90, clearKeysButton.preferredWidth());
    const auto roomForClear = controls.getWidth() >= clearWidth;

    clearKeysButton.setVisible (roomForClear);

    if (roomForClear)
        clearKeysButton.setBounds (controls.removeFromRight (clearWidth)
                                       .withSizeKeepingCentre (clearWidth, 22));

    for (auto* overlay : { static_cast<Component*> (&practiceMenu),
                           static_cast<Component*> (&chordDialog),
                           static_cast<Component*> (&planDialog),
                           static_cast<Component*> (&ioDialog) })
        if (overlay->isVisible())
            overlay->setBounds (getLocalBounds());
}

} // namespace jazz::ui
