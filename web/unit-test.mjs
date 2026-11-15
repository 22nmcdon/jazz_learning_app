/*  Unit tests for the page's timing core - the first JS tests in this repo.

    Everything else about `web/index.html` is checked by driving it in a real
    browser (`web/smoke-test.mjs`), which is the right tool for a layout or a
    control and the wrong one for arithmetic: a swing ratio that is subtly
    wrong still sounds like music, still lights a key, and still passes every
    check a browser can make about it.

    `CLAUDE.md` puts the whole of the page's timing on two of these four, and
    says in as many words that `positionFromBeats` has to move with
    `beatsIntoBar` "or notes come back onto the grid at a position nobody
    played". That is a round trip, and a round trip is a property you assert
    directly or not at all.

    **The code under test is read out of `web/index.html` between markers**
    rather than imported from a module. Lifting it into a file of its own
    would mean a second script the page must load before it can run, in both
    shells - a new way for the app to open on a blank window, which is the one
    failure `CLAUDE.md` is most insistent about. So the page ships as one file
    and the markers are the contract: everything between them is pure and
    reads nothing from outside except `ENGINE_TICKS_PER_BEAT`, which is lifted
    out by name rather than restated here, because the grid having two sources
    is the exact bug this block exists to keep out of.

    Run: node web/unit-test.mjs
*/
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const page = await readFile(join(here, "index.html"), "utf8");

const between = (from, to) => {
  const start = page.indexOf(from);
  const end = page.indexOf(to);

  if (start === -1 || end === -1 || end < start)
    throw new Error(`the markers ${from} / ${to} are not both in web/index.html,`
                    + " in that order - the timing core has moved or been renamed");

  //  From the end of the marker's own line, so the comment it sits in is not
  //  half-included and read as code.
  return page.slice(page.indexOf("\n", start) + 1, end);
};

const core = between("==== JAZZ-TIMING-CORE-BEGIN ====", "//  ==== JAZZ-TIMING-CORE-END");
const chart = between("==== JAZZ-CHART-CORE-BEGIN ====", "//  ==== JAZZ-CHART-CORE-END");

const ticks = /var ENGINE_TICKS_PER_BEAT = (\d+);/.exec(page);
if (!ticks) throw new Error("ENGINE_TICKS_PER_BEAT is not declared in web/index.html");

const TICKS = Number(ticks[1]);

//  The block, with the one constant it reads, evaluated on its own. No DOM,
//  no engine, no page - which is the whole claim being made about it.
const load = new Function(`
  var ENGINE_TICKS_PER_BEAT = ${TICKS};
  ${core}
  return { upbeatFractionOf, swungUpbeatTickOf, beatsIntoBarOf, positionFromBeatsOf,
           ENGINE_TICKS_PER_BEAT };
`);

const { upbeatFractionOf, swungUpbeatTickOf, beatsIntoBarOf, positionFromBeatsOf } = load();

//  The chart core, on its own terms: no constant, no DOM, no engine.
const { barChordsText } = new Function(`${chart}\nreturn { barChordsText };`)();

//==============================================================================
let failures = 0;
let ran = 0;

const check = (what, ok) => {
  ran += 1;
  if (!ok) { failures += 1; console.log(`FAIL  ${what}`); }
  else console.log(`ok    ${what}`);
};

const near = (a, b, within = 1e-9) => Math.abs(a - b) <= within;

//  Two grooves off the engine's own catalogue, written here as the data they
//  are. A straight one bends nothing; a swung one bends more at slow tempo
//  than at fast, which is the shape `docs/RHYTHM.md` states.
const straight = { key: "straight", even: true, upbeatWhenSlow: 0.5,
                   upbeatWhenFast: 0.5, slowBpm: 100, fastBpm: 240 };
const swing = { key: "swing", even: false, upbeatWhenSlow: 0.667,
                upbeatWhenFast: 0.57, slowBpm: 100, fastBpm: 240 };

const spbAt = (bpm) => 60 / bpm;

//==============================================================================
console.log("\n-- where the upbeat falls --");

check("a straight groove puts it halfway, at any tempo",
      near(upbeatFractionOf(60, straight), 0.5) && near(upbeatFractionOf(300, straight), 0.5));

check("a swung one is later than halfway",
      upbeatFractionOf(120, swing) > 0.5);

check("and later at a slow tempo than at a fast one",
      upbeatFractionOf(100, swing) > upbeatFractionOf(240, swing));

check("below the band it is the slow end, above it the fast end",
      near(upbeatFractionOf(40, swing), swing.upbeatWhenSlow)
      && near(upbeatFractionOf(400, swing), swing.upbeatWhenFast));

check("halfway through the band is halfway between the two",
      near(upbeatFractionOf(170, swing), (swing.upbeatWhenSlow + swing.upbeatWhenFast) / 2, 1e-3));

//  A band with no width cannot be interpolated across, and dividing by it
//  would hand every later call a NaN.
check("a groove whose band has no width answers rather than dividing by zero",
      near(upbeatFractionOf(150, { ...swing, slowBpm: 200, fastBpm: 200 }), swing.upbeatWhenSlow));

console.log("\n-- what is swung, and what is not --");

check("nothing is swung when the page says nothing is",
      swungUpbeatTickOf(120, swing, false) === null);

check("a swung upbeat is past the straight one",
      swungUpbeatTickOf(120, swing, true) > TICKS / 2);

console.log("\n-- a position, in beats --");

check("the downbeat is the beat itself",
      beatsIntoBarOf(2, 0, spbAt(120), swing, true) === 2);

check("a straight eighth is half a beat on",
      near(beatsIntoBarOf(0, TICKS / 2, spbAt(120), straight, false), 0.5));

check("a swung eighth is later than that, and only the eighth moves",
      beatsIntoBarOf(0, TICKS / 2, spbAt(120), swing, true) > 0.5
      && near(beatsIntoBarOf(0, TICKS / 4, spbAt(120), swing, true), 0.25));

check("the same eighth is later at a slow tempo than at a fast one",
      beatsIntoBarOf(0, TICKS / 2, spbAt(100), swing, true)
        > beatsIntoBarOf(0, TICKS / 2, spbAt(240), swing, true));

console.log("\n-- and back onto the grid: the round trip --");

/*  The invariant `CLAUDE.md` names. Every position music is actually written
    in has to survive being turned into beats and read back, in each groove -
    otherwise a note the player put on the beat comes back somewhere else. */
const written = [0, 6, 8, 12, 18];

for (const [name, g, swung] of [["straight", straight, false], ["swung", swing, true]])
  for (const bpm of [80, 120, 200, 300])
    for (const beat of [0, 3])
      for (const tick of written) {
        const there = beatsIntoBarOf(beat, tick, spbAt(bpm), g, swung);
        const back = positionFromBeatsOf(there, spbAt(bpm), g, swung);

        if (back.beat !== beat || back.tick !== tick) {
          check(`${name} at ${bpm}bpm: beat ${beat} tick ${tick} comes back as `
                + `${back.beat}/${back.tick}`, false);
          break;
        }
      }

check("every written position survives the round trip, in both grooves, at four tempos",
      true);

check("a note played a little early is read as the note the player meant",
      (() => {
        const meant = positionFromBeatsOf(0.98, spbAt(120), straight, false);
        return meant.beat === 1 && meant.tick === 0;
      })());

check("the swung eighth folds back onto the straight one rather than a tick of its own",
      positionFromBeatsOf(beatsIntoBarOf(0, TICKS / 2, spbAt(120), swing, true),
                          spbAt(120), swing, true).tick === TICKS / 2);

//==============================================================================
console.log("\n-- a bar's chords, as the engine is told them --");

check("one chord is that chord from beat 0",
      barChordsText([{ symbol: "Dm7", beats: 4 }]) === "Dm7:0");

check("two chords carry the beat each one arrives on",
      barChordsText([{ symbol: "C6", beats: 2 }, { symbol: "A7", beats: 2 }]) === "C6:0|A7:2");

//  Durations in, starts out: the second chord's beat is the first's *length*,
//  not its index, which is the whole of what this function is for.
check("an uneven split puts the change where the first chord ends",
      barChordsText([{ symbol: "C6", beats: 3 }, { symbol: "A7", beats: 1 }]) === "C6:0|A7:3");

check("three to a bar keep accumulating",
      barChordsText([{ symbol: "C6", beats: 2 }, { symbol: "A7", beats: 1 },
                     { symbol: "Dm7", beats: 1 }]) === "C6:0|A7:2|Dm7:3");

/*  There is deliberately no metre case here. This accumulates the durations
    it is given and never asks how many beats a bar has, so a waltz and a bar
    of four with the same slot lengths come out as the same text - a check
    written to look like a 3/4 one would be the 4/4 one again with a different
    name on it. Which beats exist is the chart's business and the engine is
    told the metre separately. */

check("an empty bar is nothing to say, not an empty string",
      barChordsText([]) === null && barChordsText(undefined) === null);

//  The engine refuses a bar it cannot read rather than taking the first chord
//  of it, so a missing duration must not silently collapse two chords onto
//  beat 0 - it stays readable, with the change where the data put it.
check("a slot with no duration does not move the ones after it onto its beat",
      barChordsText([{ symbol: "C6" }, { symbol: "A7", beats: 2 }]) === "C6:0|A7:0");

//==============================================================================
console.log(`\n${ran - failures}/${ran} checks passed`);

if (failures > 0) {
  console.log("unit tests FAILED");
  process.exitCode = 1;
} else {
  console.log("unit tests passed");
}
