# Handoff

What is half-done, what is not started, and what is quietly wrong. `CLAUDE.md`
says what you need to know to not break the repo; this says what is left to do
in it.

Keep it honest: when a section here is finished, delete it rather than marking
it done. A handoff that accumulates is one nobody reads.

---

## Where this is

Branch: `claude/gifted-wozniak-7z4jt6`.

The page's layout was restructured over these commits, and that work is done:

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
| `d1efdb5` | The chart's tools out of the settings panel into a menu of their own above the music, and the chart justified into its zone instead of leaving a void under it. |
| `a693f3a` | The screenshots reshot against the finished page, in the typeface a visitor actually sees. |

Since then, two features off the list below have been built and their bullets
deleted: the **comping-style editor** (`9f2870b`…`7b8d3a9`) and **progress
tracking** (`f061863`…`65eeb67`). What each one's design question turned out to
need is in `docs/COMPING.md` and `docs/PROGRESS.md` respectively; what they left
undone is in *Loose ends* below.

Since then, two features off the *not built* list below were built: solo
practice reads a **chord in a line as a chord** (`9f2870b` - see
`docs/SOLO_PRACTICE.md`), and a comping style can be **written rather than
chosen** (`5fef784` through this one - see `docs/COMPING.md`). Neither is
layout work; they are here so a session can see what the branch has done
without reading eight commit messages.

Verify anything you change:

```
./web/build.sh out && cp web/index.html web/sw.js out && node web/smoke-test.mjs out
./tools/test-count.sh --check
cmake --build build          # the page is copied into the JUCE shell
```

---

## Where the unbuilt features land

Settled, and most of why the settings panel was worth merging: they are sections
of `#menuPanel`. A comping-style editor off *The band*, a voicing library off
*Voicings*, practice history and ear training as sections of their own. What
does **not** go there is anything about the tune on the stand or about a
particular take — those have menus of their own, and `CLAUDE.md` says three is
the ceiling.

## Reshooting the screenshots

`docs/screenshot-desktop.png`, `-solo.png` and `-compact.png` are shot from the
built page in a real browser, not cropped by hand, and each is in a state its
README caption actually claims: a shell Cm7 at C3 flagged below the
low-interval limit (bar 5, *Voicings* set to shell, keys 48/51/58); a solo take
running over two scored bars with an enclosure landing (arm, play over bars 1
and 2, then 63/61/62 back on bar 1); and the narrow layout at 430×860.

**The trap is the typeface.** The page renders in Jost over the web and in the
fallback stack without it, and a machine that cannot reach Google Fonts will
quietly photograph the wrong one — the same trap that shipped a broken
transport strip. Chromium here cannot reach `fonts.googleapis.com` directly
even though `curl` can, and pointing Playwright at `$HTTPS_PROXY` does not work
either: the proxy only takes HTTPS `CONNECT`, so the page's own plain-HTTP
localhost request is refused and the page never boots.

What does work: `curl` the CSS **with a browser user-agent** (or it returns TTF
rather than woff2), `curl` each `fonts.gstatic.com` URL out of it, then serve
both back through `page.route()` from disk. The page is untouched and still
asks for exactly what it asks for in production. Shoot at
`deviceScaleFactor: 2` — the files come to 98–136KB, against 65–93KB for the
old 1× ones.

## Two numbers that were judged rather than measured

Both are in `web/index.html`, both look like constants and are not:

- **`MAX_EXTRA_GAP` (28px)**, the cap on how far the gaps between systems grow
  when the chart is shorter than its zone. Chosen by eye against the default
  three-system chart at five window sizes. A six-system tune has five gaps and
  reaches the cap far sooner, so if a long chart ever looks thin at the bottom
  that is the number to revisit — not the order the slack is given away in.
- **The transport strip's `min-height` (29px)** and the five explicit
  line-heights that keep the controls under it. Those *are* measured, and the
  arithmetic is written beside each rule — but it is arithmetic about one set of
  paddings. Change a padding up there and the reservation has to be re-derived,
  which is what the per-group check is for.

And one standing cost, so nobody "fixes" it: each of the strip's two groups
reserves its row **empty**, so static chord practice — which shows neither the
metre nor the take button — spends 66px on the strip at a phone width where it
would otherwise spend 29. That is the price of the chart not moving when the
clock comes on, and `the transport strip keeps the chart still` fails the moment
someone takes it back.

---

## Not built, deliberately open

Each of these needs its design question answered before any code. They are not
ordered.

- **Personal voicing library.** Where does a saved voicing live, and what is it
  saved *against* — a chord symbol, a chord quality, or a bar of a particular
  tune? Storage is the easy half.
  **Half of this question now has an answer to copy.** The tune library saves a
  named thing under a numeric id, in one object through `rememberObject`, and
  drops any record it cannot read back rather than migrating it — and the
  practice record attaches to a tune *by that id* rather than by name, which is
  what survives a rename. A voicing library saved against a chord symbol would
  work the same way; saved against a bar of a tune, it already has the tune ids
  to hang off. What is still genuinely open is only the *against what*.
- **Ear training.** The one item that is arguably a third mode rather than a
  setting, so it collides with `state.mode` being two-valued. Read
  `docs/COMPING.md`'s argument about what a third mode costs before deciding.
- **MusicXML / MuseScore import.** Still an open question in `CLAUDE.md`: is it
  wanted at all, given iReal Pro and PDF both ship? A reader is shell-side work
  plus nothing in the engine.
- **PDF reading and printing in the JUCE app.** The engine's reader is shared and
  format-agnostic; the app simply lacks a PDF text-extraction library. This is a
  dependency decision, not a design one.
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

- **The comping-style editor does not edit a style's register.**
  `lowestNote`/`highestNote` cross the wire and go back unchanged, so a copy
  keeps whatever the style it came from had. The four ship with near-identical
  registers (45-76 to 48-81), so it is rarely the thing you want - but it is a
  real gap. Whoever adds it should make the control refuse a span narrower than
  `twoHandedReach`: `compingVoicing`'s anchor sweep runs `lowestNote <= anchor
  <= highestNote - 24`, so a narrower window yields no anchors and the band
  goes quiet rather than complaining.

- **One saved style, not a library.** The editor keeps exactly one style you
  wrote, under one key. A list of them is a *library*, and the library question
  - what a saved thing is saved against, and where it lives - is the one the
  *Personal voicing library* bullet above asks. It should be answered once, for
  both, rather than twice differently.

- **Nothing shares or exports a style.** A style in a link is a second wire
  format and a second "is this trustworthy" question, and the existing share
  link carries a *chart* in iReal Pro's format via the engine's own encoder -
  there is no slot in it for a figure. The reusable parts are the plumbing
  (`copyToClipboard`, the `?chart=` boot hook), not the format.

- **The practice record keeps the last 300 takes, and then forgets.** The
  oldest go first and the panel says so rather than pretending to be complete.
  300 is a year of practising most days; the reason for a cap at all is that
  this is somebody's browser and a store that only grows is one that fails to
  write at the worst moment. Nothing warns you as it approaches.

- **A take over an unsaved chart is practice against no tune.** It counts in
  the record - the minutes, the coverage, the movement - but joins no tune's
  memory, because there is no tune to join. Nothing tells you that at the time;
  the chart menu says which tune is on the stand, which is the nearest thing.
  Whether a take should be able to adopt a tune saved *afterwards* is a real
  question and deliberately unanswered.

- **The top bar has room for one more labelled control and does not have it
  spare.** Measured when the practice record needed a button: a labelled one
  costs 38px of chart at 1024px, and even three letters do. The numbers are in
  the stylesheet beside `.record-button`. This bears directly on the planned
  move of *The band* out of `#menuPanel` into a button of its own - something
  has to leave the bar first, and the engine-status chip is the obvious
  candidate. Note also that 390px does **not** catch this: `.top-bar-right` is
  already four lines deep there.

- **`#menuPanel` is mid-restructure, and its own comment is now out of date.**
  The markup at `.menu-wrap` still says practice history and ear training will
  land as sections of it. Practice history did not - it is a dialog behind its
  own button - and the panel is being cut back to things that are genuinely
  settings. That comment should be rewritten by whoever does the move rather
  than patched now.

- **Nothing exports a practice record.** The same shape of gap as a comping
  style: it would be a second wire format and a second "is this trustworthy"
  question. The history text the engine already reads is a plausible basis, but
  it carries numeric tune ids that mean nothing outside the browser that wrote
  them.

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
