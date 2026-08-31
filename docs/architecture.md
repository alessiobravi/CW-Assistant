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
  |-- PortAudio input / WAV replay
  |-- SoapySDR input / SigMF replay
  |-- network receiver directory and KiwiSDR WebSocket source
  |-- secure remote control/event and Opus receive-media transports
  |-- Hamlib CAT
  |-- serial RTS/DTR keying
  `-- Log4OM UDP ADIF
```

Dependencies point inward. Hardware adapters implement core interfaces; the
core never includes Qt, PortAudio, Hamlib, or SoapySDR headers.

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

The dependency-free replay/analyzer path reads bounded WAV blocks, derives its
clock from sample indices, applies a Hann window, and publishes immutable dBFS
spectrum snapshots with exact frequency coordinates. Audio produces a one-sided
spectrum; complex I/Q produces an FFT-shifted full-band spectrum. This same
snapshot contract feeds the renderer and future channel detector.

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
- PortAudio: low-latency cross-platform audio capture.
- Hamlib: multi-vendor CAT model abstraction.
- SoapySDR plus device modules: RTL-SDR and SDRplay IQ input.

All are linked only into their owning adapter or desktop target. Dependency
versions will be pinned in packaging manifests after the open-source license and
minimum OS versions are selected.
