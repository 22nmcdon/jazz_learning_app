// Proves a built copy of the page actually boots.
//
// CI already proves the engine compiles and the app links. Nothing proved that
// the thing being deployed loads its engine, draws a chart and answers a click
// - and every way this page has broken so far broke exactly there, after a
// build that passed: a stylesheet that never arrived, a script that never ran.
// So this drives the real built page in a real browser and fails the deploy
// rather than the visitor.
//
//   node web/smoke-test.mjs <directory>
//
// The directory is served over HTTP rather than opened as a file, because that
// is how it is deployed: a service worker and Web MIDI both need an origin.

import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { extname, join, normalize } from "node:path";
import { chromium } from "playwright";

const root = process.argv[2];

if (!root) {
  console.error("usage: node web/smoke-test.mjs <directory>");
  process.exit(2);
}

const TYPES = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".json": "application/json",
  ".wasm": "application/wasm",
  ".css": "text/css; charset=utf-8"
};

const server = createServer(async (request, response) => {
  const path = decodeURIComponent(new URL(request.url, "http://x").pathname);
  const file = join(root, normalize(path).replace(/^(\.\.[/\\])+/, ""));

  try {
    const body = await readFile(file.endsWith("/") ? join(file, "index.html") : file);
    response.writeHead(200, { "content-type": TYPES[extname(file)] ?? "application/octet-stream" });
    response.end(body);
  } catch {
    response.writeHead(404).end("not found");
  }
});

await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));

const origin = `http://127.0.0.1:${server.address().port}`;
const browser = await chromium.launch();
const page = await browser.newPage();

/*  Records every oscillator the page starts, so a check can say what was
    actually sounded rather than only what the page says it did. It runs before
    any of the page's own script, which is what lets the file under test stay
    the shipped one, unmodified. Wrapping `start` rather than `createOscillator`
    alone keeps the scheduled time, which is the half the comp cares about. */
await page.addInitScript(() => {
  window.__sounded = [];
  const Ctor = window.AudioContext || window.webkitAudioContext;
  const realCreate = Ctor.prototype.createOscillator;

  /*  The page's own clock, so a check can say what time it is in the same
      units a scheduled sound is booked in. Taken off the first node made
      rather than off a context this script created, which would be a
      different clock entirely. */
  window.__audioNow = () => null;

  /*  `describe` is called when the node is *started*, never when it is made.
      The page builds a node and then sets its type, its frequency and its rate,
      so reading those at creation reads the defaults - every oscillator a sine
      at 440, which loses the click and every pitch with it. */
  const remember = (node, describe) => {
    const realStart = node.start.bind(node);
    const realStop = node.stop.bind(node);
    const record = { when: null, stoppedAt: null };

    node.start = function (when) {
      Object.assign(record, describe());
      record.when = when === undefined ? this.context.currentTime : when;
      window.__audioNow = () => node.context.currentTime;
      window.__sounded.push(record);
      return realStart(when);
    };

    /*  And when it is told to stop, which is how a check can tell a sound that
        was cancelled before it ever spoke from one that played. Stopping a node
        before its start time is how Web Audio cancels a booked sound, so the
        two have to be compared rather than counted. */
    node.stop = function (when) {
      record.stoppedAt = when === undefined ? this.context.currentTime : when;
      return realStop(when);
    };

    return node;
  };

  Ctor.prototype.createOscillator = function () {
    const osc = realCreate.call(this);

    return remember(osc, () => ({ type: osc.type, hz: osc.frequency.value }));
  };

  /*  The recorded instruments come out of a buffer source rather than an
      oscillator, so a check that watched only oscillators would call a sampled
      bank silent. The rate is what pitched it, which is the half worth
      keeping. */
  const realBuffer = Ctor.prototype.createBufferSource;

  Ctor.prototype.createBufferSource = function () {
    const source = realBuffer.call(this);

    return remember(source, () => ({ type: "sample", rate: source.playbackRate.value }));
  };

  /*  When a comped chord is let go of. A style says how long its chords ring
      now, and the only place that becomes audible is the release scheduled on
      each voice - `setTargetAtTime` is used for nothing else on this page, so
      recording it records exactly that and nothing else. */
  window.__released = [];

  /*  Every gain the page ramps to. A velocity becomes a peak gain and nothing
      else, so this is where "how hard was that struck" is actually observable
      - the page's own numbers are inside a closure and the notes themselves
      say nothing about how hard they were played. */
  window.__gains = [];
  const realRamp = AudioParam.prototype.exponentialRampToValueAtTime;

  AudioParam.prototype.exponentialRampToValueAtTime = function (value, when) {
    window.__gains.push(Number(value.toFixed(5)));
    return realRamp.call(this, value, when);
  };

  const realTarget = AudioParam.prototype.setTargetAtTime;

  AudioParam.prototype.setTargetAtTime = function (value, when, constant) {
    window.__released.push(when);
    return realTarget.call(this, value, when, constant);
  };

  /*  A MIDI keyboard, so the chordal half of solo practice can be driven at
      all. Notes struck together are the whole of what tells the engine it is
      reading a chord rather than a line, and a pointer plays one key at a time
      by construction - so the on-screen keyboard cannot produce one however
      fast it is clicked. The page's own handler is what runs; this hands it
      the bytes a keyboard would, which keeps the file under test the shipped
      one, unmodified. */
  window.__midi = { send: null };

  navigator.requestMIDIAccess = () => {
    const input = { name: "Smoke test keyboard", onmidimessage: null };

    window.__midi.send = (bytes) => input.onmidimessage({ data: Uint8Array.from(bytes) });

    return Promise.resolve({ inputs: new Map([["in", input]]),
                             outputs: new Map(),
                             onstatechange: null });
  };
});

// Anything the page says went wrong is a failure here. A page that boots with
// a broken handler still looks fine in a screenshot, and this is the only
// place that difference gets caught.
//
// Only what came from this origin counts. The webfonts and the PDF library are
// both allowed to be unreachable - the page is built to work without either,
// and a smoke test that goes red when a CDN is slow teaches everyone to ignore
// it.
const complaints = [];
const ours = (url) => !url || url.startsWith(origin);

page.on("pageerror", (error) => complaints.push(`page error: ${error.message}`));
page.on("console", (message) => {
  if (message.type() === "error" && ours(message.location().url)) {
    complaints.push(`console: ${message.text()}`);
  }
});
page.on("requestfailed", (request) => {
  if (ours(request.url())) complaints.push(`failed: ${request.url()}`);
});

const checks = [];
const check = (what, ok) => {
  checks.push(`${ok ? "ok  " : "FAIL"} ${what}`);
  if (!ok) process.exitCode = 1;
};

try {
  await page.goto(`${origin}/index.html`, { waitUntil: "load" });

  // The engine is the long pole: 400-odd KB of WebAssembly to arrive and
  // instantiate. Everything below is worthless until it says it is up.
  await page.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });
  check("the engine starts", true);

  // A first visit is told how the thing works before anything else happens.
  await page.waitForSelector("#helpDialog[open]", { timeout: 10000 });
  check("a first visit gets the cheat sheet", true);
  await page.locator("#helpClose").click();

  const bars = page.locator("#systems .bar");
  check("the chart is drawn", (await bars.count()) === 12);
  check("the first bar is the chord it should be",
        (await bars.first().innerText()).replace(/\s/g, "") === "Dm7");

  // Bar two, not bar one: bar one is already selected when the page loads, so
  // a single click there would open it and the second click would land on the
  // dialog. Two clicks on a bar you are not on is the sequence a visitor makes
  // - once to go there, once to ask about it.
  const bar = bars.nth(1);
  await bar.click();
  await bar.click();

  await page.waitForSelector("#chordDialog[open]", { timeout: 10000 });
  check("clicking a bar twice opens it", true);

  // Chord practice is about what the bar should be, so the bar opens on its
  // substitutions - and not on scales, which belong to playing over it.
  check("substitutions are offered", (await page.locator("#subs details.family").count()) > 0);
  check("and scales are not, in chord practice", await page.locator("#panelScales").isHidden());

  await page.locator("#dialogClose").click();

  // The other half of the app: play something and be told about it.
  await page.locator("#showVoicing").click();
  await page.waitForFunction(
    () => document.querySelectorAll("#keyboard .key[aria-pressed=\"true\"]").length > 2, null, { timeout: 10000 });
  check("Show me one puts a voicing under the hands", true);

  const verdict = (await page.locator("#verdict").innerText()).trim();
  check(`the voicing is judged (${verdict || "nothing"})`, verdict.length > 0);

  // The page says where MIDI stands without being asked. Headless Chromium
  // does have Web MIDI, so the resting message is the supported one.
  const midiNote = (await page.locator("#midiStatus").innerText()).trim();
  check(`MIDI support is reported up front (${midiNote})`, midiNote.length > 0);

  // A link to this page can carry a tune. Take the one the export offers and
  // open it in a second page: what comes back has to be the same chart.
  await page.locator("#chartButton").click();
  await page.locator("#ioButton").click();
  await page.waitForSelector("#ioDialog[open]", { timeout: 10000 });

  const shared = await page.locator("#shareLink").inputValue();
  check("a shareable link is offered", shared.startsWith(origin) && shared.includes("?chart="));

  if (shared) {
    const second = await browser.newPage();
    await second.goto(shared, { waitUntil: "load" });
    await second.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });

    // The chart in the link arrives after the engine does, so wait for the
    // bars rather than reading them the instant the engine is up.
    await second.waitForFunction(
      () => document.querySelectorAll("#systems .bar").length === 12, null, { timeout: 15000 });

    const firstBar = (await second.locator("#systems .bar").first().innerText()).replace(/\s/g, "");
    check(`the link opens its chart (${firstBar})`, firstBar === "Dm7");
    await second.close();
  }

  // A tune's metre is the tune's. iReal Pro writes it into the link, the reader
  // has always pulled it out, and the head is where it now shows up - so a
  // waltz opened from a link is counted in three rather than in four.
  {
    // index.html rather than "/": this little server types a response by its
    // file extension, and a directory has none.
    const waltz = origin + "/index.html?chart="
      + encodeURIComponent("irealb://Blue%20Waltz=Someone==Medium%20Swing===="
                           + "*A{T34Dm7 |G7 |C^7 |C^7 }");
    const third = await browser.newPage();
    await third.goto(waltz, { waitUntil: "load" });
    await third.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });
    await third.waitForFunction(
      () => document.querySelectorAll("#systems .bar").length === 4, null, { timeout: 15000 });

    check(`an imported waltz arrives in three `
          + `(${await third.locator("#timeSig").inputValue()})`,
          (await third.locator("#timeSig").inputValue()) === "3/4");

    /*  And goes back out in three, which is the half that was missing. The
        writer always wrote whatever metre the chart had; what it was handed
        was a chart rebuilt from a progression text, which says what the chords
        are and nothing about how a bar is counted - so every waltz left in
        four. The metre is on the wire now, and this is the shell end of it. */
    // A page opened cold may have the cheat sheet in front of it, and a modal
    // dialog swallows every click behind it.
    if (await third.locator("#helpDialog[open]").count())
      await third.locator("#helpClose").click();

    await third.locator("#chartButton").click();
    await third.locator("#ioButton").click();
    await third.waitForSelector("#ioDialog[open]", { timeout: 10000 });
    await third.waitForFunction(
      () => document.querySelector("#irealLink").value.length > 0, null, { timeout: 15000 });

    const exported = await third.locator("#irealLink").inputValue();

    check(`and the waltz exports as one (${(exported.match(/\[T\d+/) || ["no metre"])[0]})`,
          exported.includes("[T34"));

    await third.close();
  }

  await page.locator("#ioClose").click();

  /*  Guide tones, marked on the keys.

      The 3rd and the 7th are what carry a progression and where they go is the
      one thing a chord chart cannot show, so the next chord's two are badged on
      the keys they are actually played on. What is worth asserting is not that
      a class appeared: it is that the marks are *voiced* - they follow the hand
      up the keyboard and follow the chart when the bar moves. A hint that named
      the right two notes in the wrong octave would be a hint a player has to
      work out, which is the work it exists to save.

      `Show me one` has already put a voicing under the hands, and bar two is
      the bar we are on - so the chord coming is bar three's Cmaj7. */
  const ledTo = () => page.evaluate(() =>
    [...document.querySelectorAll("#keyboard .key.leads")]
      .map((key) => `${key.dataset.note}:${key.dataset.lead}`).sort().join(" "));

  await page.locator("#guideButton").click();
  await page.waitForFunction(() => document.querySelectorAll("#keyboard .key.leads").length > 0,
                             null, { timeout: 10000 });

  const badged = await ledTo();

  check(`the next chord's guide tones are marked on the keys (${badged})`,
        badged.length > 0
        && (await page.locator("#guideButton").getAttribute("aria-pressed")) === "true"
        && await page.locator("#leadLegend").isVisible()
        && (await page.locator("#leadChord").innerText()).trim().length > 0);

  // The 3rd and the 7th of Cmaj7 and nothing else: E and B, whatever octave
  // this particular hand puts them in.
  const degrees = await page.evaluate(() =>
    [...document.querySelectorAll("#keyboard .key.leads")]
      .map((key) => Number(key.dataset.note) % 12).sort((a, b) => a - b));

  check(`and they are that chord's 3rd and 7th (${degrees.join(", ")})`,
        JSON.stringify(degrees) === JSON.stringify([4, 11]));

  /*  Nothing under the hands is not a chord to be wrong about - it is nothing
      to lead from, so there is nothing to mark. */
  await page.locator("#clearKeys").click();
  await page.waitForFunction(() => document.querySelectorAll("#keyboard .key.leads").length === 0,
                             null, { timeout: 10000 });
  check("nothing under the hands, nothing marked", true);

  /*  The half a chart cannot tell you. One note down, and both guide tones are
      voiced to it; the same note an octave up, and both move an octave with it.
      C4 puts Cmaj7's 3rd at E4 and its 7th at the B *below* - the nearer B -
      which is the answer a player's hand actually wants. */
  const keyAt = (note) => page.locator(`#keyboard .key[data-note="${note}"]`);

  await keyAt(60).click();
  await page.waitForFunction(() => document.querySelectorAll("#keyboard .key.leads").length === 2,
                             null, { timeout: 10000 });

  const fromC4 = await ledTo();

  await page.locator("#clearKeys").click();
  await keyAt(72).click();
  await page.waitForFunction(() => document.querySelectorAll("#keyboard .key.leads").length === 2,
                             null, { timeout: 10000 });

  const fromC5 = await ledTo();

  check(`the marks are voiced for the hand, not for a reference octave `
        + `(${fromC4} -> ${fromC5})`,
        fromC4 === "59:maj7 64:3" && fromC5 === "71:maj7 76:3");

  /*  And the bar moving under a hand that has not moved asks about a different
      chord. In time this is the downbeat arriving, which is the moment the
      whole thing is for: bar five is Cm7, so what is coming is bar six's F7 -
      its 3rd at A3 and its 7th at Eb5, from the same single C5. */
  await bars.nth(4).click();
  await page.waitForFunction(() => document.querySelectorAll("#keyboard .key.leads").length === 2
                                   && !document.querySelector('#keyboard .key[data-note="71"]')
                                        .classList.contains("leads"),
                             null, { timeout: 10000 });

  // A flat is a flat sign on the badge, as it is everywhere else on the page.
  check(`and follow the chart when the bar moves (${await ledTo()})`,
        (await ledTo()) === "69:3 75:\u266d7");

  await page.locator("#clearKeys").click();
  await page.locator("#guideButton").click();

  check("and come off the keys when it is switched off",
        (await page.locator("#keyboard .key.leads").count()) === 0
        && (await page.locator("#guideButton").getAttribute("aria-pressed")) === "false"
        && await page.locator("#leadLegend").isHidden());

  /*  Put the page back the way this section found it, because the next one
      reads both: bar *two* selected, so that its click on bar one moves rather
      than opens it, and a voicing under the hands for it to let go of. Scrolled
      back up by hand as well - clicking a bar in the second system scrolled the
      chart, and what comes next measures where things sit on screen. */
  await bars.nth(1).click();
  await page.locator("#showVoicing").click();
  await page.evaluate(() => window.scrollTo(0, 0));
  await page.waitForFunction(
    () => document.querySelectorAll("#keyboard .key[aria-pressed=\"true\"]").length > 2,
    null, { timeout: 10000 });


  // --- solo practice ------------------------------------------------------
  // The other half of the app: the same notes, read one at a time against the
  // bar they land in rather than together as a chord.
  const soloKey = (note) => page.locator(`#keyboard .key[data-note="${note}"]`);
  const readout = () => page.locator("#soloNote").innerText();

  // Show me one left a voicing under the hands a few checks ago and nothing has
  // let go of it, which is what makes the next check mean something.
  const stillHeld = await page.locator("#keyboard .key[aria-pressed='true']").count();

  const chordsPaper = await page.evaluate(() => getComputedStyle(document.body).backgroundColor);
  const chordsHelp = await page.locator("#helpButton").getAttribute("aria-label");

  /*  Where everything is before the switch, and how much room the chart has.

      This used to guard a pair of paragraphs: the masthead lede and the hint
      above the chart were each written twice, once per mode, and the two
      wordings were different lengths - so hiding one outright moved everything
      below it. Both are gone and so is the grid that held their space open, but
      the property they were protecting is the one that matters and is easier to
      state now: a change of mode must not move the chart or change how much of
      it you can see. */
  const layout = () => page.evaluate(() => {
    const top = (sel) => Math.round(document.querySelector(sel).getBoundingClientRect().top);
    return { toggle: top(".mode-switch"), sheet: top(".sheet"), systems: top("#systems"),
             chartHeight: Math.round(document.querySelector(".chart-zone").getBoundingClientRect().height) };
  });
  const chordsLayout = await layout();
  const chordsTitle = await page.title();

  await page.locator("#modeSolo").click();

  // Arriving at solo practice for the first time is a first visit of its own,
  // and what opens is the solo half of the sheet - the chord half explains a
  // page that is not on screen.
  await page.waitForSelector("#helpDialog[open]", { timeout: 10000 });
  /*  The sheet is sectioned now, and the per-mode split moved off the lists
      and onto the entries inside them - most of what it says means the same in
      both modes and is written once. So the question is asked of the section
      that is open: the solo wording is there, the chord wording is not. */
  check("the first visit to solo practice gets its own cheat sheet",
        (await page.locator("#helpPanelChart li[data-mode='solo']").first().isVisible())
        && (await page.locator("#helpPanelChart li[data-mode='chords']").first().isHidden())
        && (await page.locator("#helpDialog .bar-label[data-mode='solo']").isVisible()));
  check(`the ? says which mode it explains (${chordsHelp} / `
        + `${await page.locator("#helpButton").getAttribute("aria-label")})`,
        chordsHelp === "How chord practice works"
        && (await page.locator("#helpButton").getAttribute("aria-label")) === "How solo practice works");
  await page.locator("#helpClose").click();

  // The light changes with the mode. Read after the dialog is out of the way,
  // so the 280ms fade has long finished and this is the settled colour.
  check("the page changes colour with the mode",
        (await page.evaluate(() => getComputedStyle(document.body).backgroundColor)) !== chordsPaper);

  /*  The window names the mode. It used to say so on the page as well, in an
      `<h1>` that cost a line of the chart to do it - and the mode is already
      said by the switch that set it, by the colour of the whole page, and by
      what the dock is asking for. The window title is the one place that says
      it where the page cannot. */
  check(`the window names the mode (${chordsTitle} / ${await page.title()})`,
        chordsTitle === "Jazz Learning App: Chords"
        && (await page.title()) === "Jazz Learning App: Solo");

  const soloLayout = await layout();
  const place = ({ toggle, sheet, systems }) => JSON.stringify({ toggle, sheet, systems });

  check(`nothing moves when the mode changes (${place(soloLayout)})`,
        place(soloLayout) === place(chordsLayout));

  /*  The height is deliberately not part of that. The dock is mode-dependent by
      construction - chord practice's feedback panel and solo practice's are
      different things of different heights - and the chart takes whatever is
      left, so it is a little taller in solo. What would be wrong is the chart
      being squeezed to nothing by a dock that grew, which is what this watches:
      room for more than one line of music, in either mode. */
  check(`and the chart keeps its room either way `
        + `(${chordsLayout.chartHeight}px chords, ${soloLayout.chartHeight}px solo)`,
        chordsLayout.chartHeight > 200 && soloLayout.chartHeight > 200);

  // A voicing carried into solo practice is a chord nothing over here reads -
  // it just sits on the keyboard looking pressed.
  check(`switching modes lets go of the keys (${stillHeld} held before)`,
        stillHeld > 0 && (await page.locator("#keyboard .key[aria-pressed='true']").count()) === 0);

  // Start from bar one rather than wherever the last section left off. Clicking
  // a bar you are already on opens it, so which bar is selected decides whether
  // these clicks move or open - the test has to know, not hope.
  await page.locator("#systems .bar").first().click();
  await page.waitForFunction(
    () => document.querySelector("#soloBarTally, #soloNote") !== null, null, { timeout: 10000 });

  check("solo mode shows its own panel", await page.locator("#soloPanel").isVisible());
  check("and puts the chord feedback away", await page.locator("#feedback").isHidden());
  check("the voicing shape has no say over a single note",
        await page.locator("[data-mode='chords']").first().isHidden());

  // Bar one is Dm7: D is its root, and Db is in neither the chord nor D Dorian.
  await soloKey(62).click();
  await page.waitForFunction(
    () => document.querySelector("#soloNote").dataset.colour !== undefined, null, { timeout: 10000 });
  check(`a note is read back (${(await readout()).replace(/\s+/g, " ")})`,
        (await page.locator("#soloNote").getAttribute("data-colour")) === "chordTone");

  // A note outside the harmony is not called outside the moment it is played:
  // nothing knows that yet. It is open, and what the page can say is what would
  // close it.
  await soloKey(61).click();
  await page.waitForFunction(
    () => document.querySelector("#soloNote").dataset.colour === "unresolved",
    null, { timeout: 10000 });
  check(`a note outside the harmony is open rather than wrong `
        + `(${(await page.locator("#soloAgainst").innerText()).replace(/\s+/g, " ")})`,
        /lands on/.test(await page.locator("#soloAgainst").innerText()));

  // The chip is the part a player actually catches: it sits in one place, it
  // has no colour while the note is open, and the key stays lit under it.
  check(`the open note is on show, uncoloured, while it is open `
        + `(${await page.locator("#soloOpen").innerText()})`,
        (await page.locator("#soloOpen").isVisible())
        && (await page.locator("#soloOpen").getAttribute("data-colour")) === null
        && (await page.locator('#keyboard .key[data-note="61"].sounded').count()) === 1);

  // ...and once the line steps home from it, it is read again as an approach.
  // This is the one behaviour in solo mode that changes after the fact, so it
  // is the one worth driving end to end rather than trusting.
  await soloKey(62).click();
  await page.waitForFunction(
    () => document.querySelector("#soloOpen").dataset.colour === "approach",
    null, { timeout: 10000 });
  check(`an outside note that steps home is read again as an approach `
        + `(${await page.locator("#soloOpen").innerText()})`, true);

  // The other half, and the only place the app ever says a note did not work:
  // an open note the line never closed. The note straight after it lands
  // somewhere else, which is the earliest moment that is true - so it is the
  // moment it has to be said, not the one after.
  await soloKey(61).click();
  await soloKey(69).click();
  await page.waitForFunction(
    () => document.querySelector("#soloOpen").dataset.colour === "outside",
    null, { timeout: 10000 });
  check(`a note the line never closed is flagged by the very next note `
        + `(${await page.locator("#soloOpen").innerText()})`, true);

  // And the key it was played on lights in the verdict's colour, because the
  // note being answered is behind the one under the player's fingers.
  check("the key the open note was played on says how it ended",
        (await page.locator('#keyboard .key[data-note="61"]').getAttribute("data-colour")) === "outside");

  // An enclosure is an approach note everywhere a tier is counted, and its own
  // colour on the key and the chip, where one note's story is being told.
  for (const note of [63, 61, 62]) { await soloKey(note).click(); await page.waitForTimeout(120); }

  await page.waitForFunction(
    () => document.querySelector("#soloOpen").dataset.colour === "enclosure",
    null, { timeout: 10000 });
  check(`an enclosure is told apart from a plain approach `
        + `(${await page.locator("#soloOpen").innerText()})`,
        (await page.locator('#keyboard .key[data-note="63"]').getAttribute("data-colour")) === "enclosure"
        && (await page.locator('#keyboard .key[data-note="61"]').getAttribute("data-colour")) === "enclosure");

  /*  One note can close one open note and pass the window on another at the
      same moment: G is the chromatic approach the F# earned, and the Eb before
      it enclosed nothing. Both have to be let go of - handling only the first
      list that had anything in it left the other key lit and breathing for the
      rest of the take. */
  for (const note of [63, 66, 67]) { await soloKey(note).click(); await page.waitForTimeout(120); }

  await page.waitForFunction(
    () => document.querySelector('#keyboard .key[data-note="66"]').dataset.colour === "approach",
    null, { timeout: 10000 });
  check(`a note that closes one and strands another lets go of both `
        + `(${await page.locator("#soloOpen").innerText()})`,
        (await page.locator('#keyboard .key[data-note="63"]').getAttribute("data-colour")) === "outside"
        && (await page.locator('#keyboard .key.sounded[data-colour="unresolved"]').count()) === 0);

  /*  Chords in a line: a player comping behind themselves, or soloing in
      blocks. A G13, the same shape with two voices pushed down a semitone to
      make a G7alt, then a Cmaj7 - which is the most ordinary way there is of
      getting from the one to the other, and which read as a handful of notes
      that went nowhere until the page started saying which notes were struck
      together. The Ab steps into the G of the next voicing at the same moment
      the Eb steps into its D.

      Driven over MIDI because that is the only input that can strike two notes
      at once, and struck in one synchronous burst the way a keyboard sends
      them. */
  await page.locator("#menuButton").click();
  await page.locator("#connectMidi").click();
  await page.waitForFunction(
    () => document.querySelector("#midiStatus").classList.contains("good"),
    null, { timeout: 10000 });

  check(`a MIDI keyboard is seen (${(await page.locator("#midiStatus").innerText()).trim()})`, true);

  await page.locator("#menuButton").click();

  await page.evaluate(() => {
    const voicings = [[53, 57, 59, 64], [53, 56, 59, 63], [52, 55, 59, 62]];

    return voicings.reduce((wait, voicing) => wait.then(() => {
      voicing.forEach((note) => window.__midi.send([0x90, note, 80]));
      voicing.forEach((note) => window.__midi.send([0x80, note, 0]));

      return new Promise((done) => setTimeout(done, 200));
    }), Promise.resolve());
  });

  await page.waitForFunction(
    () => document.querySelector('#keyboard .key[data-note="56"]').dataset.colour === "approach",
    null, { timeout: 10000 });

  check(`a voicing moving chromatically into the next one resolves voice by voice `
        + `(${await page.locator("#soloOpen").innerText()})`,
        (await page.locator('#keyboard .key[data-note="63"]').getAttribute("data-colour")) === "approach");

  // And nothing was left lit asking a question that had been answered.
  check("neither voice was left hanging",
        (await page.locator('#keyboard .key[data-colour="outside"]').count()) === 0);

  /*  And read as a chord, which is the other half of what "struck together"
      buys. With four keys down the page stops reporting whichever of them
      arrived last - the line's note is the one on top, and underneath it is a
      chord with a name. The verdict is the engine's sentence, so this asserts
      that one arrived rather than checking its wording. */
  const chordLine = (await page.locator("#soloNote").innerText()).trim();
  const chordVerdict = (await page.locator("#soloAgainst").innerText()).trim();

  check(`a chord in the line is read as a chord (${chordLine})`,
        chordLine.includes("on top of 4 notes struck together"));
  check(`and it is named against the bar (${chordVerdict})`,
        /says|reads as|no one name/.test(chordVerdict));

  // The control, and the reason the shell has to be the one to say it: the
  // same pitches clicked one at a time are the line they actually are.
  for (const note of [53, 56, 59, 63]) { await soloKey(note).click(); await page.waitForTimeout(90); }
  for (const note of [52, 55, 59, 62]) { await soloKey(note).click(); await page.waitForTimeout(90); }

  await page.waitForFunction(
    () => document.querySelector('#keyboard .key[data-note="56"]').dataset.colour === "outside",
    null, { timeout: 10000 });

  check("the same notes played one at a time are read as the line they are", true);

  // The same control, for the chord reading: one note at a time is a line, and
  // a line has no chord in it to read.
  check("and a line is not read as a chord",
        ! (await page.locator("#soloNote").innerText()).includes("struck together"));

  // Nothing is counted until a take is armed.
  check("nothing is counted before arming", await page.locator("#soloTallies").isHidden());

  // The same bar, opened in solo mode, is about scales instead.
  const soloBar = bars.first();
  await soloBar.click();
  await page.waitForSelector("#chordDialog[open]", { timeout: 10000 });

  const scale = (await page.locator("#scaleName").innerText()).trim();
  check(`a scale is named in solo mode (${scale || "nothing"})`, scale.length > 0);
  check("the scale has its notes", (await page.locator("#scaleNotes li").count()) >= 5);
  check("and reharmonisation is not offered here",
        await page.locator("#panelReharm").isHidden());

  const alternatives = await page.locator("#scaleRows li").count();
  check(`alternative scales are offered (${alternatives})`, alternatives > 0);

  // Choosing one re-aims the bar rather than only redrawing the panel.
  if (alternatives > 1) {
    await page.locator("#scaleRows li button").nth(1).click();
    const chosen = (await page.locator("#scaleName").innerText()).trim();
    check(`choosing a scale takes (${chosen})`, chosen !== scale);

    await page.locator("#dialogClose").click();

    // The dock names what the engine is holding you to, the way chord practice
    // names the chord it expects - so it has to be the scale just chosen, not
    // the engine's own first answer.
    await page.waitForFunction(
      (want) => document.querySelector("#soloScale").textContent.trim() === want,
      chosen, { timeout: 10000 }).catch(() => {});
    check(`the dock names the scale being read against (${await page.locator("#soloScale").innerText()})`,
          (await page.locator("#soloScale").innerText()).trim() === chosen);

    // Reopening the bar must not quietly reset that choice: the engine would
    // still be reading against it while the panel highlighted another.
    await page.locator("#systems .bar").first().click();
    await page.waitForSelector("#chordDialog[open]", { timeout: 10000 });
    check("reopening the bar keeps the scale that was chosen",
          (await page.locator("#scaleName").innerText()).trim() === chosen
          && (await page.locator("#soloScale").innerText()).trim() === chosen);
  }

  await page.locator("#dialogClose").click();

  // The style picker narrows the vocabulary, and its list comes from the engine.
  await page.locator("#menuButton").click();
  const styles = await page.locator("#scaleStyle option").count();
  check(`the engine's styles fill the menu (${styles})`, styles >= 5);
  await page.locator("#menuButton").click();

  /*  In time puts a clock behind the chart. Driven fast and looped over two
      bars, so the test watches a real roll rather than a stubbed one.

      Reached without opening anything: the switch, the tempo, the metre and
      the take button are on the transport strip. Every click on one of them
      below used to be a pair of menu clicks around it, and a pair with half of
      it missing leaves the menu hanging open over the page - which is why they
      came out together rather than one at a time. */
  await page.locator("#playLive").click();

  // The tempo and the metre are on the transport strip, not in a menu, so they
  // are readable and reachable while playing rather than behind a button.
  check("the tempo mark is on the transport strip",
        (await page.locator("#metreRow").isVisible())
        && (await page.locator(".transport #tempo").count()) === 1
        && (await page.locator(".transport #timeSig").count()) === 1);

  // What is left of the old Playing group is one popover off the strip: the
  // things a take is set up with rather than played with.
  await page.locator("#transportButton").click();
  check("choosing in time reveals what a clock needs",
        (await page.locator("#countInRow").isVisible())
        && (await page.locator("#loopRow").isVisible()));

  // A loop nobody has chosen is the whole tune, not bar one to bar one.
  check(`the loop starts as the whole chart `
        + `(${await page.locator("#loopFrom").inputValue()}-${await page.locator("#loopTo").inputValue()})`,
        (await page.locator("#loopFrom").inputValue()) === "0"
        && (await page.locator("#loopTo").inputValue()) === "11");
  await page.locator("#transportButton").click();

  // Count in first, which is the default: the dots count and the chart waits.
  // One button does both jobs now - starting a take is what sets it rolling.
  await page.locator("#armTake").click();
  await page.waitForFunction(
    () => document.querySelector("#beatRow").classList.contains("counting"),
    null, { timeout: 10000 });
  check("a count-in counts before the chart moves",
        (await page.locator("#systems .bar.rolling").count()) === 0);
  check("one button arms the take and starts the clock",
        (await page.locator("#armTake").getAttribute("aria-pressed")) === "true");
  await page.locator("#armTake").click();
  check("and stopping it stops both",
        (await page.locator("#armTake").getAttribute("aria-pressed")) === "false"
        && !(await page.locator("#beatRow").isVisible()));

  // The metre is the chart's, and the dots are the metre's.
  await page.selectOption("#timeSig", "3/4");
  check("the metre fills the beat row (3)",
        (await page.locator("#beatRow .beat").count()) === 3);
  await page.selectOption("#timeSig", "4/4");

  await page.locator("#transportButton").click();
  await page.uncheck("#countIn");
  await page.selectOption("#loopFrom", "0");
  await page.selectOption("#loopTo", "1");
  await page.locator("#transportButton").click();

  await page.fill("#tempo", "300");
  await page.dispatchEvent("#tempo", "change");

  // A space typed into the tempo box is a character, not a command, so the
  // test leaves the field the way a player would before reaching for it.
  await page.locator("#tempo").press("Space");
  check("a space in the tempo box is not a transport key",
        (await page.locator("#armTake").getAttribute("aria-pressed")) === "false");

  // The space bar is the transport key everywhere else a musician meets one,
  // and it works from wherever the last click left the focus.
  await page.evaluate(() => document.activeElement.blur());
  await page.keyboard.press("Space");

  // Arming asks the engine for a take, so it lands a tick later even on the
  // web - the app awaits a bridge call for the same thing.
  await page.waitForFunction(
    () => document.querySelector("#armTake").getAttribute("aria-pressed") === "true",
    null, { timeout: 10000 });
  check("the space bar starts a take", true);

  // A bar at 300bpm is 800ms, so the chart has to have moved off bar one
  // without anything being clicked.
  await page.waitForFunction(
    () => document.querySelector("#systems .bar:nth-child(2)").classList.contains("rolling"),
    null, { timeout: 10000 });
  check("the clock moves the chart on its own", true);

  // ...and come back round, because the loop is two bars long.
  await page.waitForFunction(
    () => document.querySelector("#systems .bar").classList.contains("rolling"),
    null, { timeout: 10000 });
  check("and loops the range it was given", true);

  await page.keyboard.press("Space");
  await page.waitForFunction(
    () => document.querySelector("#armTake").getAttribute("aria-pressed") === "false",
    null, { timeout: 10000 });
  check("and the space bar stops it again",
        (await page.locator("#systems .bar.rolling").count()) === 0);

  /*  Start slow and work up. What is worth asserting is not that a number in a
      box went up - it is that the clock followed it, and that it changed where
      a chorus begins rather than under a phrase. So this measures the beats:
      the gaps have to come in runs, one run per pass of the loop, each shorter
      than the last. */
  await page.locator("#transportButton").click();
  await page.check("#ramp");
  await page.fill("#rampBy", "20");
  await page.dispatchEvent("#rampBy", "change");
  await page.fill("#rampTo", "300");
  await page.dispatchEvent("#rampTo", "change");
  await page.selectOption("#loopTo", "0");      // one bar, so a chorus is four beats
  await page.locator("#transportButton").click();

  await page.fill("#tempo", "200");
  await page.dispatchEvent("#tempo", "change");
  await page.evaluate(() => document.activeElement.blur());

  // Inline rather than `forgetSounds`, which is declared below this point.
  await page.evaluate(() => { window.__sounded = []; });
  await page.keyboard.press("Space");
  await page.waitForTimeout(3000);
  await page.keyboard.press("Space");
  await page.waitForFunction(
    () => document.querySelector("#armTake").getAttribute("aria-pressed") === "false",
    null, { timeout: 10000 });

  const rampBeats = await page.evaluate(() =>
    window.__sounded.filter((s) => s.type === "square").map((s) => s.when).sort((a, b) => a - b));

  const rampGaps = rampBeats.slice(1).map((w, i) => w - rampBeats[i]);
  const rampTempi = [...new Set(rampGaps.map((g) => Math.round(60 / g)))];

  check(`the tempo ramp steps the clock, not just the box (${rampTempi.join(" -> ")})`,
        rampTempi.length > 1
        && rampTempi.every((t, i) => i === 0 || t > rampTempi[i - 1])
        && (await page.locator("#tempo").inputValue()) === String(rampTempi[rampTempi.length - 1]));

  // Each step lands on a chorus, never inside one: every run of equal gaps is
  // a whole number of bars long. A ramp that moved mid-phrase would pass the
  // check above and be the thing nobody could play against.
  const runs = rampGaps.reduce((out, gap) => {
    const last = out[out.length - 1];
    if (last && Math.abs(last.gap - gap) < 0.01) last.beats += 1;
    else out.push({ gap, beats: 1 });
    return out;
  }, []);

  check(`and steps on a chorus, not inside one (${runs.map((r) => r.beats).join("+")} beats)`,
        runs.slice(0, -1).every((run) => run.beats % 4 === 0));

  await page.locator("#transportButton").click();
  await page.uncheck("#ramp");
  await page.selectOption("#loopTo", "1");
  await page.locator("#transportButton").click();
  await page.fill("#tempo", "300");
  await page.dispatchEvent("#tempo", "change");
  await page.evaluate(() => document.activeElement.blur());

  /*  The tune changing under you while you play it.

      A chart is a fixed thing to practise against and a real one is not - a
      band calls a substitution and everybody follows. One bar of the loop is
      reharmonised as each chorus comes round, and the point of the exercise is
      that you find out by reading the chart.

      Two halves, and the second matters as much as the first: it is an
      exercise, not an edit, so stopping the take has to put the tune back
      exactly as it was. */
  const chartNow = () => page.evaluate(() =>
    [...document.querySelectorAll("#systems .bar")]
      .map((bar) => (bar.dataset.label || "").replace(/^Bar \d+, /, "")).join(" | "));

  const before = await chartNow();

  await page.locator("#transportButton").click();
  await page.check("#reharmLive");
  await page.selectOption("#reharmReach", "advanced");
  await page.locator("#transportButton").click();
  await page.evaluate(() => document.activeElement.blur());

  await page.keyboard.press("Space");
  await page.waitForFunction(
    (was) => [...document.querySelectorAll("#systems .bar")]
               .map((bar) => (bar.dataset.label || "").replace(/^Bar \d+, /, "")).join(" | ") !== was,
    before, { timeout: 15000 });

  const during = await chartNow();

  check("the tune is reharmonised under you as a chorus comes round",
        during !== before);

  // And the bar it changed says so on the chart, the way one you chose by hand
  // does - a change you cannot see is one you cannot play.
  check("and the bar that moved is marked",
        (await page.locator("#systems .bar.reharmonised").count()) > 0);

  await page.keyboard.press("Space");
  await page.waitForFunction(
    () => document.querySelector("#armTake").getAttribute("aria-pressed") === "false",
    null, { timeout: 10000 });
  await page.waitForFunction(
    (was) => [...document.querySelectorAll("#systems .bar")]
               .map((bar) => (bar.dataset.label || "").replace(/^Bar \d+, /, "")).join(" | ") === was,
    before, { timeout: 10000 });

  check("and put back exactly as it was when the take stops", true);

  check("leaving no mark on the chart behind it",
        (await page.locator("#systems .bar.reharmonised").count()) === 0);

  /*  The same dialog, asked a different question.

      With a clock behind the chart and the exercise armed, the six plans stop
      being "rewrite the tune now" and become "which one should this take
      follow". Same list, same descriptions - listing them again in the menu
      would be a second copy of six write-ups to go stale - and the Static /
      In time split decides which question is being asked. */
  await page.locator("#chartButton").click();
  await page.locator("#planButton").click();   // which closes the panel behind it
  await page.waitForSelector("#planDialog[open]", { timeout: 10000 });

  const planTitle = (await page.locator("#planTitle").innerText()).trim();

  check(`the plan dialog asks about the take instead (${planTitle})`,
        planTitle === "Reharmonise as you play");

  // Picking one sets the take's plan rather than rewriting the chart, which is
  // the whole difference: nothing on screen may move yet.
  const beforePicking = await chartNow();

  await page.locator(".plan-name").first().click();
  await page.waitForFunction(() => !document.querySelector("#planDialog[open]"), null,
                             { timeout: 10000 });

  check("and choosing one arms the take rather than rewriting the chart",
        (await chartNow()) === beforePicking
        && (await page.locator("#reharmAmount").inputValue()) === "tune");

  /*  And the whole-tune mode really does move more than a bar. Escalating, so
      it is reharmonising what the last chorus left rather than the chart the
      take started from - which is what makes the tune go further out pass by
      pass instead of landing somewhere and staying. */
  await page.locator("#transportButton").click();
  await page.selectOption("#reharmAmount", "tune");
  await page.locator("#transportButton").click();
  await page.evaluate(() => document.activeElement.blur());

  const barsOf = (chart) => chart.split(" | ");
  const wholeBefore = await chartNow();

  await page.keyboard.press("Space");
  await page.waitForFunction(
    (was) => [...document.querySelectorAll("#systems .bar")]
               .map((bar) => (bar.dataset.label || "").replace(/^Bar \d+, /, "")).join(" | ") !== was,
    wholeBefore, { timeout: 20000 });

  const wholeDuring = await chartNow();
  const moved = barsOf(wholeBefore).filter((bar, i) => bar !== barsOf(wholeDuring)[i]).length;

  check(`the whole tune moves, not one bar of it (${moved} bars)`, moved > 1);

  await page.keyboard.press("Space");
  await page.waitForFunction(
    () => document.querySelector("#armTake").getAttribute("aria-pressed") === "false",
    null, { timeout: 10000 });
  await page.waitForFunction(
    (was) => [...document.querySelectorAll("#systems .bar")]
               .map((bar) => (bar.dataset.label || "").replace(/^Bar \d+, /, "")).join(" | ") === was,
    wholeBefore, { timeout: 10000 });

  check("and a whole-tune take puts every bar of it back", true);

  await page.locator("#transportButton").click();
  // Back to a bar before switching off: the amount goes away with the
  // exercise, and a hidden select is one nothing can choose from.
  await page.selectOption("#reharmAmount", "bar");
  await page.uncheck("#reharmLive");
  await page.locator("#transportButton").click();
  await page.evaluate(() => document.activeElement.blur());

  // Off the clock the dialog goes back to meaning what it always meant.
  await page.locator("#playStatic").click();
  await page.locator("#chartButton").click();
  await page.locator("#planButton").click();
  await page.waitForSelector("#planDialog[open]", { timeout: 10000 });

  check("and means rewrite-it-now again once the clock is off",
        (await page.locator("#planTitle").innerText()).trim() === "Reharmonise");

  await page.locator("#planClose").click();
  await page.locator("#playLive").click();
  await page.evaluate(() => document.activeElement.blur());


  // --- comping ------------------------------------------------------------
  // The band behind the soloist. The engine says which notes; everything the
  // page does is when - so these checks read the notes that actually sounded.
  //
  // The piano's voices are sines; the metronome's click is a square wave, and
  // sorting by that is what keeps the click out of these numbers. Every note
  // builds two oscillators - a carrier, which is the pitch, and the tine
  // ringing it, which is not a note - so only every other one counts.
  const compedVoicing = async () => {
    const sounded = await page.evaluate(() => window.__sounded);

    return sounded.filter((s) => s.type === "sine")
                  .map((s) => Math.round(69 + 12 * Math.log2(s.hz / 440)))
                  .filter((note, i) => i % 2 === 0)
                  .slice(0, 4);
  };

  const forgetSounds = () => page.evaluate(() => {
    window.__sounded = [];
    window.__released = [];
    window.__gains = [];
  });

  /*  Whether the band is on offer at all. It is a section of the settings
      panel now rather than a button beside the chart, so `isVisible` would be
      answering "is the panel open" - read the section's own `hidden` instead,
      which is the thing `applyMode` sets and is true of it whether or not
      anyone has opened anything. */
  const bandOffered = () => page.evaluate(() => !document.querySelector("#bandGroup").hidden);

  /*  Two things sit between this and a bar: the settings panel, which at a
      phone width is drawn over most of the page, and the bar's own dialog,
      which a second click on the bar already selected opens. Close both, or
      everything after this is clicking on something else. */
  const goToBar = async (n) => {
    if (!(await page.locator("#menuPanel").isHidden()))
      await page.locator("#menuButton").click();

    await bars.nth(n).click();

    if (await page.locator("#chordDialog[open]").count())
      await page.locator("#dialogClose").click();
  };

  await page.locator("#menuButton").click();
  // Three instruments, all three of them playable. This asserted that one was
  // disabled for as long as the drummer was named and not built; the kit is
  // the thing that changed, not the test's opinion of it.
  check("comping offers a band of three, all of them playable",
        (await page.locator("#bandGroup").isVisible())
        && (await page.locator("#bandGroup input[type=checkbox]").count()) === 3
        && (await page.locator("#bandGroup input:disabled").count()) === 0);

  // Two recorded basses, and neither is on the player's own sound menu: nobody
  // practises voicings on a double bass.
  const bassSounds = await page.locator("#bassSound option").allInnerTexts();

  check(`the bass has its own recorded instruments (${bassSounds.join(", ")})`,
        bassSounds.some((name) => /upright/i.test(name))
        && bassSounds.some((name) => /electric/i.test(name))
        && !(await page.locator("#soundBank option[value=upright]").count()));

  // One registry of sounds, not a second copy of the list.
  const compSounds = await page.locator("#compSound option").allInnerTexts();
  const menuSounds = await page.locator("#soundBank option").allInnerTexts();

  check(`the band is offered the same sounds as the player (${compSounds.join(", ")})`,
        compSounds.length === menuSounds.length
        && compSounds.every((name, i) => name === menuSounds[i]));

  // On bar one first: clicking a bar closes whichever panel is open, so the
  // toggle has to be the last thing touched before the sound is read.
  await page.locator("#menuButton").click();
  await goToBar(0);
  await page.locator("#menuButton").click();
  await forgetSounds();
  await page.locator("#compPiano").check();
  await page.waitForFunction(() => window.__sounded.length >= 8, null, { timeout: 10000 });

  const overDm7 = await compedVoicing();
  check(`the band comps the bar it is switched on over (${overDm7.join(" ")})`,
        overDm7.length === 4);

  // Rootless: a two-handed voicing of Dm7 has no D in it, and it sits under
  // where a soloist plays rather than on top of them.
  check("what it plays is a rootless voicing, below the line",
        overDm7.every((note) => note % 12 !== 2)
        && overDm7[0] >= 45 && overDm7[3] <= 84);

  await forgetSounds();
  await goToBar(1);
  await page.waitForFunction(() => window.__sounded.length >= 8, null, { timeout: 10000 });

  const overG7 = await compedVoicing();

  // The whole point of asking the engine for the *next* voicing rather than a
  // fresh one: Dm7 to G7 shares two notes, and a comp that re-spelled every
  // chord would move every finger.
  const held = overG7.filter((note) => overDm7.indexOf(note) !== -1).length;
  check(`moving on leads the voicing from the last one (${overG7.join(" ")}, ${held} held)`,
        held >= 2);

  await page.locator("#menuButton").click();
  await page.selectOption("#compSound", "silent");
  await forgetSounds();
  await goToBar(2);
  await page.waitForTimeout(600);
  check("a band set to silent is silent", (await compedVoicing()).length === 0);

  // --- comping styles, and the rhythm they actually play ------------------
  // The generator's output only means something if it reaches the speakers, so
  // these read the times the page scheduled rather than the plan it was given.

  await page.locator("#menuButton").click();

  // The check above left the band silent, and a silent band schedules nothing
  // for these to read.
  await page.selectOption("#compSound", { value: "ep" });

  const compStyleNames = await page.locator("#compStyle option").allInnerTexts();

  check(`the comping styles come from the engine (${compStyleNames.length})`,
        compStyleNames.length >= 2
        && (await page.locator("#compStyleNote").innerText()).trim().length > 0);

  // Loop one bar, fast, no count-in, so a few bars go by quickly. The options
  // are selected by value: their labels are bar numbers, and "1" as a label is
  // bar index 0, which is a one-bar loop when you wanted two.
  await page.locator("#menuButton").click();
  await page.locator("#transportButton").click();
  await page.uncheck("#countIn");
  await page.selectOption("#loopFrom", { value: "0" });
  await page.selectOption("#loopTo", { value: "3" });
  await page.locator("#transportButton").click();
  await page.fill("#tempo", "240");
  await page.dispatchEvent("#tempo", "change");
  await page.evaluate(() => document.activeElement.blur());

  /** Rolls a take in one comping style and returns when each chord was struck,
      in beats from the first click. */
  const compRhythmOf = async (style) => {
    await page.locator("#menuButton").click();
    await page.selectOption("#compStyle", style);
    await page.locator("#compPiano").check();
    await page.locator("#menuButton").click();

    await forgetSounds();
    await page.keyboard.press("Space");
    await page.waitForTimeout(2600);
    await page.keyboard.press("Space");
    await page.waitForFunction(
      () => document.querySelector("#armTake").getAttribute("aria-pressed") === "false",
      null, { timeout: 10000 });

    const sounded = await page.evaluate(() => window.__sounded);
    // The click is a square wave and marks the beats; the piano is sines.
    const beats = sounded.filter((s) => s.type === "square").map((s) => s.when).sort((a, b) => a - b);
    const struck = [...new Set(sounded.filter((s) => s.type === "sine").map((s) => s.when))];

    // 240bpm, so a beat is a quarter of a second.
    return struck.map((w) => (w - beats[0]) / 0.25).sort((a, b) => a - b);
  };

  /** How long each chord of the roll just taken rang for, in beats.

      Each start paired with the next release, which is what a release is: one
      instrument plays these in order, and the engine trims a hit so it never
      rings past the one after it. */
  const holdsOfLastRoll = async () => {
    const holds = await page.evaluate(() => {
      const starts = [...new Set(window.__sounded.filter((s) => s.type === "sine")
                                                 .map((s) => s.when))].sort((a, b) => a - b);
      const ends = [...new Set(window.__released)].sort((a, b) => a - b);

      return starts.map((start) => ends.find((end) => end > start + 1e-6))
                   .filter((end) => end !== undefined)
                   .map((end, i) => end - starts[i]);
    });

    return holds.map((h) => h / 0.25).sort((a, b) => a - b);   // 240bpm
  };

  const fourToTheBar = await compRhythmOf("four");
  const fourHolds = await holdsOfLastRoll();
  const sparse = await compRhythmOf("basie");

  check(`a style says how much the band plays (four: ${fourToTheBar.length}, `
        + `basie: ${sparse.length})`,
        fourToTheBar.length >= sparse.length * 2 && sparse.length > 0);

  // Four to the bar is on the beat, every beat: nothing between them.
  const offTheBeat = (times) =>
    times.filter((t) => Math.abs(t - Math.round(t)) > 0.1).length;

  check("four to the bar plays on the beat and nowhere else",
        offTheBeat(fourToTheBar) === 0);

  // Basie's is the pushed one, and a push in a swung bar is two thirds of the
  // way through the beat - not half, which is what swing means and what the
  // engine deliberately does not know about.
  const swung = sparse.filter((t) => Math.abs((t - Math.floor(t)) - 2 / 3) < 0.08);

  check(`a swung push lands two thirds through the beat (${sparse.map((t) => t.toFixed(2)).join(" ")})`,
        swung.length > 0);

  /*  A style says how long its chords ring, not only where they fall - the
      difference between a Basie punch and a ballad's sustain, which used to be
      no difference at all because a voicing rang until the next one stopped it
      whatever the style was. Checked as the two ends of the range: the ballad
      has to hold longer than four-to-the-bar's damped chunk, and four to the
      bar has to let go well inside its own beat.
  */
  await compRhythmOf("ballad");
  const balladHolds = await holdsOfLastRoll();

  const middle = (holds) => holds[Math.floor(holds.length / 2)];

  check(`a style says how long its chords ring `
        + `(four ${middle(fourHolds)?.toFixed(2)} beats, ballad ${middle(balladHolds)?.toFixed(2)})`,
        fourHolds.length > 0 && balladHolds.length > 0
        && middle(balladHolds) > middle(fourHolds));

  check(`and four to the bar lets go inside its own beat (${middle(fourHolds)?.toFixed(2)})`,
        middle(fourHolds) < 0.95);

  /*  A style you can look at, and copy without changing it.

      The check that matters before any editing exists, because it proves the
      two halves of the grammar agree through the real path: the engine writes
      a style out as flat text, the page draws it, the page writes it back, and
      the engine reads it. Copy the Charleston, change nothing, play it - and
      the band has to play the Charleston, hit for hit.

      Anything lost in that round trip shows up here as a different figure. A
      test that only asserted the dialog opens would pass with the writer
      dropping every slot on the floor.
  */
  const charleston = await compRhythmOf("charleston");

  await page.locator("#menuButton").click();
  check("the engine offers the style editor", await page.locator("#styleEdit").isVisible());

  await page.locator("#styleEdit").click();
  await page.waitForSelector("#styleDialog[open]", { timeout: 10000 });

  // Opened from the settings panel, which at a phone width would cover it.
  check("writing your own opens from The band, and closes the panel",
        await page.locator("#menuPanel").isHidden());

  const litCells = await page.locator('#styleGrid .style-cell[aria-pressed="true"]').count();
  const rows = await page.locator("#styleGrid .style-row").count();

  check(`the style is drawn as its figure (${litCells} chords over ${rows} rows)`,
        litCells === 3 && rows === 2);

  await page.locator("#styleApply").click();
  await page.waitForFunction(() => !document.querySelector("#styleDialog").open,
                             null, { timeout: 10000 });

  check("and applying it puts it in the menu",
        (await page.locator('#compStyle option[value="yours"]').count()) === 1);

  const copied = await compRhythmOf("yours");

  check(`a copy of a style plays what the style plays `
        + `(${charleston.map((t) => t.toFixed(2)).join(" ")} vs `
        + `${copied.map((t) => t.toFixed(2)).join(" ")})`,
        copied.length > 0 && copied.join(",") === charleston.join(","));

  // And the page put the whole style on the wire, not a key the engine would
  // have quietly failed to find and fallen back to four-to-the-bar for.
  const reference = await page.evaluate(() => document.querySelector("#compStyle").value);
  check(`the copy is its own style, not the name of one (${reference})`, reference === "yours");

  /*  A grid you can click.

      The positive half: put a chord where the style had none and the band
      plays it. Bar 2's downbeat is empty in the Charleston - it starts on one
      and pushes the and of two - so lighting it is a figure the style did not
      have, and the comp has to gain a hit.
  */
  await page.locator("#menuButton").click();
  await page.locator("#styleEdit").click();
  await page.waitForSelector("#styleDialog[open]", { timeout: 10000 });

  const emptyCell = page.locator('#styleGrid .style-cell[aria-pressed="false"]').first();
  await emptyCell.click();

  check("clicking an empty cell puts a chord there",
        (await page.locator('#styleGrid .style-cell[aria-pressed="true"]').count()) === 4);

  // Its numbers are the ones under the grid, not in the cell - so the grid
  // stays one row per position at a phone width.
  check("and the chord in hand has its own numbers",
        await page.locator("#styleSlot").isVisible()
        && (await page.locator("#styleWeight").inputValue()) === "60");

  await page.locator("#styleApply").click();
  await page.waitForFunction(() => !document.querySelector("#styleDialog").open,
                             null, { timeout: 10000 });

  const widened = await compRhythmOf("yours");

  check(`a chord added to the figure is a chord the band plays `
        + `(${charleston.length} -> ${widened.length})`,
        widened.length > charleston.length);

  /*  And the feel survives the copy - which needs the *right* feel to test.

      A first version of this check copied the ballad and asserted its chords
      landed on thirds. It passed with the bug deliberately put back, because
      swing bends exactly one position - the straight eighth at tick 12 - and a
      triplet bar has no tick 12 in it at all. The check could not fail.

      The feel that can is **sixteenths**, which shares tick 12 with the eighth
      and is the one no style in the catalogue uses. So: take the Charleston,
      count it in sixteenths, and its chords on the and of two and the and of
      four stop being swung eighths and become straight sixteenths - half way
      through the beat rather than two thirds. Read as eighths they would go
      back to two thirds, which is what the page did for any style it could not
      find before `currentCompStyle()` existed.
  */
  await page.locator("#menuButton").click();
  await page.selectOption("#compStyle", "charleston");
  await page.locator("#styleEdit").click();
  await page.waitForSelector("#styleDialog[open]", { timeout: 10000 });

  await page.selectOption("#styleFeel", "sixteenths");

  check("counting a style in sixteenths redraws it in four rows",
        (await page.locator("#styleGrid .style-row").count()) === 4);

  // The Charleston's chords are on ticks 0 and 12, both of which a sixteenth
  // bar has - so changing the feel keeps the figure rather than dropping it.
  check("and its figure survives, because those positions exist in both",
        (await page.locator('#styleGrid .style-cell[aria-pressed="true"]').count()) === 3);

  await page.locator("#styleApply").click();
  await page.waitForFunction(() => !document.querySelector("#styleDialog").open,
                             null, { timeout: 10000 });

  const sixteenths = await compRhythmOf("yours");
  const intoBeat = sixteenths.map((t) => t - Math.floor(t));

  check(`a style counted in sixteenths is not swung `
        + `(${sixteenths.map((t) => t.toFixed(2)).join(" ")})`,
        intoBeat.some((o) => Math.abs(o - 0.5) < 0.06)
        && !intoBeat.some((o) => Math.abs(o - 2 / 3) < 0.06));

  /*  The band does not play the same two chords all night.

      There used to be exactly one voicing per chord, for ever - the search took
      the strict minimum and the minimum never moved, so a two-bar loop was the
      same two voicings however long you played over it. This rolls several
      choruses of that loop and counts what was actually struck: more shapes
      than there are chords means the band is choosing rather than repeating.
  */
  await page.locator("#menuButton").click();
  await page.selectOption("#compStyle", "charleston");
  await page.locator("#compPiano").check();
  await page.locator("#menuButton").click();

  await forgetSounds();
  await page.keyboard.press("Space");
  await page.waitForTimeout(5000);
  await page.keyboard.press("Space");
  await page.waitForFunction(
    () => document.querySelector("#armTake").getAttribute("aria-pressed") === "false",
    null, { timeout: 10000 });

  const voicings = await page.evaluate(() => {
    // Notes struck at the same instant are one chord. The electric piano builds
    // two oscillators per note, which changes none of that.
    const struck = new Map();

    for (const sound of window.__sounded) {
      if (sound.type !== "sine" || sound.when === null) continue;

      const at = sound.when.toFixed(3);

      if (!struck.has(at)) struck.set(at, []);
      struck.get(at).push(Math.round(sound.hz));
    }

    return [...struck.values()].map((hz) => hz.sort((a, b) => a - b).join("-"));
  });

  const distinct = new Set(voicings).size;

  check(`the band voices a chord more than one way (${distinct} shapes over `
        + `${voicings.length} chords of a two-chord loop)`,
        voicings.length > 4 && distinct > 2);

  /*  Stopping a take stops the band, now rather than at the end of the bar.

      On the web a bar of accompaniment is handed to Web Audio all at once, a
      bar ahead, which is what makes it sample-accurate - and it means that by
      the time a take is stopped the rest of the bar already exists as booked
      nodes. Silencing the chord and the bass note that happen to be sounding
      does not touch those, so the band used to play on for the remainder of
      the bar: a couple of seconds of a tune nobody was playing any more.

      Measured as every node's own end against the moment of the stop, because
      what is booked is not what is heard - a node stopped before its start
      time never sounds at all, which is exactly how the rest of the bar is
      taken back. */
  await page.locator("#menuButton").click();
  await page.locator("#compBass").check();
  await page.locator("#compDrums").check();
  await page.locator("#menuButton").click();

  await forgetSounds();
  await page.keyboard.press("Space");
  await page.waitForTimeout(1800);
  await page.keyboard.press("Space");

  const tail = await page.evaluate(() => {
    const stoppedAt = window.__audioNow();

    const past = window.__sounded
      .filter((s) => s.when !== null)
      // Still to be heard: it has not been stopped, or it stops after this
      // moment having actually started before then.
      .filter((s) => s.stoppedAt === null || (s.stoppedAt > stoppedAt && s.stoppedAt > s.when))
      .map((s) => (s.stoppedAt === null ? Infinity : s.stoppedAt) - stoppedAt);

    return past.length ? Math.max(...past) : 0;
  });

  check(`stopping a take stops the band with it (${tail.toFixed(2)}s of tail)`,
        isFinite(tail) && tail < 0.4);

  /*  And it played that bar like people rather than like a file.

      Every note of the band used to come out at one of a handful of fixed
      levels - every comped note at exactly the same gain, every bass note at
      another, each drum at a third - which is the single thing that gives a
      backing track away. Velocity now moves stroke to stroke, voice to voice
      inside a chord, and between a hit on the beat and one off it.

      Counted rather than eyeballed, over a stretch where the piano, the bass
      and the kit were all playing and the player was silent: a handful of
      distinct gains means the fixed levels are back. */
  const gains = await page.evaluate(() => [...new Set(window.__gains)].length);

  check(`and plays it unevenly, the way people do (${gains} distinct velocities)`,
        gains > 40);

  await page.waitForFunction(
    () => document.querySelector("#armTake").getAttribute("aria-pressed") === "false",
    null, { timeout: 10000 });

  await page.locator("#menuButton").click();
  await page.locator("#compBass").uncheck();
  await page.locator("#compDrums").uncheck();
  await page.locator("#menuButton").click();

  /*  The grand piano is a recording where the electric piano is synthesised, so
      the same comp on the same bar comes out of a different kind of node. That
      is the check: not that it made a sound, but that it made it the new way. */
  await page.locator("#menuButton").click();
  await page.selectOption("#compSound", { value: "grand" });
  await page.locator("#menuButton").click();
  await goToBar(1);
  await forgetSounds();
  await goToBar(0);
  await page.waitForFunction(() => window.__sounded.some((s) => s.type === "sample"),
                             null, { timeout: 15000 });

  check("a recorded piano is played as a recording, not as the synth",
        (await page.evaluate(() =>
          window.__sounded.filter((s) => s.type === "sample").length)) >= 4);

  await page.locator("#menuButton").click();
  await page.selectOption("#compSound", { value: "ep" });
  await page.locator("#menuButton").click();

  /*  The bass. A recording rather than a synth, so these read buffer sources -
      and a walking line is one note to the beat, which is what separates it
      from the piano's comping at a glance. */
  await page.locator("#menuButton").click();
  await page.locator("#compPiano").uncheck();
  await page.locator("#compBass").check();
  await page.locator("#menuButton").click();

  await forgetSounds();
  await page.keyboard.press("Space");
  await page.waitForTimeout(2600);
  await page.keyboard.press("Space");
  await page.waitForFunction(
    () => document.querySelector("#armTake").getAttribute("aria-pressed") === "false",
    null, { timeout: 10000 });

  const walked = await page.evaluate(() =>
    window.__sounded.filter((s) => s.type === "sample").map((s) => s.when));
  const clicked = await page.evaluate(() =>
    window.__sounded.filter((s) => s.type === "square").map((s) => s.when).sort((a, b) => a - b));

  check(`the bass is a recording, and it walks (${walked.length} notes, ${clicked.length} beats)`,
        walked.length > 0 && Math.abs(walked.length - clicked.length) <= 2);

  // One to the beat means on the beat: nothing between them.
  const bassOffBeats = walked
    .map((w) => (w - clicked[0]) / 0.25)
    .filter((b) => Math.abs(b - Math.round(b)) > 0.1);

  check(`the walking line lands on beats (${bassOffBeats.length} off)`, bassOffBeats.length === 0);

  await page.locator("#menuButton").click();
  await page.locator("#compBass").uncheck();
  await page.locator("#menuButton").click();

  /*  The drummer. The one member of the band that asks the engine nothing, so
      what is worth checking is not that a call came back but that the kit
      actually keeps time - and keeps it the way a ride pattern does rather
      than the way a metronome does.

      Two signatures, and they are distinct by construction: the cymbals and
      the snare are noise read out of a buffer, so they arrive as samples, and
      the kick is a falling sine, which nothing else on this page is. */
  await page.locator("#menuButton").click();
  await page.locator("#compDrums").check();
  await page.locator("#menuButton").click();

  await forgetSounds();
  await page.keyboard.press("Space");
  await page.waitForTimeout(2600);
  await page.keyboard.press("Space");
  await page.waitForFunction(
    () => document.querySelector("#armTake").getAttribute("aria-pressed") === "false",
    null, { timeout: 10000 });

  const kit = await page.evaluate(() => ({
    cymbals: window.__sounded.filter((s) => s.type === "sample").map((s) => s.when),
    kicks: window.__sounded.filter((s) => s.type === "sine" && s.hz > 80 && s.hz < 110).length,
    beats: window.__sounded.filter((s) => s.type === "square").map((s) => s.when).sort((a, b) => a - b)
  }));

  check(`the kit plays (${kit.cymbals.length} cymbals, ${kit.kicks} kicks)`,
        kit.cymbals.length > 0 && kit.kicks > 0);

  /*  A ride pattern is not a metronome: it strikes on every beat *and* skips
      off the backbeats, so it has to be denser than the click it plays over.
      A kit that only ever landed on the beat would pass every check above this
      one and still be a metronome with a cymbal on it. */
  const skips = kit.cymbals
    .map((w) => (w - kit.beats[0]) / 0.25)
    .filter((b) => Math.abs(b - Math.round(b)) > 0.1);

  check(`the ride skips off the beat rather than marking it (${skips.length} off)`,
        skips.length > 0);

  await page.locator("#menuButton").click();
  await page.locator("#compDrums").uncheck();
  await page.locator("#menuButton").click();

  /*  The grid's other consumer, over the same clock. A note played while the
      transport rolls carries where in the bar it fell; the engine reads it and
      the page must not break sending it. Static, there is no position to send
      and the same note reads exactly as it always did - which is the half of
      this that had to stay true. */
  await page.keyboard.press("Space");
  await page.waitForFunction(
    () => document.querySelector("#armTake").getAttribute("aria-pressed") === "true",
    null, { timeout: 10000 });
  await page.waitForTimeout(300);

  await soloKey(62).click();
  await page.waitForTimeout(250);
  const whileRolling = await readout();

  await page.keyboard.press("Space");
  await page.waitForFunction(
    () => document.querySelector("#armTake").getAttribute("aria-pressed") === "false",
    null, { timeout: 10000 });

  check(`a note played on the clock is still read back (${whileRolling})`,
        whileRolling.length > 0 && /D/.test(whileRolling));

  // Put it back the way the rest of the checks expect to find it.
  await page.locator("#menuButton").click();
  await page.selectOption("#compSound", "ep");
  await page.locator("#compPiano").uncheck();
  await goToBar(0);

  // Back to static for the checks that follow, which click bars themselves.
  await page.locator("#playStatic").click();
  await page.locator("#menuButton").click();
  await page.selectOption("#scaleStyle", "pentatonic");
  await page.locator("#menuButton").click();
  await bars.first().click();
  await page.waitForSelector("#chordDialog[open]", { timeout: 10000 });

  const narrowed = await page.locator("#scaleRows li").allInnerTexts();
  check(`a style narrows the scales offered (${narrowed.length})`,
        narrowed.length > 0 && narrowed.every((row) => /Pentatonic|Blues/.test(row)));

  await page.locator("#dialogClose").click();
  await page.locator("#menuButton").click();
  await page.selectOption("#scaleStyle", "modes");
  await page.locator("#menuButton").click();

  await page.locator("#armTake").click();
  await page.waitForSelector("#armTake[aria-pressed='true']", { timeout: 10000 });
  check("arming is visible on the dock itself",
        await page.locator(".dock.armed").count() === 1);

  await soloKey(62).click();
  await soloKey(65).click();
  await page.waitForFunction(
    () => document.querySelector("#soloTakeTally").innerText.includes("%"), null, { timeout: 10000 });
  check(`the take counts as it goes (${await page.locator("#soloTakeTally").innerText()})`, true);

  // The chart carries what the take made of each bar, which is the thing a
  // summary in the dock cannot do: say it where the player is looking.
  await page.waitForFunction(
    () => document.querySelectorAll("#systems .bar .bar-take:not([hidden])").length === 1,
    null, { timeout: 10000 });
  check("the bar being played over is marked on the chart", true);

  const marked = await page.locator("#systems .bar").first().getAttribute("data-take");
  check(`and the mark carries its numbers (${marked})`, /notes:.*chord.*scale.*outside/.test(marked));

  // The score is the strip's one-number summary, and it comes from the engine
  // rather than from arithmetic in the page.
  // The take so far is two chord tones; play a chromatic approach into a third
  // and the bar should count it as landing rather than as outside.
  await soloKey(64).click();
  await soloKey(63).click();
  await soloKey(62).click();
  await page.waitForFunction(
    () => /approach/.test(document.querySelector("#systems .bar").dataset.take || ""),
    null, { timeout: 10000 });
  check(`the chart counts an approach note as its own tier `
        + `(${await page.locator("#systems .bar").first().getAttribute("data-take")})`,
        (await page.locator("#systems .bar .bar-take .ap").count()) === 1);

  const scored = (await page.locator("#systems .bar .bar-score").first().innerText()).trim();
  check(`the bar carries a score (${scored})`, /^\d{1,3}%$/.test(scored));
  check("and a screen reader is told it too",
        (await page.locator("#systems .bar").first().getAttribute("aria-label")).includes(scored));

  /*  And the bar has more to say when you stop to ask it. The strip says how
      the bar went at a glance; clicking the bar you are already on opens the
      same reading with its working out - which tier every note landed in, and
      in chord practice every chord struck there and how it read.

      The tab strip has been hidden since each mode had one panel; a bar the
      take covered gives it something to switch between, which is exactly when
      it comes back. */
  await page.locator("#systems .bar").first().click();
  await page.waitForSelector("#chordDialog[open]", { timeout: 10000 });

  check("a bar the take covered offers what it made of it",
        (await page.locator("#chordTabs").isVisible())
        && !(await page.locator("#tabTake").isHidden()));

  await page.locator("#tabTake").click();

  const takeHead = (await page.locator("#takeHead").innerText()).trim();
  const takeTiers = await page.evaluate(() =>
    [...document.querySelectorAll("#takeRows li")].map((row) => row.innerText.replace(/\s+/g, " ")));

  check(`and reads the bar out tier by tier (${takeHead}: ${takeTiers.length} tiers)`,
        /^\d{1,3}%\s+-\s+\d+ notes?$/.test(takeHead)
        && takeTiers.length > 0
        // Every row is a tier with a count and a share of the bar.
        && takeTiers.every((row) => /\d+\s+-\s+\d+%$/.test(row)));

  // The other mode's panel is not offered here. Reharmonising is not something
  // solo practice prevents, it is a question this mode is not about.
  check("without offering the other mode's question",
        await page.locator("#tabReharm").isHidden());

  await page.locator("#dialogClose").click();

  // A bar nothing was played over has nothing extra to say, so the strip stays
  // away rather than offering an empty panel.
  await page.locator("#systems .bar").nth(7).click();
  await page.locator("#systems .bar").nth(7).click();
  await page.waitForSelector("#chordDialog[open]", { timeout: 10000 });

  check("a bar the take never reached says nothing extra",
        !(await page.locator("#chordTabs").isVisible()));

  await page.locator("#dialogClose").click();
  await page.locator("#systems .bar").first().click();

  // Walking to another bar during a take must not end it. Bar two is not the
  // bar we are on, so this moves rather than opening it.
  await page.locator("#systems .bar").nth(1).click();
  await soloKey(67).click();
  await page.waitForFunction(
    () => document.querySelector("#soloBarTally").innerText.includes("G7"), null, { timeout: 10000 });
  check("moving bar re-targets without ending the take",
        await page.locator("#armTake").getAttribute("aria-pressed") === "true");

  await page.locator("#armTake").click();
  await page.waitForSelector("#soloSummary:not([hidden])", { timeout: 10000 });

  // Two bars played over, two bars marked - and the marks outlive the take,
  // because that is when they are worth reading.
  check("every bar played over keeps its mark",
        (await page.locator("#systems .bar .bar-take:not([hidden])").count()) === 2);

  const takeSummary = (await page.locator("#soloSummaryHead").innerText()).trim();
  check(`the take is summarised (${takeSummary})`, takeSummary.includes("over 2 bars"));

  await page.locator("#modeChords").click();
  check("switching back restores chord practice", await page.locator("#feedback").isVisible());
  check("and chord practice has no band to comp for it", await bandOffered() === false);
  check("and the chart stops carrying the take's marks",
        (await page.locator("#systems .bar .bar-take:not([hidden])").count()) === 0);

  //  --- comping practice: chord practice with a clock -----------------------
  //
  //  Not a third mode. The question is chord practice's own - does this voicing
  //  say what the bar says - and the clock adds the half that needs one: did it
  //  land where the style puts it.

  /*  The pedal is shown where there is something for it to hold: a MIDI
      keyboard's key releases, which is the whole reason it exists, and the
      auto-release in solo practice and comping. A keyboard is connected by
      now, so static chord practice is exactly where it earns its place. */
  check("the sustain pedal is there for a connected keyboard",
        await page.locator("#sustainPedal").isVisible());

  await page.locator("#playLive").click();

  check("and stays in time, where notes are let go of for you",
        await page.locator("#sustainPedal").isVisible());

  check("in time, chord practice gets a clock and a take",
        (await page.locator("#armTake").isVisible()) && (await page.locator("#metreRow").isVisible()));
  check("and a band to comp against", await bandOffered() === true);
  check("and the same button asks the question this setting is about",
        (await page.locator("#showVoicing").innerText()).trim().toLowerCase() === "show me a comp");

  // Held notes are static's idea. In time "Play chord" and "Name it" have
  // nothing to work on, because a comp is struck rather than held.
  check("the controls that need held notes stand down",
        !(await page.locator("#playChord").isVisible())
          && !(await page.locator("#nameChord").isVisible()));

  const compKey = (note) => page.locator(`#keyboard .key[data-note="${note}"]`);
  const strike = async (notes) => {
    for (const note of notes) await compKey(note).click();
    await page.waitForTimeout(150);
  };

  await strike([53, 57, 60, 65]);

  check("the keys stop latching - a comp is struck, not held",
        (await page.locator('#keyboard .key[aria-pressed="true"]').count()) === 0);

  // Nothing is counting yet, and the page says exactly that rather than
  // inventing a downbeat - the same honesty a solo take played statically has.
  check("with no clock running a comp is read for its notes and not its placing",
        await page.locator("#compPlace").getAttribute("data-placement") === "unplaced");

  await page.locator("#armTake").click();

  check("arming starts the clock as well, in one gesture",
        await page.locator("#armTake").getAttribute("aria-pressed") === "true");

  /*  A chord struck during the count-in is honestly unplaced - the count-in is
      not bar one, and `positionNow` says so rather than inventing a downbeat.
      So this keeps comping until one lands on the chart, rather than striking
      once and hoping the count-in is over: the retry is the check, not a
      workaround for one. */
  let placement = "unplaced";

  for (let attempt = 0; attempt < 12 && placement === "unplaced"; ++attempt) {
    await strike([53, 57, 60, 65]);
    await page.waitForTimeout(250);
    placement = await page.locator("#compPlace").getAttribute("data-placement");
  }

  check(`a comped chord is placed against the style (${placement})`,
        ["figure", "idiomatic", "offStyle"].includes(placement));
  check("and said in a line that names where it fell",
        (await page.locator("#verdict").innerText()).trim().length > 0);

  /*  A take is read back over the bars it played *through*, so this has to let
      one finish. That is not the test being careful, it is the rule: a bar
      stopped in the middle was not played to the end, and counting it would
      mark every four-to-the-bar take sparse in its last bar. */
  const rollingBar = await page.evaluate(() => {
    const bar = document.querySelector("#systems .bar.rolling");
    return bar ? bar.dataset.index : null;
  });

  await page.waitForFunction(
    (was) => {
      const bar = document.querySelector("#systems .bar.rolling");
      return bar !== null && bar.dataset.index !== was;
    },
    rollingBar, { timeout: 15000 });

  await strike([53, 57, 60, 65]);

  await page.locator("#armTake").click();
  await page.waitForSelector("#compSummary:not([hidden])", { timeout: 10000 });

  const compHead = (await page.locator("#compSummaryHead").innerText()).trim();
  check(`the take is read back as placement, register and density (${compHead})`,
        /Placement \d+%, register \d+%, density \d+%/.test(compHead));

  /*  ...and onto the bars it was played over, the way a solo take's numbers
      are. The dock says which bar got away; the chart says it where a player's
      eyes already are. A count rather than a percentage, because the engine
      scores a take's placement and not a bar's - see `compBarMark`. */
  const compMarks = await page.evaluate(() =>
    [...document.querySelectorAll("#systems .bar")]
      .filter((bar) => !bar.querySelector(".bar-take").hidden)
      .map((bar) => ({
        tiers: [...bar.querySelectorAll(".bar-take i")].map((tier) => tier.className),
        number: bar.querySelector(".bar-score").textContent,
        words: bar.dataset.take || ""
      })));

  check(`a comping take marks the bars it covered (${compMarks.length} bars)`,
        compMarks.length > 0
        && compMarks.every((m) => m.tiers.length > 0
                                  && /^\d+$/.test(m.number)
                                  && /chords?: .* the figure/.test(m.words)));

  // Only comping's own tiers, never the solo strip's. The two modes share one
  // drawing path, and the way that goes wrong is one mode's colours meaning
  // the other mode's thing.
  check("and in comping's own tiers, not a solo strip's",
        compMarks.every((m) => m.tiers.every((t) => ["is", "un", "out"].includes(t))));

  await page.locator("#playStatic").click();

  await strike([53, 57, 60, 65]);
  check("and static chord practice is exactly as it was",
        (await page.locator('#keyboard .key[aria-pressed="true"]').count()) === 4
          && !(await page.locator("#compPlace").isVisible())
          && await bandOffered() === false);

  // The colophon names the build, which is how anyone looking at the site can
  // tell whether it is serving what was pushed. "development" is the right
  // answer for a copy that was not deployed, so this only asks that it says
  // something - the workflow itself checks the stamp was replaced.
  // In the panel's About section now, so the panel has to be opened to read
  // it: `innerText` of something `display: none` is the empty string, which
  // would have passed a check for "it says something" by saying nothing.
  await page.locator("#menuButton").click();
  const build = (await page.locator("#colophonBuild").innerText()).trim();
  await page.locator("#menuButton").click();
  check(`the page names its build (${build})`, build.startsWith("Build:"));

  // The page checks at boot that the engine beside it exports every call it
  // makes, and says so loudly when it does not. What is worth asserting here
  // is the quiet half: this pair was built together, so the banner must stay
  // down and the chip must read ready. A false positive on a good engine
  // would be worse than the silence the banner was written to replace, and
  // it is exactly what a wrong guess about how Emscripten exposes its
  // exports would look like.
  check("a page and engine built together say nothing about each other",
        !(await page.locator("#staleEngine").isVisible())
          && (await page.locator("#engineStatus").getAttribute("data-state")) === "ready");

  // Offline: the worker has to be registered and awake, or the page is no
  // more use on a train than it was before.
  const worker = await page.evaluate(async () => {
    const registration = await navigator.serviceWorker.getRegistration();
    return registration ? String(registration.active && registration.active.state) : "none";
  });
  check(`the offline worker is running (${worker})`, worker === "activated");

  // And it has to actually serve the page with the network gone. A worker that
  // registers but caches nothing is the failure this catches - and it is a real
  // one: a worker only starts intercepting after the page has loaded, so unless
  // it goes and fetches the essentials itself on install, a first visit leaves
  // the cache empty and offline only appears to work because the browser's own
  // HTTP cache answers the reload.
  const offline = await browser.newContext();
  const revisit = await offline.newPage();
  await revisit.goto(`${origin}/index.html`, { waitUntil: "load" });
  await revisit.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });

  // Polled from here rather than with waitForFunction: that takes the predicate's
  // return value as-is, and an async predicate returns a promise, which is an
  // object, which is truthy - so it would pass on the first tick every time.
  const cached = async () => revisit.evaluate(async () => {
    const names = await caches.keys();

    if (names.length === 0) return [];

    const cache = await caches.open(names[0]);
    return (await cache.keys()).map((request) => new URL(request.url).pathname);
  });

  let inCache = [];

  for (let tick = 0; tick < 80 && inCache.length < 3; tick++) {
    inCache = await cached();
    if (inCache.length < 3) await revisit.waitForTimeout(250);
  }

  check(`the worker stocks its cache on the first visit (${inCache.length} files)`,
        inCache.length >= 3);

  await offline.setOffline(true);
  await revisit.reload({ waitUntil: "load" });
  await revisit.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });
  check("the page still starts with the network gone", true);
  await offline.close();

  /*  The frame. The page is three zones - head, chart, dock - and only the
      middle one scrolls. Read on its own page so the careful ordering of the
      sections above is left alone.

      What this is really protecting is the thing the layout was changed for:
      the dock used to be `position: sticky`, so it *overlaid* the chart rather
      than sitting under it, and a bar scrolled beneath it was drawn and
      unreadable. A chart with all twelve bars "on screen" showed four. */
  {
    const framed = await browser.newPage({ viewport: { width: 1100, height: 700 } });
    await framed.goto(`${origin}/index.html`, { waitUntil: "load" });
    await framed.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });
    if (await framed.locator("#helpDialog[open]").count()) await framed.locator("#helpClose").click();

    const zone = await framed.evaluate(() => {
      const z = document.querySelector(".chart-zone");
      return { pageScrolls: document.documentElement.scrollHeight > window.innerHeight + 0.5,
               chartScrolls: z.scrollHeight > z.clientHeight };
    });

    check(`the page does not scroll, the chart does `
          + `(page ${zone.pageScrolls}, chart ${zone.chartScrolls})`,
          zone.pageScrolls === false && zone.chartScrolls === true);

    // Scrolled to the very end, the last bar is still above the dock rather
    // than behind it.
    const clear = await framed.evaluate(() => {
      const z = document.querySelector(".chart-zone");
      z.scrollTop = z.scrollHeight;
      const floor = document.querySelector(".dock").getBoundingClientRect().top;
      const bars = [...document.querySelectorAll("#systems .bar[data-index]")];
      const last = bars[bars.length - 1].getBoundingClientRect();
      return { covered: last.bottom > floor + 0.5, bars: bars.length };
    });

    check(`and the dock never covers a bar (${clear.bars} bars)`, clear.covered === false);

    /*  And the rolling bar brings itself into view. Nothing on this page
        scrolled anything at all before the frame, so on a tune longer than the
        chart zone the mark simply rolled off the bottom and stayed there - at
        the one moment a player cannot reach for the scrollbar. */
    await framed.evaluate(() => document.querySelector(".chart-zone").scrollTop = 0);
    await framed.locator("#playLive").click();
    await framed.locator("#transportButton").click();
    await framed.selectOption("#loopFrom", "0");
    await framed.selectOption("#loopTo", { index: 11 });   // the whole twelve bars
    await framed.locator("#transportButton").click();
    await framed.fill("#tempo", "300");
    await framed.dispatchEvent("#tempo", "change");
    await framed.locator("#armTake").click();

    // Watched all the way to the last system rather than sampled once: a mark
    // that is in view at the top of the form and gone by the end of it is
    // exactly the failure this replaces.
    let strayed = null;
    let reached = 0;

    for (let tick = 0; tick < 80 && reached < 8; tick += 1) {
      const at = await framed.evaluate(() => {
        const rolling = document.querySelector("#systems .bar.rolling");
        if (!rolling) return null;
        const zone = document.querySelector(".chart-zone").getBoundingClientRect();
        const line = rolling.closest(".system").getBoundingClientRect();
        return { index: Number(rolling.dataset.index),
                 inView: line.top >= zone.top - 1 && line.bottom <= zone.bottom + 1 };
      });

      if (at !== null) {
        reached = Math.max(reached, at.index);
        if (!at.inView && strayed === null) strayed = at.index;
      }

      await framed.waitForTimeout(200);
    }

    check(`the rolling bar keeps itself in view (reached bar ${reached + 1}`
          + `${strayed === null ? "" : `, lost it at bar ${strayed + 1}`})`,
          reached >= 8 && strayed === null);

    await framed.locator("#armTake").click();
    await framed.close();
  }

  /*  The chart fills the zone it is given.

      A twelve-bar tune is shorter than the zone on any roomy window, and the
      difference used to bank at the bottom as dead cream - 156px of it at
      1440x900, which reads as the page having run out rather than as the page
      being laid out. It is given away in two goes: the gaps between systems
      grow first, up to a cap, and what is left over becomes symmetric padding.

      Both sizes, because the interesting half is the one where nothing should
      happen. A chart taller than its zone has nothing to give away, and if the
      spacing moved there it would be stretching a chart that is already being
      scrolled. */
  for (const [width, height, room] of [[1440, 900, "room to spare"],
                                       [1100, 700, "more chart than room"]]) {
    const filled = await browser.newPage({ viewport: { width, height } });

    await filled.goto(`${origin}/index.html`, { waitUntil: "load" });
    await filled.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });
    await filled.evaluate(() => document.fonts.ready.then(() => true));
    if (await filled.locator("#helpDialog[open]").count()) await filled.locator("#helpClose").click();

    const laid = await filled.evaluate(() => {
      const zone = document.querySelector(".chart-zone");
      const edge = zone.getBoundingClientRect();
      const sheet = document.querySelector(".sheet").getBoundingClientRect();
      const systems = [...document.querySelectorAll(".system")];

      return {
        gap: systems.length > 1
               ? Math.round(systems[1].getBoundingClientRect().top
                            - systems[0].getBoundingClientRect().bottom)
               : null,
        // The section keeps a little air under the sheet by design. Anything
        // beyond that is the void this exists to close.
        under: Math.round(edge.bottom - sheet.bottom),
        air: Math.round(parseFloat(getComputedStyle(
               document.querySelector(".sheet-section")).paddingBottom)),
        scrolls: zone.scrollHeight > zone.clientHeight + 0.5
      };
    });

    if (room === "room to spare")
      check(`the chart fills the zone when there is ${room} `
            + `(gaps ${laid.gap}px, ${laid.under}px under the sheet)`,
            laid.gap > 4 && laid.under <= laid.air + 1 && laid.scrolls === false);
    else
      check(`and is spaced exactly as it was when there is ${room} (gaps ${laid.gap}px)`,
            laid.gap === 4 && laid.scrolls === true);

    /*  And a tune of one system, which is the case the gaps cannot help with:
        there are none. All of the slack has to go into the margins or it is
        not centred at all. Typed rather than reloaded, because editing the
        chart re-lays it out and that is the path worth exercising. */
    if (room === "room to spare") {
      await filled.locator("#chartButton").click();
      await filled.locator("#editToggle").click();
      await filled.fill("#progression", "| C7 | F7 |");
      await filled.dispatchEvent("#progression", "input");
      await filled.waitForFunction(
        () => document.querySelectorAll(".system").length === 1, null, { timeout: 10000 });

      await filled.locator("#chartButton").click();
      await filled.locator("#editToggle").click();

      const alone = await filled.evaluate(() => {
        const edge = document.querySelector(".chart-zone").getBoundingClientRect();
        const sheet = document.querySelector(".sheet").getBoundingClientRect();
        const only = document.querySelector(".system").getBoundingClientRect();

        return { above: Math.round(only.top - sheet.top),
                 below: Math.round(sheet.bottom - only.bottom),
                 under: Math.round(edge.bottom - sheet.bottom),
                 air: Math.round(parseFloat(getComputedStyle(
                        document.querySelector(".sheet-section")).paddingBottom)) };
      });

      /*  Even margins are not enough on their own to say this worked - the
          sheet's own padding is symmetric to begin with, so a chart that had
          been left alone entirely would also sit evenly inside a short page
          with the void underneath it. What says it worked is the page reaching
          the bottom of the zone *and* the music sitting in the middle of it. */
      check(`a chart of one system is centred instead `
            + `(${alone.above}px over, ${alone.below}px under, ${alone.under}px to the dock)`,
            alone.under <= alone.air + 1 && Math.abs(alone.above - alone.below) <= 2);
    }

    await filled.close();
  }

  /*  What survives a reload, and what must not.

      A settings layer is the one kind of feature that cannot be checked by
      looking at the page once: every assertion here is about the *second*
      visit. Its own context, because that is what owns the store - the rest of
      the suite would otherwise be practising against whatever this left
      behind.

      The negative half matters as much as the positive. Reloading is meant to
      find the stand where you left it, not to resume a session, so the chart
      and the take have to come back as they came the first time. A settings
      layer that quietly restored a reharmonised chart would look like this one
      working. */
  {
    const kept = await browser.newContext();
    const before = await kept.newPage();

    await before.goto(`${origin}/index.html`, { waitUntil: "load" });
    await before.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });
    if (await before.locator("#helpDialog[open]").count()) await before.locator("#helpClose").click();

    await before.locator("#playLive").click();
    await before.fill("#tempo", "184");
    await before.dispatchEvent("#tempo", "change");
    await before.selectOption("#timeSig", "3/4");

    await before.locator("#transportButton").click();
    await before.selectOption("#loopFrom", { index: 2 });
    await before.selectOption("#loopTo", { index: 5 });
    await before.locator("#transportButton").click();

    await before.locator("#menuButton").click();
    await before.selectOption("#soundBank", "grand");
    await before.locator("#compBass").check();
    await before.locator("#compDrums").check();
    await before.selectOption("#bassSound", { index: 1 });
    const styleWanted = await before.locator("#compStyle option").nth(1).getAttribute("value");
    await before.selectOption("#compStyle", styleWanted);
    await before.locator("#menuButton").click();

    await before.locator("#guideButton").click();

    // Something to prove is *not* remembered: a chart that is not the one the
    // page ships with.
    await before.locator("#chartButton").click();
    await before.locator("#editToggle").click();
    await before.fill("#progression", "| Eb7 | Ab7 |");
    await before.dispatchEvent("#progression", "input");
    await before.waitForFunction(
      () => document.querySelectorAll("#systems .chord").length === 2, null, { timeout: 10000 });

    await before.reload({ waitUntil: "load" });
    await before.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });
    if (await before.locator("#helpDialog[open]").count()) await before.locator("#helpClose").click();

    const back = await before.evaluate(() => ({
      tempo: document.querySelector("#tempo").value,
      metre: document.querySelector("#timeSig").value,
      inTime: document.querySelector("#playLive").getAttribute("aria-checked"),
      bank: document.querySelector("#soundBank").value,
      bass: document.querySelector("#compBass").checked,
      drums: document.querySelector("#compDrums").checked,
      piano: document.querySelector("#compPiano").checked,
      bassSound: document.querySelector("#bassSound").value,
      style: document.querySelector("#compStyle").value,
      guide: document.querySelector("#guideButton").getAttribute("aria-pressed"),
      from: document.querySelector("#loopFrom").value,
      to: document.querySelector("#loopTo").value,
      bars: document.querySelectorAll("#systems .bar[data-index]").length,
      armed: document.querySelector("#armTake").getAttribute("aria-pressed")
    }));

    check(`the practice settings come back (${back.tempo}bpm, ${back.metre}, `
          + `${back.bank}, bars ${Number(back.from) + 1}-${Number(back.to) + 1})`,
          back.tempo === "184" && back.metre === "3/4" && back.inTime === "true"
          && back.bank === "grand" && back.from === "2" && back.to === "5");

    check(`and so does the band (${back.style}, bass ${back.bassSound})`,
          back.bass === true && back.drums === true && back.style === styleWanted
          && back.bassSound !== "upright" && back.guide === "true");

    // Two things it must not bring back. The chart is the tune on the stand,
    // not a setting; a take is work, and a page that opened mid-take would be
    // counting someone in who had not asked to play.
    check(`but not the chart you were editing (${back.bars} bars)`, back.bars === 12);
    check("and not a take", back.armed === "false");

    /*  A loop is bar numbers, so a range recalled onto a shorter chart is two
        numbers rather than a loop. `fillLoopRange()` is what already knows
        that, which is why the recall goes back through it rather than writing
        the pickers directly.

        Arrived at through a link, because that is the only way a *different*
        chart is on screen at the moment the settings are read: the chart is
        deliberately not remembered, so reloading always brings the twelve bars
        back and bars 3-6 would still fit. A four-bar tune in the address is
        the real case, and the one that would otherwise go unnoticed. */
    await before.locator("#chartButton").click();
    await before.locator("#editToggle").click();
    await before.fill("#progression", "| C7 | F7 |");
    await before.dispatchEvent("#progression", "input");
    await before.waitForFunction(
      () => document.querySelectorAll("#systems .chord").length === 2, null, { timeout: 10000 });

    await before.locator("#chartButton").click();
    await before.locator("#ioButton").click();
    await before.waitForSelector("#ioDialog[open]", { timeout: 10000 });
    await before.waitForFunction(
      () => document.querySelector("#shareLink").value.length > 0, null, { timeout: 15000 });

    const twoBars = await before.locator("#shareLink").inputValue();

    await before.goto(twoBars, { waitUntil: "load" });
    await before.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });
    if (await before.locator("#helpDialog[open]").count()) await before.locator("#helpClose").click();

    await before.locator("#transportButton").click();

    const narrowed = await before.evaluate(() => ({
      from: document.querySelector("#loopFrom").value,
      to: document.querySelector("#loopTo").value,
      options: document.querySelectorAll("#loopTo option").length
    }));

    check(`a loop that no longer fits widens to the whole tune `
          + `(bars ${Number(narrowed.from) + 1}-${Number(narrowed.to) + 1} of ${narrowed.options})`,
          narrowed.options < 12 && narrowed.from === "0"
          && narrowed.to === String(narrowed.options - 1));

    await before.locator("#transportButton").click();
    await kept.close();

    /*  And a browser that refuses the store at all. Some private windows throw
        on every access rather than handing back an empty one, so every read
        and write is wrapped - and since they all now go through one pair of
        accessors, a missing `try` would be silent everywhere at once rather
        than in the one place it was forgotten. Remembering is the feature that
        is allowed to fail here; booting is not. */
    const shy = await browser.newContext();
    const denied = await shy.newPage();
    const thrown = [];

    denied.on("pageerror", (error) => thrown.push(String(error).split("\n")[0]));

    await denied.addInitScript(() => {
      const refuse = () => { throw new DOMException("denied", "SecurityError"); };
      Object.defineProperty(window, "localStorage", { configurable: true, get: refuse });
    });

    await denied.goto(`${origin}/index.html`, { waitUntil: "load" });
    await denied.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });
    if (await denied.locator("#helpDialog[open]").count()) await denied.locator("#helpClose").click();

    await denied.locator("#playLive").click();
    await denied.fill("#tempo", "150");
    await denied.dispatchEvent("#tempo", "change");

    check(`a browser that refuses to remember still works (${thrown[0] || "no errors"})`,
          thrown.length === 0
          && (await denied.locator("#systems .bar[data-index]").count()) === 12
          && (await denied.locator("#tempo").inputValue()) === "150");

    await shy.close();

    /*  The style the player wrote, across a reload - and thrown away rather
        than guessed at when it no longer means what it said.

        Its own context again, because the store is what owns this. Written
        through the page rather than by reaching into `localStorage`, so what
        is asserted is the thing the editor actually saves.
    */
    const styleStore = await browser.newContext();
    const wrote = await styleStore.newPage();

    await wrote.goto(`${origin}/index.html`, { waitUntil: "load" });
    await wrote.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });
    if (await wrote.locator("#helpDialog[open]").count()) await wrote.locator("#helpClose").click();

    await wrote.locator("#modeSolo").click();
    if (await wrote.locator("#helpDialog[open]").count()) await wrote.locator("#helpClose").click();

    await wrote.locator("#menuButton").click();
    await wrote.selectOption("#compStyle", "ballad");
    await wrote.locator("#styleEdit").click();
    await wrote.waitForSelector("#styleDialog[open]", { timeout: 10000 });
    await wrote.locator("#styleApply").click();

    const written = await wrote.evaluate(() => window.localStorage.getItem("jazzCompCustom"));

    await wrote.reload({ waitUntil: "load" });
    await wrote.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });
    if (await wrote.locator("#helpDialog[open]").count()) await wrote.locator("#helpClose").click();

    check("a style you wrote is still there after a reload",
          (await wrote.locator('#compStyle option[value="yours"]').count()) === 1
          && (await wrote.evaluate(() => document.querySelector("#compStyle").value)) === "yours");

    // And it is the same style, not an empty one wearing the name. The ballad
    // is counted in triplets, so its grid comes back three rows deep.
    //
    // Back to solo first: which mode you were in is deliberately not
    // remembered, and The band is only shown where there is a band to hear.
    await wrote.locator("#modeSolo").click();
    if (await wrote.locator("#helpDialog[open]").count()) await wrote.locator("#helpClose").click();

    await wrote.locator("#menuButton").click();
    await wrote.locator("#styleEdit").click();
    await wrote.waitForSelector("#styleDialog[open]", { timeout: 10000 });

    check(`and it is the style it was, figure and feel `
          + `(${await wrote.locator('#styleGrid .style-cell[aria-pressed="true"]').count()} chords, `
          + `${await wrote.locator("#styleGrid .style-row").count()} rows)`,
          (await wrote.locator('#styleGrid .style-cell[aria-pressed="true"]').count()) === 4
          && (await wrote.locator("#styleGrid .style-row").count()) === 3);

    await wrote.locator("#styleClose").click();

    /*  Now poison it. A store the page does not own can hold anything,
        including the last version of this page's idea of what a style is -
        and the answer to that is to drop it, not to guess. What must not
        happen is a half-applied style: the shipped catalogue has to be exactly
        what it was, with no "Yours" in the menu at all.
    */
    const damaged = (change) => {
      const it = JSON.parse(written);
      change(it);
      return JSON.stringify(it);
    };

    for (const [what, poison] of [
           ["a tick off the grid", damaged((o) => { o.slots[0].tick = 999; })],
           ["a feel nobody writes", damaged((o) => { o.feel = "quavers"; })],
           ["a field this page has never sent", damaged((o) => { delete o.variation; })],
           ["a grid of a different size", damaged((o) => { o.ticksPerBeat = 48; })],
           ["no chords at all", damaged((o) => { o.slots = []; })],
           ["nothing that parses", "{not json at all"]]) {
      await wrote.evaluate((text) => window.localStorage.setItem("jazzCompCustom", text), poison);

      await wrote.reload({ waitUntil: "load" });
      await wrote.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });
      if (await wrote.locator("#helpDialog[open]").count()) await wrote.locator("#helpClose").click();

      const offered = await wrote.locator('#compStyle option[value="yours"]').count();
      const shipped = await wrote.locator("#compStyle option").count();

      check(`a stored style with ${what} is dropped, and the catalogue is untouched`,
            offered === 0 && shipped === 4);
    }

    await styleStore.close();
  }

  /*  The tune library.

      Its own context, because what is being proved is what the *store* holds
      across a reload, and written through the page rather than by reaching
      into `localStorage` - so what is asserted is the thing the menu actually
      saves.

      The check that matters most is the one that looks like nothing: a reload
      on its own must put no tune on the stand. That is the half of "work is
      not remembered" this feature keeps, and the only way to see it is to save
      a tune, reload, and find the default chart still there.
  */
  {
    const tunes = await browser.newContext();
    const stand = await tunes.newPage();

    await stand.goto(`${origin}/index.html`, { waitUntil: "load" });
    await stand.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });
    if (await stand.locator("#helpDialog[open]").count()) await stand.locator("#helpClose").click();

    const chordsOn = async () =>
      (await stand.locator("#systems .chord").allInnerTexts()).join(" ");

    const shipped = await chordsOn();

    // Nothing saved yet, so the picker is not there to be seen.
    await stand.locator("#chartButton").click();
    check("with nothing saved, the picker is not shown at all",
          await stand.locator("#openTuneField").isHidden());

    // Save the chart as it ships, then rewrite it into something else.
    await stand.fill("#tuneName", "The one it ships with");
    await stand.locator("#saveTune").click();

    check("saving fills the picker and names the tune",
          (await stand.locator("#openTuneField").isVisible())
          && (await stand.locator("#openTune option").count()) === 2
          && (await stand.locator("#openTune option").nth(1).innerText()) === "The one it ships with");

    await stand.locator("#editToggle").click();
    await stand.fill("#progression", "| Fm7 | Bb7 | Ebmaj7 | Ebmaj7 |");
    await stand.waitForFunction(
      () => document.querySelectorAll("#systems .chord").length === 4, null, { timeout: 15000 });

    const rewritten = await chordsOn();

    check(`rewriting the chart by hand replaces it (${rewritten})`,
          rewritten !== shipped && rewritten.indexOf("Fm7") === 0);

    // And it is no longer the saved tune, so a take here is not counted
    // against it - the practice record still gets the take, with no tune.
    // Asserted through the note the menu actually shows, because the page has
    // no test hooks and should not grow one: which tune you are on is a thing a
    // player needs told before they press Start a take.
    await stand.locator("#chartButton").click();

    check("and the chart on the stand is no longer that saved tune",
          (await stand.locator("#tuneNote").innerText()).indexOf("not saved") >= 0);

    // The whole point: it comes back, by name.
    await stand.selectOption("#openTune", { label: "The one it ships with" });
    await stand.waitForFunction(
      (want) => Array.from(document.querySelectorAll("#systems .chord"))
                     .map((c) => c.textContent).join(" ") === want,
      shipped, { timeout: 15000 });

    check("a saved tune goes back on the stand, chords and all", (await chordsOn()) === shipped);

    await stand.locator("#chartButton").click();

    check("and the menu names the tune it is on",
          (await stand.locator("#tuneNote").innerText())
            .indexOf("The one it ships with is on the stand") === 0);

    /*  The one that has to fail loudly if this feature ever overreaches.

        A reload restores no tune. The library survives, the stand does not -
        which is the amended rule in one assertion: nothing comes back unless
        you asked for it by name.
    */
    await stand.evaluate(() => {
      document.querySelector("#progression").value = "| Cm7 | F7 |";
      document.querySelector("#progression").dispatchEvent(new Event("input"));
    });
    await stand.waitForFunction(
      () => document.querySelectorAll("#systems .chord").length === 2, null, { timeout: 15000 });

    await stand.reload({ waitUntil: "load" });
    await stand.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });
    if (await stand.locator("#helpDialog[open]").count()) await stand.locator("#helpClose").click();

    await stand.locator("#chartButton").click();

    check("a reload puts no tune on the stand, saved or otherwise",
          (await chordsOn()) === shipped
          && (await stand.locator("#tuneNote").innerText()).indexOf("not saved") >= 0);

    check("but the library is still there, by name",
          (await stand.locator("#openTune option").count()) === 2);

    // Saving under a name already used replaces that tune and keeps its id,
    // which is what keeps practice already logged against it attached.
    const wasId = await stand.evaluate(() =>
      JSON.parse(window.localStorage.getItem("jazzTunes")).tunes[0].id);

    await stand.fill("#tuneName", "the one it ships WITH");
    await stand.locator("#saveTune").click();

    const library = await stand.evaluate(() =>
      JSON.parse(window.localStorage.getItem("jazzTunes")));

    check("saving over a name replaces that tune and keeps its id",
          library.tunes.length === 1 && library.tunes[0].id === wasId);

    /*  Poison the store, the way the comping style's checks do. A tune that
        cannot be read back is dropped rather than repaired, and what must not
        happen is a half-read tune reaching the stand. */
    const good = JSON.stringify(library);
    const damagedTune = (change) => {
      const it = JSON.parse(good);
      change(it);
      return JSON.stringify(it);
    };

    for (const [what, poison] of [
           ["no name", damagedTune((o) => { delete o.tunes[0].name; })],
           ["no progression", damagedTune((o) => { o.tunes[0].progression = ""; })],
           ["a metre that is not a number", damagedTune((o) => { o.tunes[0].beats = "four"; })],
           ["a field this page has never sent", damagedTune((o) => { delete o.tunes[0].composer; })],
           ["an id that is not one", damagedTune((o) => { o.tunes[0].id = "first"; })],
           ["nothing that parses", "{not json at all"]]) {
      await stand.evaluate((text) => window.localStorage.setItem("jazzTunes", text), poison);

      await stand.reload({ waitUntil: "load" });
      await stand.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });
      if (await stand.locator("#helpDialog[open]").count()) await stand.locator("#helpClose").click();

      await stand.locator("#chartButton").click();

      check(`a saved tune with ${what} is dropped, and the stand is untouched`,
            (await stand.locator("#openTuneField").isHidden())
            && (await chordsOn()) === shipped);
    }

    await tunes.close();
  }

  /*  The practice record.

      Its own context, and the check that matters is the round trip: a row this
      page writes has to be a row the engine's grammar reads. Those are two
      pieces of code in two languages agreeing about eighteen fields in an
      order, and the way that breaks is silently - the page thinks it is
      keeping a history and every reading of it comes back an error.

      So this plays a real take, stops it, and hands what the store holds
      straight to the engine.
  */
  {
    const record = await browser.newContext();
    const logging = await record.newPage();

    await logging.goto(`${origin}/index.html`, { waitUntil: "load" });
    await logging.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });
    if (await logging.locator("#helpDialog[open]").count()) await logging.locator("#helpClose").click();

    await logging.locator("#modeSolo").click();
    if (await logging.locator("#helpDialog[open]").count()) await logging.locator("#helpClose").click();

    check("nothing is recorded before anything is played",
          (await logging.evaluate(() => window.localStorage.getItem("jazzPractice"))) === null);

    // An armed take nobody played over writes nothing: a row of zeros would
    // tell the record somebody sat down and did nothing, which is a claim
    // rather than an absence.
    await logging.locator("#armTake").click();
    await logging.locator("#armTake").click();

    check("an armed take nobody played over is not a row",
          (await logging.evaluate(() => window.localStorage.getItem("jazzPractice"))) === null);

    // Now play one. Bar 1 is Dm7, so D and F are chord tones and C# is not.
    await logging.locator("#armTake").click();

    for (const note of [62, 65, 61, 69]) {
      await logging.locator(`#keyboard .key[data-note="${note}"]`).click();
      await logging.waitForTimeout(40);
    }

    await logging.locator("#armTake").click();
    await logging.waitForFunction(
      () => window.localStorage.getItem("jazzPractice") !== null, null, { timeout: 10000 });

    const rows = await logging.evaluate(() =>
      JSON.parse(window.localStorage.getItem("jazzPractice")).takes);

    check(`a played take writes one row (${rows.length})`, rows.length === 1);

    check("and the row carries counts, a day and the harmony it was over",
          rows[0].chordTones + rows[0].scaleTones + rows[0].approachTones
            + rows[0].unresolved + rows[0].outside === 4
          && rows[0].day > 19000
          && rows[0].qualities > 0
          && rows[0].mode === 0
          && rows[0].tune === 0);

    // The thing this feature exists not to keep. Not in the row, under any
    // spelling, because the engine's practice wire never sends one.
    check("and no score of any kind is written down",
          Object.keys(rows[0]).every((key) => key.toLowerCase().indexOf("score") < 0)
          && Object.keys(rows[0]).every((key) => key.toLowerCase().indexOf("fit") < 0));

    /*  A comping take writes a row too, with no line in it.

        Both halves of the app are practice. A record that counted only the
        soloing would tell somebody they had not practised on the days they
        spent comping, which is the one thing it must not get wrong.
    */
    await logging.locator("#modeChords").click();
    if (await logging.locator("#helpDialog[open]").count()) await logging.locator("#helpClose").click();

    await logging.locator("#playLive").click();
    await logging.locator("#armTake").click();
    await logging.waitForFunction(
      () => document.querySelector("#armTake").getAttribute("aria-pressed") === "true",
      null, { timeout: 10000 });

    // A comping take needs a *finished* bar - a bar stopped part-way through
    // was not played to the end, and counting it marks every four-to-the-bar
    // take sparse in its last bar. So this waits for the chart to roll and for
    // two bar lines to go by, rather than for a number of milliseconds.
    const rollingBar = () => logging.evaluate(() => {
      const at = document.querySelector("#systems .bar.rolling");
      return at ? Number(at.dataset.index) : -1;
    });

    await logging.waitForFunction(
      () => document.querySelector("#systems .bar.rolling") !== null, null, { timeout: 20000 });

    const startedOn = await rollingBar();

    for (const note of [62, 65, 69]) await logging.locator(`#keyboard .key[data-note="${note}"]`).click();

    await logging.waitForFunction(
      (was) => {
        const at = document.querySelector("#systems .bar.rolling");
        return at !== null && Number(at.dataset.index) >= was + 2;
      }, startedOn, { timeout: 30000 });

    await logging.locator("#armTake").click();
    await logging.waitForTimeout(600);

    const both = await logging.evaluate(() =>
      JSON.parse(window.localStorage.getItem("jazzPractice")).takes);

    check(`comping is practice too, and its row has no line in it (${both.length} rows)`,
          both.length === 2
          && both[1].mode === 1
          && both[1].chordTones + both[1].scaleTones + both[1].outside === 0);

    /*  The round trip, through the real control.

        This is the check the whole feature rests on: eighteen fields in an
        order, written by the page and read by the engine, in two languages.
        It breaks silently - a page that thinks it is keeping a history while
        every reading of it comes back an error - so it is asserted through
        the button a player actually presses rather than through a hook.
    */
    await logging.locator("#recordButton").click();
    await logging.waitForSelector("#progressDialog[open]", { timeout: 10000 });

    const said = await logging.locator("#recordSummary").innerText();

    check(`the engine reads the page's own record back (${said.slice(0, 26)}...)`,
          said.indexOf("2 takes") === 0);

    check("and the panel draws the engine's words rather than its own",
          (await logging.locator("#recordSaid li").count()) > 0);

    // The coverage half: what has been played over, and what has not. The
    // outlined chips are the half a player cannot see for themselves.
    check(`both halves of the coverage are drawn `
          + `(${await logging.locator('#recordQualities .record-chip[data-met="yes"]').count()} met, `
          + `${await logging.locator('#recordQualities .record-chip[data-met="no"]').count()} not)`,
          (await logging.locator('#recordQualities .record-chip[data-met="yes"]').count()) > 0
          && (await logging.locator('#recordQualities .record-chip[data-met="no"]').count()) > 0
          && (await logging.locator("#recordRoots .record-chip").count()) === 12);

    // Nothing in this panel is a mark. Not in the figures, not in the words.
    const panelText = await logging.locator("#progressPanelRecord").innerText();

    check("and nothing in the panel is a score",
          panelText.toLowerCase().indexOf("score") < 0
          && panelText.indexOf("/100") < 0
          && panelText.indexOf("out of 100") < 0);

    /*  Your tunes, and one tune's memory.

        The finding this half exists for is coverage *within* a tune, so the
        check plays a saved tune's front half only, twice, and asks whether the
        panel can tell. That is the shape almost everybody's practice has and
        the thing no single take can see.
    */
    await logging.locator("#progressClose").click();

    // Save the chart, then play two takes over its first bar only.
    await logging.locator("#chartButton").click();
    await logging.fill("#tuneName", "Front half only");
    await logging.locator("#saveTune").click();
    await logging.locator("#chartButton").click();

    await logging.locator("#modeSolo").click();
    if (await logging.locator("#helpDialog[open]").count()) await logging.locator("#helpClose").click();

    // Three, because the engine deliberately says nothing across takes until
    // there are three of them - with fewer, every bar reached was reached in
    // "every take", which is true and worth nothing.
    for (let round = 0; round < 3; round += 1) {
      await logging.locator("#armTake").click();
      for (const note of [62, 65, 69]) {
        await logging.locator(`#keyboard .key[data-note="${note}"]`).click();
        await logging.waitForTimeout(40);
      }
      await logging.locator("#armTake").click();
      await logging.waitForTimeout(250);
    }

    await logging.locator("#recordButton").click();
    await logging.waitForSelector("#progressDialog[open]", { timeout: 10000 });
    await logging.locator("#progressTabTunes").click();

    check(`the tunes tab lists what is saved `
          + `(${await logging.locator("#tuneList .tune-row").count()})`,
          (await logging.locator("#tuneList .tune-row").count()) === 1
          && (await logging.locator(".tune-row-name").innerText()) === "Front half only"
          && (await logging.locator(".tune-row-said").innerText()).indexOf("3 takes") === 0);

    await logging.locator(".tune-row").click();
    await logging.waitForSelector("#tuneMemoryView:not([hidden])", { timeout: 10000 });

    // Every bar of the chart is drawn, reached or not: the unreached ones are
    // the whole point, so they cannot be left out of the strip.
    const bars = await logging.evaluate(() =>
      Array.from(document.querySelectorAll("#tuneBars .tune-bar"))
           .map((cell) => cell.dataset.reached));

    check(`one tune's memory draws every bar, reached or not `
          + `(${bars.length} bars, ${bars.filter((b) => b === "none").length} never reached)`,
          bars.length === 12 && bars[0] === "all" && bars.filter((b) => b === "none").length === 11);

    check("and says in words which bars never come up",
          (await logging.locator("#tuneSaid").innerText()).indexOf("never been reached") > 0);

    check("with no score anywhere in it",
          (await logging.locator("#tuneMemoryView").innerText()).toLowerCase().indexOf("score") < 0);

    /*  Forgetting takes two presses, because this page uses no `confirm()`
        anywhere and a webview is the wrong place to start. */
    await logging.locator("#tuneForget").click();

    // `.link-btn` uppercases its label, and `innerText` returns what is
    // rendered rather than what was written - so this matches either.
    check("forgetting a tune asks first, and has not forgotten it yet",
          (await logging.locator("#tuneForget").innerText()).toLowerCase().indexOf("really") === 0
          && (await logging.evaluate(() =>
               JSON.parse(window.localStorage.getItem("jazzTunes")).tunes.length)) === 1);

    await logging.locator("#tuneForget").click();
    await logging.waitForSelector("#tuneListView:not([hidden])", { timeout: 10000 });

    const after = await logging.evaluate(() => ({
      tunes: JSON.parse(window.localStorage.getItem("jazzTunes")).tunes.length,
      takes: JSON.parse(window.localStorage.getItem("jazzPractice")).takes.length
    }));

    // The takes stay. They happened, and the record is about the player rather
    // than about the tune - they simply stop belonging to anything.
    check(`the second press forgets the tune and keeps the practice `
          + `(${after.tunes} tunes, ${after.takes} takes)`,
          after.tunes === 0 && after.takes === 5);

    await logging.locator("#progressClose").click();

    await record.close();
  }

  /*  The chart's own tools, which moved off a toolbar above the music and into
      the settings panel's Chart section. Three of the four had no check at all
      - only Import / export and Reharmonise the tune did - and the failure
      they now share is new: a button inside a panel that is shut is a button
      nothing can press, and each of them shows you something down in the chart
      that the panel is in front of.

      On a page of its own, because it rewrites the chart and the ordering of
      the sequence above is deliberate. */
  {
    const edited = await browser.newPage({ viewport: { width: 1100, height: 800 } });
    await edited.goto(`${origin}/index.html`, { waitUntil: "load" });
    await edited.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });
    if (await edited.locator("#helpDialog[open]").count()) await edited.locator("#helpClose").click();

    const chordsNow = async () =>
      (await edited.locator("#systems .chord").allInnerTexts()).join(" ");

    const opening = await chordsNow();

    await edited.locator("#chartButton").click();
    await edited.locator("#editToggle").click();

    check("the editor opens from the chart menu, and closes it on the way",
          (await edited.locator("#editor").isVisible())
          && (await edited.locator("#chartPanel").isHidden()));

    await edited.fill("#progression", "| Fmaj7 | Bb7 |");
    await edited.dispatchEvent("#progression", "input");
    await edited.waitForFunction(
      () => document.querySelectorAll("#systems .chord").length === 2, null, { timeout: 10000 });

    check(`and typing into it rewrites the chart (${await chordsNow()})`,
          (await chordsNow()) === "Fmaj7 B\u266d7");

    await edited.locator("#chartButton").click();
    await edited.locator("#restoreChart").click();
    await edited.waitForFunction(
      (was) => [...document.querySelectorAll("#systems .chord")]
                 .map((c) => c.textContent).join(" ") === was,
      opening, { timeout: 10000 });

    check("and Restore original gives back the tune it opened with",
          (await edited.locator("#chartPanel").isHidden()));

    await edited.close();
  }

  /*  And the strip above the chart holds still. Most of what is on it needs a
      clock - the metre, the tempo, the dots - and the take button needs a mode
      that can grade one, so a strip left to wrap freely is one, two or three
      rows deep depending on which corner of mode x In time you are standing
      in. Every one of those moves the top of the chart, which means turning
      the clock on shoves the music down under the eye that is reading it.

      Measured at the chart's top edge rather than on the strip, because that
      is the thing a player would see move - and it catches a row appearing
      anywhere above the chart rather than only in the strip. At both sizes,
      because the strip is one row on a laptop and two on a phone and it fails
      differently at each: on a laptop a group grows, on a phone the two groups
      fold onto one line and back.

      And in two typefaces, which is the run that matters most and the one this
      check did not have when it first shipped green. The page renders in Jost
      over the web and in the fallback stack in the app and offline
      (`docs/BRANDING.md`), and the strip's reservation is a pixel count - so
      it holds or fails to hold *per face*. It was sized against the fallback
      on a machine that could not reach Google Fonts, and every control on the
      strip is a button or a form control taking its height from `line-height:
      normal`, which is the font's own metrics. In Jost they came out 37 to 39
      against 29 reserved, the reservation stopped binding, and the deployed
      site moved the chart when the clock came on while every local run said it
      did not.

      A webfont cannot be the thing under test here - it needs the network, and
      a check that quietly passes when a CDN is unreachable is the check that
      let this through. So the second pass overrides the *metrics* instead:
      `ascent-override` and `descent-override` on a face built from whatever
      sans is installed is exactly what `normal` is computed from, so it
      reproduces a taller face with nothing fetched. Without the strip's
      explicit line-heights this pass reports three different chart tops at
      390px. */
  const TALL_METRICS = `
    @font-face {
      font-family: "MetricsProbe";
      src: local("DejaVu Sans"), local("Liberation Sans"), local("Arial");
      ascent-override: 150%; descent-override: 45%; line-gap-override: 0%;
    }
    :root { --sans: "MetricsProbe", sans-serif !important; }`;

  for (const [width, height, face] of [[1100, 700, "as served"], [390, 780, "as served"],
                                       [1100, 700, "a taller face"], [390, 780, "a taller face"]]) {
    const held = await browser.newPage({ viewport: { width, height } });

    if (face !== "as served")
      await held.addInitScript((css) => {
        addEventListener("DOMContentLoaded", () => {
          const style = document.createElement("style");
          style.textContent = css;
          document.head.appendChild(style);
        });
      }, TALL_METRICS);

    await held.goto(`${origin}/index.html`, { waitUntil: "load" });
    await held.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });

    /*  The webfont is `display: swap`, so it lands whenever it lands. Measured
        on both sides of that, a perfectly reserved strip still reports two
        numbers - the whole top bar resizes as the face changes. */
    await held.evaluate(() => document.fonts.ready.then(() => true));

    if (await held.locator("#helpDialog[open]").count()) await held.locator("#helpClose").click();

    const measure = () => held.evaluate(() => {
      const box = (selector) => {
        const found = document.querySelector(selector);
        return found ? Math.round(found.getBoundingClientRect().height) : null;
      };

      return { top: Math.round(document.querySelector(".chart-zone").getBoundingClientRect().top),
               clock: box(".transport-clock"),
               take: box(".transport-take") };
    });

    const seen = [];

    for (const mode of ["modeChords", "modeSolo"]) {
      await held.locator(`#${mode}`).click();
      if (await held.locator("#helpDialog[open]").count()) await held.locator("#helpClose").click();

      const said = mode.replace("mode", "").toLowerCase();

      seen.push([`${said} static`, await measure()]);
      await held.locator("#playLive").click();
      seen.push([`${said} in time`, await measure()]);

      await held.locator("#armTake").click();
      await held.waitForFunction(
        () => !document.querySelector("#beatRow").hidden, null, { timeout: 10000 });
      seen.push([`${said} rolling`, await measure()]);

      await held.locator("#armTake").click();
      await held.locator("#playStatic").click();
    }

    /*  Named per state rather than deduplicated to a set of numbers. The first
        time this failed it said `(93, 94)` and nothing else, which left the
        one thing worth knowing - which of the six moved - to be worked out
        from the CSS. */
    const tops = [...new Set(seen.map(([, m]) => m.top))];

    check(`the transport strip keeps the chart still at ${width}px, ${face}`
          + ` (${tops.length === 1 ? tops[0] : seen.map(([at, m]) => `${at} ${m.top}`).join(", ")})`,
          tops.length === 1);

    /*  And the reason it holds, asserted directly: a group taller than its own
        reservation is the failure, and this is the line that names which one.
        The chart-top check above sees the consequence; this sees the cause. */
    const grown = seen.filter(([, m]) => m.clock !== 29 || m.take !== 29)
                      .map(([at, m]) => `${at} clock ${m.clock} take ${m.take}`);

    check(`and both its rows are the 29px they reserve at ${width}px, ${face}`
          + `${grown.length ? " (" + grown.join(", ") + ")" : ""}`,
          grown.length === 0);

    await held.close();
  }

  /*  The cheat sheet fits, section by section, on a desktop window and a phone
      one. It is sectioned precisely because it stopped fitting: solo practice's
      sheet was 2,650px of content in 590px of dialog at a phone width, four and
      a half screens of scrolling, and a sheet you scroll is one people stop
      reading at the fold.

      Measured rather than eyeballed, and measured on every section in both
      modes, because the sections are different lengths and the mode decides
      which entries inside them are showing. The body keeps `overflow-y: auto`
      as a safety valve for a browser with larger text - what this asserts is
      that it never has to use it. */
  for (const [label, viewport] of [["a desktop window", { width: 1280, height: 860 }],
                                   ["a phone", { width: 390, height: 780 }]]) {
    const sheet = await browser.newPage({ viewport });
    await sheet.goto(`${origin}/index.html`, { waitUntil: "load" });
    await sheet.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });

    const over = [];

    for (const mode of ["chords", "solo"]) {
      if (await sheet.locator("#helpDialog[open]").count()) await sheet.locator("#helpClose").click();
      await sheet.locator(`#mode${mode === "solo" ? "Solo" : "Chords"}`).click();
      if (!(await sheet.locator("#helpDialog[open]").count())) await sheet.locator("#helpButton").click();
      await sheet.waitForSelector("#helpDialog[open]", { timeout: 10000 });

      for (const tab of ["helpTabChart", "helpTabPlaying", "helpTabReading",
                         "helpTabTime", "helpTabBand"]) {
        await sheet.locator("#" + tab).click();
        await sheet.waitForTimeout(80);

        const spill = await sheet.evaluate(() => {
          const body = document.querySelector("#helpDialog .tab-body");
          return Math.round(body.scrollHeight - body.clientHeight);
        });

        if (spill > 0) over.push(`${mode}/${tab.replace("helpTab", "")} by ${spill}px`);
      }
    }

    check(`the cheat sheet never has to be scrolled on ${label}`
          + `${over.length ? " (" + over.join(", ") + ")" : ""}`,
          over.length === 0);

    await sheet.close();
  }

  /*  And every section says something. A tab that opens on nothing is worse
      than no tab, and the per-mode entries make that a real risk: a section
      whose entries were all written for the other mode would be an empty
      panel with a name on it. */
  {
    const filled = await page.evaluate(() => {
      const panels = ["helpPanelChart", "helpPanelPlaying", "helpPanelReading",
                      "helpPanelTime", "helpPanelBand"];
      return panels.map((id) => {
        const li = [...document.querySelectorAll(`#${id} > li`)];
        const chords = li.filter((e) => e.dataset.mode !== "solo").length;
        const solo = li.filter((e) => e.dataset.mode !== "chords").length;
        return { id, chords, solo };
      });
    });

    const thin = filled.filter((p) => p.chords < 3 || p.solo < 3);

    check(`every section of the cheat sheet is worth opening in both modes `
          + `(${filled.map((p) => p.id.replace("helpPanel", "") + " " + p.chords + "/" + p.solo).join(", ")})`,
          thin.length === 0);
  }

  // A phone-sized window, in both modes. Everything that has ever overlapped
  // here looked perfect on a desktop one, and the masthead ran off the right
  // edge the moment a fourth control was added to it.
  const narrow = await browser.newPage({ viewport: { width: 390, height: 780 } });
  await narrow.goto(`${origin}/index.html`, { waitUntil: "load" });
  await narrow.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000, state: "attached" });
  await narrow.locator("#helpClose").click();

  for (const mode of ["modeChords", "modeSolo"]) {
    await narrow.locator(`#${mode}`).click();

    // A first arrival opens the sheet, which would otherwise be measured
    // instead of the masthead underneath it.
    if (await narrow.locator("#helpDialog[open]").count()) await narrow.locator("#helpClose").click();

    const spill = await narrow.evaluate(() => {
      const offscreen = [];

      for (const element of document.querySelectorAll(
             ".top-bar > *, .top-bar-right > *, .transport-clock > *, .transport-take > *,"
             + " .chart-bar > *, .dock-actions > *, .dock-status > *")) {
        const box = element.getBoundingClientRect();

        if (box.width > 0 && (box.right > window.innerWidth + 0.5 || box.left < -0.5))
          offscreen.push(element.id || element.className);
      }

      return { page: document.documentElement.scrollWidth - window.innerWidth, offscreen };
    });

    check(`nothing runs off the side at 390px (${mode.replace("mode", "")})`,
          spill.page <= 0 && spill.offscreen.length === 0);
  }

  /*  The practice button's real cost.

      `.top-bar-right` wraps, and every pixel of top-bar height comes out of
      the chart - which is the rule this page's whole frame is built on. A
      button that pushes that row into two has taken a line of music away from
      somebody on a phone, and it does that silently.

      So this measures the row rather than trusting it: every child of
      `.top-bar-right` has to sit on the same line. The words go below the
      page's one breakpoint and the button becomes the same circle the help
      button is, which is what buys the room.
  */
  const topRow = await narrow.evaluate(() => {
    const tops = Array.from(document.querySelectorAll(".top-bar-right > *"))
                      .filter((el) => el.getBoundingClientRect().width > 0)
                      .map((el) => Math.round(el.getBoundingClientRect().top));

    const bar = document.querySelector(".top-bar").getBoundingClientRect().height;
    const button = document.querySelector("#recordButton");

    button.style.display = "none";
    const without = document.querySelector(".top-bar").getBoundingClientRect().height;
    button.style.display = "";

    return { rows: new Set(tops).size, count: tops.length,
             cost: Math.round(bar - without) };
  });

  /*  The height itself, not the row count - which is what this check first
      asked and got wrong. `.top-bar-right` already sits on four lines at 390px,
      so "is it one row" was never the question. What matters is whether the
      button takes any of the chart, and the only way to know is to measure the
      bar with it and without it.
  */
  check(`the practice button takes no height from the chart at 390px `
        + `(${topRow.cost}px, ${topRow.count} controls)`,
        topRow.cost === 0 && topRow.count >= 4);

  /*  And the panel behind it fits.

      Measured against a child with `min-width`, never `width`: a flex row
      shrinks the latter back and the scan passes while the thing it was
      watching for happens anyway. That is how this check went vacuous once
      before, on the style editor.
  */
  await narrow.locator("#recordButton").click();
  await narrow.waitForSelector("#progressDialog[open]", { timeout: 10000 });

  const recordSpill = await narrow.evaluate(() => {
    const spilling = [];

    for (const element of document.querySelectorAll(
           "#progressDialog .tab, #progressDialog .dialog-head > *,"
           + " .record-figure, .record-chip, .record-said li")) {
      const at = element.getBoundingClientRect();

      if (at.width > 0 && (at.right > window.innerWidth + 0.5 || at.left < -0.5))
        spilling.push(element.className || element.id);
    }

    return spilling;
  });

  check(`the practice record fits a 390px screen${recordSpill.length ? " (" + recordSpill.join(", ") + ")" : ""}`,
        recordSpill.length === 0);

  check("and its own body scrolls rather than the page behind it",
        await narrow.evaluate(() => {
          const body = document.querySelector("#progressPanelRecord");
          return getComputedStyle(body).overflowY === "auto";
        }));

  await narrow.locator("#progressClose").click();

  /*  And again at a laptop width, which is where a labelled button cost 38px.
      390 was never the size that caught this: the bar is already four lines
      deep there and one more control changed nothing. */
  {
    const wide = await browser.newPage({ viewport: { width: 1024, height: 800 } });
    await wide.goto(`${origin}/index.html`, { waitUntil: "load" });
    await wide.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000, state: "attached" });
    if (await wide.locator("#helpDialog[open]").count()) await wide.locator("#helpClose").click();

    const cost = await wide.evaluate(() => {
      const barH = () => document.querySelector(".top-bar").getBoundingClientRect().height;
      const button = document.querySelector("#recordButton");
      const withIt = barH();

      button.style.display = "none";
      const without = barH();
      button.style.display = "";

      return Math.round(withIt - without);
    });

    check(`the practice button takes no height from the chart at 1024px (${cost}px)`, cost === 0);
    await wide.close();
  }

  /*  And in time, where the dock foot carries the take button and the beat dots
      at the same time as everything else. That is the widest the foot ever
      gets, so it is the one worth measuring. */
  await narrow.locator("#modeChords").click();
  if (await narrow.locator("#helpDialog[open]").count()) await narrow.locator("#helpClose").click();

  await narrow.locator("#playLive").click();
  await narrow.locator("#armTake").click();
  await narrow.waitForFunction(() => !document.querySelector("#beatRow").hidden, null, { timeout: 10000 });

  const compSpill = await narrow.evaluate(() => {
    const offscreen = [];

    for (const element of document.querySelectorAll(
           ".top-bar > *, .top-bar-right > *, .transport-clock > *, .transport-take > *,"
           + " .chart-bar > *, .dock-actions > *, .dock-status > *, .feedback > *")) {
      const box = element.getBoundingClientRect();

      if (box.width > 0 && (box.right > window.innerWidth + 0.5 || box.left < -0.5))
        offscreen.push(element.id || element.className);
    }

    return { page: document.documentElement.scrollWidth - window.innerWidth, offscreen };
  });

  check("nothing runs off the side at 390px (Chords, in time)",
        compSpill.page <= 0 && compSpill.offscreen.length === 0);

  /*  And nothing runs off the side of a *control*, which the scan above cannot
      see: a box that is too narrow for what is in it is exactly the right
      width as far as its own rectangle is concerned. The tempo box is the one
      that matters, because it is the only field here showing a value rather
      than a label - and it is the one the strip's tightening reached for
      first, at which point 120 read as `12` with the last digit simply gone.
      A number input keeps room for its own spinner, so the answer is not the
      three digits' worth it looks like it needs. Read at both ends of the
      range the box accepts. */
  const clipped = [];

  for (const bpm of ["120", "300"]) {
    await narrow.fill("#tempo", bpm);
    await narrow.dispatchEvent("#tempo", "change");
    await narrow.evaluate(() => document.activeElement.blur());

    if (await narrow.evaluate(() => {
      const box = document.querySelector("#tempo");
      return box.scrollWidth > box.clientWidth + 0.5;
    })) clipped.push(bpm);
  }

  check(`the tempo box shows the whole tempo at 390px (${clipped.length ? clipped.join(", ") : "120, 300"})`,
        clipped.length === 0);

  /*  And every panel opens onto the screen. The scan above cannot see this
      either: a panel is absolutely positioned and shut while it runs, so its
      button being in bounds says nothing about where the panel lands. Which is
      how the transport's own popover first opened half off a 430px screen -
      `.menu-wrap` anchors a panel to its button, and this button is in the
      middle of a row rather than at the end of one. */
  const panels = [];

  for (const [button, panel] of [["#menuButton", "#menuPanel"],
                                 ["#transportButton", "#transportPanel"],
                                 ["#chartButton", "#chartPanel"]]) {
    if (!(await narrow.locator(button).isVisible())) continue;

    await narrow.locator(button).click();

    const box = await narrow.locator(panel).boundingBox();
    if (box && (box.x < -0.5 || box.x + box.width > 390 + 0.5)) panels.push(panel);

    /*  And nothing inside it is wider than it is. The box check above says the
        panel landed on the screen; it says nothing about the four chart tools
        or a select with a long option sitting outside its edge, which is where
        the settings panel's new sections could go wrong. */
    if (await narrow.evaluate((id) => {
      const it = document.querySelector(id);
      return it.scrollWidth > it.clientWidth + 0.5;
    }, panel)) panels.push(`${panel} contents`);

    /*  Closed by its own way out rather than by the button again. At this
        width the settings panel is a bottom sheet drawn over the top bar, so
        the button that opened it is underneath it - which is what the sheet's
        close row is for, and why it only exists here. */
    if (panel === "#menuPanel") await narrow.locator("#menuClose").click();
    else await narrow.locator(button).click();
  }

  check(`every panel opens onto the screen at 390px${panels.length ? " (" + panels.join(", ") + ")" : ""}`,
        panels.length === 0);

  /*  And the style editor, which is the one thing here whose width is decided
      by the music rather than by the page: a column per beat, so a chart in
      seven is a wider grid than a chart in four. A dialog rather than a panel,
      so the scan above cannot reach it and neither can the narrow scan - both
      run with everything shut.

      Measured on the grid's own scroll width rather than the dialog's, because
      a grid that overflows is exactly the right width as far as the dialog is
      concerned - the same blind spot the tempo box has.
  */
  await narrow.locator("#modeSolo").click();
  if (await narrow.locator("#helpDialog[open]").count()) await narrow.locator("#helpClose").click();

  await narrow.locator("#menuButton").click();
  await narrow.locator("#styleEdit").click();
  await narrow.waitForSelector("#styleDialog[open]", { timeout: 10000 });

  const editorSpill = await narrow.evaluate(() => {
    const spilling = [];

    const dialog = document.querySelector("#styleDialog");
    const box = dialog.getBoundingClientRect();

    if (box.left < -0.5 || box.right > window.innerWidth + 0.5) spilling.push("the dialog");

    for (const id of ["#styleGrid", "#styleSlot", ".style-controls", ".style-actions"]) {
      const it = document.querySelector(id);
      if (it && it.scrollWidth > it.clientWidth + 0.5) spilling.push(id);
    }

    // Every control in it, the way the narrow scan reads the top bar's rows.
    for (const element of document.querySelectorAll(
           "#styleGrid .style-cell, #styleGrid .style-row-head,"
           + " .style-slot > *, .style-controls > *, .style-actions > *")) {
      const at = element.getBoundingClientRect();

      if (at.width > 0 && (at.right > window.innerWidth + 0.5 || at.left < -0.5))
        spilling.push(element.id || element.className);
    }

    return spilling;
  });

  check(`the style editor fits a 390px screen${editorSpill.length ? " (" + editorSpill.join(", ") + ")" : ""}`,
        editorSpill.length === 0);

  await narrow.locator("#styleClose").click();

  await narrow.locator("#armTake").click();
  await narrow.close();
} catch (error) {
  check(`no exception (${error.message.split("\n")[0]})`, false);
} finally {
  check("the page reported nothing broken", complaints.length === 0);
  console.log(checks.join("\n"));
  complaints.slice(0, 10).forEach((c) => console.log(`     ${c}`));

  await browser.close();
  server.close();
}

console.log(process.exitCode ? "\nsmoke test FAILED" : "\nsmoke test passed");
