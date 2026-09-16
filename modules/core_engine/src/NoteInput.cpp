#include "jazz/core/NoteInput.h"

#include <algorithm>

namespace jazz::core
{

void NoteInputSource::addListener (NoteInputListener* listener)
{
    if (listener != nullptr
        && std::find (listeners.begin(), listeners.end(), listener) == listeners.end())
        listeners.push_back (listener);
}

void NoteInputSource::removeListener (NoteInputListener* listener)
{
    listeners.erase (std::remove (listeners.begin(), listeners.end(), listener), listeners.end());
}

void NoteInputSource::broadcast (const NoteEvent& event)
{
    // Copy first: a listener may remove itself while handling the event.
    const auto snapshot = listeners;

    for (auto* listener : snapshot)
        listener->noteEventReceived (event);
}

void NoteInputSource::broadcastSustain (bool isDown)
{
    // Copy first: a listener may remove itself while handling the event.
    const auto snapshot = listeners;

    for (auto* listener : snapshot)
        listener->sustainChanged (isDown);
}

void VoicingCollector::noteEventReceived (const NoteEvent& event)
{
    lastSource = event.source;

    if (event.isNoteOn)
    {
        if (std::find (sounding.begin(), sounding.end(), event.midiNote) == sounding.end())
            sounding.push_back (event.midiNote);

        if (std::find (keysDown.begin(), keysDown.end(), event.midiNote) == keysDown.end())
            keysDown.push_back (event.midiNote);

        // Any new note restarts the window, so a rolled or tapped chord is
        // gathered up rather than reported note by note - and so a chord
        // pedalled one note at a time arrives as the chord it adds up to.
        lastNoteOnTime = event.timestampSeconds;
        awaitingSettle = true;
    }
    else
    {
        keysDown.erase (std::remove (keysDown.begin(), keysDown.end(), event.midiNote),
                        keysDown.end());

        // The pedal defers the release rather than cancelling it: the note goes
        // when the foot comes up.
        if (! sustainDown)
            sounding.erase (std::remove (sounding.begin(), sounding.end(), event.midiNote),
                            sounding.end());

        // Releasing a note ends the chord: report whatever was still down.
        if (awaitingSettle && sounding.empty())
            awaitingSettle = false;
    }

    if (onHeldNotesChanged != nullptr)
        onHeldNotesChanged (heldNotes());

    emitIfSettled (event.timestampSeconds);
}

void VoicingCollector::sustainChanged (bool isDown)
{
    if (sustainDown == isDown)
        return;

    sustainDown = isDown;

    if (sustainDown)
        return;   // nothing changes until the pedal comes back up

    releaseUnheldNotes();
}

void VoicingCollector::releaseUnheldNotes()
{
    const auto before = sounding.size();

    sounding.erase (std::remove_if (sounding.begin(), sounding.end(),
                                    [this] (int note)
                                    {
                                        return std::find (keysDown.begin(), keysDown.end(), note)
                                               == keysDown.end();
                                    }),
                    sounding.end());

    if (sounding.size() == before)
        return;

    if (sounding.empty())
        awaitingSettle = false;

    if (onHeldNotesChanged != nullptr)
        onHeldNotesChanged (heldNotes());
}

void VoicingCollector::advanceTime (double nowSeconds)
{
    emitIfSettled (nowSeconds);
}

void VoicingCollector::emitIfSettled (double nowSeconds)
{
    if (! awaitingSettle || sounding.empty())
        return;

    if (nowSeconds - lastNoteOnTime < options.chordWindowSeconds)
        return;

    awaitingSettle = false;

    if (onVoicing != nullptr)
        onVoicing (heldNotes(), lastSource);
}

Voicing VoicingCollector::heldNotes() const
{
    return Voicing::fromNotes (sounding);
}

Voicing VoicingCollector::keysHeld() const
{
    return Voicing::fromNotes (keysDown);
}

void VoicingCollector::reset()
{
    sounding.clear();
    keysDown.clear();
    awaitingSettle = false;

    // The pedal is not reset: a foot does not lift because the notes were
    // cleared, and pretending otherwise would swallow the next chord's sustain.
}

} // namespace jazz::core
