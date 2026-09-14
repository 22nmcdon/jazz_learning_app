#include "TestFramework.h"
#include "jazz/core/NoteInput.h"

using namespace jazz::core;

namespace
{
    /** Stand-in for the on-screen keyboard and the hardware MIDI source: both
        push events through the same interface, which is the point of the test.
    */
    class TestSource : public NoteInputSource
    {
    public:
        std::string inputName() const override { return "Test source"; }

        void press (int note, double time, NoteSource source = NoteSource::onScreenKeyboard)
        {
            broadcast ({ note, 0.8f, true, source, time });
        }

        void release (int note, double time, NoteSource source = NoteSource::onScreenKeyboard)
        {
            broadcast ({ note, 0.0f, false, source, time });
        }
    };
}

TEST ("simultaneous touches arrive as one voicing, not three note-ons")
{
    TestSource source;
    VoicingCollector collector;
    source.addListener (&collector);

    auto voicings = 0;
    Voicing received;

    collector.onVoicing = [&] (const Voicing& voicing, NoteSource)
    {
        ++voicings;
        received = voicing;
    };

    // Three fingers landing within a few milliseconds of each other.
    source.press (51, 0.000);
    source.press (55, 0.008);
    source.press (58, 0.015);

    collector.advanceTime (0.100);

    CHECK_EQ (voicings, 1);
    CHECK_EQ (received.size(), std::size_t (3));
    CHECK_EQ (received.describe(), std::string ("Eb3 G3 Bb3"));
}

TEST ("a note added after the window re-reports the fuller voicing")
{
    TestSource source;
    VoicingCollector collector;
    source.addListener (&collector);

    std::vector<std::size_t> sizes;
    collector.onVoicing = [&] (const Voicing& voicing, NoteSource) { sizes.push_back (voicing.size()); };

    source.press (51, 0.0);
    collector.advanceTime (0.1);       // first chord settles

    source.press (55, 0.2);
    collector.advanceTime (0.3);       // second finger lands later

    CHECK_EQ (sizes.size(), std::size_t (2));
    CHECK_EQ (sizes[0], std::size_t (1));
    CHECK_EQ (sizes[1], std::size_t (2));
}

TEST ("nothing is reported while the chord is still being played")
{
    TestSource source;
    VoicingCollector collector;
    source.addListener (&collector);

    auto voicings = 0;
    collector.onVoicing = [&] (const Voicing&, NoteSource) { ++voicings; };

    source.press (51, 0.00);
    source.press (55, 0.02);
    collector.advanceTime (0.04);      // still inside the chord window

    CHECK_EQ (voicings, 0);
}

TEST ("releasing every key ends the chord without a second report")
{
    TestSource source;
    VoicingCollector collector;
    source.addListener (&collector);

    auto voicings = 0;
    collector.onVoicing = [&] (const Voicing&, NoteSource) { ++voicings; };

    source.press (51, 0.0);
    source.press (55, 0.0);
    collector.advanceTime (0.1);

    source.release (51, 0.5);
    source.release (55, 0.6);
    collector.advanceTime (1.0);

    CHECK_EQ (voicings, 1);
    CHECK (collector.heldNotes().isEmpty());
}

TEST ("held notes are reported for live key highlighting")
{
    TestSource source;
    VoicingCollector collector;
    source.addListener (&collector);

    std::vector<std::size_t> heldCounts;
    collector.onHeldNotesChanged = [&] (const Voicing& voicing) { heldCounts.push_back (voicing.size()); };

    source.press (60, 0.0);
    source.press (64, 0.0);
    source.release (60, 0.3);

    CHECK_EQ (heldCounts.size(), std::size_t (3));
    CHECK_EQ (heldCounts.back(), std::size_t (1));
}

TEST ("MIDI and on-screen input produce identical voicings")
{
    const auto collect = [] (NoteSource inputSource)
    {
        TestSource source;
        VoicingCollector collector;
        source.addListener (&collector);

        Voicing result;
        collector.onVoicing = [&] (const Voicing& voicing, NoteSource) { result = voicing; };

        source.press (52, 0.000, inputSource);
        source.press (55, 0.005, inputSource);
        source.press (59, 0.010, inputSource);
        collector.advanceTime (0.1);

        return result;
    };

    CHECK_EQ (collect (NoteSource::hardwareMidi).describe(),
              collect (NoteSource::onScreenKeyboard).describe());
}

TEST ("the source reports which input produced the chord")
{
    TestSource source;
    VoicingCollector collector;
    source.addListener (&collector);

    auto reported = NoteSource::onScreenKeyboard;
    collector.onVoicing = [&] (const Voicing&, NoteSource s) { reported = s; };

    source.press (60, 0.0, NoteSource::hardwareMidi);
    collector.advanceTime (0.1);

    CHECK (reported == NoteSource::hardwareMidi);
}

TEST ("a removed listener stops receiving events")
{
    TestSource source;
    VoicingCollector collector;
    source.addListener (&collector);
    source.removeListener (&collector);

    source.press (60, 0.0);
    collector.advanceTime (0.1);

    CHECK (collector.heldNotes().isEmpty());
}

TEST ("duplicate note-ons do not double up")
{
    TestSource source;
    VoicingCollector collector;
    source.addListener (&collector);

    source.press (60, 0.0);
    source.press (60, 0.01);

    CHECK_EQ (collector.heldNotes().size(), std::size_t (1));
}
