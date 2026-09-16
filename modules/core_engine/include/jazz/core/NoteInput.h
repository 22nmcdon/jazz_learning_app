#pragma once

#include "jazz/core/Voicing.h"

#include <functional>
#include <string>
#include <vector>

namespace jazz::core
{

/** Where a note came from. Carried for display only - no analysis logic may
    branch on it, which is what keeps the on-screen keyboard a first-class input.
*/
enum class NoteSource
{
    hardwareMidi,
    onScreenKeyboard
};

struct NoteEvent
{
    int midiNote {};
    float velocity { 0.8f };
    bool isNoteOn {};
    NoteSource source { NoteSource::onScreenKeyboard };
    double timestampSeconds {};
};

class NoteInputListener
{
public:
    virtual ~NoteInputListener() = default;
    virtual void noteEventReceived (const NoteEvent& event) = 0;

    /** The sustain pedal went down or came up.

        A pedal is not a note, so it is not squeezed into NoteEvent; but it
        travels the same interface, because whether a note is still sounding is
        exactly the question every listener downstream is already answering.
        Listeners that do not care about the pedal need not implement it.
    */
    virtual void sustainChanged (bool /*isDown*/) {}
};

/** Base class for anything that produces notes: hardware MIDI in the platform
    shell, the on-screen keyboard widget in the UI layer.

    Everything downstream of this interface sees one event shape, so the analyser
    never learns whether a chord was played on a keyboard or tapped on glass.
*/
class NoteInputSource
{
public:
    virtual ~NoteInputSource() = default;

    /** Name shown in the input picker, e.g. "On-screen keyboard". */
    virtual std::string inputName() const = 0;

    void addListener (NoteInputListener* listener);
    void removeListener (NoteInputListener* listener);

protected:
    void broadcast (const NoteEvent& event);
    void broadcastSustain (bool isDown);

private:
    std::vector<NoteInputListener*> listeners;
};

/** Groups note events into chords.

    Notes that arrive close together - three fingers landing on a touchscreen, or
    a slightly rolled chord on a MIDI keyboard - are coalesced into a single
    Voicing rather than reported one note at a time, which is what the analyser
    expects. Time is supplied by the caller so this is testable without a clock.
*/
class VoicingCollector : public NoteInputListener
{
public:
    struct Options
    {
        /** How long to wait after the last note-on before calling the chord complete. */
        double chordWindowSeconds { 0.06 };
    };

    VoicingCollector() = default;
    explicit VoicingCollector (Options optionsToUse) : options (optionsToUse) {}

    /** Called when a complete voicing has settled. */
    std::function<void (const Voicing&, NoteSource)> onVoicing;

    /** Called whenever the set of held notes changes, for live key highlighting. */
    std::function<void (const Voicing&)> onHeldNotesChanged;

    void noteEventReceived (const NoteEvent& event) override;
    void sustainChanged (bool isDown) override;

    /** Drives the chord-window timeout; call from a timer (or directly in tests). */
    void advanceTime (double nowSeconds);

    /** Everything still sounding: the keys down, plus whatever the pedal is
        holding after the fingers left.
    */
    Voicing heldNotes() const;

    /** Just the keys physically down, which is what the keyboard draws as
        pressed. A pedalled note is sounding but nobody is holding it.
    */
    Voicing keysHeld() const;

    bool isSustaining() const noexcept { return sustainDown; }

    void reset();

private:
    void emitIfSettled (double nowSeconds);
    void releaseUnheldNotes();

    Options options;

    /** Notes sounding, and the subset of them a finger is still on. Splitting
        the two is the whole of sustain: a pedal stops a release from reaching
        the first list, and lifting it lets every deferred release through.
    */
    std::vector<int> sounding;
    std::vector<int> keysDown;

    NoteSource lastSource { NoteSource::onScreenKeyboard };
    double lastNoteOnTime {};
    bool awaitingSettle {};
    bool sustainDown {};
};

} // namespace jazz::core
