# Architecture

## Component boundaries

```text
desktop-ui (Qt Quick/QML)
  |-- custom QQuickItem scene-graph spectrum/waterfall renderer
  |-- receiver/channel controls
  |-- QSO workflow panels
  `-- diagnostics and settings

application services
  |-- receiver coordinator
  |-- channel tracker/scheduler
  |-- QSO state machine and macro expansion
  |-- TX safety supervisor
  |-- log coordinator and durable outbox
  `-- remote station coordinator and state snapshots

dependency-free core
  |-- sample and spectral data contracts
  |-- bounded queues and worker scheduling
  |-- CW timing/decoding state
  |-- QSO/ADIF domain model
  `-- hardware-neutral interfaces

adapters
  |-- Qt Multimedia input / WAV replay
  |-- SoapySDR input / SigMF replay
  |-- network receiver directory and KiwiSDR WebSocket source
  |-- secure remote control/event and Opus receive-media transports
  |-- Hamlib CAT
  |-- serial RTS/DTR keying
  `-- Log4OM UDP ADIF
```

Dependencies point inward. Hardware adapters implement core interfaces; the
core never includes Qt, Hamlib, or SoapySDR headers.

## Sample and threading model

```text
capture callback
      | fixed block, try_push
      v
bounded SPSC ring ----overflow counter----> diagnostics
      |
      v
DSP dispatcher ----FFT frame----> UI snapshot ring ----> render thread
      |
      +---- detector/tracker
      |
      `---- bounded work queue ----> fixed worker pool
                                      | per-channel state
                                      v
                              decoded event queue
                                      |
                                      v
                              application/QSO thread
```

There is one capture callback per active source, one dispatcher per receiver,
and a bounded worker pool. A tracked frequency is a state object, not a thread.
Tasks for the same channel are serialized and carry monotonically increasing
sample sequence numbers. Different channels may execute concurrently.

The FFT is calculated once per input window. Candidate channels reuse its bins;
their narrowband pipelines then perform NCO mixing, filtering/decimation, AGC,
tone/envelope estimation, adaptive dit timing, symbol decoding, and language
confidence scoring. This avoids repeating a wide FFT for every signal.

The planned decoder uses an explainable semi-Markov timing path plus an optional
compact causal learned likelihood path. A bounded rolling buffer permits
delayed multi-hypothesis rescoring, joint co-channel separation, and
conservative strongest-track interference cancellation without delaying live
capture. Context and callsign sources can re-rank alternatives but remain
distinct from acoustic output. See
[High-accuracy CW decoder strategy](decoder-strategy.md) for the pass model,
resource limits, training corpus, and benchmark gates.

The delivered M2 baseline scans the complete processed FFT passband for local
peaks, suppresses duplicate nearby candidates, and maintains a bounded
independent state object for every tracked frequency. Candidate discovery uses
the shared display FFT and rejects numerical-floor peaks outside an initial
96 dB range below its strongest bin, then each track consumes the original
audio block using a phase-continuous complex mixer, parallel three-stage 60,
120, and 240 Hz filters, separately smoothed lower/upper noise references, and
500 Hz evidence updates. Acquisition holds the 120 Hz path before measured WPM,
drift, and SNR select another width. A bounded 0–1 center-concentration measure
helps reject wide energy. The geometric mean of both side references and a
per-track adaptive floor/peak envelope drive smoothed key probability and
adaptive timing; display
averaging, gain, palette, and guide settings cannot assert key-down. This work
runs inside the live/WAV DSP workers rather than the UI thread.

Each track separates provisional from stable text and publishes letters,
digits, punctuation/prosigns, WPM, SNR, and an initial evidence-derived
confidence score. Stable track IDs select one of 24 shared UI colors, and silent
tracks expire after a bounded hold. The 700 Hz guide is a rendering reference
only and is absent from the decoder data path.

Timing acquisition is also bounded per track: nine deterministic decoders start
at 8, 12, 16, 20, 25, 32, 40, 50, and 60 WPM. Recent bounded element quality,
unknown-symbol rate, and a conservative 20 WPM prior rank them while output is
provisional. After at least 2.5 seconds of keyed evidence and sufficient symbol
or score separation, one path becomes the presentation leader but all fixed
anchors continue processing. A 2.5-second post-transmission silence reselects
the best complete path, commits its stable prefix, and permits a fresh bounded
acquisition for a different sender/speed. Current allocated state
is reported conservatively, including dynamic evidence buffers; the timing
corpus enforces a 256 KiB per-decoder ceiling and currently remains far below
it. This estimate is a regression guard, not a replacement for platform peak
memory measurement.

Candidate peaks use parabolic sub-bin interpolation, and a bounded predictor
maintains carrier frequency plus drift while preserving track identity. A
saturated bank admits a stronger new carrier by evicting only the weakest
unmatched unverified track; verified tracks are protected. Established tracks
reject identity-breaking frequency innovations rather than carrying old text
onto a new station.
Corpus-qualified configurable widths, robust noise quantiles, calibrated
confidence, delayed multi-pass refinement, a bounded worker pool for those
heavier passes, and co-channel source separation remain.

Candidate state is not presentation state. A local-prominence guard rejects
broad spectral shoulders using a near check and FFT-resolution-independent
hertz-scaled references. Remaining tracks progress through candidate,
Morse-likely, verified, and lost states. Repeated spectral persistence across
normal key-up gaps, keyed edges, narrowband coherence, spacing cadence,
known-symbol ratio, mark timing, and character confidence supply inspectable
rejection reasons. Passing evidence must persist before publication, and a
separate failure hold is required before demotion; every gate remains live
after verification. Private states cannot consume UI colors,
detected-signal count, or session rows. Stable characters carry bounded
per-character evidence; completed-word gating is applied again before a
structurally valid callsign is exposed.

The presentation model is separate from the decoder bank. All tracks continue
processing, while an ordered list of operator-opened IDs controls the session
cards. Closing or reordering a card cannot mutate DSP state. Callsign tokens and
frequency labels are derived views of each stable track ID.

Actual-RF presentation is evidence-gated. A profile must explicitly associate
its selected live input with the configured radio, and a supported provider
must report valid RX state. Checked transverter resolution is followed by the
profile-selected CW-U/CW-L tone offset around the configured CW pitch. Missing
evidence produces an explicit audio-frequency label; RF is never guessed.

## Graphical rendering

QML owns layout and controls, while a custom C++ `QQuickItem` produces Qt Scene
Graph geometry and shader nodes. Qt Shader Tools produces portable shader
packages for Direct3D, Metal, Vulkan, and OpenGL backends.

The spectrum trace, waterfall history/material, channel overlays, interactions,
and render metrics are separate classes. DSP publishes bounded immutable
snapshots; it does not share a large mutable render model or wait on the render
thread. The CPU FFT is shared by detection and display. GPU compute is optional
future work, while the baseline supports a public-Qt scene-graph path and CPU
texture fallback.

The baseline waterfall uses the render backend's `QSGImageNode`. The node is
created on the render thread only after `createTextureFromImage()` returns a
valid texture, and it owns replacement textures. An empty/reset receiver keeps
the waterfall node absent or hidden; null textures are never attached to the
scene graph.

Waterfall rows occupy a timestamp-anchored constant-duration window. The row
capacity is `line rate × history seconds`; missing intervals and unfilled older
history are dark rows. The complete fixed-height time image is scaled to the
available pane, so resizing changes pixel pitch but never the time represented
from top to bottom. The analyzer uses overlapping 2,048-sample Hann windows with
a sample-rate-derived hop matching the selected line density, retaining
frequency resolution while increasing real dit/dah timing observations.

The dependency-free replay/analyzer path reads bounded WAV blocks, derives its
clock from sample indices, applies a Hann window, and publishes immutable dBFS
spectrum snapshots with exact frequency coordinates. Each snapshot carries
averaged bins for stable tracking/the Audio spectrum view and unaveraged bins
for the CW symbols view. Audio produces a one-sided
spectrum; complex I/Q produces an FFT-shifted full-band spectrum. The shared
audio path can reject frame DC, apply bounded manual or smoothed automatic
gain, and crop published bins to an input-derived or explicit bandwidth. This same
snapshot contract feeds the renderer and future channel detector.

The operator can switch live between **Audio spectrum** (smoothed rows) and
**CW symbols** (unaveraged, noise-gated, nearest-neighbor acoustic keying
rows). The latter visualizes received dit/dah/gap evidence; it does not render
or invent decoded characters and does not reset DSP state.

Only a functional 2D spectrum and waterfall are supported. The renderer does
not include a 3D spectrum or decorative shader effects.

See [ADR 0001](decisions/0001-qt-quick-spectrum-renderer.md) for the detailed
rendering decision and accepted constraints.

## Backpressure

- Capture never waits. If its SPSC ring is full, the new block is rejected and
  an overrun is recorded with its sequence number.
- DSP work is bounded. The scheduler can pause the lowest-priority unselected
  channels when the latency budget is exceeded.
- UI snapshots are replaceable. The renderer consumes the newest complete
  spectrum/waterfall row and may skip older display-only snapshots.
- Logging uses a durable outbox because losing a log record is materially
  different from dropping a display frame.

## Radio profiles

CAT and keying are intentionally distinct. A profile contains a Hamlib model
and CAT port plus an independent keying port, RTS/DTR assignment, and polarity.
Multiple profiles may be saved. Initially only one profile owns TX at a time;
additional receivers can remain active. This prevents two rigs from being keyed
by one QSO state machine.

CAT4OM is modeled as a remote CAT service rather than a serial adapter. Its
Control WebSocket supplies authoritative pushed snapshots and group-wide master
ownership. The adapter may read/set frequency and split when advertised, but it
does not route network PTT or CW around CW Assistant's station-local transmit
guard. Profile passwords are never persisted.

A network SDR is a receive-only source with its own tuned frequency. It is not a
CAT rig and cannot acquire TX ownership. Any action that copies its frequency to
a local rig crosses an explicit operator-confirmation boundary.

Port enumeration is read-only. Opening a keying port initializes both control
lines to their inactive polarity before the profile can be armed.

## TX safety state machine

```text
DISARMED -> ARMED -> AWAITING_CONFIRMATION -> CONFIRMED -> TRANSMITTING
    ^          ^                                      |          |
    |          `------------- QSO complete -----------'          |
    `---------------- disarm / restart ---------------------------'

Any state -> FAULT -> explicit reset -> DISARMED
```

The application state machine grants permission; the serial adapter remains
responsible for a hard maximum-key-down timer and best-effort line release on
close. Callsign confidence may enable the confirmation button but cannot bypass
it.

## QSO panels

Panels will be declarative data, not executable plugins. A workflow contains:

- named fields such as call, RST, serial, name, QTH, and locator;
- ordered receive/confirm/transmit steps;
- editable CW message templates with field substitution;
- validation and completion rules;
- contest-specific serial-number allocation and duplicate checks.

Once this format is stable, signed or sandboxed extension mechanisms can be
considered without exposing raw serial/keying access.

## Callsign interaction and policy

Channel overlays publish stable observation IDs so hover/press detail cards can
be enriched without blocking the render thread. Live DSP fields are available
immediately; worked-before, prefix, and logger fields arrive asynchronously and
are discarded if the observation is no longer current.

The persistent exact-match ignore list is a core policy, not a visual toggle.
Ignored observations are removed before display and queue models. The QSO state
machine and TX guard independently recheck the same current policy before any
transition that can lead to keying.

## Network receivers

Directory providers produce normalized metadata but do not open streams.
Protocol adapters open one selected endpoint and implement `ISampleSource`.
This keeps discovery, caching, browser handoff, WebSocket framing, and sample
delivery independently testable.

KiwiSDR is the first direct adapter. Browser-only receivers use an external
browser/virtual-audio workflow unless their operator exposes a documented and
authorized stream. Directory refresh has a persistent cache, explicit user
refresh, rate limiting, and bounded health checks so volunteer receivers are not
polled aggressively.

## Remote operation

The same application core is hosted by standalone and station-server roles. A
remote client is a projection of server state plus an authenticated command
source; it is not a remote serial-port bridge. The station remains authoritative
for clocks, hardware, safety, logging, decoder state, and callsign policy.

Control/events use a versioned secure reliable channel. Audio and high-rate
visual data are separate subscriptions with bounded queues and downgradeable
bandwidth profiles. Each rig has one expiring operator lease, while multiple
observer clients may subscribe. Complete CW messages are scheduled locally at
the station, preventing network jitter from changing Morse element timing.

See [ADR 0002](decisions/0002-secure-remote-operation.md) for authentication,
failure, reconnect, and TX safety rules.

## Planned external libraries

- Qt 6 Quick/QML: UI and hardware-accelerated scene graph.
- Qt 6 Multimedia: cross-platform audio device discovery and capture.
- Hamlib: multi-vendor CAT model abstraction.
- SoapySDR plus device modules: RTL-SDR and SDRplay IQ input.

All are linked only into their owning adapter or desktop target. Dependency
versions will be pinned in packaging manifests after the open-source license and
minimum OS versions are selected.
