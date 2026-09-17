# Branding

How this app looks, and why — enough of it to build a new screen, a landing page or a
second surface that a player would recognise as the same product.

The look is not decoration bolted on afterwards. It is a lead sheet: cream paper, printed
heads, hand-lettered chords, and a few pigments that each mean exactly one thing. Every
rule below either serves that, or serves the fact that the page has to stay readable on a
phone at arm's length while someone's hands are on a keyboard.

The palette and type follow the Milo McDonald portfolio — cream paper, charcoal ink, blush
and gold, Playfair / Cormorant / Jost. That is the family resemblance to keep.

---

## 1. The idea in one line

**A Real Book page that answers back.** Printed where it is telling you something fixed
(the title, the metre, the credit), handwritten where a player would have written it (the
chords), and quiet everywhere else so that the five colours that carry meaning are the
loudest things on the page.

## 2. Colour

### Chord practice — the default light

| Token | Value | What it is |
|---|---|---|
| `--cream` | `#faf6f0` | The page. Warm, not white. |
| `--cream-2` | `#f2ebe1` | The dock, and anything sitting *under* the page. |
| `--paper` | `#fffcf7` | The lead sheet itself, and every input. One stop brighter than the page, which is what makes it read as a sheet laid on a desk. |
| `--charcoal` | `#241f1d` | Ink. Headings, chords, barlines, filled buttons. Never pure black. |
| `--text` | `#3a332f` | Body copy. |
| `--text-soft` | `#6b615a` | Secondary copy, labels, hints. |
| `--line` | `#e4d9cc` | Every border and rule on the page. |
| `--blush` | `#d9a6a0` | The accent at rest — hover borders, underlines. |
| `--blush-deep` | `#c07f79` | The accent doing work — focus rings, active menus, links. |
| `--gold` | `#b08d57` | Ready, counting in, scale tone. |

### Solo practice — the same page, a different light

`body.soloing` redefines the tokens; it does not restyle components. Paper drops a stop
and goes cooler, and the rose accent becomes slate. Nothing is named in that block but
colours, which is the point — the whole page changes and no component knows.

| Token | Solo value |
|---|---|
| `--cream` | `#eeedea` |
| `--cream-2` | `#e1e0da` |
| `--paper` | `#fafaf7` |
| `--charcoal` | `#20202a` |
| `--line` | `#d7d5cd` |
| `--text` | `#343139` |
| `--text-soft` | `#656069` |
| `--blush` | `#99a1bb` |
| `--blush-deep` | `#5c6488` |

The flip is a change of light, so it fades rather than snaps: `280ms ease` on
background, border and colour, for the surfaces only. Keys are deliberately left out —
they answer a finger, and a key that took 280ms to look pressed would be worse at the job
it does every second. `prefers-reduced-motion` turns all of it off.

### The five that never move

These say the same thing in both modes, so they sit outside the mode palette and the mode
accent is chosen to stay clear of all of them.

| Token | Value | Means |
|---|---|---|
| `--sage` | `#6f7f63` | Chord tone. Also "this matches". |
| `--gold` | `#b08d57` | Scale tone. Also a suggestion. |
| `--approach` | `#8fae6d` | Approach note. Green, not a lightened rust — an approach note is the line working, and a colour on the way to "did not land" reads as a near miss. |
| `--enclosure` | `#3f8f88` | Enclosure. The only cool colour of the five, so it cannot be mistaken for either green. Earns its own colour where one note's story is told; shares the approach tier on the bar strip, where tiers are counted. |
| `--rust` | `#a4553f` | Outside. Also armed, also a problem. |
| `--unresolved` | `#b6afa4` | Open, undecided. The quietest of the set on purpose: nothing has been decided, so it must not look like a verdict arriving early. |

### The wash

`--wash` / `--wash-strong` are the accent laid over a bar — hover, then selection. They
exist as tokens rather than as an `rgba()` spelled out at the call site precisely because
the accent moves with the mode and a literal `rgba(...)` would not follow it.

**Rule: a colour written as `rgba(...)` is a colour that will not follow the mode.** If it
needs to, it is a token.

## 3. Type

| Token | Stack | Used for |
|---|---|---|
| `--serif` | `"Playfair Display", Georgia, serif` | Headings, verdicts, sheet title, accidentals. Weight 600, `letter-spacing: 0.01em`. |
| `--script` | `"Cormorant Garamond", Georgia, serif` | Italic only: the lede under the masthead, the composer credit. |
| `--sans` | `"Jost", ui-sans-serif, system-ui, sans-serif` | Everything else. Body is **300** at 16px / 1.65. |
| `--hand` | `"Kalam", "Bradley Hand", cursive` | Chord symbols, and only chord symbols. Weight 700. |
| `--mono` | `ui-monospace, Menlo, Consolas` | Paths and build stamps in the colophon. |

Two type mannerisms carry most of the character:

- **The eyebrow.** 11px, uppercase, `letter-spacing: 0.28em`, `--blush-deep`, weight 500.
  It sits above a heading or labels a field.
- **The small caps control.** 10.5px, uppercase, `letter-spacing: 0.14em–0.18em`, weight
  500. Every button, pill and status chip is set this way. Nothing on this page shouts in
  large type; it whispers in wide-tracked small type instead.

Chord symbols have their own rules: hand face at `clamp(19px, 2.6vw, 28px)`, extensions at
`0.74em` raised `0.42em`, and accidentals borrowed from the serif at `0.82em`, because
handwriting faces have no flat or sharp glyph.

**Loading fonts: never render-blocking.** The webfont `<link>` carries `data-href`, not
`href`, and is promoted by script only when the page is served over the web. A webview with
no route out that is waiting on a stylesheet paints no text at all, and an invisible
interface is far worse than one in the fallback faces. A `<link rel="stylesheet">` with an
empty `href` is not a safe parking spot either — it counts as a stylesheet still on its way
and stalls every script after it.

## 4. Shape, edge and depth

There are exactly three radii, and each one means something:

- **`1px` — paper.** Inputs, selects, filled buttons. Effectively square; a sheet of paper
  does not have rounded corners.
- **`30px` (pill) — a mode or a state.** The status chip, the mode switch, the menu button,
  the arm button. Something you are *in*, not something you *do*. Small chips use `20px`.
- **`50%` — a dot.** Beats, the armed light, the legend, the help `?`.

Borders are `1px solid var(--line)` almost everywhere. The two exceptions both mean
something: a barline is `2px solid var(--charcoal)`, and a finding is a `2px` left rule in
the colour of its kind (sage / gold / rust).

Depth is used once. The lead sheet gets
`box-shadow: 0 1px 0 var(--cream-2), 0 14px 34px rgba(36, 31, 29, 0.06)` — a sheet on a
desk. Nothing else floats. State that needs to be visible from across the room is an
**inset 3px top rule** instead: `.bar.rolling` and `.dock.armed` both take
`inset 0 3px 0 var(--rust)`.

Buttons come in three weights:

- **Filled** (`.play-btn`, `.file-btn`) — charcoal on cream, 1px radius. Hover goes
  `--blush-deep`. The primary action of a section, and there is one per section at most.
- **Outlined pill** (`.menu-button`, `.arm-btn`, `.mode-switch`) — transparent, `--line`
  border, hover to `--blush`. A mode or a menu.
- **Underlined** (`.link-btn`) — no box at all, a `1px` blush underline. Hover deepens it;
  a pressed state thickens it to `2px` rather than filling anything, because the pedal is a
  state and not an action.

## 5. Motion

Almost nothing moves, which is what lets the two things that do move work.

| What | Timing |
|---|---|
| Mode flip on surfaces | `280ms ease` (background, border, colour) |
| Bar hover / selection | `140ms ease` |
| Armed dot | `armed 1.6s ease-in-out infinite`, 50% at `opacity: 0.25` |
| An open note (key and chip) | `waiting 1.3s ease-in-out infinite`, 50% at `opacity: 0.45` |

Both animations live inside `@media (prefers-reduced-motion: no-preference)`. The breathing
is load-bearing rather than decorative: the moment a note lands is two notes after the one
the player is listening to, so it has to be catchable out of the corner of an eye.

## 6. Layout

- `--max: 1080px`, centred, `padding-inline: 24px`.
- **One breakpoint, at 760px.** Below it the sheet head centres, bars grow, and every
  dialog becomes a bottom sheet. There are no size classes. Add a second breakpoint only
  when something actually collides — check at 430px before calling a layout done.
- Measure is capped in `ch`: `62ch` for ledes and findings, `60ch` for help copy.
- Anything that reserves height, reserves it always. `.feedback` and `.solo-live` both hold
  `min-height: 46px` so the dock under the keyboard never grows and shrinks.
- Tap targets are sized for touch **at every width**, so there is no "touch mode" to get
  out of step with.
- `:focus-visible` is `2px solid var(--blush-deep)` at `2px` offset, everywhere.

## 7. Voice

The writing is part of the brand, and it has three rules.

1. **Name the thing, don't judge the player.** The engine reads a bar; it does not mark a
   player. "Outside", never "wrong". The verdict line says what the voicing *says*, not
   whether you failed.
2. **A plain sentence, then the detail.** Ledes are one sentence with a dash in the middle:
   *"Play a line over the chart and see where every note lands — chord tone, scale tone or
   outside. Arm a take and it keeps count across every bar you walk through."*
3. **Labels are two or three words, sentence case inside small caps.** "Voicings ·
   substitutions". "Lines · scales · takes". "Read it". Middots separate; ampersands do
   not appear.

The colophon is the house signature: engine path in mono, the test count, where it is
running, the build stamp, a link to the source. Quiet, factual, 13px, `--text-soft`.

## 8. Drop-in tokens

Everything above, as a block you can paste into a new surface.

```css
:root {
  --cream:      #faf6f0;
  --cream-2:    #f2ebe1;
  --paper:      #fffcf7;
  --charcoal:   #241f1d;
  --blush:      #d9a6a0;
  --blush-deep: #c07f79;
  --gold:       #b08d57;
  --line:       #e4d9cc;
  --text:       #3a332f;
  --text-soft:  #6b615a;

  /* fixed meaning, both modes */
  --sage:       #6f7f63;
  --approach:   #8fae6d;
  --enclosure:  #3f8f88;
  --rust:       #a4553f;
  --unresolved: #b6afa4;

  --wash:        rgba(217, 166, 160, 0.16);
  --wash-strong: rgba(217, 166, 160, 0.26);

  --serif:  "Playfair Display", Georgia, "Times New Roman", serif;
  --script: "Cormorant Garamond", Georgia, serif;
  --sans:   "Jost", ui-sans-serif, system-ui, -apple-system, "Segoe UI", sans-serif;
  --hand:   "Kalam", "Bradley Hand", "Segoe Print", cursive;
  --mono:   ui-monospace, Menlo, Consolas, monospace;

  --max: 1080px;
}

body.soloing {
  --cream:      #eeedea;
  --cream-2:    #e1e0da;
  --paper:      #fafaf7;
  --charcoal:   #20202a;
  --line:       #d7d5cd;
  --text:       #343139;
  --text-soft:  #656069;
  --blush:      #99a1bb;
  --blush-deep: #5c6488;

  --wash:        rgba(92, 100, 136, 0.15);
  --wash-strong: rgba(92, 100, 136, 0.24);
}

body {
  margin: 0;
  background: var(--cream);
  color: var(--text);
  font-family: var(--sans);
  font-weight: 300;
  font-size: 16px;
  line-height: 1.65;
  -webkit-font-smoothing: antialiased;
}

h1, h2, h3 {
  font-family: var(--serif);
  font-weight: 600;
  color: var(--charcoal);
  letter-spacing: 0.01em;
  margin: 0;
}

.eyebrow {
  text-transform: uppercase;
  letter-spacing: 0.28em;
  font-size: 11px;
  color: var(--blush-deep);
  font-weight: 500;
  margin: 0 0 12px;
}

:focus-visible { outline: 2px solid var(--blush-deep); outline-offset: 2px; }
```

## 9. Checklist before you ship a surface

- [ ] No hard-coded hex or `rgba()` where a token exists — it will not follow the mode.
- [ ] Radius is `1px`, `20–30px` or `50%`, and it means paper / state / dot.
- [ ] Controls are small caps with wide tracking, not sentence-case buttons.
- [ ] Chord symbols are in `--hand`; nothing else is.
- [ ] The five meaning colours still mean what they mean.
- [ ] Checked at 430px wide, not only at 1200px.
- [ ] No render-blocking webfont; fallbacks in every stack.
- [ ] Any animation is inside `prefers-reduced-motion: no-preference`.
- [ ] Nothing on screen calls a player wrong.
