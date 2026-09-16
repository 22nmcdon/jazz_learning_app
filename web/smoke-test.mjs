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

  const scale = (await page.locator("#scaleName").innerText()).trim();
  check(`a scale is named (${scale || "nothing"})`, scale.length > 0);

  const degrees = await page.locator("#scaleNotes li").count();
  check("the scale has its notes", degrees >= 7);

  const alternatives = await page.locator("#scaleRows li").count();
  check("alternative scales are offered", alternatives > 0);

  // Switching tabs is hiding one panel and showing another, which the page does
  // with the hidden attribute alone. Worth checking: a class that sets display
  // quietly overrides it, and that is how the share row stayed visible in the
  // app for a while.
  await page.locator("#tabReharm").click();
  check("the scales panel goes away", await page.locator("#panelScales").isHidden());
  check("substitutions are offered", (await page.locator("#subs details.family").count()) > 0);

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

  // Offline: the worker has to be registered and awake, or the page is no
  // more use on a train than it was before.
  const worker = await page.evaluate(async () => {
    const registration = await navigator.serviceWorker.getRegistration();
    return registration ? String(registration.active && registration.active.state) : "none";
  });
  check(`the offline worker is running (${worker})`, worker === "activated");

  // And it has to actually serve the page with the network gone. A worker
  // that registers but caches nothing is the failure this catches.
  const offline = await browser.newContext();
  const revisit = await offline.newPage();
  await revisit.goto(`${origin}/index.html`, { waitUntil: "load" });
  await revisit.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });
  await offline.setOffline(true);
  await revisit.reload({ waitUntil: "load" });
  await revisit.waitForSelector("#engineStatus[data-state='ready']", { timeout: 60000 });
  check("the page still starts with the network gone", true);
  await offline.close();
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
