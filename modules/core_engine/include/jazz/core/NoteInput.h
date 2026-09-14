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

    /** Drives the chord-window timeout; call from a timer (or directly in tests). */
    void advanceTime (double nowSeconds);

    Voicing heldNotes() const;
    void reset();

private:
    void emitIfSettled (double nowSeconds);

    Options options;
    std::vector<int> held;
    NoteSource lastSource { NoteSource::onScreenKeyboard };
    double lastNoteOnTime {};
    bool awaitingSettle {};
};

} // namespace jazz::core
