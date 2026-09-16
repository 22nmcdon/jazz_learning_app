#include "jazz/ui/OnScreenKeyboard.h"

#include <algorithm>

namespace jazz::ui
{

using namespace juce;

namespace
{
    /** Semitone offsets of the white keys within an octave. */
    constexpr int whiteKeyOffsets[] { 0, 2, 4, 5, 7, 9, 11 };

    /** White-key index each black key sits after, and its semitone offset. */
    constexpr int blackKeyAfterWhite[] { 0, 1, 3, 4, 5 };
    constexpr int blackKeyOffsets[]    { 1, 3, 6, 8, 10 };

    constexpr int whiteKeysPerOctave = 7;
}

OnScreenKeyboard::OnScreenKeyboard()
{
    setOpaque (true);

    addAndMakeVisible (octaveDownButton);
    addAndMakeVisible (octaveUpButton);

    octaveDownButton.onClick = [this] { shiftOctave (-1); };
    octaveUpButton.onClick   = [this] { shiftOctave (1); };

    addAndMakeVisible (clearButton);
    clearButton.onClick = [this] { clearHeldNotes(); };

    addAndMakeVisible (latchButton);
    latchButton.setColour (ToggleButton::textColourId, theme::textDim);
    latchButton.setColour (ToggleButton::tickColourId, theme::accent);
    latchButton.onClick = [this] { setLatchEnabled (latchButton.getToggleState()); };

    addAndMakeVisible (sustainButton);
    sustainButton.setColour (ToggleButton::textColourId, theme::textDim);
    sustainButton.setColour (ToggleButton::tickColourId, theme::accent);
    sustainButton.onClick = [this] { setSustainPedal (sustainButton.getToggleState()); };

    for (auto* button : { &octaveDownButton, &octaveUpButton, &clearButton })
    {
        button->setColour (TextButton::buttonColourId, theme::surfaceRaised);
        button->setColour (TextButton::textColourOffId, theme::text);
    }

    setLatchEnabled (currentInteractionMode() == InteractionMode::pointer);
}

void OnScreenKeyboard::setLatchEnabled (bool shouldLatch)
{
    if (! shouldLatch)
        clearHeldNotes();

    latchEnabled = shouldLatch;
    latchButton.setToggleState (shouldLatch, dontSendNotification);
    clearButton.setEnabled (shouldLatch);
    repaint();
}

void OnScreenKeyboard::clearHeldNotes()
{
    for (auto note : latchedNotes)
        broadcast ({ note, 0.0f, false, core::NoteSource::onScreenKeyboard, now() });

    latchedNotes.clear();
    repaint();
}

void OnScreenKeyboard::holdNotes (const std::vector<int>& midiNotes)
{
    clearHeldNotes();

    // One timestamp for the lot, so the collector groups them as a chord rather
    // than a run of single notes.
    const auto timestamp = now();

    for (auto note : midiNotes)
    {
        latchedNotes.push_back (note);
        broadcast ({ note, 0.8f, true, core::NoteSource::onScreenKeyboard, timestamp });
    }

    ensureNotesVisible (midiNotes);
    repaint();
}

void OnScreenKeyboard::setSustainPedal (bool isDown)
{
    if (sustainDown == isDown)
        return;

    sustainDown = isDown;
    sustainButton.setToggleState (isDown, dontSendNotification);

    // One sustain event on the shared interface, whether it came from this
    // button or from a pedal - the collector cannot tell, and should not.
    broadcastSustain (isDown);
    repaint();
}

void OnScreenKeyboard::showSustainPedal (bool isDown)
{
    if (sustainDown == isDown)
        return;

    sustainDown = isDown;
    sustainButton.setToggleState (isDown, dontSendNotification);
    repaint();
}

void OnScreenKeyboard::toggleLatchedNote (int midiNote)
{
    const auto existing = std::find (latchedNotes.begin(), latchedNotes.end(), midiNote);

    if (existing != latchedNotes.end())
    {
        latchedNotes.erase (existing);
        broadcast ({ midiNote, 0.0f, false, core::NoteSource::onScreenKeyboard, now() });
    }
    else
    {
        latchedNotes.push_back (midiNote);
        broadcast ({ midiNote, 0.8f, true, core::NoteSource::onScreenKeyboard, now() });
    }

    repaint();
}

void OnScreenKeyboard::setVisibleOctaves (int octaves)
{
    const auto clamped = jlimit (jmax (1, minimumOctaves), 7, octaves);

    if (clamped == visibleOctaves)
        return;

    visibleOctaves = clamped;
    rebuildKeys();
    repaint();
}

void OnScreenKeyboard::setLowestOctaveNote (int midiNote)
{
    // Always start on a C so the octave buttons move in whole octaves.
    const auto clamped = jlimit (12, 96, midiNote - core::toPitchClass (midiNote));

    if (clamped == lowestNote)
        return;

    lowestNote = clamped;
    rebuildKeys();
    repaint();
}

void OnScreenKeyboard::shiftOctave (int direction)
{
    setLowestOctaveNote (lowestNote + direction * core::semitonesPerOctave);
}

void OnScreenKeyboard::ensureNotesVisible (const std::vector<int>& midiNotes)
{
    if (midiNotes.empty())
        return;

    const auto lowest = *std::min_element (midiNotes.begin(), midiNotes.end());
    const auto highest = *std::max_element (midiNotes.begin(), midiNotes.end());
    const auto drawnHighest = lowestNote + visibleOctaves * core::semitonesPerOctave - 1;

    if (lowest >= lowestNote && highest <= drawnHighest)
        return;

    const auto newLowest = jmin (lowestNote, lowest - core::toPitchClass (lowest));
    const auto octaves = (jmax (highest, drawnHighest) - newLowest) / core::semitonesPerOctave + 1;

    minimumOctaves = jlimit (1, 7, octaves);
    lowestNote = jlimit (12, 96, newLowest);

    setVisibleOctaves (minimumOctaves);
    rebuildKeys();
    repaint();
}

void OnScreenKeyboard::setHighlightedNotes (const std::vector<int>& midiNotes)
{
    highlightedNotes = midiNotes;
    repaint();
}

void OnScreenKeyboard::setSuggestedNotes (const std::vector<int>& midiNotes)
{
    suggestedNotes = midiNotes;
    repaint();
}

void OnScreenKeyboard::applySizeClass (SizeClass sizeClass)
{
    // A phone gets one or two octaves and the octave buttons; a desktop window
    // has room for more of the keyboard at once.
    // jmax against the minimum keeps a widened keyboard wide: a note played
    // off the end of it must not disappear on the next resize.
    switch (sizeClass)
    {
        case SizeClass::compact:  setVisibleOctaves (jmax (2, minimumOctaves)); break;
        case SizeClass::regular:  setVisibleOctaves (jmax (3, minimumOctaves)); break;
        case SizeClass::expanded: setVisibleOctaves (jmax (4, minimumOctaves)); break;
    }

    setLatchEnabled (currentInteractionMode() == InteractionMode::pointer);
}

void OnScreenKeyboard::resized()
{
    auto area = getLocalBounds();
    const auto headerHeight = jmax (minimumTouchTarget(), 32);
    auto header = area.removeFromTop (headerHeight);

    const auto buttonWidth = jmax (minimumTouchTarget(), 44);
    octaveDownButton.setBounds (header.removeFromLeft (buttonWidth).reduced (4, 4));
    octaveUpButton.setBounds (header.removeFromRight (buttonWidth).reduced (4, 4));
    latchButton.setBounds (header.removeFromLeft (jmax (buttonWidth + 24, 72)).reduced (4, 4));
    clearButton.setBounds (header.removeFromRight (jmax (buttonWidth + 12, 60)).reduced (4, 4));

    // The pedal only fits once the octave label has room; on the narrowest
    // window it steps aside rather than sitting on top of the label.
    const auto sustainWidth = jmax (buttonWidth + 40, 86);
    const auto roomForSustain = header.getWidth() > sustainWidth + 70;

    sustainButton.setVisible (roomForSustain);

    if (roomForSustain)
        sustainButton.setBounds (header.removeFromLeft (sustainWidth).reduced (4, 4));

    keyboardArea = area;
    rebuildKeys();
}

void OnScreenKeyboard::rebuildKeys()
{
    keys.clear();

    if (keyboardArea.isEmpty())
        return;

    const auto totalWhiteKeys = whiteKeysPerOctave * visibleOctaves;
    const auto whiteWidth = static_cast<float> (keyboardArea.getWidth()) / static_cast<float> (totalWhiteKeys);
    const auto blackWidth = whiteWidth * 0.62f;
    const auto blackHeight = static_cast<float> (keyboardArea.getHeight()) * 0.62f;
    const auto top = static_cast<float> (keyboardArea.getY());
    const auto left = static_cast<float> (keyboardArea.getX());

    for (auto octave = 0; octave < visibleOctaves; ++octave)
    {
        for (auto white = 0; white < whiteKeysPerOctave; ++white)
        {
            const auto index = octave * whiteKeysPerOctave + white;
            const Rectangle<float> area { left + static_cast<float> (index) * whiteWidth,
                                          top,
                                          whiteWidth,
                                          static_cast<float> (keyboardArea.getHeight()) };

            keys.push_back ({ lowestNote + octave * core::semitonesPerOctave + whiteKeyOffsets[white],
                              false, area });
        }
    }

    // Black keys are added last so hit testing finds them first.
    for (auto octave = 0; octave < visibleOctaves; ++octave)
    {
        for (auto black = 0; black < 5; ++black)
        {
            const auto whiteIndex = octave * whiteKeysPerOctave + blackKeyAfterWhite[black];
            const auto centre = left + static_cast<float> (whiteIndex + 1) * whiteWidth;
            const Rectangle<float> area { centre - blackWidth * 0.5f, top, blackWidth, blackHeight };

            keys.push_back ({ lowestNote + octave * core::semitonesPerOctave + blackKeyOffsets[black],
                              true, area });
        }
    }
}

const OnScreenKeyboard::Key* OnScreenKeyboard::keyAt (juce::Point<float> position) const
{
    // Reverse order: black keys were appended last and sit on top.
    for (auto key = keys.rbegin(); key != keys.rend(); ++key)
        if (key->area.contains (position))
            return &*key;

    return nullptr;
}

bool OnScreenKeyboard::isHeld (int midiNote) const
{
    if (std::find (latchedNotes.begin(), latchedNotes.end(), midiNote) != latchedNotes.end())
        return true;

    return std::any_of (notesByTouchSource.begin(), notesByTouchSource.end(),
                        [midiNote] (const auto& entry) { return entry.second == midiNote; });
}

double OnScreenKeyboard::now() const
{
    return Time::getMillisecondCounterHiRes() * 0.001;
}

void OnScreenKeyboard::noteDown (int midiNote, juce::MouseInputSource source)
{
    const auto index = source.getIndex();
    const auto existing = notesByTouchSource.find (index);

    if (existing != notesByTouchSource.end())
    {
        if (existing->second == midiNote)
            return;

        broadcast ({ existing->second, 0.0f, false, core::NoteSource::onScreenKeyboard, now() });
        notesByTouchSource.erase (existing);
    }

    notesByTouchSource[index] = midiNote;

    // Every touch reports separately and with its own timestamp; grouping them
    // into a chord is VoicingCollector's job, not the widget's.
    broadcast ({ midiNote, 0.8f, true, core::NoteSource::onScreenKeyboard, now() });
    repaint();
}

void OnScreenKeyboard::noteUp (juce::MouseInputSource source)
{
    const auto existing = notesByTouchSource.find (source.getIndex());

    if (existing == notesByTouchSource.end())
        return;

    broadcast ({ existing->second, 0.0f, false, core::NoteSource::onScreenKeyboard, now() });
    notesByTouchSource.erase (existing);
    repaint();
}

void OnScreenKeyboard::mouseDown (const juce::MouseEvent& event)
{
    const auto* key = keyAt (event.position);

    if (key == nullptr)
        return;

    if (latchEnabled)
        toggleLatchedNote (key->midiNote);
    else
        noteDown (key->midiNote, event.source);
}

void OnScreenKeyboard::mouseDrag (const juce::MouseEvent& event)
{
    if (latchEnabled)
        return;  // dragging across keys would toggle a smear of latched notes

    if (const auto* key = keyAt (event.position))
        noteDown (key->midiNote, event.source);
}

void OnScreenKeyboard::mouseUp (const juce::MouseEvent& event)
{
    if (! latchEnabled)
        noteUp (event.source);
}

void OnScreenKeyboard::mouseExit (const juce::MouseEvent& event)
{
    if (! latchEnabled)
        noteUp (event.source);
}

void OnScreenKeyboard::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    const auto contains = [] (const std::vector<int>& notes, int midiNote)
    {
        return std::find (notes.begin(), notes.end(), midiNote) != notes.end();
    };

    g.setFont (Font (FontOptions (11.0f)));

    for (const auto& key : keys)
    {
        if (key.isBlack)
            continue;

        const auto held = isHeld (key.midiNote) || contains (highlightedNotes, key.midiNote);
        const auto suggested = contains (suggestedNotes, key.midiNote);

        g.setColour (held ? theme::accent : suggested ? theme::accentMuted : Colours::white);
        g.fillRect (key.area.reduced (1.0f));

        g.setColour (theme::outline);
        g.drawRect (key.area, 1.0f);

        // Label every C so the player can see where they are after an octave shift.
        if (core::toPitchClass (key.midiNote) == 0)
        {
            g.setColour (held ? theme::text : theme::textDim);
            g.drawText (String (core::midiNoteName (key.midiNote)),
                        key.area.toNearestInt().reduced (2, 4),
                        Justification::centredBottom);
        }
    }

    for (const auto& key : keys)
    {
        if (! key.isBlack)
            continue;

        const auto held = isHeld (key.midiNote) || contains (highlightedNotes, key.midiNote);
        const auto suggested = contains (suggestedNotes, key.midiNote);

        g.setColour (held ? theme::accent : suggested ? theme::accentMuted : Colour (0xff101014));
        g.fillRoundedRectangle (key.area.reduced (1.0f), 2.0f);
    }

    // Octave readout between the two shift buttons.
    g.setColour (theme::textDim);
    g.setFont (Font (FontOptions (theme::bodyFontSize())));
    g.drawText ("Octave " + String (core::octaveOf (lowestNote)) + " - "
                    + String (core::octaveOf (lowestNote + visibleOctaves * core::semitonesPerOctave - 1)),
                getLocalBounds().removeFromTop (jmax (minimumTouchTarget(), 32)),
                Justification::centred);
}

} // namespace jazz::ui
