#include "WebUi.h"

#include "jazz/api/EngineApi.h"

#include <vector>

#include <BinaryData.h>



namespace jazz::app
{

using namespace juce;

namespace
{
    // The two directions of the bridge. Names are shared with the page, and
    // nothing else may be sent across it: a rule in one and a drawing in the
    // other is how the shells started disagreeing last time.
    const Identifier engineCallEvent  { "jazz.engine.call" };
    const Identifier engineReplyEvent { "jazz.engine.reply" };
    const Identifier soundEvent       { "jazz.sound" };
    const Identifier fileOpenEvent    { "jazz.file.open" };
    const Identifier fileReadEvent    { "jazz.file.read" };
    const Identifier midiEvent        { "jazz.midi" };
    const Identifier sustainEvent     { "jazz.sustain" };
    const Identifier statusEvent      { "jazz.status" };
    const Identifier pageLogEvent     { "jazz.log" };

    /** An argument from the page, as the engine wants it. */
    String argString (const var& args, int index)
    {
        if (auto* array = args.getArray())
            if (isPositiveAndBelow (index, array->size()))
                return (*array)[index].toString();

        return {};
    }

    int argInt (const var& args, int index)
    {
        if (auto* array = args.getArray())
            if (isPositiveAndBelow (index, array->size()))
                return static_cast<int> ((*array)[index]);

        return 0;
    }

    /** Lays one compiled-in resource down on disk, saying whether it landed. */
    bool writeResource (const File& target, const char* resourceName)
    {
        int size = 0;
        const auto* data = BinaryData::getNamedResource (resourceName, size);

        return data != nullptr
                 && target.replaceWithData (data, static_cast<std::size_t> (size));
    }

    /** Answers one of the engine's questions.

        Named rather than dispatched by table so an unknown name is an answer
        the page can show, not a silent empty string.
    */
    std::string answer (const String& name, const var& args)
    {
        const auto text = [&args] (int i) { return argString (args, i).toStdString(); };
        const auto number = [&args] (int i) { return argInt (args, i); };

        if (name == "jazzParseChart")        return api::parseChart (text (0).c_str());
        if (name == "jazzScalesForChord")    return api::scalesForChord (text (0).c_str(), text (1).c_str());
        if (name == "jazzScaleStyles")       return api::scaleStyles();
        if (name == "jazzImportIRealPro")    return api::importIRealPro (text (0).c_str());
        if (name == "jazzChartFromPage")     return api::chartFromPage (text (0).c_str());
        if (name == "jazzReharmPlans")       return api::reharmPlans (text (0).c_str());
        if (name == "jazzReharmStyles")      return api::reharmStyles();
        if (name == "jazzVoicingShape")
            return api::voicingShape (text (0).c_str(), text (1).c_str());
        if (name == "jazzVoicingFromShape")
            return api::voicingFromShape (text (0).c_str(), text (1).c_str(), number (2));
        if (name == "jazzVoicedGuideTones")  return api::voicedGuideTones (text (0).c_str(), text (1).c_str());
        if (name == "jazzIdentifyChord")     return api::identifyChord (text (0).c_str());

        if (name == "jazzReharmonise")
            return api::reharmonise (text (0).c_str(), number (1), number (2), number (3),
                                     text (4).c_str());

        if (name == "jazzAnalyseVoicing")
            return api::analyseVoicing (text (0).c_str(), text (1).c_str(), text (2).c_str());

        if (name == "jazzExportIRealPro")
            return api::exportIRealPro (text (0).c_str(), text (1).c_str(),
                                        text (2).c_str(), text (3).c_str(),
                                        number (4), number (5));

        if (name == "jazzRecogniseSubstitution")
            return api::recogniseSubstitution (text (0).c_str(), number (1),
                                               text (2).c_str(), number (3));

        if (name == "jazzIdiomaticVoicings")
            return api::idiomaticVoicings (text (0).c_str(), number (1),
                                           text (2).c_str(), number (3));

        if (name == "jazzCompingVoicing")
            return api::compingVoicing (text (0).c_str(), text (1).c_str());

        if (name == "jazzCompStyles")        return api::compStyles();

        if (name == "jazzWalkingBass")
            return api::walkingBass (text (0).c_str(), number (1), number (2), number (3));

        if (name == "jazzCompPlan")
            return api::compPlan (text (0).c_str(), text (1).c_str(),
                                  number (2), number (3), number (4));

        if (name == "jazzCompHit")
            return api::compHit (text (0).c_str(), text (1).c_str(), number (2),
                                 number (3), number (4), text (5).c_str(), number (6));

        if (name == "jazzCompTake")
            return api::compTake (text (0).c_str(), text (1).c_str(),
                                  number (2), number (3), text (4).c_str());

        // Solo practice. The take these drive lives in jazz::api, so the app and
        // the browser behave the same way without this shell remembering a thing.
        if (name == "jazzSoloStartTake")     return api::soloStartTake();
        if (name == "jazzSoloEndTake")       return api::soloEndTake();
        if (name == "jazzSoloPlayNote")
            return api::soloPlayNote (number (0), number (1), number (2), number (3));

        if (name == "jazzSoloSetBar")
            return api::soloSetBar (number (0), text (1).c_str(), text (2).c_str(),
                                    text (3).c_str(), number (4));

        // The practice record. Stateless like everything above the take: the
        // history is handed over whole on every call and nothing here keeps it.
        if (name == "jazzPracticeReading")
            return api::practiceReading (text (0).c_str(), number (1));

        if (name == "jazzTuneProgress")
            return api::tuneProgress (text (0).c_str(), text (1).c_str(), number (2));

        return "{\"ok\":false,\"error\":\"No engine call named "
               + name.toStdString() + "\"}";
    }
}

//==============================================================================
WebUi::WebUi (MidiDeviceInput& midiInput, ElectricPiano& piano)
    : midi (midiInput), sound (piano)
{
    using Options = WebBrowserComponent::Options;

    auto options = Options {}
        .withNativeIntegrationEnabled()
        .withEventListener (engineCallEvent, [this] (const var& r) { handleEngineCall (r); })
        .withEventListener (soundEvent,      [this] (const var& r) { handleSound (r); })
        .withEventListener (fileOpenEvent,   [this] (const var& r) { handleFileOpen (r); })
        // Without this a script error in the page is completely silent: there
        // is no console to look at in a shipped app, and the symptom is an
        // interface that simply stops responding.
        .withEventListener (pageLogEvent, [] (const var& report)
        {
            if (auto* object = report.getDynamicObject())
                Logger::writeToLog ("[page] " + object->getProperty ("message").toString()
                                    + " (" + object->getProperty ("line").toString() + ")");
        });

   #if JUCE_WINDOWS
    // Paint the page's cream underneath, so resizing does not flash white.
    options = options.withBackend (Options::Backend::webview2)
                     .withWinWebView2Options (Options::WinWebView2 {}
                                                  .withBackgroundColour (Colour (0xfffaf6f0)));
   #endif

    browser = std::make_unique<WebBrowserComponent> (options);
    addAndMakeVisible (*browser);

    midi.addListener (&relay);

    browser->goToURL (pageUrl());

    // The page asks for status once it is up, but push it anyway in case a
    // device was already open before the webview finished loading.
    pushStatus();
}

WebUi::~WebUi()
{
    midi.removeListener (&relay);

    // The page is rewritten every launch, so there is never a reason to leave
    // one behind - and a copy of the interface sitting in a shared directory
    // between runs is part of what the unpredictable name exists to avoid.
    // Recursively, because it is a folder now: the page has pdf.js beside it.
    if (pageFolder.isDirectory())
        pageFolder.deleteRecursively();
}

void WebUi::resized()
{
    if (browser != nullptr)
        browser->setBounds (getLocalBounds());
}

//==============================================================================
/** Writes the page out and returns a URL for it.

    The page ships inside the binary, but it is handed to the webview as a file
    rather than through JUCE's resource provider. On Linux that provider does
    not reliably deliver a document this size - the page arrives, its CSS paints
    and its script never runs - while the very same page loads perfectly from a
    URL. A file in the app's own temporary directory is the smallest thing that
    behaves the same way on every platform, needs no server and no network, and
    is rewritten on each launch so it can never go stale against the binary.

    **The name has to be unpredictable, and that is a security property rather
    than a tidiness one.** This wrote to `<temp>/jazz-learning-app-ui/index.html`
    - a fixed path. On Linux the temp directory is `/tmp`, which every account
    on the machine can write to, so anything else running there could create
    that directory first, or replace the file between this write and the
    webview's read. What it would get is its own HTML inside a webview holding a
    native bridge to the engine, the MIDI devices and the file system. macOS and
    Windows give each account a private temp directory, so this was a Linux hole
    specifically - which is exactly the sort that goes unnoticed on a Mac.

    `createTempFile` gives a name nobody can guess, and the sticky bit on `/tmp`
    does the rest: a directory somebody else owns cannot be replaced, and a name
    they cannot predict cannot be staked out in advance.

    **It is a directory now, and it used to be a single file.** The note here
    used to say that nothing in the page is fetched relative to itself, so the
    name was free to be random - true when the styles and the script were inline
    and the engine came over the bridge. pdf.js broke it: reading a PDF needs a
    real script file, and the page asks for it at `assets/pdf.min.js`, which is
    a sibling. So the unguessable name names a folder instead, holding the page
    and that one directory. The security argument is untouched by the change -
    an unpredictable directory is no easier to stake out than an unpredictable
    file - but it is now a claim about a folder, which is why this paragraph
    exists rather than the one it replaced.
*/
String WebUi::pageUrl()
{
    // A name nobody can guess, used as a folder rather than as a file.
    pageFolder = File::createTempFile ({});

    // Nothing should be here - the name is random and this process just made it
    // up - but a folder that already exists is one whose contents are somebody
    // else's, and the page must not be laid down beside them.
    if (pageFolder.exists())
        pageFolder.deleteRecursively();

    const auto assets = pageFolder.getChildFile ("assets");

    // Creates the parent on the way, so the folder itself needs no separate
    // call. A failure here is a shell with nowhere to put its interface.
    if (! assets.createDirectory())
        return {};

    const auto page = pageFolder.getChildFile ("index.html");

    if (! writeResource (page, "index_html"))
        return {};

    /*  pdf.js, beside the page where it asks for it.

        Best effort on purpose. Reading a PDF is one feature; the interface is
        everything. A shell that could not write these two files should still
        start and let somebody paste an iReal Pro link, which is exactly what
        the page does when the library will not load - `readPdf` says so in
        words rather than failing silently.

        The worker is written even though nothing ends up running in one.
        Measured from a `file://` page: WebKitGTK constructs the `Worker`
        happily and Chromium refuses it outright ("cannot be accessed from
        origin 'null'") - and in both, pdf.js settles on its own fake worker
        and parses on the main thread. Slower, and fine for one import. It is
        laid down because that fallback still loads the file, and because the
        two engines disagreeing about the same page is exactly the sort of
        thing to write down rather than rediscover.
    */
    writeResource (assets.getChildFile ("pdf.min.js"), "pdf_min_js");
    writeResource (assets.getChildFile ("pdf.worker.min.js"), "pdf_worker_min_js");

    return page.getFullPathName().startsWith ("/")
             ? "file://" + page.getFullPathName()
             : page.getFullPathName();
}

//==============================================================================
void WebUi::handleEngineCall (const var& request)
{
    if (auto* object = request.getDynamicObject())
    {
        const auto id = object->getProperty ("id");
        const auto name = object->getProperty ("name").toString();
        const auto args = object->getProperty ("args");

        /*  Nothing the engine throws may reach the message loop.

            This is native code with no sandbox under it: an exception out of
            `answer` propagates through JUCE's webview callback with nothing to
            catch it, which is `std::terminate` - the whole app, gone. That was
            not hypothetical. A shared iReal Pro link whose metre was twenty
            digits threw `std::out_of_range` out of the chart reader and closed
            the app on whoever opened it.

            The reader no longer throws, and this is the floor under that. The
            page gets an error it can show, which is what every other failure
            here already does. */
        auto* reply = new DynamicObject();
        reply->setProperty ("id", id);

        try
        {
            reply->setProperty ("json", String (answer (name, args)));
        }
        catch (const std::exception& problem)
        {
            reply->setProperty ("json", String ("{\"ok\":false,\"error\":\"The engine could not read what "
                                                + name + " was given: " + String (problem.what()) + ".\"}"));
        }
        catch (...)
        {
            reply->setProperty ("json", String ("{\"ok\":false,\"error\":\"The engine could not read what "
                                                + name + " was given.\"}"));
        }

        sendToPage (engineReplyEvent, var (reply));
    }
}

void WebUi::handleSound (const var& request)
{
    auto* object = request.getDynamicObject();

    if (object == nullptr)
        return;

    const auto what = object->getProperty ("what").toString();

    if (what == "note")
    {
        const auto note = static_cast<int> (object->getProperty ("note"));
        const auto velocity = static_cast<float> (static_cast<double> (object->getProperty ("velocity")));

        if (static_cast<bool> (object->getProperty ("on")))
            sound.noteOn (note, velocity > 0.0f ? velocity : 0.8f);
        else
            sound.noteOff (note);
    }
    else if (what == "chord")
    {
        sound.allNotesOff();

        if (auto* notes = object->getProperty ("notes").getArray())
            for (const auto& note : *notes)
                sound.noteOn (static_cast<int> (note), 0.8f);
    }
    else if (what == "bank")
    {
        // Which recorded instrument the player's own keys use. The band's are
        // named on each message instead, because three instruments may be on
        // three different sounds at once.
        sound.setPlayerBank (object->getProperty ("bank").toString().toStdString());
    }
    else if (what == "bass")
    {
        const auto note = static_cast<int> (object->getProperty ("note"));
        const auto touch = static_cast<float> (static_cast<double> (object->getProperty ("velocity")));

        if (note > 0)
            sound.bassNote (note, object->getProperty ("bank").toString().toStdString(),
                            touch > 0.0f ? touch : 1.0f);
        else
            sound.stopBass();
    }
    else if (what == "comp")
    {
        // A channel of its own, not a chord: it must not silence what the
        // player is holding, and what the player does must not silence it.
        std::vector<int> notes;

        if (auto* array = object->getProperty ("notes").getArray())
            for (const auto& note : *array)
                notes.push_back (static_cast<int> (note));

        /*  How hard each one, against the usual. One per note when the page
            sends them and empty when it does not - an older page simply gets
            the even chord it always got, rather than a silent one. */
        std::vector<float> touch;

        if (auto* array = object->getProperty ("velocities").getArray())
            for (const auto& one : *array)
                touch.push_back (static_cast<float> (static_cast<double> (one)));

        if (notes.empty())
            sound.stopComping();
        else
            sound.compChord (notes, object->getProperty ("bank").toString().toStdString(), touch);
    }
    else if (what == "click")
    {
        sound.click (static_cast<bool> (object->getProperty ("accented")));
    }
    else if (what == "drum")
    {
        /*  The page owns the pattern and names the piece; this shell owns the
            sound. Named rather than numbered so the two halves can be read
            side by side - the page sends "hiHat" and this looks for "hiHat".

            An unknown piece is silence rather than a substitute: a kit that
            answered a name it did not know with the nearest thing it had would
            hide exactly the mismatch worth seeing. */
        const auto piece = object->getProperty ("piece").toString();
        const auto level = static_cast<float> (static_cast<double> (object->getProperty ("level")));
        const auto struck = level > 0.0f ? level : 1.0f;

        if (piece == "ride")        sound.drum (ElectricPiano::DrumPiece::ride, struck);
        else if (piece == "hiHat")  sound.drum (ElectricPiano::DrumPiece::hiHat, struck);
        else if (piece == "snare")  sound.drum (ElectricPiano::DrumPiece::snare, struck);
        else if (piece == "kick")   sound.drum (ElectricPiano::DrumPiece::kick, struck);
    }
    else if (what == "silence")
    {
        sound.allNotesOff();
    }
    else if (what == "sustain")
    {
        sound.setSustain (static_cast<bool> (object->getProperty ("down")));
    }
}

void WebUi::handleFileOpen (const var&)
{
    chooser = std::make_unique<FileChooser> (
        "Open a chart",
        File::getSpecialLocation (File::userHomeDirectory),
        "*.html;*.htm;*.txt;*.irealb;*.irealbook;*.pdf");

    const auto browserFlags = FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (browserFlags, [this] (const FileChooser& picked)
    {
        const auto file = picked.getResult();

        if (file == File())
            return;

        auto* result = new DynamicObject();

        /*  A PDF is not text, so it goes over as bytes and the page's own
            pdf.js turns it into positioned text.

            This shell has no PDF library and does not need one. The engine's
            reader was always format-agnostic - `ChartFormats.h` says pulling
            text out of a file is the shell's job and deciding what it means is
            the engine's - and the missing half was never theory, it was the
            extractor. The page already had one. What it lacked in the app was
            the file, which `pageUrl` now writes beside it.

            Base64 because the bridge carries a `var`, and a `var` carries a
            string. A lead sheet is well under a megabyte; this is not the path
            to send a scanned book down.
        */
        if (file.hasFileExtension ("pdf"))
        {
            if (MemoryBlock bytes; file.loadFileAsData (bytes) && bytes.getSize() > 0)
            {
                result->setProperty ("name", file.getFileName());
                result->setProperty ("pdf", Base64::toBase64 (bytes.getData(), bytes.getSize()));
            }
            else
            {
                result->setProperty ("error", "That file is empty, or could not be opened.");
            }
        }
        else if (const auto text = file.loadFileAsString(); text.isNotEmpty())
            result->setProperty ("text", text);
        else
            result->setProperty ("error", "That file is empty, or could not be opened.");

        sendToPage (fileReadEvent, var (result));
    });
}

//==============================================================================
void WebUi::DeviceRelay::noteEventReceived (const core::NoteEvent& event)
{
    auto* note = new DynamicObject();
    note->setProperty ("note", event.midiNote);
    note->setProperty ("on", event.isNoteOn);
    note->setProperty ("velocity", event.velocity);

    owner.sendToPage (midiEvent, var (note));
}

void WebUi::DeviceRelay::sustainChanged (bool isDown)
{
    auto* pedal = new DynamicObject();
    pedal->setProperty ("down", isDown);

    owner.sendToPage (sustainEvent, var (pedal));
}

void WebUi::midiDevicesChanged()
{
    pushStatus();
}

void WebUi::pushStatus()
{
    const auto names = midi.openDeviceNames();

    auto* status = new DynamicObject();
    status->setProperty ("midi", names.isEmpty() ? String ("No MIDI device - use the on-screen keyboard")
                                                 : names.joinIntoString (", "));
    status->setProperty ("midiConnected", ! names.isEmpty());
    status->setProperty ("sound", sound.statusMessage());
    status->setProperty ("soundAvailable", sound.isRunning());

    sendToPage (statusEvent, var (status));
}

void WebUi::sendToPage (const Identifier& event, const var& payload)
{
    // The webview must be touched on the message thread, and MIDI callbacks do
    // not arrive on it. The hop is guarded by a SafePointer because a note can
    // be in flight while the window is closing, and a raw `this` would outlive
    // us by exactly that long.
    if (MessageManager::existsAndIsCurrentThread())
    {
        if (browser != nullptr)
            browser->emitEventIfBrowserIsVisible (event, payload);

        return;
    }

    MessageManager::callAsync ([safe = Component::SafePointer<WebUi> (this), event, payload]
    {
        if (safe != nullptr && safe->browser != nullptr)
            safe->browser->emitEventIfBrowserIsVisible (event, payload);
    });
}

} // namespace jazz::app
