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
        if (name == "jazzIdentifyChord")     return api::identifyChord (text (0).c_str());

        if (name == "jazzReharmonise")
            return api::reharmonise (text (0).c_str(), number (1), number (2), number (3));

        if (name == "jazzAnalyseVoicing")
            return api::analyseVoicing (text (0).c_str(), text (1).c_str(), text (2).c_str());

        if (name == "jazzExportIRealPro")
            return api::exportIRealPro (text (0).c_str(), text (1).c_str(),
                                        text (2).c_str(), text (3).c_str());

        if (name == "jazzRecogniseSubstitution")
            return api::recogniseSubstitution (text (0).c_str(), number (1),
                                               text (2).c_str(), number (3));

        if (name == "jazzIdiomaticVoicings")
            return api::idiomaticVoicings (text (0).c_str(), number (1),
                                           text (2).c_str(), number (3));

        if (name == "jazzCompingVoicing")
            return api::compingVoicing (text (0).c_str(), text (1).c_str());

        if (name == "jazzCompStyles")        return api::compStyles();

        if (name == "jazzCompPlan")
            return api::compPlan (text (0).c_str(), text (1).c_str(),
                                  number (2), number (3), number (4));

        // Solo practice. The take these drive lives in jazz::api, so the app and
        // the browser behave the same way without this shell remembering a thing.
        if (name == "jazzSoloStartTake")     return api::soloStartTake();
        if (name == "jazzSoloEndTake")       return api::soloEndTake();
        if (name == "jazzSoloPlayNote")
            return api::soloPlayNote (number (0), number (1), number (2));

        if (name == "jazzSoloSetBar")
            return api::soloSetBar (number (0), text (1).c_str(), text (2).c_str(),
                                    text (3).c_str(), number (4));

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
*/
String WebUi::pageUrl()
{
    int size = 0;
    const auto* data = BinaryData::getNamedResource ("index_html", size);

    if (data == nullptr)
        return {};

    pageFile = File::getSpecialLocation (File::tempDirectory)
                   .getChildFile ("jazz-learning-app-ui")
                   .getChildFile ("index.html");

    pageFile.getParentDirectory().createDirectory();
    pageFile.replaceWithData (data, static_cast<std::size_t> (size));

    return pageFile.getFullPathName().startsWith ("/")
             ? "file://" + pageFile.getFullPathName()
             : pageFile.getFullPathName();
}

//==============================================================================
void WebUi::handleEngineCall (const var& request)
{
    if (auto* object = request.getDynamicObject())
    {
        const auto id = object->getProperty ("id");
        const auto name = object->getProperty ("name").toString();
        const auto args = object->getProperty ("args");

        auto* reply = new DynamicObject();
        reply->setProperty ("id", id);
        reply->setProperty ("json", String (answer (name, args)));

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
    else if (what == "comp")
    {
        // A channel of its own, not a chord: it must not silence what the
        // player is holding, and what the player does must not silence it.
        std::vector<int> notes;

        if (auto* array = object->getProperty ("notes").getArray())
            for (const auto& note : *array)
                notes.push_back (static_cast<int> (note));

        if (notes.empty())
            sound.stopComping();
        else
            sound.compChord (notes);
    }
    else if (what == "click")
    {
        sound.click (static_cast<bool> (object->getProperty ("accented")));
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

        // A PDF is not text, and this shell has no library to make it text.
        if (file.hasFileExtension ("pdf"))
            result->setProperty ("error",
                                 "This app cannot read a PDF yet - it has no PDF library. "
                                 "Export the tune as an iReal Pro link, or share it as HTML, "
                                 "and paste that instead.");
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
