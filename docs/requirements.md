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
- Provide independently selectable DC rejection, bounded manual/automatic
  input gain, automatic-gain target, and automatic/manual processing bandwidth;
  keep these controls separate from visualization range scaling.
- Accept mono audio and complex IQ through the same timestamped block contract.
- Detect candidate tones using a shared spectral analysis stage.
- Keep spectral candidates private until configurable prominence, persistence,
  narrowband coherence, keyed-edge/spacing cadence, Morse-symbol validity,
  unknown-output, timing-quality, and character-confidence gates verify a CW
  trace. Track candidate, Morse-likely, verified, and lost states with
  inspectable rejection reasons. Qualification must use bounded recent
  evidence, require sustained entry, and continuously re-evaluate verified
  tracks with demotion hysteresis. Unverified candidates must not receive
  overlays, session rows, or a detected-signal count.
- Track frequency drift and maintain separate timing/decoder state per channel.
- Preserve soft tone/envelope/timing evidence and combine an explainable
  adaptive timing decoder with an optional compact causal learned likelihood
  model; decoding must continue when that model is unavailable.
- Run a low-latency causal pass plus bounded delayed refinement passes over a
  rolling narrowband buffer. Later passes may revise only visibly provisional
  text and must never block capture.
- For co-channel pileups, use sub-bin frequency/phase, WPM, element/spacing
  cadence, edge shape, and fading as probabilistic operator fingerprints. Joint
  separation and reconstruction/cancellation must retain the original mixture
  and expose ambiguity when the input is not identifiable.
- Permit conservative strongest-track reconstruction/cancellation before a
  weak-track retry; reject the retry unless both residual and decode scores
  improve.
- Schedule candidates by signal strength, arrival order, or explicit operator
  selection. Manual selection always has a configurable priority boost.
- Display decoded text, estimated WPM, tone frequency, SNR, confidence, and
  callsign candidates without blocking capture.
- Display a callsign on a trace only after the CW track is verified and stable
  text contains a structurally valid complete token terminated by a word gap.
- Distinguish raw evidence, provisional text, stable text, and context-derived
  suggestions. Store per-character pass/evidence provenance and calibrate
  confidence on held-out recordings.
- Provide an explicitly enabled, bounded diagnostic capture mode that can
  correlate raw/conditioned audio, spectra, private candidate decisions,
  decoded character evidence, timing/frequency references, and pipeline
  overruns. Require review and redact credentials/private identifiers before
  export.
- Optionally predict and validate partial decoded callsigns against a pinned,
  checksummed list. The main decoding workspace controls this in real time;
  suggestions must remain visibly separate from raw decoder output and expose
  confidence and list provenance. Absence from a list is never proof of an
  invalid callsign.
- Keep callsign data behind a provider interface. Settings select provider
  roles, precedence, update policy, and credentials so fast offline completion,
  jurisdiction-limited official validation, and optional directory enrichment
  can be combined without hard-coding a logger or service.
- Replay WAV and SigMF/IQ recordings deterministically.
- Provide configurable noise blanking, AGC, key-click suppression, audio mute,
  and a sharp continuously adjustable CW monitor filter without coupling these
  controls to decoder correctness.
- Automatically calibrate or manually correct frequency offset and I/Q
  gain/phase imbalance for quadrature inputs, with visible diagnostics and a
  resettable calibration state.

### Display

- Spectrum and waterfall share the same FFT output used by channel detection.
- Provide profile-persisted **Audio spectrum** and **CW symbols** views. The
  former may use averaged FFT power; the latter uses unaveraged acoustic rows
  and crisp sampling to expose dit/dah/gap timing. Switching views must not
  reset or influence decoding, and CW symbols must not fabricate text.
- Render the configured CW pitch/width as a bold axis-boundary guide that is
  visually distinct from the filter-width area of verified signal traces;
  keep vertical decoded annotations in
  the spectrum region rather than over waterfall history.
- Clicking a trace selects the nearest tracked channel; clicking a callsign
  opens the QSO confirmation panel.
- Left-clicking the waterfall moves the local CW guide center to the pointed
  signal. Right-clicking is a distinct RX-only CAT tuning request that maps the
  pointed actual RF frequency to the configured CW pitch using current
  sideband, dial frequency, split state, and transverter offset; unavailable or
  ambiguous mappings are rejected visibly.
- Anchor decoded observations to checked absolute RF Hz and stable track IDs,
  not screen coordinates. Continued decoding updates an observation in place;
  configurable loss and expiry timers remove stale overlays and list entries
  consistently while permitting short-gap reacquisition.
- Rendering is independently rate-limited (initial target 30 or 60 FPS) and may
  drop display frames. DSP sample blocks must not be dropped to keep UI current.
- Zoom, dynamic range, palette, averaging, CW filter width, and tone pitch are
  configurable.
- Configure target render FPS and waterfall line/scroll rate independently.
- Keep the displayed waterfall history on a constant configurable time axis.
  Pane resizing, startup fill, and missing source intervals must not stretch or
  collapse dit/dah timing; line rate changes temporal resolution, not duration.
- Generate genuine waterfall timing frames with bounded overlapping analysis;
  never duplicate a spectrum row merely to satisfy a requested line rate.
- Provide a toggleable, configurable CW center/passband guide and labeled
  frequency ticks on the X axis. The guide is a visual reference only and must
  never select, constrain, reset, or otherwise drive decoding.
  Configure automatic/manual range, lower and upper dB bounds, averaging, peak
  hold/decay, palette, color gain, black level, contrast/gamma, grid, label
  density/font, spectrum height, zoom, and pan. Invalid combinations are
  clamped and defaults may be selected from measured machine performance.
- Hovering for a configurable delay, or pressing and holding, opens a callsign
  detail card without starting a QSO. Available details include normalized call,
  confidence, decoded context, SNR, tone/absolute frequency, WPM, first/last
  heard time, queue position, CQ state, worked-before/log status, DXCC/prefix,
  country, and ignore status. Enrichment may arrive asynchronously.
- Use a functional 2D display only. A 3D spectrum and ornamental GPU effects are
  explicit non-goals; rendering features must improve operation, diagnostics,
  accessibility, or performance.
- Provide an expandable operator workspace rather than a fixed decoder layout:
  spectrum, waterfall, band map, active calls, frequency-sliced decoder list,
  watch list, QSO workflow, logging, and diagnostics are independent dockable
  panels.
- Track calls across frequency changes and inactivity, show CQ/running/searching
  state, and color decoded tokens by semantic type and confidence using an
  accessible palette.
- Support keyboard and pointer tuning at an exact frequency, direct selection of
  a tracked station, next/previous signal and band navigation, and visual panning
  without unnecessary CAT retunes.

### Radio and transmission

- Store multiple named transceiver profiles and switch only while TX is idle.
- Each profile specifies Hamlib model, CAT serial settings, and an independent
  serial keying profile.
- Every frequency-control adapter exposes RX and TX VFO state and split
  capability through the shared core contract. Unsupported split requests fail
  explicitly; they never silently collapse to simplex.
- Store independent signed RX and TX transverter offsets in hertz. UI and logs
  distinguish radio dial frequency from calculated actual RF frequency, reject
  overflow/zero results, and show both before satellite transmission.
- Support selecting RTS or DTR and active polarity independently for PTT and
  KEY. A profile may use one or two physical serial ports.
- Start disarmed on every launch and after every device reconnect.
- Require operator confirmation of the exact selected callsign before the first
  transmission in a QSO.
- Release KEY then PTT on timeout, adapter error, device removal, workflow
  failure, or emergency stop.
- Never perform serial discovery by toggling RTS/DTR on unknown ports.
- A callsign on the persistent ignore list is ineligible for display, queueing,
  QSO confirmation, and transmission. TX safety rechecks the policy at request,
  confirmation, and keying time. Initial rules are normalized exact matches;
  wildcard/prefix rules are out of scope until their ambiguity is designed.
- Initial reference radios are Yaesu FT-450D and FT-818/FT-818ND. Their supplied
  defaults remain fully editable: port, baud, data bits, parity, stop bits,
  RTS/flow-control mode, polling interval, and timeout.
- On Windows, OmniRig Rig 1/Rig 2 is the first frequency-control integration and
  its native configuration is reachable from the application Settings pane.
  Hamlib provides the platform-neutral frequency-control path.
- CAT4OM is a network frequency-control backend using its native major-version
  compatible JSON WebSocket Control channel. It consumes pushed state, treats
  VFO names as opaque identifiers, honors group ownership, and never exposes
  CAT4OM PTT/CW operations around the local transmit-safety boundary.

### Configuration and instances

- First launch of every station profile opens a guided wizard covering radio,
  CAT alternatives, direct key/PTT, receive source, display defaults, logging,
  and a non-transmitting configuration review.
- If multiple profiles are present and none was explicitly selected, show a
  startup profile chooser with create/select actions. A command-line profile
  override remains available for shortcuts, headless servers, and automation.
- Every named station profile isolates radio, keying, audio, SDR, UI, logging,
  workflow, and remote settings. Multiple processes may run concurrently with
  different profiles.
- Acquire OS-level resource ownership locks before opening physical audio, SDR,
  or serial devices. A second profile receives a clear conflict diagnostic and
  may not steal an active device.
- Profiles define ordered station-equipment rules over canonical ADIF bands.
  Each rule can name the radio, transverter/converter chain, and antenna; the
  first matching actual-RF band wins. Cross-band QSOs preserve distinct TX and
  RX equipment descriptions.

### Logging

- Produce ADIF 3.1.7-compatible QSO records.
- Treat ADIF conformance as a release gate and follow the current specification,
  field dependencies, enumerations, deprecation policy, official resources,
  schemas, and test fixtures as detailed in the conformance policy.
- For split and satellite QSOs, calculate exact actual-RF transmit and receive
  frequencies from dial values and signed transverter offsets. Export consistent
  `FREQ`, `FREQ_RX`, `BAND`, `BAND_RX`, `PROP_MODE=SAT`, `SAT_NAME`, and
  `SAT_MODE` values as applicable.
- Derive logging-station `MY_RIG` and `MY_ANTENNA` from the station profile and
  calculated actual-RF bands. Never populate contacted-station `RIG` from local
  configuration. Cross-band descriptions identify TX and RX chains explicitly.
- First integration: Log4OM 2 inbound ADIF message over configurable UDP host
  and port. TCP and other logger protocols are separate future adapters.
- Queue unsent records locally and make retries visible; never silently report
  a QSO as logged.

### Operational compatibility

- Provide a frequency band map, verified callsign list, operator watch list,
  editable regional band plans, and an option to decode only designated CW
  segments.
- Validate candidate callsigns using configurable strictness, allocation and
  syntax rules, watch/ignore state, and optional contest master-call data.
- Export verified spots through a compatible read-only DX-cluster TCP service,
  optionally restrict output to CQ callers, and default legacy plaintext
  services to loopback unless the operator deliberately exposes them.
- Export timestamped spectrum frames by configurable UDP for logger/contest
  integrations while keeping this adapter independent of the renderer.
- Record and replay audio or I/Q in interoperable WAV/RF64 form with UTC,
  operator, station, center-frequency, channel-mapping, and software metadata;
  rotate safely, loop playback, inspect metadata, and preserve deterministic
  decoder replay.
- Support automatic receive startup per profile, a decode-disabled monitoring
  mode, multiple receivers through profiles/instances, and visible health
  indicators for CAT, input bandwidth, CPU pressure, overruns, and calibration.

### SDR

- Use SoapySDR as the device-neutral API.
- Initial plugins: RTL-SDR and SDRplay 3.
- Keep vendor drivers out of the distributable core. Detect missing modules and
  show actionable installation diagnostics.

### Network SDR

- Present a searchable receiver directory with receiver name, type, country,
  location/grid, frequency coverage, availability, and connection capability.
- Filter by required frequency/band, geography, protocol, and availability.
- Cache directory results and refresh conservatively according to each
  provider's terms and rate limits. Do not scrape an undocumented directory.
- First direct-stream protocol: KiwiSDR over WebSockets, supporting server-tuned
  audio and, where offered and permitted, IQ/waterfall data.
- Classic browser-only WebSDR entries open in the system browser or use a user-
  selected virtual audio device. Direct streaming is enabled only for a
  documented or explicitly authorized server interface.
- Treat OpenWebRX and other protocols as separate adapters; directory type does
  not imply stream compatibility.
- Network sources are receive-only. They never assert PTT/KEY, authorize TX, or
  silently retune a local radio. Linking a remote receive frequency to a local
  transmit rig requires an explicit operator action and frequency confirmation.
- Send an operator identity when required, honor receiver capacity/timeouts,
  use one connection per selected receiver, and reconnect with bounded backoff.

### Remote station operation

- The application runs as Standalone, Station Server, or Remote Client from a
  versioned startup profile. Server mode can run headless with a local status
  and emergency-stop console available.
- The station server exclusively owns hardware, CAT, keying, decoder state,
  logger connections, transmission timing, watchdogs, and audit records.
- Use a versioned secure WebSocket control/event protocol with verified TLS.
  Plaintext is loopback-test-only; public raw port forwarding is unsupported.
- Pair clients to a station identity and assign observer/operator/administrator
  roles. Support credential revocation and rotation without reinstalling.
- Grant an authenticated operator a short exclusive control lease per rig.
  Heartbeat loss expires the lease, cancels queued TX, and releases KEY/PTT.
- Transmit complete CW messages timed by the server. Never transport paddle,
  dot/dash, KEY, or PTT edges as remote timing commands.
- Every TX request carries an idempotency ID, exact confirmed callsign, rig,
  message, WPM, and weighting and passes all local safety/policy checks.
- Stream Opus receive audio, spectrum/waterfall frames, decoded events, and
  state independently. Provide bandwidth/latency profiles and bounded queues.
- Raw IQ streaming is opt-in, capacity-controlled, and disabled by default.
- On reconnect send a full epoch/sequence snapshot, then deltas. Never restore
  an old control lease, armed state, queued TX, or confirmation implicitly.
- Record authenticated client actions, lease transitions, CAT changes, TX
  decisions, faults, and emergency stops in a bounded/exportable audit log.

## Non-functional requirements

- C++20 with CMake; warning-clean on MSVC, Clang, and GCC.
- Windows 11 or newer on x64 is the first release target. macOS Sonoma 14 or
  newer is supported on Apple silicon and Intel x64; Debian and Ubuntu are the
  supported Linux families. Earlier Windows and macOS releases are unsupported.
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

- Minimum supported Debian/Ubuntu versions and reference Windows 11 x64 hardware.
- Expected WPM range, Farnsworth behavior, international characters, and
  prosigns.
- Whether full break-in or semi-break-in operation is in the initial scope.
