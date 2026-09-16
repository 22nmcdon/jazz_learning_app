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

  await page.locator("#ioClose").click();

  // --- solo practice ------------------------------------------------------
  // The other half of the app: the same notes, read one at a time against the
  // bar they land in rather than together as a chord.
  const soloKey = (note) => page.locator(`#keyboard .key[data-note="${note}"]`);
  const readout = () => page.locator("#soloNote").innerText();

  await page.locator("#modeSolo").click();

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

  await soloKey(61).click();
  await page.waitForFunction(
    () => document.querySelector("#soloNote").dataset.colour === "outside", null, { timeout: 10000 });
  check("a note outside the scale is read as outside", true);

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
  }

  await page.locator("#dialogClose").click();

  // The style picker narrows the vocabulary, and its list comes from the engine.
  await page.locator("#menuButton").click();
  const styles = await page.locator("#scaleStyle option").count();
  check(`the engine's styles fill the menu (${styles})`, styles >= 5);

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

  const takeSummary = (await page.locator("#soloSummaryHead").innerText()).trim();
  check(`the take is summarised (${takeSummary})`, takeSummary.includes("over 2 bars"));
  check("and broken down by bar", (await page.locator("#soloBars li").count()) === 2);

  await page.locator("#modeChords").click();
  check("switching back restores chord practice", await page.locator("#feedback").isVisible());

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
