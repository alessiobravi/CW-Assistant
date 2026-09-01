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

## Implementation checkpoint — 2026-09-01

The published baseline immediately before this decoder-verification slice was
commit `b1949aa` (`Publish only verified CW traces`). The current source after
that baseline contains the lifecycle and diagnostics work below. A fresh
session must inspect Git status and history to determine whether it is looking
at the completed commit, a corrective commit, or preserved local changes. Never
discard a dirty working tree or begin the same implementation again.

The slice includes:

- per-track `candidate -> Morse-likely -> verified -> lost` state and explicit
  rejection reasons in `cw_channel_bank`;
- hertz-scaled peak references intended to avoid FFT-bin-dependent rejection
  of the strong live trace visible near 1.55 kHz in the operator screenshot;
- cadence, transition, timing, and bounded per-character confidence evidence
  in `cw_decoder`;
- verification state/evidence exposed through the desktop decoder model;
- a deterministic verification corpus covering clean, 30 WPM, and
  weak/fading/drifting CW plus four non-CW interference cases;
- documentation of the lifecycle, thresholds, measurable acceptance targets,
  and planned bounded diagnostic capture (`OBS-003`).

Before changing this slice further, the next session must:

1. Confirm the implemented spectral persistence tolerates normal key-up gaps
   while cadence/coherence provide the stronger qualification; do not restore
   a consecutive-FFT-frame gate that rejects short high-speed marks.
2. Confirm decoder state/resource reporting continues to include bounded
   dynamic character-evidence storage when that structure changes.
3. Retain desktop integration assertions for the exposed verification state,
   reason, confidence, and character evidence.
4. Run all three dependency-free commands below, `git diff --check`, and the
   relevant secret/private-data scan. Review the complete result, not only the
   final exit code.
5. Update all affected records if implementation details change, then commit
   with the owner identity, push it, and monitor every hosted build and
   continuous publication asset until all are green.

The real-radio screenshot is not a reusable signal fixture. Even if synthetic
tests pass, retain the limitation that the 1.55 kHz hardware case needs a new
binary and the same receiver/audio setup for confirmation. `OBS-003` is the
planned path to collecting a bounded, consented, reproducible diagnostic bundle
for cases like this.

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
WPM, SNR, key probability, confidence, and character evidence.

The spectrum and constant-time waterfall use the same FFT output. Visualization
controls beneath the spectrum apply live. The configurable 700 Hz/200 Hz CW
guide is a translucent visual overlay only; it never limits decoding or chooses
a channel. Verified tracks use stable colors. Clicking a marker opens or
reopens its decoder session, closing a session leaves DSP active, and cards can
be reordered. Pointer-based guide centering and separately confirmed CAT RX
retuning are explicitly not implemented yet (`UI-003`).

Actual RF labels are shown only when a live audio input is explicitly linked to
a configured radio and a control provider reports valid state. Sideband,
configured pitch, split state, and checked transverter offsets participate in
the mapping. Otherwise the UI must label the value as audio frequency rather
than guessing RF.

## Decoder verification lifecycle

Raw spectral peaks are private. The implemented lifecycle is:

```text
candidate -> Morse-likely -> verified -> lost
```

Peak discovery uses a permissive near-shape check and hertz-scaled far
references so detection does not depend on FFT bin width. A candidate needs
repeated spectral persistence across normal key-up gaps, keyed edges, spacing
observations, and narrowband coherence before becoming Morse-likely.
Verification additionally requires at least three known symbols, at most 20%
unknown output, adequate spacing cadence, mark timing, and mean character
confidence. Only verified tracks receive UI colors, rows, counts, or sessions.

Every private track carries an inspectable rejection reason. Verification-time
cadence, timing, character confidence, and a combined score are frozen for
audit; live measurements continue updating. Stable decoded logical characters
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
recordings are still required. A September 2026 operator screenshot showed a
strong approximately 1.55 kHz audio trace visible in the waterfall but absent
from decoding in the prior build. The hertz-scaled prominence qualification
and rejection diagnostics were added in response. Do not claim that hardware
case is resolved until a new continuous binary is tested with the same setup.

## Diagnostic capture requirement

`OBS-003` specifies a future operator-controlled full-debug mode. It must create
a bounded, portable analysis bundle that can correlate:

- timestamped raw and conditioned audio;
- spectrum frames and time/frequency references;
- private candidate lifecycle, rejection reasons, and verification evidence;
- provisional/stable decoder streams and per-character evidence;
- overruns, sequence gaps, queue/latency data, and relevant profile/radio state.

Capture must require explicit enablement, enforce duration and size limits,
allow review before export, and redact credentials and private identifiers. Do
not add silent background recording or commit captured station audio/data.

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

## Recommended next decoder work after this lifecycle slice

1. Validate the new continuous build against the operator's same live audio
   setup and record which private gate rejects the approximately 1.55 kHz trace
   if it still does not publish.
2. Implement `OBS-003` diagnostic capture so future RF cases can be reproduced
   deterministically without screenshots alone.
3. Add consented, legally reusable real receiver recordings and annotations to
   the corpus; tune thresholds from measured false-publication, acquisition,
   and character-error results rather than visual intuition.
4. Implement pointer guide centering and the separate guarded CAT RX-retune
   action (`UI-003`).
5. Only after the causal baseline is measured on real data, implement rolling
   multiple-pass weak-signal refinement (`CW-003`), then co-channel pileup
   separation (`CW-004`).
