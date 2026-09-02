# Development session handoff

This document is the model-neutral starting point for a fresh development
session. Read it completely before editing, then read `AGENTS.md`,
`BACKLOG.md`, `CHANGELOG.md`, and the documents linked under **Required
reading**. Repository source and tests are authoritative if this summary ever
lags behind them.

## First actions in a fresh session

1. Confirm the workspace root and inspect `git status`, recent commits, and any
   uncommitted changes. Preserve existing work; never reset or overwrite it.
2. Read `AGENTS.md` and obey its documentation, verification, authorship, and
   radio-safety rules.
3. Read the active backlog entries related to the requested task. Do not infer
   permission for unrelated features or any transmission action.
4. Run the smallest relevant deterministic tests before changing behavior.
5. Implement one coherent slice, updating `CHANGELOG.md`, `BACKLOG.md`, the
   applicable user manual, and engineering documentation in the same commit.
6. Commit with the repository owner's identity and no automated co-author
   trailer. Push only when the slice is coherent and locally verified.
7. Monitor every hosted platform job and continuous publication to completion.
   If anything fails or is cancelled, diagnose and correct it, retaining the
   exact implementation title for each corrective commit, until everything is
   green.

Some managed workspaces provide Git metadata outside the source directory. Use
the Git worktree/metadata supplied by the environment; do not initialize a
second unrelated repository, copy authentication material into this tree, or
record credential paths in documentation. If authenticated Git metadata is not
available, continue with safe local work and report the publication gap.

## Implementation checkpoint — 2026-09-02

The repository coordinates and recovered local Git operation are documented in
`AGENTS.md`. In managed workspaces where a `.git` entry cannot be created, this
tree may contain a local untracked `.cwa-git` store; invoke Git with
`git --git-dir=.cwa-git --work-tree=.` and verify the canonical origin before
any remote operation. Never use metadata from another project.

The operator supplied three private WAV/debug bundles and authorized a complete
capture-driven fix, native verification, and push. Replaying their raw audio
through the current production core established four primary failures: the
24-track bank silently starved later peaks; old tracks could walk onto new
peaks while retaining decoder history; the min-side noise reference and
unbounded narrow/wide ratio were unstable; and lifetime evidence plus an
early one-path WPM lock prevented recovery. A separate display issue was also
confirmed: temporally averaged waterfall rows smoothed acoustic dits/dahs into
generic traces.

The implemented recovery slice now:

1. admits a strong new candidate into a saturated bank by evicting only the
   weakest unmatched unverified occupancy, protects verified tracks, decays
   unmatched persistence, and retains decoded/Morse-likely candidates across
   ordinary word gaps;
2. rejects identity-breaking innovations on established tracks so decoder text
   and evidence cannot migrate to a different nearby carrier;
3. combines both side references geometrically, normalizes a per-track
   floor/peak key envelope, and reports bounded 0–1 spectral concentration;
4. bases character, cadence, unknown-fraction, verification, and WPM scores on
   bounded recent evidence; all nine fixed WPM anchors continue processing
   after presentation selection and the best complete path is reselected at a
   silence/flush boundary;
5. continuously re-evaluates verification with a sustained-entry interval and
   a longer exit hold instead of permanently latching a transient pass;
6. publishes averaged and instantaneous bins from the same FFT and provides a
   profile-persisted **Audio spectrum** / **CW symbols** selector. CW symbols
   is a crisp unaveraged acoustic keying raster, not reconstructed text, and
   switching it does not reset decoding;
7. adds the native `cwa_capture_replay` executable for repeatable local audits
   of private `audio.wav` files without adding those files to the repository.

Deterministic core tests include saturated admission, identity-jump rejection,
recent metrics, and instantaneous-bin coverage. The dependency-free core,
decoder benchmark, verification hard-negative benchmark, and native capture
tool are the required local gates. The private captures now demonstrate stable
tracking of the long approximately 1.42 kHz stream and acquisition of the later
approximately 2.016 kHz and 700 Hz streams instead of total starvation. They
also show that decoded character accuracy remains materially below acceptable
on difficult field audio; do not claim full CW recognition is solved. The next
decoder step is the full semi-Markov/Viterbi timing path and a licensed,
optional compact learned likelihood adapter only after held-out native
benchmarks prove a gain. Pattern tokens may add evidence but must never bypass
acoustic gates. The marker-click defect described below remains open.

## Implementation checkpoint — 2026-09-01 (historical)

Published baseline as of this checkpoint: commit `fd15f6a` (`Drop tracks a
VFO shift carries past 0 Hz instead of leaving them invalid`), confirmed
fully green on the hosted matrix (`continuous` tag resolved to it). A fresh
session must inspect Git status and history to determine whether it is
looking at this commit, a later one, a corrective commit, or preserved
local changes. Never discard a dirty working tree or begin the same
implementation again.

This checkpoint followed an extended investigation into a persistent field
report ("visible CW signals never get identified, or only the
strongest/cleanest ones do"), driven throughout by real operator debug
captures (`OBS-003`, now implemented — see below) rather than synthetic
reproduction alone, which had already been tried and failed to reproduce the
field case. In order, this investigation produced:

1. `OBS-003` implemented end to end: an operator-started, bounded ("Debug
   capture" control) recording of raw live audio (`audio.wav`) plus a
   per-track private diagnostic log (`diagnostics.jsonl`, 1 Hz, every track
   including unverified ones), now also carrying RX/TX radio frequency and
   split state on every line so a capture shows whether the VFO moved
   during it. Capped at 5 minutes, never silent, requires explicit start.
2. A character-distribution plausibility gate
   (`CwVerificationReason::ImplausibleCharacterDistribution`): real capture
   data showed the opposite of the original complaint — noise was being
   *over*-verified, decoding to text overwhelmingly made of the two
   single-element characters E/T, which the existing unknown-symbol-fraction
   gate did not catch. This gate re-checks even an already-verified track
   once enough text has accumulated, since implausibility can only be
   judged from accumulated text. Validated against two independent real
   captures (12 of 14 false positives caught directly; the remaining 2 were
   already caught by the existing unknown-fraction gate once it was made to
   re-evaluate continuously).
3. A verification-state/reason consistency fix (`updateVerification()`
   re-derives state from current evidence every call instead of only ever
   advancing it).
4. A VFO/on-air operator display: a large VFO readout (RX green, TX yellow
   with a SPLIT badge, rig-display decimal precision) replacing a small
   single-line label; a styled but intentionally unwired "ON AIR"
   indicator (no radio backend currently reports live transmit/PTT state);
   the CW pitch guide moved to a bold line on the spectrum/waterfall
   boundary instead of a vertical band, so it can't be confused with an
   identified signal (verified tracks are the ones shown as a colored
   area, sized to their actual filter width).
5. `CwChannelBank::shiftTrackedFrequencies()`: retuning the linked radio's
   RX VFO during live audio now re-centers every currently tracked signal
   (accounting for CW-U/CW-L sideband direction) instead of losing its
   identity, and drops a track outright if a shift carries it past 0 Hz
   (found via a real capture spanning a live VFO sweep) rather than leaving
   an invalid negative-frequency candidate.
6. Application update checking (`PKG-004`, partial): background + manual
   checks against the published release manifest, checksum-verified
   download, handoff to the OS's own installer — no silent self-install
   yet (deliberately deferred pending code signing, `PKG-001`/`PKG-002`).
7. **The most significant finding, found via real contest-capture data
   containing a legible `TEST` mid-stream that never verified**:
   `timing_quality` and `mean_character_confidence` were mathematically
   forced identical (same accumulator fed by both in
   `CwTimingDecoder::finishCharacter()`), so two separately configured
   verification thresholds were really checking one blended signal twice.
   Fixed: `timing_quality` now accumulates a genuinely independent pure
   element-duration-ratio signal; `CwMultiSpeedDecoder::score()` was
   updated to keep scoring WPM-hypothesis selection on the blended
   `mean_character_confidence` to preserve its original tuned behavor (an
   initial attempt to leave it on the new independent `timing_quality`
   destabilized WPM lock on the existing deterministic test — an 8 WPM
   signal started reporting ~31 WPM with the filter width oscillating
   every step — caught with an instrumented before/after trace before
   shipping, not just eyeballing the diff).
8. That fix exposed, rather than fully resolved, a **bigger architectural
   weakness in `CW-001`**, confirmed against real capture data and agreed
   with the operator to fix properly rather than patch further:
   `CwMultiSpeedDecoder` currently locks irrevocably onto one of nine WPM
   hypotheses once ~2.5s has elapsed and the leader has decoded as few as
   2 symbols (`considerLock()`); once locked, the other eight hypotheses
   are never evaluated again, and the only recovery path is a full 2.5s
   silence gap that contest/QSK traffic often never provides. This can be
   self-reinforcing-wrong (a miscalibrated lock suppresses the very
   confidence signal the per-element adaptation gates on). **This is the
   next priority at that checkpoint; the 2026-09-02 slice above supersedes
   that status. Full investigation notes, a detailed options
   comparison, and the agreed design direction are written to
   `docs/development/decoder-timing-redesign-notes.md` — a local-only file,
   listed in `.gitignore`, never committed; read it directly from disk
   (gitignore does not hide it from a filesystem read) before starting this
   work. It is deliberately not duplicated into this git-tracked file.

Standing rule for every session and every agent working in this repository:
never name any external reference project, product, or repository anywhere
in this repository (not in code, commits, or any doc — git-tracked or not,
committed or not). Describe any resulting design decision purely on its own
technical merits.

Before changing decoder verification/timing behavior further, the next
session must:

1. Read `docs/development/decoder-timing-redesign-notes.md` in full before
   starting the `CW-001` redesign.
2. Confirm the implemented spectral persistence tolerates normal key-up gaps
   while cadence/coherence provide the stronger qualification; do not restore
   a consecutive-FFT-frame gate that rejects short high-speed marks.
3. Confirm decoder state/resource reporting continues to include bounded
   dynamic character-evidence storage when that structure changes.
4. Retain desktop integration assertions for the exposed verification state,
   reason, confidence, and character evidence.
5. Run all three dependency-free commands below, `git diff --check`, and the
   relevant secret/private-data scan. Review the complete result, not only the
   final exit code. Also verify with a real (non-Apple-Clang) GCC compile
   when touching designated-initializer struct literals — Apple Clang has
   silently accepted out-of-declaration-order initializers that GCC and
   MSVC correctly reject, more than once in this project's history.
6. Update all affected records if implementation details change, then commit
   with the owner identity, push only after the operator explicitly
   confirms (do not push automatically after committing — the operator has
   asked to control when each commit ships, sometimes aggregating several
   local commits into one push), and monitor every hosted build and
   continuous publication asset until all are green.

## Product objective and non-negotiable scope

CW Assistant is a modular GPL-3.0-or-later C++20 application for high-accuracy,
full-passband amateur-radio CW reception, visualization, decoding, logging,
operator-assisted QSO workflows, and eventually guarded remote operation.

- Supported desktop targets: Windows 11 or newer on x64, macOS Sonoma 14 or
  newer on Apple silicon and Intel x64, Debian/Ubuntu x64, and a portable Linux
  archive.
- Use Qt Quick and the existing two-dimensional scene-graph spectrum/waterfall.
  Do not introduce 3D or novelty visualization.
- Keep UI, documentation, commits, and discussions in project-native terms.
  Do not name or advertise comparison products or visual inspirations. Do not
  add an automated system as author or co-author.
- Audio input, WAV replay, SDR input, and network SDR input share timestamped
  receive contracts. RTL-SDR and SDRplay through SoapySDR are the first native
  SDR targets; a selectable receive-only directory remains planned.
- Radio control starts with configurable Windows OmniRig and CAT4OM paths,
  reference profiles for Yaesu FT-450D and FT-818/FT-818ND, editable serial
  speed/framing, split operation, and signed transverter offsets.
- Multiple radio profiles and simultaneous application instances are required.
  CAT and direct PTT/KEY ports are independently selectable; baud rate, data
  bits, parity, stop bits, handshake, and DTR/RTS behavior must always be
  user-configurable. Never probe or assert serial control lines speculatively.
- Named station profiles must work through both the startup chooser and command
  line. Multiple application instances may operate different devices, with
  cross-process device ownership still required.
- The post-install wizard and Settings pane must expose audio input even in SWL
  mode, offer an explicit no-radio receive-only path, present positively
  detected radios without claiming that an arbitrary serial port is a radio,
  and allow later editing of station identity, audio, radio, serial, offsets,
  antenna mappings, logging, and remote roles.
- Logging starts with Log4OM 2 and standards-conformant ADIF. Preserve satellite
  mode/name, RX/TX bands and exact frequencies, antenna, radio, and transverter
  context.
- The primary workspace is a modern full-spectrum decoder: one stable color per
  verified signal across spectrum, waterfall, and its optional session card;
  cards open on marker click, close without stopping decode, and can be dragged
  into operator order. Spectrum/waterfall speed, FPS, bounds, gain, bandwidth,
  palette, and guide visibility belong in live controls. Hover details and a
  persistent callsign ignore list remain required.
- Ordinary QSO, DX pileup, and contest workflows need configurable operator
  panels/macros. Optional callsign-list prediction/validation must be visibly
  switchable in the live decoder and must never silently replace acoustic
  evidence.
- Standalone, remote-client, and station-server roles are planned. All TX
  timing and hardware safety remain station-local.
- Transmission is always disarmed by default. Decoder output never directly
  asserts PTT or KEY. Human confirmation, callsign matching, timeouts,
  emergency release, and safe inactive serial lines are mandatory before any
  hardware transmission implementation.
- The author shown in the product is Alessio Bravi (IU0LFQ / AD2FC), with
  `https://iu0lfq.it/` as the author website.

## Current implemented receive path

The capture callback places timestamped blocks into a bounded SPSC queue. A DSP
worker performs one shared FFT and maintains bounded per-frequency tracks; a
track is state, not an operating-system thread. Original samples are mixed to
each tracked carrier and processed through phase-continuous 60, 120, and 240 Hz
paths with two side noise references. Nine deterministic timing hypotheses
cover 8–60 WPM. The winning path publishes provisional/stable text, adaptive
WPM, SNR, key probability, confidence, and character evidence. **This
hypothesis-selection mechanism currently locks irrevocably once one hypothesis
is chosen — see the implementation checkpoint above and
`docs/development/decoder-timing-redesign-notes.md`; a redesign is the current
top decoder priority.**

The spectrum and constant-time waterfall use the same FFT output. Visualization
controls beneath the spectrum apply live. The configurable 700 Hz/200 Hz CW
guide is a bold line on the boundary between the spectrum plot and the
waterfall history (not a vertical band or translucent overlay — that
treatment was deliberately moved to verified-signal identification instead,
so the two can't be confused); it never limits decoding or chooses a
channel. Verified tracks are shown as a colored vertical area sized to their
actual narrowband filter width, with a thinner keying-state line on top,
using stable colors. Clicking a marker is *supposed to* open or reopen its
decoder session, closing a session leaves DSP active, and cards can be
reordered — **the operator has reported at least twice this session that
clicking a verified marker does not open a session; static code review found
nothing conclusively wrong (the click handler, `id` typing, and hit-area
geometry all look correct), and the specific diagnostic needed (does the
cursor change to a pointing hand on hover?) was never answered. Still open,
low priority relative to `CW-001` above but worth revisiting.** Pointer-based
guide centering and separately confirmed CAT RX retuning are explicitly not
implemented yet (`UI-003`).

Actual RF labels are shown only when a live audio input is explicitly linked to
a configured radio and a control provider reports valid state. Sideband,
configured pitch, split state, and checked transverter offsets participate in
the mapping. Otherwise the UI must label the value as audio frequency rather
than guessing RF. The decoder panel (now titled "CW Decoder") also shows a
large VFO-style readout under the same gating conditions — RX in green, TX in
yellow when split is active, rig-display decimal precision — and retuning the
linked radio's RX VFO during live audio re-centers every currently tracked
signal to follow the retune rather than losing it (dropping a track outright
if the shift carries it past 0 Hz).

## Decoder verification lifecycle

Raw spectral peaks are private. The implemented lifecycle is:

```text
candidate -> Morse-likely -> verified -> lost
```

Peak discovery uses a permissive near-shape check and hertz-scaled far
references so detection does not depend on FFT bin width. A candidate needs
repeated spectral persistence across normal key-up gaps, keyed edges, spacing
observations, and narrowband coherence before becoming Morse-likely.
Verification additionally requires at least three known symbols, at most 30%
unknown output in the bounded recent window, adequate spacing cadence, mark
timing, and mean character confidence. A passing track must sustain every gate
for the configured entry interval; a verified track is demoted only after a
longer sustained failure interval. A character-distribution plausibility check
(`ImplausibleCharacterDistribution`) re-evaluates even an already-verified
track once at least 40 characters have accumulated, rejecting text whose
E/T fraction exceeds 0.35 (calibrated against real captured noise false
positives) — the one gate that can retroactively un-verify a track, since
plausibility can only be judged from accumulated text, not one instant's
evidence. Only verified tracks receive UI colors, rows, counts, or sessions.

Every private track carries an inspectable rejection reason. Recent cadence,
timing, character confidence, and a combined score remain live and are logged
for audit. Stable decoded logical characters
carry bounded symbol, confidence, timing-quality, and known/unknown evidence.
Callsign text remains hidden until verified stable text contains a complete
structurally valid word terminated by a gap.

The deterministic verification benchmark enforces these current regression
targets:

- clean, 30 WPM, and weak/fading/drifting CW each publish exactly one track
  within six simulated seconds;
- steady carriers, speech-like amplitude modulation, irregular impulses, and
  pumping broadband noise publish zero tracks;
- the verification corpus stays below a 0.20 real-time processing factor;
- the separate timing benchmark retains zero edits in its current 49-character
  8–55 WPM matrix, no speed failures, and no false characters in noise.

These are corpus limits, not universal RF performance claims. Real receiver
recordings are still required. An early September 2026 operator screenshot
showed a strong approximately 1.55 kHz audio trace visible in the waterfall
but absent from decoding; the hertz-scaled prominence qualification and
rejection diagnostics were added in response, but synthetic reproduction of
the underlying field report failed repeatedly. Real operator debug captures
(`OBS-003`, see below) later showed the actual defects: noise was being
*over*-verified (fixed by the character-distribution plausibility gate), a
verification-gate metric-conflation bug (fixed, see the implementation
checkpoint above), and an irrevocable WPM-hypothesis lock in `CW-001`. The
2026-09-02 slice keeps every fixed anchor processing and reselects at safe
boundaries, but field character accuracy still requires the full timing model.
Do not claim signal identification is fully resolved until the remaining
`CW-001` work described in the checkpoint above and in
`docs/development/decoder-timing-redesign-notes.md` is implemented and
validated against real capture data.

## Diagnostic capture requirement

`OBS-003` is implemented: an operator-started "Debug capture" control (in the
decoder panel header; a move into the Settings pane remains) records, to a
timestamped folder under the app's standard data location:

- the exact raw audio feeding the decoder (`audio.wav`);
- a private per-track diagnostic log (`diagnostics.jsonl`, 1 Hz), covering
  every currently tracked frequency — including tracks that never become
  visible — with SNR, narrowband coherence, filter width, verification
  state/reason, spectral observations, key transitions, decoded/unknown
  symbol counts, timing/cadence quality, WPM, provisional/stable text, and
  (as of this checkpoint) the linked radio's RX/TX frequency and split state
  so a VFO move during the capture is visible after the fact.

Capture requires explicit enablement, is capped at 5 minutes, allows review
before sharing, and is never silent or automatic. This has been the primary,
effective path for root-causing every real decoder defect found and fixed
this session — prefer it over synthetic reproduction attempts, which failed
to reproduce the original field report. Remaining scope (tracked as
`OBS-003`): move the control into the Settings pane, add a button to open
the capture folder directly, make the 5-minute auto-stop duration
configurable, plus conditioned/spectrum frames, overruns, a review step, and
credential/private-identifier redaction before export. Do not commit
captured station audio/data to the repository.

## Source map

- `src/core/src/cw_channel_bank.cpp` and its header: spectral candidates,
  tracking, per-track filters, verification lifecycle, diagnostics, snapshots.
- `src/core/src/cw_decoder.cpp` and its header: adaptive single/multi-speed
  timing, cadence and per-character evidence.
- `src/core/src/spectrum_analyzer.cpp`: shared FFT and audio conditioning.
- `src/desktop/replay/live_audio_worker.cpp`: live audio queue and DSP worker.
- `src/desktop/replay/replay_controller.cpp`: receive/replay presentation state.
- `src/desktop/replay/decoder_channel_model.cpp`: immutable decoder data exposed
  to QML.
- `src/desktop/qml/Main.qml`: spectrum/waterfall overlays and decoder sessions.
- `tests/core_tests.cpp`: dependency-free deterministic unit/integration tests.
- `tests/cw_decoder_benchmark.cpp`: character/speed/noise resource gate.
- `tests/cw_verification_benchmark.cpp`: verified-track acquisition and
  interference false-publication gate.
- `.github/workflows/desktop-ci.yml`: four-platform build, native smoke tests,
  packages, checksums, release manifest, and continuous publication.

## Required reading

- `docs/requirements.md` — normative product behavior and safety requirements.
- `docs/architecture.md` — module/thread/data-flow boundaries.
- `docs/decoder-strategy.md` — high-accuracy and multiple-pass design.
- `docs/manuals/operator-guide.md` — current operator-visible behavior.
- `docs/manuals/configuration-reference.md` — persisted settings and internal
  measured defaults.
- `docs/adif-conformance.md` — logging conformance gates.
- `docs/decisions/0001-qt-quick-spectrum-renderer.md` — renderer decision.
- `docs/decisions/0002-secure-remote-operation.md` — remote safety boundary.

## Verification commands

Preferred local build when CMake is available:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

On the current maintainer workstation, an isolated Qt/CMake test toolchain may
survive under `/private/tmp/cwa-qt`, `/private/tmp/cwa-python`, and
`/private/tmp/cwa-build` even when `cmake` is absent from `PATH`. Inspect those
project-scoped locations before declaring desktop tests unavailable. If the
temporary Python launcher reports a missing `cmake` module, invoke the retained
native binaries under `cwa-python/cmake/data/bin/` directly; do not reinstall or
change project dependencies merely to repair an ephemeral wrapper.

Dependency-free fallback when CMake is unavailable:

```sh
clang++ -std=c++20 -Wall -Wextra -Wpedantic -Isrc/core/include \
  tests/core_tests.cpp src/core/src/*.cpp -o /tmp/cwa_core_tests
/tmp/cwa_core_tests

clang++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Isrc/core/include \
  tests/cw_decoder_benchmark.cpp src/core/src/*.cpp \
  -o /tmp/cwa_decoder_benchmark
/tmp/cwa_decoder_benchmark

clang++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Isrc/core/include \
  tests/cw_verification_benchmark.cpp src/core/src/*.cpp \
  -o /tmp/cwa_verification_benchmark
/tmp/cwa_verification_benchmark
```

Also run `git diff --check`, parse changed workflow YAML, and scan the proposed
commit for private-key material and credentials without printing secret values.
Local dependency-free tests do not replace the complete hosted Qt/platform
matrix.

## Hosted completion rule

For every implementation push, inspect the complete workflow run and every
platform job. Success requires Windows 11 x64, Linux x64, macOS Apple silicon,
macOS Intel, and the continuous-publication job to pass. The `continuous` tag
must resolve to the implementation commit, and the MSI, Debian package, Linux
archive, both macOS archives, `SHA256SUMS`, and `latest.json` must be reachable.
Do not report completion while a job is queued, failed, skipped unexpectedly,
or cancelled.

## Recommended next decoder work after this checkpoint

1. **Top decoder priority**: extend the delivered `CW-001` recent-window and
   continuous-fixed-anchor slice into the full explainable semi-Markov/Viterbi
   timing path. Define an append-only consensus boundary before allowing a
   leader switch inside an active transmission; never freeze an outgoing
   leader's unconfirmed full text. Read
   `docs/development/decoder-timing-redesign-notes.md` in full before starting;
   validate against every deterministic benchmark, the native replay of all
   private captures, and `PERF-001`'s CPU/state limits.
2. Add consented, legally reusable real receiver recordings and annotations to
   the corpus; tune thresholds from measured false-publication, acquisition,
   and character-error results rather than visual intuition.
3. Recognize well-known CW/contest patterns (`CQ`, `TEST`, `599`, `5NN`,
   `TU`, `UP`, and similarly distinctive ones) in accumulated text as
   additional, independent verification evidence (`CW-006`) — motivated by
   a real capture where a track's text visibly contained a legible `TEST`
   but never verified. Calibrate/test against real noise captures so it
   cannot reopen the noise-verification problem the plausibility gate
   closed.
4. Implement pointer guide centering and the separate guarded CAT RX-retune
   action (`UI-003`).
5. Only after the causal baseline is measured on real data, implement rolling
   multiple-pass weak-signal refinement (`CW-003`), then co-channel pileup
   separation (`CW-004`).
