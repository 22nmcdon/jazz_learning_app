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

Since then the settings were restructured again, and `#menuPanel` gave three of
its six sections away: the engine chip became a dot (`74c984c`), *The band* took
a button of its own in the chart's row (`6d6ecaf`), and *Voicings* and *Scales*
took one between them (`776a616`), with the colour picker deleted rather than
moved. What is left under *Practice* is the machine - Sound, MIDI, About. The
rule that replaced "three menus is the ceiling" is in `CLAUDE.md`.

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

Since then, PDF and the open questions. `bd4fe12` vendored **pdf.js** into
`assets/` — it had been fetched from cdnjs on every visit with no Subresource
Integrity, and the blocker recorded here was a session that could not reach the
CDN to hash it; the npm registry is reachable and is what the CDN mirrors, so
the tarball was checked against npm's own `dist.integrity`. `810923e` hid the
**print button** in the app, where it had been calling `window.print()` into a
webview that opens no dialog. `ac597f4` made the app **read a PDF** with the
page's own reader, which is why `WebUi` writes a temp *folder* now rather than a
temp file. And five open questions were closed rather than left to be
re-derived — see *Decided, not deferred* below.

Since then, the last open question closed. `5b3b3f0` deleted the `brazilian`
and `quartal` reharmonisation vocabularies — counted, they were tagged on **no
rule** and **one rule** respectively, so the style menu that was about to be
wired up had an entry that was a lie. `Options::style` became an optional in
the same commit, because `common` was doing double duty as both a rule tag and
the "do not filter" value. `b4a5d8a` put the two surviving vocabularies on the
wire and gave the bar dialog a picker.

Since then, the voicing library. `71ac183` answered the question that kept it
unbuilt — **what a saved voicing is saved against** — as a chord *quality*,
stored as offsets from the root plus the register it sat in, so a shape found
over Dm7 comes back on every minor chord in every key. *Show me one* cycles
yours before the engine's. The saved-collection pattern was extracted in the
same commit, which is the "answer it once for both" this file asked for.

Since then, the last unbuilt feature. `fdd19e0` added `LineWriter` - a
generator whose notes carry `LineAnalyzer`'s own colours, held to it by a
round-trip test over twenty-four seeds - and `95fc0b3` put it on the dock
button. Two things the tests taught rather than confirmed are written up in
`docs/SOLO_PRACTICE.md`: the writer commits to a note's colour and not to the
gesture, and an approach note has to be outside the scale the take reads
against or the reading calls it a scale tone.

Verify anything you change:

```
./web/build.sh out && cp web/index.html web/sw.js out && node web/smoke-test.mjs out
./tools/test-count.sh --check
cmake --build build          # the page is copied into the JUCE shell
```

---

## Where the unbuilt features land

**Each one lands beside the question it answers**, which is the rule the panels
now follow rather than the old "everything is a section of `#menuPanel`". A
comping-style editor is off *The band* and a voicing library is off *Voicings* —
both of which are buttons in the chart's row now, not sections of the settings
panel. Practice history turned out to be a dialog of its own behind the top
bar's record button, which is the first thing that broke the old rule. What
lands under *Practice* is what is about the machine: a sound, a device, a
version.

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

## Not built

**There are no unbuilt features left** - what follows is one unexamined
symptom, and then the decisions. Everything else in this file is a loose end.

The last feature to go was **line suggestions** (`fdd19e0`, `95fc0b3`): solo
practice writes a line now as well as reading one, and the dock button says
*Show me a line* where it used to say *Which scale?*. What the design question
turned out to need is in `docs/SOLO_PRACTICE.md`.

- **Metronome jitter.** Not investigated. Every beat time is an origin plus a
  beat count at one spacing (see `CLAUDE.md`), so start by checking whether the
  jitter is in the scheduling or in the reporting.
**Decided, not deferred — do not re-open without being asked:**

- **Rule-based vs data-informed reharmonisation.** Rule-based. The dead style
  filter was collected (see the entry in *Where this is*), and what the counting
  turned up is the part worth keeping: two of the five vocabularies had one rule
  and none. The way back into this question is **more rules or better tags**,
  not a corpus — 36 rules that each explain themselves are worth more here than
  a ranking that cannot.

- **Ear training.** Not wanted. This was the one item that argued for a third
  mode, so `state.mode` stays two-valued with nothing pulling at it.
- **MusicXML / MuseScore import.** Not wanted. iReal Pro and PDF both ship and
  both read into the same engine.
- **Audio / pitch-detection input.** Out, for the reason now written into
  `CLAUDE.md`'s *Input Scope*: this is a pianist's app end to end and a
  keyboard already has MIDI. What would reopen it is a non-keyboard audience,
  not easier DSP.
- **A dense multi-column desktop layout.** No — and the evidence moved against
  it, since settings spread into five popovers rather than converging into one
  pinnable panel. See `CLAUDE.md`'s *Open Questions*.
- **Scoring rhythm in solo practice.** No: words, permanently. The criterion is
  in `CLAUDE.md`'s invariants and argued in `docs/SOLO_PRACTICE.md` — comping
  scores placement because the player chose a `CompStyleDefinition` off a menu,
  and a chart offers no comparable standard.

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
  *Personal voicing library* bullet asked. **Half of that is now done**:
  `savedCollection` in `web/index.html` is the shelf the tune library and the
  voicing library both sit on, and converting this one to it is mechanical.
  What is not mechanical is the UI - a shelf of styles wants a picker and
  somewhere to type a name, in a panel that is already seven controls deep and
  wanting two columns. That is the part to design before writing anything.

- **Nothing shares or exports a style.** A style in a link is a second wire
  format and a second "is this trustworthy" question, and the existing share
  link carries a *chart* in iReal Pro's format via the engine's own encoder -
  there is no slot in it for a figure. The reusable parts are the plumbing
  (`copyToClipboard`, the `?chart=` boot hook), not the format.

- **An abnormal exit leaves the app's page folder in the temp directory.**
  `~WebUi` deletes it on a clean shutdown, and a `SIGTERM` under test did not
  run that path - one folder survived every ten-run loop. This is not new
  behaviour (the single temp *file* had the same fate), but it is 1.8MB now
  rather than 440KB, so it is worth knowing. The name is unguessable and
  unique per launch - ten launches gave ten distinct names, checked - so this
  is litter rather than a hole. Cleaning stale ones at startup means scanning
  the temp directory and deleting folders this process does not own, which is
  a worse idea than the litter.

- **The app's PDF import is not in the smoke test, because nothing can click a
  native file picker.** It was verified with a temporary probe on both ends -
  an env var standing in for the `FileChooser`, and the page reporting the bar
  count over `jazz.log` - which is reverted. The page's *own* PDF path is
  covered. If the app's import breaks, no check here will say so.

- **WKWebView is the one webview nothing has tested.** `file://` behaviour for
  the vendored pdf.js was measured on WebKitGTK and on Chromium (which stands
  in for WebView2) and both read a PDF, by different routes - WebKitGTK builds
  a `Worker` and Chromium refuses to, and both end up on pdf.js's fake worker.
  macOS and iOS were not reachable from the session that did this. WKWebView is
  the strictest of the three about `file://` subresources, so it is the one to
  check first if the app cannot read a PDF on a Mac.

- **The PDF fixtures are generated, and prove transport rather than reading.**
  Both the smoke test's and the app probe's. `CLAUDE.md`'s rule stands - every
  import bug so far was one no invented fixture would have had - and reading is
  still only exercised against real exports in `ChartFormatsTests`. A real
  iReal Pro or MuseScore PDF export checked into the tests would be worth more
  than either.

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
  the stylesheet beside `.record-button`. Note also that 390px does **not**
  catch this: `.top-bar-right` is already four lines deep there, so measure with
  the control and without it, at a narrow width *and* at 1024.
  **What the restructure did about it was stop using the bar.** The engine chip
  became a dot, which bought back width, and then *The band* and *Voicings* went
  to `.chart-bar` instead - a row that already exists and costs the chart zero
  at every width. That row is where the next one should go too.

- **Nothing exports a practice record.** The same shape of gap as a comping
  style: it would be a second wire format and a second "is this trustworthy"
  question. The history text the engine already reads is a plausible basis, but
  it carries numeric tune ids that mean nothing outside the browser that wrote
  them.

- **The page's CSP allows `'unsafe-inline'`, and that is structural.** The page
  *is* one inline `<style>` and one inline `<script>` - the whole architecture
  is a single file the app embeds in its binary - so nonces would need
  rewriting at build time in two places to protect a document that is already
  trusted end to end. Worth revisiting only if the page ever stops being one
  file.

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
  it once the panel is open, and the band says it out loud when it is not.
  **The band has its own button again** (`6d6ecaf`), so if someone is ever
  surprised by a band they did not know was on, `#bandButton` is the place for
  the mark and `aria-pressed` is what it wore before — but do not add one
  speculatively. No other button on the page says anything about its contents,
  and one that did would be the odd one out until a second joined it.

- **Nothing else on the page is measured in two faces.** The cheat sheet's
  no-scroll checks and the 390px narrow scan are pixel assertions too, and both
  run against whichever face the machine happens to render. They pass today in
  both, but neither is *checked* in both — the strip's second pass is the only
  one. If either starts failing on CI and not locally, that is the first thing
  to suspect, and the `TALL_METRICS` block in `web/smoke-test.mjs` is the tool.

- **`actions/upload-pages-artifact@v3` and `actions/deploy-pages@v4`** both have
  a v5 out. Neither is in the Node 20 deprecation warning, so neither was bumped
  with `checkout` and `setup-emsdk` — but they will want doing eventually.

- **The Bluetooth pairing sheet is built and unreachable.**
  `MidiDeviceInput::showBluetoothPairingDialog()` has no caller - nothing in the
  page or the bridge opens it - but it is the only iOS/Android pairing entry
  point, and `app/CMakeLists.txt` still requests the Bluetooth permission for
  it. **Kept deliberately**: mobile is a real target and this is what it needs,
  so the gap is a missing bridge message rather than dead code. Whoever wires it
  up adds a `sound`-style message to `WebUi` and a control that only shows on a
  platform that has the sheet.

- **`VoicingCollector` and `NoteInputSource::inputName()` are exercised only by
  tests.** Grepped across `modules`, `app`, `web/src` and `tools`: no production
  caller for either. **Kept deliberately** - `README.md` sells the note-input
  abstraction as part of the engine's remit and the architecture anticipates an
  AUv3/VST3 target, so their shape is the point. `NoteInputSource` and
  `NoteInputListener` themselves are live (`MidiDeviceInput` is a source,
  `WebUi::DeviceRelay` a listener); it is these two that nothing outside the
  tests touches. Noted so the next audit does not re-derive it.

- **`assets/pdf.js-LICENSE` is not compiled into the app binary.**
  `app/CMakeLists.txt` lists five of the six files in `assets/`; the licence is
  the omission, while `web/build.sh` does ship it beside the page. So the
  desktop app carries 1.4MB of pdf.js with its licence left behind. A licensing
  judgement rather than a cleanup, which is why it was flagged and not fixed.

- **`pages.yml` pins a literal branch name** (`claude/gifted-wozniak-7z4jt6`) as
  its deploy trigger. Correct today - that is the default branch - but renaming
  the default would stop deploys with no error anywhere.

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
