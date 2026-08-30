# Product requirements baseline

Version: 0.1, 2026-08-30

## Operating scope

CW Assistant receives either demodulated audio or SDR IQ, discovers CW signals
within the visible passband, decodes each tracked signal independently, and
shows candidates on a spectrum/waterfall. The operator can select a decoded
callsign and, after confirmation, conduct a guided CW QSO.

The initial workflows are ordinary QSO, DX pileup, and contest. Workflows use
editable structured panels rather than unrestricted scripts at first. A panel
defines fields, exchange steps, macros, validation, and transitions; this makes
operator behavior inspectable and prevents arbitrary code from controlling TX.

## Functional requirements

### Receive and DSP

- Enumerate and select audio input devices.
- Configure sample rate, input channel, block size, latency preference, and
  maximum simultaneously decoded channels.
- Accept mono audio and complex IQ through the same timestamped block contract.
- Detect candidate tones using a shared spectral analysis stage.
- Track frequency drift and maintain separate timing/decoder state per channel.
- Schedule candidates by signal strength, arrival order, or explicit operator
  selection. Manual selection always has a configurable priority boost.
- Display decoded text, estimated WPM, tone frequency, SNR, confidence, and
  callsign candidates without blocking capture.
- Replay WAV and SigMF/IQ recordings deterministically.

### Display

- Spectrum and waterfall share the same FFT output used by channel detection.
- Clicking a trace selects the nearest tracked channel; clicking a callsign
  opens the QSO confirmation panel.
- Rendering is independently rate-limited (initial target 30 or 60 FPS) and may
  drop display frames. DSP sample blocks must not be dropped to keep UI current.
- Zoom, dynamic range, palette, averaging, CW filter width, and tone pitch are
  configurable.

### Radio and transmission

- Store multiple named transceiver profiles and switch only while TX is idle.
- Each profile specifies Hamlib model, CAT serial settings, and an independent
  serial keying profile.
- Support selecting RTS or DTR and active polarity independently for PTT and
  KEY. A profile may use one or two physical serial ports.
- Start disarmed on every launch and after every device reconnect.
- Require operator confirmation of the exact selected callsign before the first
  transmission in a QSO.
- Release KEY then PTT on timeout, adapter error, device removal, workflow
  failure, or emergency stop.
- Never perform serial discovery by toggling RTS/DTR on unknown ports.

### Logging

- Produce ADIF 3.1.7-compatible QSO records.
- First integration: Log4OM 2 inbound ADIF message over configurable UDP host
  and port. TCP and other logger protocols are separate future adapters.
- Queue unsent records locally and make retries visible; never silently report
  a QSO as logged.

### SDR

- Use SoapySDR as the device-neutral API.
- Initial plugins: RTL-SDR and SDRplay 3.
- Keep vendor drivers out of the distributable core. Detect missing modules and
  show actionable installation diagnostics.

## Non-functional requirements

- C++20 with CMake; warning-clean on MSVC, Clang, and GCC.
- Win64 first, with CI builds for current Windows, macOS, and Ubuntu.
- Core and DSP have no dependency on Qt and are usable by tests and tools.
- No allocation, locks, logging, filesystem access, or UI calls in an audio
  callback.
- Bounded queues with explicit overflow counters; no unbounded sample queues.
- Configuration is versioned and migrations are tested.
- Hardware-free unit and replay tests are required for every DSP change.
- User-visible diagnostics expose underruns, overruns, queue depths, active
  workers, processing latency, and dropped display frames.

## Performance acceptance targets to measure

Final defaults require benchmark recordings and a reference Win64 machine. The
initial engineering targets are:

- No capture overruns during a 60-minute replay/live soak test.
- UI remains interactive while the configured decoder-channel limit is active.
- End-of-character decoded latency below 250 ms beyond the Morse timing itself.
- TX line release within 50 ms of an emergency-stop request where the OS and
  serial driver permit it.

The channel limit, FFT size, overlap, display rate, and worker count will be
selected by startup calibration, with user overrides. A safe starting worker
count is `max(1, hardware_concurrency - 2)`, capped by the configured channel
limit.

## Decisions still required

- Open-source license (GPL-3.0-or-later, MPL-2.0, or another OSI license).
- Minimum Windows/macOS/Linux versions and reference Win64 hardware.
- Expected WPM range, Farnsworth behavior, international characters, and
  prosigns.
- First physical transceiver/keying interface used for hardware acceptance.
- Whether full break-in or semi-break-in operation is in the initial scope.
