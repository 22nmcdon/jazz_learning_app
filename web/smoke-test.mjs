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

  Ctor.prototype.createOscillator = function () {
    const osc = realCreate.call(this);
    const realStart = osc.start.bind(osc);

    osc.start = function (when) {
      window.__sounded.push({ type: osc.type, hz: osc.frequency.value, when });
      return realStart(when);
    };

    return osc;
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
  await page.locator("#menuButton").click();
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
    await third.close();
  }

  await page.locator("#ioClose").click();

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

  // Where everything is before the switch. The masthead and the hint above the
  // chart are written twice, once per mode, and the two wordings are different
  // lengths - so this is the check that the chart does not jump up or down a
  // line when the mode changes.
  const layout = () => page.evaluate(() => {
    const top = (sel) => Math.round(document.querySelector(sel).getBoundingClientRect().top);
    return { toggle: top(".mode-switch"), sheet: top(".sheet"),
             hint: top(".sheet-hint:not([hidden])"), systems: top("#systems") };
  });
  const chordsLayout = await layout();
  const chordsTitle = await page.title();

  await page.locator("#modeSolo").click();

  // Arriving at solo practice for the first time is a first visit of its own,
  // and what opens is the solo half of the sheet - the chord half explains a
  // page that is not on screen.
  await page.waitForSelector("#helpDialog[open]", { timeout: 10000 });
  check("the first visit to solo practice gets its own cheat sheet",
        (await page.locator(".help-list[data-mode='solo']").isVisible())
        && (await page.locator(".help-list[data-mode='chords']").isHidden()));
  check(`the ? says which mode it explains (${chordsHelp} / `
        + `${await page.locator("#helpButton").getAttribute("aria-label")})`,
        chordsHelp === "How chord practice works"
        && (await page.locator("#helpButton").getAttribute("aria-label")) === "How solo practice works");
  await page.locator("#helpClose").click();

  // The light changes with the mode. Read after the dialog is out of the way,
  // so the 280ms fade has long finished and this is the settled colour.
  check("the page changes colour with the mode",
        (await page.evaluate(() => getComputedStyle(document.body).backgroundColor)) !== chordsPaper);

  check(`the masthead names the mode (${chordsTitle} / ${await page.title()})`,
        chordsTitle === "Jazz Learning App: Chords"
        && (await page.title()) === "Jazz Learning App: Solo"
        && (await page.locator(".masthead h1").innerText()).includes("Solo"));

  const soloLayout = await layout();
  check(`nothing moves when the mode changes (${JSON.stringify(soloLayout)})`,
        JSON.stringify(soloLayout) === JSON.stringify(chordsLayout));

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

  // In time puts a clock behind the chart. Driven fast and looped over two
  // bars, so the test watches a real roll rather than a stubbed one.
  await page.locator("#playLive").click();
  check("choosing in time reveals what a clock needs",
        (await page.locator("#countInRow").isVisible())
        && (await page.locator("#loopRow").isVisible()));

  // The tempo and the metre are at the head of the chart, not in the menu, so
  // they are readable while playing rather than behind a button.
  check("the tempo mark is at the head of the chart",
        (await page.locator("#metreRow").isVisible())
        && (await page.locator(".sheet-head #tempo").count()) === 1
        && (await page.locator(".sheet-head #timeSig").count()) === 1);

  // A loop nobody has chosen is the whole tune, not bar one to bar one.
  check(`the loop starts as the whole chart `
        + `(${await page.locator("#loopFrom").inputValue()}-${await page.locator("#loopTo").inputValue()})`,
        (await page.locator("#loopFrom").inputValue()) === "0"
        && (await page.locator("#loopTo").inputValue()) === "11");

  // Count in first, which is the default: the dots count and the chart waits.
  // One button does both jobs now - starting a take is what sets it rolling.
  await page.locator("#menuButton").click();
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

  await page.locator("#menuButton").click();
  await page.uncheck("#countIn");
  await page.selectOption("#loopFrom", "0");
  await page.selectOption("#loopTo", "1");
  await page.locator("#menuButton").click();

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

  const forgetSounds = () => page.evaluate(() => { window.__sounded = []; });

  /*  Two things sit between this and a bar: the comping panel, which is drawn
      over the chart it hangs under, and the bar's own dialog, which a second
      click on the bar already selected opens. Close both, or everything after
      this is clicking on something else. */
  const goToBar = async (n) => {
    if (!(await page.locator("#compingPanel").isHidden()))
      await page.locator("#compingButton").click();

    await bars.nth(n).click();

    if (await page.locator("#chordDialog[open]").count())
      await page.locator("#dialogClose").click();
  };

  await page.locator("#compingButton").click();
  check("comping offers a band, two of whom are not built yet",
        (await page.locator("#compingPanel").isVisible())
        && (await page.locator("#compingPanel input:disabled").count()) === 2);

  // One registry of sounds, not a second copy of the list.
  const compSounds = await page.locator("#compSound option").allInnerTexts();
  const menuSounds = await page.locator("input[name=soundBank]").evaluateAll(
    (radios) => radios.map((r) => r.parentElement.textContent.trim()));

  check(`the band is offered the same sounds as the player (${compSounds.join(", ")})`,
        compSounds.length === menuSounds.length
        && compSounds.every((name, i) => name === menuSounds[i]));

  // On bar one first: clicking a bar closes whichever panel is open, so the
  // toggle has to be the last thing touched before the sound is read.
  await page.locator("#compingButton").click();
  await goToBar(0);
  await page.locator("#compingButton").click();
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

  await page.locator("#compingButton").click();
  await page.selectOption("#compSound", "silent");
  await forgetSounds();
  await goToBar(2);
  await page.waitForTimeout(600);
  check("a band set to silent is silent", (await compedVoicing()).length === 0);

  // --- comping styles, and the rhythm they actually play ------------------
  // The generator's output only means something if it reaches the speakers, so
  // these read the times the page scheduled rather than the plan it was given.

  await page.locator("#compingButton").click();

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
  await page.locator("#compingButton").click();
  await page.locator("#menuButton").click();
  await page.uncheck("#countIn");
  await page.selectOption("#loopFrom", { value: "0" });
  await page.selectOption("#loopTo", { value: "3" });
  await page.locator("#menuButton").click();
  await page.fill("#tempo", "240");
  await page.dispatchEvent("#tempo", "change");
  await page.evaluate(() => document.activeElement.blur());

  /** Rolls a take in one comping style and returns when each chord was struck,
      in beats from the first click. */
  const compRhythmOf = async (style) => {
    await page.locator("#compingButton").click();
    await page.selectOption("#compStyle", style);
    await page.locator("#compPiano").check();
    await page.locator("#compingButton").click();

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

  const fourToTheBar = await compRhythmOf("four");
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

  await page.locator("#compingButton").click();
  await page.locator("#compPiano").uncheck();
  await page.locator("#compingButton").click();

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
  await page.locator("#compingButton").click();
  await page.selectOption("#compSound", "ep");
  await page.locator("#compPiano").uncheck();
  await goToBar(0);

  // Back to static for the checks that follow, which click bars themselves.
  await page.locator("#menuButton").click();
  await page.locator("#playStatic").click();
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
  check("and chord practice has no band to comp for it",
        !(await page.locator("#compingButton").isVisible()));
  check("and the chart stops carrying the take's marks",
        (await page.locator("#systems .bar .bar-take:not([hidden])").count()) === 0);

  // The colophon names the build, which is how anyone looking at the site can
  // tell whether it is serving what was pushed. "development" is the right
  // answer for a copy that was not deployed, so this only asks that it says
  // something - the workflow itself checks the stamp was replaced.
  const build = (await page.locator("#colophonBuild").innerText()).trim();
  check(`the page names its build (${build})`, build.startsWith("Build:"));

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

      for (const element of document.querySelectorAll(".masthead-controls > *, .dock-foot > *")) {
        const box = element.getBoundingClientRect();

        if (box.width > 0 && (box.right > window.innerWidth + 0.5 || box.left < -0.5))
          offscreen.push(element.id || element.className);
      }

      return { page: document.documentElement.scrollWidth - window.innerWidth, offscreen };
    });

    check(`nothing runs off the side at 390px (${mode.replace("mode", "")})`,
          spill.page <= 0 && spill.offscreen.length === 0);
  }

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
