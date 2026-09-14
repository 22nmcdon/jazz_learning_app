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

void VoicingCollector::noteEventReceived (const NoteEvent& event)
{
    lastSource = event.source;

    if (event.isNoteOn)
    {
        if (std::find (held.begin(), held.end(), event.midiNote) == held.end())
            held.push_back (event.midiNote);

        // Any new note restarts the window, so a rolled or tapped chord is
        // gathered up rather than reported note by note.
        lastNoteOnTime = event.timestampSeconds;
        awaitingSettle = true;
    }
    else
    {
        held.erase (std::remove (held.begin(), held.end(), event.midiNote), held.end());

        // Releasing a note ends the chord: report whatever was still down.
        if (awaitingSettle && held.empty())
            awaitingSettle = false;
    }

    if (onHeldNotesChanged != nullptr)
        onHeldNotesChanged (heldNotes());

    emitIfSettled (event.timestampSeconds);
}

void VoicingCollector::advanceTime (double nowSeconds)
{
    emitIfSettled (nowSeconds);
}

void VoicingCollector::emitIfSettled (double nowSeconds)
{
    if (! awaitingSettle || held.empty())
        return;

    if (nowSeconds - lastNoteOnTime < options.chordWindowSeconds)
        return;

    awaitingSettle = false;

    if (onVoicing != nullptr)
        onVoicing (heldNotes(), lastSource);
}

Voicing VoicingCollector::heldNotes() const
{
    return Voicing::fromNotes (held);
}

void VoicingCollector::reset()
{
    held.clear();
    awaitingSettle = false;
}

} // namespace jazz::core
