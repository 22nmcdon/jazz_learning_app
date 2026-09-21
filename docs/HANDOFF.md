# Handoff

What is half-done, what is not started, and what is quietly wrong. `CLAUDE.md`
says what you need to know to not break the repo; this says what is left to do
in it.

Keep it honest: when a section here is finished, delete it rather than marking
it done. A handoff that accumulates is one nobody reads.

---

## Where this is

Branch: `claude/gifted-wozniak-7z4jt6`.

The last four commits restructured the page's layout:

| Commit | What it did |
|---|---|
| `a296503` | Three-zone frame: `body` is a flex column at `100dvh`, a new `.chart-zone` takes the remaining height and scrolls itself, `.dock` stopped being sticky. The rolling bar now brings itself into view — nothing in the page scrolled anything before. |
| `6bf5152` | Cut the masthead's prose and moved the chart's head into a one-row `.top-bar`. Bars readable went 4/12 → 12/12 at 1280×860 and **0/12 → 12/12** at 430×860. |
| `603ffd6` | Sectioned the cheat sheet into five tabs so it never has to be scrolled, and added six topics it never covered. |
| `c418613` | The transport onto a strip of its own — Static/In time, the metre, the tempo, the take button and the beat dots, all of them out of the Practice menu. What is left of the *Playing* group is one popover off the strip. |
| `c4c5eea` | Reserved the strip's height in **both** of the page's typefaces. The first version sized it against the fallback face alone and shipped a chart that moved when the clock came on. |
| `16dab04` | One settings panel: the comping panel and the chart's toolbar merged into `#menuPanel` in seven sections, and the colophon with them. |
| `200a44c` | The dock foot regrouped into what you press and what you read. |
| `f03c65f` | Practice settings remembered across reloads, through one pair of storage accessors. |
| *(this one)* | The chart's tools out of the settings panel into a menu of their own above the music, and the chart justified into its zone instead of leaving a void under it. |

Verify anything you change:

```
./web/build.sh out && cp web/index.html web/sw.js out && node web/smoke-test.mjs out
./tools/test-count.sh --check
cmake --build build          # the page is copied into the JUCE shell
```

---

## The layout work: commit 7

Six of seven are done. The last is the paperwork.

**Where the unbuilt features land is settled**: they are sections of
`#menuPanel`, which is what commit 4 was mostly for. A comping-style editor off
*The band*, a voicing library off *Voicings*, practice history and ear training
as sections of their own.

### 7 — Docs, screenshots and counts

**Reshoot `docs/screenshot-*.png`.** All three were taken on 2026-09-17 and
predate the entire restructure — the compact one shows the exact 0-of-12-bars
state the work fixed, and none of them has the transport strip. README carries a
note under each saying so; remove those notes when the images are replaced.

Then update `README.md`'s layout narrative for the finished shape, and run
`./tools/test-count.sh`.

### The second-breakpoint question — answered, and no new breakpoint

The transport strip did not fit one row at 390px and `flex-wrap` was enough: the
strip is two groups, one row each, and they share a line above 760px and take
one each below it. No new breakpoint — what the existing 760px query gained is
six lines of tightening (gaps, and the take button's letter-spacing), because
the wider group wanted 346px of the 342px a 390px phone offers.

**The tempo box is not one of them**, and it looks like it should be. Taking it
from 62px to 48 showed `12` for 120 — the last digit gone, nothing to say so.
A number input keeps room for its own spinner, so its content wants 56px of the
60 that 62 leaves it. `the tempo box shows the whole tempo at 390px` reads it at
both ends of the range now.

What that cost, and the thing to keep in mind for the strip's neighbours: each
group reserves its row **empty**, so static chord practice — which shows neither
the metre nor the take button — now spends 66px on the strip where it used to
spend 29. That is deliberate. The alternative is the chart moving when the clock
comes on, and `the transport strip keeps the chart still` fails if it does.

---

## Not built, deliberately open

Each of these needs its design question answered before any code. They are not
ordered.

- **Comping style customisation / an editor.** A `CompStyleDefinition` is slots
  on a grid; an editor is a grid you can click. The question is whether a
  user-made style is a first-class citizen of the engine's catalogue — which
  `CLAUDE.md`'s single-source-of-truth rule says the engine owns — or a shell-side
  overlay the engine never sees.
- **Personal voicing library.** Where does a saved voicing live, and what is it
  saved *against* — a chord symbol, a chord quality, or a bar of a particular
  tune? Storage is the easy half.
- **Progress tracking beyond per-take stats.** Needs a decision on what is worth
  keeping across sessions, and on whether the engine ever learns about history
  (today it has no memory at all, which is what keeps it testable).
- **Ear training.** The one item that is arguably a third mode rather than a
  setting, so it collides with `state.mode` being two-valued. Read
  `docs/COMPING.md`'s argument about what a third mode costs before deciding.
- **MusicXML / MuseScore import.** Still an open question in `CLAUDE.md`: is it
  wanted at all, given iReal Pro and PDF both ship? A reader is shell-side work
  plus nothing in the engine.
- **PDF reading and printing in the JUCE app.** The engine's reader is shared and
  format-agnostic; the app simply lacks a PDF text-extraction library. This is a
  dependency decision, not a design one.
- **Chordal (not line) reading in solo practice.** `LineAnalyzer` reads notes
  one at a time and already knows which were struck together; block-chord
  *reading* is a different question from the per-voice reading it does now.
- **Metronome jitter.** Not investigated. Every beat time is an origin plus a
  beat count at one spacing (see `CLAUDE.md`), so start by checking whether the
  jitter is in the scheduling or in the reporting.
- **Rule-based vs data-informed reharmonisation.** An open question in
  `CLAUDE.md`. The rules are deliberately kept separable and each carries its own
  explanation, so this stays answerable later.
- **Licks / line suggestions.** Named, never designed.

**Out of scope, needs an explicit decision to change:** audio / pitch-detection
input. `CLAUDE.md`'s Input Scope defers it deliberately — it is a much larger
DSP undertaking. Do not start it because a feature seems to want it.

---

## Loose ends

Small, verified, and none of them urgent.

- **`MAX_EXTRA_GAP` is 28px and was chosen by eye**, against the default
  twelve-bar chart at five window sizes. It is the one number in `layOutChart()`
  that is a judgement rather than a measurement. A tune of six systems has five
  gaps to fill and will reach the cap far sooner than the default three-system
  one does, so if a long chart ever looks thin at the bottom, that is the number
  to revisit — not the order the slack is given away in.

- **One unexplained smoke-test timeout.** A single run failed with
  `page.waitForFunction: Timeout 15000ms` while the chart menu was half-wired;
  four runs since have been clean and CI has been clean. Noted rather than
  chased. If it comes back, the 15s waits are the share-link exports and the
  rolling-bar wait, and the thing to suspect first is `layOutChart()` running
  during a take.

- **Count-in, the tempo ramp and *Reharmonise as you play* are not
  remembered.** They were left out of commit 6 on purpose — the handoff's
  agreed list did not include them, and they read as setup for one particular
  exercise rather than as preferences. The loop range is the awkward one on
  that line: it is just as per-exercise and it *is* remembered. If someone
  wants the ramp back across reloads, the accessors are there and it is four
  lines; the question to answer first is whether the whole *Take setup* popover
  should persist as a unit.

- **The band lost its at-a-glance mark.** The old *Comping* button wore
  `aria-pressed` when any of the three players was on, so you could see the band
  was playing without opening anything. Merging the panels deleted the button
  and `updateCompingButton()` with it. Nothing replaced it: the checkboxes say
  it once the panel is open, and the band says it out loud when it is not. If
  someone is ever surprised by a band they did not know was on, the answer is a
  mark on the **Practice** button — but do not add one speculatively, it would
  be the only thing that button says about its contents.

- **Nothing else on the page is measured in two faces.** The cheat sheet's
  no-scroll checks and the 390px narrow scan are pixel assertions too, and both
  run against whichever face the machine happens to render. They pass today in
  both, but neither is *checked* in both — the strip's second pass is the only
  one. If either starts failing on CI and not locally, that is the first thing
  to suspect, and the `TALL_METRICS` block in `web/smoke-test.mjs` is the tool.

- **`actions/upload-pages-artifact@v3` and `actions/deploy-pages@v4`** both have
  a v5 out. Neither is in the Node 20 deprecation warning, so neither was bumped
  with `checkout` and `setup-emsdk` — but they will want doing eventually.

- **`app/src/ElectricPiano.cpp:205`** — `const auto frequency = frequencyOf
  (midiNote);` is declared and never used in that function. A
  `-Wunused-variable` warning. Pre-existing.

- **`web/smoke-test.mjs:775`** — the check *"the clock moves the chart on its
  own"* asserts `#systems .bar:nth-child(2)` is rolling. A `.system`'s first
  child is `.system-number`, so `.bar:nth-child(2)` is the **first** bar of each
  system — the check asserts bar one is rolling, which is where the take starts.
  It is vacuous. Assert a later bar instead.

- **`web/smoke-test.mjs:748`** — `selectOption("#loopTo", "1")` (and again at
  845). The options
  carry value `0..11` and label `1..12`, so a bare string is ambiguous. A
  previous session verified it resolves to bar index 0, which makes a **one**-bar
  loop while the comment at L779 says the loop is two bars long. **Re-confirm
  before changing anything** — the two readings differ only in which of value or
  label Playwright tries first. Then use the unambiguous `{ index: n }` form that
  L1804 already uses.

- **The printed chart keeps a faint ring on the selected bar.** The print rules
  clear `.bar`'s background but not the `box-shadow` on `[aria-pressed="true"]`.
  Pre-existing and cosmetic.

---

## What the smoke test already encodes

Two sets of checks were added with the layout work, and they replace the
throwaway probes used to build it — so the numbers quoted in those commit
messages are reproducible without writing measuring scripts again.

- **The frame** — that the page itself does not scroll while the chart does,
  that the dock never covers a bar, and that the rolling bar keeps itself in
  view through a chorus. The last of these was checked with a negative control:
  with the `scrollIntoView` removed it fails and names the bar it lost.
- **The cheat sheet** — every section, in both modes, at a desktop and a phone
  size, reports no overflow. The body keeps `overflow-y: auto` as a safety valve
  for a browser with larger text; what the check asserts is that it never has to
  use it.

The narrow-layout scan at the end of the file is the safety net for all of this.
It scans `.top-bar`, `.top-bar-right`, `.transport-clock`, `.transport-take`,
`.chart-bar`, `.dock-actions`, `.dock-status` and `.feedback` at 390px. **Anything that moves
a control into a new container must add that container to the scan**, or the net
has a hole in it.

To prove the scan reaches a container, give one of its children `min-width`, not
`width`. Every container it scans is a flex row, so a `width: 900px` child is
simply shrunk back to fit and the deliberately-broken control is not broken at
all — which reads exactly like a hole in the scan and is not one. This cost a
round of head-scratching; `CLAUDE.md`'s note about a 900px control predates the
containers being flex.

- **The chart fills its zone** — at a size with room to spare the gaps have
  grown and the sheet reaches the dock; at a size without, the gaps are back to
  their base and the zone still scrolls, which is the half that says the
  justification knows when to do nothing. And a one-system tune is centred,
  which the gaps cannot do for it. That last one needs both halves asserted:
  even margins alone pass on a chart that was never touched, because the
  sheet's own padding is symmetric to begin with — it was written that way
  first and the negative control caught it.
- **What a reload keeps** — the settings coming back, *and* the chart and the
  take not coming back with them, which is the half a working-looking bug would
  hide. In its own browser context, because that is what owns the store. The
  loop-no-longer-fits case is reached through a shared link rather than by
  editing the chart, since the chart is deliberately not remembered and
  reloading always brings the twelve bars back — a four-bar tune in the address
  is the only way a different chart is on screen when the settings are read.
  Beside it, that a browser refusing `localStorage` outright still boots: every
  accessor is wrapped, and now that they are one pair a missing `try` would be
  silent everywhere at once.
- **The settings panel** — that the editor and *Restore original* still work
  now that they are buttons inside a panel rather than on a toolbar, and that
  each closes the panel it was pressed from. Three of the four chart tools had
  no check at all before they moved. Also that every panel opens onto the screen
  at 390px, **and** that nothing inside one is wider than it is. The second half is the
  one with teeth: a panel's own box can land perfectly while a row of four chart
  tools or a long select option hangs off its edge, and the narrow scan cannot
  see inside a panel that is shut while it runs.
- **The transport strip** — that the chart's top edge is the same number in
  every corner of mode × In time, at a laptop size and a phone one, **and in two
  typefaces**. The second face is the one that matters: it shipped broken past
  the first version of this check, because the reservation is a pixel count and
  was sized on a machine that could not reach Google Fonts. The taller face is
  forced with `ascent-override`/`descent-override` rather than by fetching a
  webfont, so it needs no network — a check that quietly passes when a CDN is
  unreachable is what let it through. Beside it, that each group really is the
  29px it reserves, which is the line that names the cause rather than the
  consequence. Beside that, that the tempo box is not narrower than the number
  in it — the one kind of overflow the narrow scan cannot see, since a box too
  small for its contents is the right width as far as its own rectangle is
  concerned.
