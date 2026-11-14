# What is in here, and under what terms

`assets/` holds the files **both shells** need: the app compiles them into its
binary and `web/build.sh` copies them beside the page. The repository's own
code is MIT (see `LICENSE` at the root); these files are not all the
repository's own, so they are listed here one by one.

## pdf.js — Apache 2.0

`pdf.min.js`, `pdf.worker.min.js` and `pdf.js-LICENSE` are Mozilla's pdf.js,
vendored rather than fetched so the app can read a PDF with no network and the
page can do it under a `default-src 'none'` policy. The licence text sits
beside them, ships with the page, and is compiled into the app binary.

`isEvalSupported: false` is set wherever it is loaded, which is the documented
mitigation for CVE-2024-4367.

## The three samples — free to use, origin not recorded

`piano-grand.wav`, `bass-upright.wav` and `bass-electric.wav`.

**These were found as free-to-use files and where they came from was not
written down at the time.** That is the honest state of it rather than a
licence claim: nobody can point at the terms they arrived under, so nobody
should assume they are redistributable, and this file exists so that the gap
is visible instead of implied by silence.

They are **due to be replaced with the project author's own recordings**, at
which point this section says who recorded them and when, and the question
closes. Until then, anyone forking this repository should treat the three
`.wav` files as the one part of it they have no licence for, and either record
their own or delete them. Deleting them does not break anything: a recording
that will not load leaves that bank **silent** (`loadBankSample` says so where
it catches the failure), and the electric piano and the synthesised bass are
not sampled at all, so there is still a sound to practise against. What is
lost is the grand and the recorded bass.

Everything else here is covered by the root `LICENSE`.
