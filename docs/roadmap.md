# Delivery roadmap

## M0 — foundation (delivered)

- C++20/CMake project and platform-neutral core
- Fixed-allocation sample blocks and bounded SPSC handoff
- Candidate selection policies and per-rig configuration contracts
- Human-confirmed TX permission state machine
- Basic ADIF serialization and hardware-free tests

## M1 — receive and visualize (delivered)

- Qt Multimedia device enumeration, capture, conditioning, and bounded handoff
- WAV input and deterministic replay clock
- Windowing, FFT, spectrum averaging, waterfall row generation
- Qt Quick application shell and custom scene-graph spectrum/waterfall based on
  the rendering decision in ADR 0001
- Spectrum trace baseline held 12 dB below the estimated noise floor, with the
  waterfall palette starting above that baseline so no palette range is spent
  coloring noise
- Waterfall history slid by the number of bins the band moved across a retune
  rather than erased, dropped only when the span itself changed, with the
  per-bin conditioning baseline re-established afterwards
- Runtime counters and capture-soak tests (counters implemented; no dedicated
  soak target is registered in the test suite)

Acceptance: select an audio port on Win64, see a smooth waterfall/spectrum, and
replay the same recording to identical spectral results without overruns.

## M2 — multichannel decode (current)

- Peak detector and channel lifecycle tracker
- Narrowband filter/decimator and adaptive CW envelope/timing decoder
- Confidence-fused deterministic/compact-causal decoder with explicit
  provisional and stable text
- Bounded rolling-buffer refinement, multi-hypothesis rescoring, co-channel
  operator fingerprinting, and conservative interference cancellation for weak
  and overlapping signals (refinement, rescoring, and completed-turn timing
  fingerprints implemented; interference cancellation not started)
- Morse alphabet, abbreviation, word-gap-prefix, distinctive-token, and contest
  exchange dictionaries shipped as editable data files, with the alphabet also
  generated into the library at build time as a fallback
- Decoding gated on a track's own measured peak level, the per-track baseband
  chain skipped for a track that will not be decoded, and weak-signal decoding
  exposed as an operator setting
- Runs of six or more one- and two-element characters replaced with a single
  space in published text
- Worker pool with per-channel ordering and load shedding (not started)
- Callsign extraction, confidence, channel list, and waterfall selection
- Delayed callsign detail card and persistent exact-match ignore policy (the
  exact-match ignore policy exists in the core; neither has a user interface)
- Corpus annotations and decoder benchmark report
- Receive-path profiler reporting per-stage wall time against tracked-signal
  count, and a spacing benchmark scoring word-boundary placement with and
  without the shipped vocabulary

Acceptance: locate and independently decode multiple annotated signals from the
CC0 pileup fixture with published accuracy, false-output, latency, revision,
CPU, and memory metrics. Every optional learned or later-pass stage must show an
independent held-out gain over the deterministic baseline.

## M3 — radio, guarded transmit, and QSO panels (in progress)

- Hamlib serial CAT adapter and multiple saved rig profiles (saved rig profiles
  implemented; the Hamlib path is the loopback rigctld network client, and no
  serial CAT adapter exists)
- Cross-platform serial RTS/DTR key/PTT adapter (implemented, with a
  disconnected-line acceptance probe before keying can be armed)
- Maximum-key-down watchdog and emergency stop (implemented; three seconds
  keyed for a message and fifteen for TUNE)
- Ordinary, DX-pileup, and contest workflow panels (conversation profiles and
  versioned contest exchange grammars load from data files; the panels
  themselves are not built)
- Explicit callsign confirmation and CW message scheduling (implemented)
- Fixed keying scope: 8 to 60 WPM, the ASCII character set, semi break-in, and
  a closed in-application table of seven transmittable prosigns

Acceptance: pass loopback line tests, then complete a human-confirmed QSO on the
chosen reference rig without any decoder event directly controlling TX.

## M4 — Log4OM and SDR

- Durable logging outbox and Log4OM 2 UDP ADIF adapter
- SoapySDR device discovery and RX stream adapter (foundation and official
  package integration implemented; physical-device qualification remains)
- Bundled RTL-SDR runtime and SDRplay setup diagnostics (RTL-SDR package-load
  validation implemented; SDRplay vendor runtime stays operator-installed;
  cross-platform live acceptance remains)
- IQ tuning, frequency mapping, and CAT/SDR frequency synchronization
- Bounded operator debug capture writing complex IQ as SigMF, an `iq.sigmf-data`
  file beside an `iq.sigmf-meta` sidecar, and audio as WAV, under byte and
  duration budgets (implemented)
- Cached network SDR directory with frequency/location/protocol filters
- KiwiSDR receive-only WebSocket adapter and browser handoff for unsupported
  receiver protocols

Acceptance: log a completed QSO into Log4OM and repeat the M2 decoder benchmark
from both an RTL-SDR and an SDRplay receiver.

## M5 — packaging and release

- Signed Win64 installer, macOS application bundle, and Linux packages
- CI build/test matrix, dependency/license manifest, crash diagnostics
- User manual, hardware compatibility table, and reproducible release process

## M6 — secure remote station operation

The core carries the role, protocol-version, bandwidth-profile, transmit-request
and expiring control-lease types this milestone needs, but nothing in the
application or the desktop uses them, so remote operation remains a
specification rather than a shipped feature.

- Standalone/server/client startup profiles and headless station service
- Pairing, mutual identity verification, roles, revocation, and audit log
- Versioned secure control/event protocol and full reconnect snapshots
- Exclusive expiring per-rig control leases and fault-injection tests
- Opus receive audio plus selectable spectrum/event/IQ bandwidth profiles
- Station-local CW message scheduler with idempotency and disconnect release

Acceptance: an authenticated remote operator completes a human-confirmed QSO
through a VPN while injected loss, delay, duplication, reconnect, client crash,
and server-side device removal cannot duplicate a message or leave KEY/PTT
asserted.
