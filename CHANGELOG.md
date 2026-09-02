# Changelog

All notable changes to CW Assistant are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and releases will use
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Added dependency-free decoder foundations for the next accuracy pass: a
  bounded acoustic event lattice retains timestamped mark/gap evidence and
  produces N-best Morse segmentations without callsign or language influence;
  a separate callsign-evidence ranker preserves the raw span while scoring only
  acoustically compatible suggestions with capped provider evidence; and
  validated conversation profiles distinguish neutral monitoring, open-ended
  ordinary QSOs, and rule-specific contest exchanges. These APIs do not replace
  the live transcript or provide any path from decoder events to transmission.

- Open decoder cards now follow newly appended text automatically unless the
  operator is selecting text. Confirmed remote callsigns are bold and
  color-emphasized; an exact match for the station profile's own callsign is
  highlighted, displays **YOUR CALL HEARD**, and flashes the card five times.
  This is a receive-only visual notification and cannot initiate transmission.

- The spectrum panel now offers two profile-persisted live views: **Audio
  spectrum** keeps the smoothed FFT waterfall, while **CW symbols** renders
  only verified channels' keyed/unkeyed acoustic envelopes on a neutral
  background with nearest-neighbor sampling, so dit, dah, and gap edges remain
  visible without mixing full-passband receiver texture into the raster. Both views use
  the same live/replay stream and can be switched without resetting decoder
  state; the symbols view is raw keying evidence, not reconstructed text.
- Added the native `cwa_capture_replay` audit executable. It replays one or
  more operator-provided `audio.wav` captures through the production spectrum,
  tracking, and decoding path and reports every publication plus bounded-bank
  summary metrics, including the assigned color lease index; private captures
  remain local and are not CI fixtures.
- Added an independent bounded cadence estimator that fits recent key-down
  durations to 1/3 units and key-up durations to 1/3/7 units. Production
  capture audits now report its acoustic WPM and fit confidence alongside the
  selected decoder WPM, without using decoded words as timing evidence.
- `CwChannelBank::shiftTrackedFrequencies()`: retuning the linked radio's RX
  VFO while live audio is running now re-centers every currently tracked
  signal by the exact audio-domain shift the retune implies (accounting for
  CW-U/CW-L sideband direction), resynchronizing each track's narrowband
  mixer/filter at its new position without discarding decoded text or
  verification state — a deliberate retune no longer loses an
  already-identified signal's identity the way an unexplained jump would.
  Covered by a dedicated core test asserting the shift preserves state and
  text, then continues decoding correctly at the new frequency.
- The VFO readout now shows decimal/centesimal precision matching a real
  rig's display (e.g. `7016.45 kHz` on 40 m) via a dedicated formatter,
  instead of the coarser rounding used for spectrum axis labels.
- Debug capture (`OBS-003`) diagnostics snapshots now include a `radio`
  object (availability, RX/TX frequency, split state) on every line, so a
  capture can show whether/when the operator's VFO moved during the
  recording — a common, easily overlooked explanation for a signal that
  stops decoding partway through a capture. Covered by an extension to the
  existing `cwa_live_audio_pipeline_test`.

- Application update checking (initial `PKG-004` slice): a background check
  runs a few seconds after startup (disableable in Settings), and Settings →
  About gains a manual **Check for updates** button, comparing this build's
  version against the `version` field of the published continuous-release
  manifest. When an update is available, **Download update** fetches this
  platform's artifact, verifies its SHA-256 against the published
  `SHA256SUMS` before saving it, and discards anything that fails
  verification. Nothing is installed automatically: **Open installer** hands
  the verified download to the OS's own installer/package handler, and
  **Show in folder** reveals it as a fallback. New `Qt6::Network`-based
  `UpdateChecker` class; no silent self-install yet (tracked as the
  remaining scope of `PKG-004`).

### Changed

- A verified marker without a confirmed callsign now labels only its frequency,
  avoiding repetitive vertical **CW stream** text. The confirmed callsign
  replaces the frequency as soon as sufficient decode/context evidence exists.

- The live spectrum controls now auto-collapse to their header when the
  pointer leaves the panel, reclaiming vertical space for the spectrum and
  waterfall. Hovering expands them immediately, and **Pin** keeps them open
  while making several adjustments. Opening the Audio spectrum/CW symbols
  selector also keeps the panel expanded until the selector closes.
- CW verification and WPM selection now use bounded recent character/cadence
  evidence instead of lifetime averages. All nine fixed 8–60 WPM hypotheses
  continue processing after the initial presentation choice, and the best
  complete path is selected again at a safe silence/flush boundary, so an
  early speed decision no longer permanently disables every alternative.
- Candidate verification now requires a sustained passing interval and
  verified tracks use a separate failure hold before demotion. Narrowband
  coherence is a bounded 0–1 concentration measure, and keying evidence uses
  a per-track adaptive floor/peak envelope derived from robust two-sided
  noise references. The bounded recent unknown-symbol allowance is 30%,
  calibrated so two uncertain characters in a short otherwise-valid segment
  do not erase a verified signal; the hard-negative corpus remains the guard.
  The independent blended character-confidence floor is recalibrated to 0.40:
  hosted live-pipeline evidence showed a valid 15-symbol/85-edge track with
  passing 0.522 cadence and 0.546 pure timing was otherwise held out by the old
  0.50 value. The separate timing gate and hard-negative corpus remain intact.
- Verified-stream exit hysteresis is now six seconds, and frequency prediction
  is capped across key-up gaps rather than extrapolating a noisy drift estimate
  indefinitely. Internal tracking can still follow bounded real drift, while
  the operator-facing marker stays at its identity anchor until a known VFO
  retune moves it. A 750 ms presentation hold bridges normal Morse word gaps.
- The VFO frequency readout is now a large, prominent display (RX in green,
  TX in yellow when split is active) with a distinct SPLIT badge, instead of
  a small single-line label — matching the visual weight of the decoder
  panel it sits beside. Added a placeholder "ON AIR" indicator next to it,
  styled and ready but intentionally not wired to live state: neither the
  CAT4OM protocol nor the OmniRig properties this app currently polls
  expose an actual transmit/PTT signal, and local keying/PTT hardware
  control isn't implemented yet (`KEY-001`, `SAFE-001`).
- The CW pitch guide's axis line now sits exactly on the boundary between
  the spectrum plot and the waterfall history (matching the same 0.36
  height split the renderer already uses), rather than at the very bottom
  of the waterfall — that boundary is where an operator actually reads
  frequency against traces.

### Fixed

- Decoder-card dragging is now limited to a dedicated drag handle, so the
  adjacent close button reliably dismisses the card while its stream continues
  decoding and remains available to reopen from the spectrum marker.

- Reference serial defaults and ADIF/equipment fixtures now initialize every
  aggregate field explicitly, keeping the dependency-free core warning-clean
  under real GCC as well as Clang/MSVC.
- Clicking a verified stream now uses a direct topmost pointer target and
  reliably opens a larger, scrollable, selectable decoded-text window
  in the decoder pane. An operator-opened session follows the retained
  frequency/color identity if the tracker reacquires it under a new internal
  ID, instead of silently closing the card. Its bounded presentation
  transcript and an already-confirmed callsign also survive that replacement.
- Callsign labels now reject noise-like alphanumeric tokens with separated
  digit runs after an ordinary letter prefix, while retaining international,
  portable, numeric-prefix, and contiguous multi-digit special-event calls. A
  structurally plausible token is promoted automatically
  only with decoded exchange evidence (`DE`, `CQ`, `TU`, or `UP`) or exact
  repetition, preventing a lone report-like fragment from becoming the stream
  name. Until then the marker shows only its frequency. Labels use
  an 18 px base size, enlarge to 32 px on hover, and confirmed callsigns also
  appear prominently in the open decoder-card header.
- Retained stream identity no longer treats residual energy at the remembered
  frequency as a live carrier. Only a currently matched spectral peak can fill
  the marker or enable keyed CW-symbol rows, so background noise cannot keep a
  departed stream visibly active or paint random symbols throughout its hold.
- Verified CW areas no longer vanish after the short verification-failure hold
  merely because the carrier is silent: inactive observations now remain for
  the configured decoded-signal timeout (still 30 seconds by default, now
  selectable up to 300 seconds). If a track does expire, its frequency retains
  the same palette color for at least five minutes, including across a known
  radio retune, so later passes do not appear to change identity.
- Verified-stream areas now use a stable 120 Hz presentation width instead of
  following the decoder's rapidly adaptive analysis filter, eliminating size
  flicker while the actual 60/120/240 Hz filter remains visible in diagnostics.
  Inactive retained streams leave the plot empty except for a short horizontal
  identity-color mark on the frequency axis. Stream labels use a larger base
  font and magnify further while their marker is hovered.
- Display timing, level, CW-guide, noise, averaging, and retention controls now
  use labeled sliders with live numeric readouts. The receiver workspace lays
  them out over multiple responsive rows instead of one overflowing strip of
  number boxes; the Settings page uses the same interaction.
- Fixed macOS development archives being rejected as damaged: their custom
  bundle metadata no longer expands the name, executable, identifier, and icon
  to empty values, and deployment now seals the bundle only after its canonical
  `VERSION` resource is installed. Hosted macOS jobs validate every required
  plist value and the complete strict code-signing resource envelope before
  upload.
- Fixed a persistent, unverified frequency track carrying an implausible
  timing/text hypothesis into a later real transmission. When recent
  single-element-dominated output remains rejected and the independent
  acoustic cadence fit confirms Morse timing, only that track's decoder state
  is reacquired; its carrier/noise tracking remains continuous and verified
  text is never rewritten.
- Fixed Audio spectrum showing the receiver passband as a bright textured
  block and CW symbols turning instantaneous broadband fluctuations into
  horizontal confetti. Waterfall suppression now uses a slow per-bin baseline
  plus 55–180 Hz local side references. CW symbols no longer thresholds the
  full FFT at all: it draws only active verified channels' keying envelopes as
  sharp three-bin marks against a neutral background. Unverified/passband noise
  remains available in Audio spectrum without obscuring Morse timing.
- Fixed transient `latest.json`, checksum, or package HTTP 404/server failures
  surfacing immediately during continuous-release replacement. Publication
  now leaves the previous manifest available while binaries/checksums are
  replaced and uploads the new manifest last; the client retries transient
  network and publication failures three times with bounded backoff.

- SSH-queryable hosted job markers now retain the tail of `ctest` output when
  the test stage fails, while preserving pipeline failure through `pipefail`;
  cross-platform failures can therefore be diagnosed without privileged API
  access instead of exposing only the word `failure`.
- Updated the live-audio integration fixture for sustained verification: it
  supplies five keyed `SOS` repetitions, asserts both averaged and
  instantaneous live spectrum bins, and retains a bounded 10-second internal
  failure deadline (15-second outer CTest limit). This corrects the obsolete
  two-repetition/5-second timeout without weakening decoder assertions.
- Live-pipeline test timeouts now print the final frame-valid flag, published
  channel model, and verification summary, making a fail-closed hosted result
  actionable when the expected verified channel is absent.
- Fixed the bounded 24-track bank silently discarding every later carrier once
  full. Strong new candidates can now replace the weakest unmatched
  unverified occupancy, evidence decays while unmatched, and decoded or
  Morse-likely candidates survive normal word gaps. Established tracks reject
  identity-breaking frequency innovations, preventing an old decoder/text
  history from walking onto a different nearby peak. Deterministic regressions
  cover both saturated admission and identity preservation.
- Fixed verification latching forever after a transient pass: every acoustic
  and timing gate is continuously re-evaluated, with hysteresis preventing
  ordinary short fades from making a valid marker flap.
- Fixed `CwChannelBank::shiftTrackedFrequencies()` leaving a nonsensical
  negative-frequency track behind when a VFO retune (or several small
  retunes accumulating, e.g. an operator tuning across the band rather than
  centering on one station) carried a tracked signal's audio-domain
  frequency past 0 Hz. Such a track is now dropped outright instead of
  lingering as an invalid candidate; a track shifted too far *positive*
  already correctly expires through the existing retention timeout once it
  stops matching spectral peaks, so needed no equivalent change. Found via
  a real debug capture spanning a live VFO sweep. Covered by a new core
  test.
- Fixed `timing_quality` and `mean_character_confidence` being mathematically
  forced identical: `CwTimingDecoder::finishCharacter()` fed the exact same
  per-character `confidence_` value into both accumulators, so the two
  separately configured verification-gate thresholds
  (`minimum_verification_timing_quality`, `minimum_character_confidence`)
  were really gating on one blended signal, not independent evidence —
  found via real contest debug-capture data: a track whose text visibly
  contained a legible `TEST` never verified because the combined metric sat
  at 0.338 for its entire life. `timing_quality` now accumulates a genuinely
  separate pure element-duration-ratio precision signal, excluding the
  amplitude/keying-probability component that stays part of
  `mean_character_confidence`. `CwMultiSpeedDecoder::score()` (WPM-hypothesis
  selection) was updated to keep scoring on `mean_character_confidence`,
  preserving its original, already-tuned behavior — an initial attempt to
  leave it pointed at `timing_quality` destabilized WPM lock on the existing
  deterministic test, caught by an instrumented before/after trace before
  shipping. Covered by a new core-test assertion that the two fields
  diverge. The remaining known issue — both are lifetime-cumulative
  averages since track creation rather than windowed, so early garbled
  history can still drag down a currently-clean track's confidence — is
  deferred to its own change (tracked in `BACKLOG.md` under `CW-001`).
- Fixed the root cause behind reports of CW visibly present in the spectrum
  never being identified: analysis of an operator-provided debug capture
  (using the new `OBS-003` capture tool) showed every falsely verified track
  decoding to text overwhelmingly made of just `E` and `T` — the two
  single-element Morse characters, which timing noise reproduces far more
  often than any other character since random on/off fluctuations rarely
  sustain the longer runs needed for anything else — while the existing
  unknown-symbol-fraction gate stayed well under its threshold throughout
  (1-11%), so it never caught this failure mode. Added a character-
  distribution plausibility gate (`CwVerificationReason::
  ImplausibleCharacterDistribution`, config fields
  `minimum_plausibility_check_characters` [default 40] and
  `maximum_simple_character_fraction` [default 0.35]) that holds a track out
  of, or retroactively removes it from, `Verified` once enough decoded text
  has accumulated to judge it — unlike every other gate, this one keeps
  re-checking even an already-verified track, since implausibility can only
  be judged from accumulated text, not a single instant's evidence. The
  0.35 threshold was calibrated directly against the real capture: the three
  false-positive tracks measured 0.59, 0.45, and 0.76, while the most
  plausible real candidate measured 0.27 and the benchmark's own legitimate
  decoded text ("SOSCQTEST123") measures 0.25. The check itself is exposed
  as a standalone, directly testable pure function
  (`isCharacterDistributionImplausible`) rather than inlined into the gate,
  and is covered by dedicated unit tests plus the full existing
  `cwa_verification_benchmark` hard-negative suite (still 0 false
  publications) and `ctest` suite (all 6 tests), confirmed with both the CI
  compiler matrix's designated-initializer-strict GCC and a direct local GCC
  build.
- The hosted live-audio integration fixture now sends actual keyed Morse and
  waits for a verified channel instead of treating a continuous carrier as a
  valid decoded station.
- macOS staged-version verification now reads deterministic bundle metadata
  generated directly from the canonical build value and validates the copy of
  `VERSION` stored inside each self-contained application bundle. Hosted checks
  print the expected and native metadata values when diagnosing a mismatch.

### Changed

- Swapped which of the two spectrum overlays reads as an "area": the CW
  pitch guide is now a pair of dashed vertical boundaries at the configured
  center ± half-width (no filled band), while an active verified CW track is
  identified primarily by a stable-width colored vertical area with a thinner
  keying-state line drawn on top. The two were easy to confuse when both were
  drawn as bands; only the identified-signal highlight is now an area.

### Added

- A VFO frequency readout in the decoder panel showing the connected radio's
  actual RX dial frequency, plus TX dial frequency when split is active.
  Hidden entirely unless a live radio/CAT source is actually driving the
  audio (radio enabled, linked to the current audio input, and a resolved
  frequency plan available) and the source is live audio rather than WAV
  replay — so it stays out of the way for receive-only SWL setups and file
  playback, which have no radio state to show. `AppSettings` gained
  `controlledTxRfHz()`/`controlledSplitActive()` alongside the existing
  `controlledRxRfHz()`, sharing one internal `resolvedControlledFrequencies()`
  helper; `ReplayController::setRadioFrequencyContext()` now threads TX
  frequency and split state through to new `radioFrequencyAvailable`/
  `radioRxFrequencyHz`/`radioTxFrequencyHz`/`radioSplitActive` properties.
  The decoder panel is also renamed from "Full-spectrum CW decoder" to
  "CW Decoder".
- An operator-started, bounded debug capture (initial `OBS-003` slice): a
  "Debug capture" button in the decoder panel records the raw live audio
  feeding the decoder to a WAV file plus a JSON-lines log of every track's
  full private diagnostic state (frequency, SNR, narrowband coherence,
  filter width, verification state/reason, spectral observations, key
  transitions, decoded/unknown symbols, timing/cadence quality, WPM,
  provisional and stable text) once per second, to a timestamped folder
  under the application's standard data location. Capped at 5 minutes;
  never starts implicitly; the button and a status line make an active
  capture clearly visible; the operator is told to review the resulting
  files before sharing them, since the audio is whatever the selected input
  picked up. Backed by a new dependency-free `WavWriter` (round-trip tested
  against the existing `WavReplaySource` reader) and
  `CwChannelBank::allTrackDiagnostics()`, which exposes full per-track state
  for every track, verified or not — distinct from the normal display model,
  which continues to expose only verified tracks. Verified end to end with
  an extended `cwa_live_audio_pipeline_test` that drives a real decode
  through the pipeline and confirms both output files are well-formed.
- A "Diagnostics" toggle in the decoder panel header showing the
  pre-verification candidate/Morse-likely counts and rejection-reason tally
  as an opt-in, once-per-second snapshot (off by default) rather than a
  continuously live-updating label, so it stays available for
  troubleshooting without the flickering the always-on version had.
- Pre-verification pipeline diagnostics (private candidate and Morse-likely
  track counts plus a tally of the specific gate each currently failing track
  is blocked on) are now computed and exposed through the desktop model layer
  from `CwChannelBank::verificationDiagnostics()`, which already tracked this
  internally. An initial always-visible decoder-panel readout of this data
  proved to be constantly flickering and not useful in practice, since
  `narrowband_coherence` is a naturally noisy per-instant metric even for a
  clean tracked tone; the readout was removed from the default view. The data
  remains available on the model for a future dedicated diagnostics view.
  Unverified candidates still receive no spectrum overlay, session row, or
  detected-signal count.
- Fixed a verification-state/reason inconsistency: a track that reached
  Morse-likely could later fail an earlier gate again (for example
  `narrowband_coherence` dropping back under threshold) and stay stuck
  reporting a Morse-likely state alongside a reason that gate no longer
  supports, because `verification_state` only ever advanced and never
  re-derived from current evidence. `CwChannelBank::updateVerification()` now
  re-derives state from current evidence on every call. A deterministic core
  test asserts state/reason consistency is maintained throughout a naturally
  flickering scenario. This was confirmed against two operator screenshots of
  the initial (flickering) diagnostics readout showing Morse-likely tracks
  reporting `low-narrowband-coherence`, which the old gate ordering could
  never produce correctly.
- A configurable decoded-signal timeout (default 30 seconds, replacing the
  previous fixed 8-second value): `CwChannelBank` gained a `configure()`
  method to apply a new configuration to an existing bank without discarding
  current tracks, exposed end to end as Settings → Display → "Decoded signal
  timeout" and applied to both the live-audio and WAV-replay decoder workers.
- A deterministic broad-spectral-hump hard-negative case in the verification
  benchmark. A raised-cosine spectral feature far wider than the near/far
  prominence reference windows (matching adjacent SSB audio, AGC pumping, or
  a receiver-filter skirt rather than a narrowband CW carrier) now has a
  permanent regression test confirming it is rejected before any track is
  created, closing a coverage gap the existing steady-carrier and
  speech-like-AM cases did not exercise (those reject on keying pattern
  rather than peak shape).
- An explicit candidate → Morse-likely → verified → lost lifecycle with
  inspectable rejection reasons and frozen verification-time confidence. The
  gate now combines repeated spectral persistence, keyed edges, narrowband
  coherence, spacing cadence, known/unknown symbols, mark timing, and mean
  character confidence. Persistence tolerates ordinary key-up gaps instead of
  requiring adjacent FFT frames, preserving short high-speed marks. Bounded
  per-character evidence is carried with stable decoder output, exposed to the
  desktop model, and included in conservative decoder-state resource reporting.
- A deterministic verification benchmark with enforceable targets: clean,
  30 WPM, and weak/fading/drifting CW must acquire within six simulated
  seconds; steady carriers, speech-like amplitude modulation, irregular
  impulses, and pumping broadband noise must publish no tracks; processing
  must remain below a 0.20 real-time factor.
- Backlog specification for an explicitly enabled, bounded and redactable full
  diagnostic capture bundle containing audio, spectra, decoder evidence,
  time/frequency references, overruns, and relevant station context.
- Verified-CW publication gate between internal spectral candidates and the
  operator UI. A candidate now needs local peak prominence, repeated spectral
  observations, at least three known Morse symbols, bounded unknown-symbol
  fraction, and adequate timing quality before it receives a colored trace or
  contributes to the detected-signal count. Unverified candidates expire after
  750 ms. A deterministic five-second shaped-noise test publishes zero traces.
- FFT-resolution-aware peak qualification now uses a permissive near-shape
  check plus Hz-scaled far references. This admits real narrowband traces wider
  than a few FFT bins while rejecting broad spectral pumping before decoding.
- Completed-word callsign confirmation: vertical callsign text is withheld
  until the track is verified, timing quality passes, the text is stable, and a
  word gap proves the token is complete. Frequency/callsign annotations now sit
  inside the upper spectrum region instead of across waterfall history.
- The configurable CW guide is now one semi-transparent red band overlay rather
  than two lines that could be mistaken for decoded signal traces.
- One canonical effective build version across the About pane, Qt application
  identity, `--version`, Windows executable metadata and MSI, macOS bundle,
  Debian package, installed `VERSION` file, CAT4OM handshake, and continuous
  release manifest. Hosted builds use `major.minor.workflow-run`; local builds
  retain revision `0`. The QML startup smoke test now verifies the rendered
  About value against the application identity.
- Operator-selected decoded sessions: every detected signal keeps decoding in
  the background, while clicking its colored spectrum/waterfall marker opens a
  session card. Cards can be closed without stopping DSP, reopened from the
  marker, and reordered by dragging. Conservative decoded callsign candidates
  and frequency labels run vertically beside the matching colored trace.
  Session reordering uses an explicitly bounded cross-platform index type.
- Checked actual-RF labels for linked live radio audio. Profiles explicitly
  confirm that the selected input belongs to the configured radio and choose
  CW-U/USB or CW-L/LSB tone direction. Live OmniRig frequency polling on Windows
  and pushed CAT4OM frequency state are combined with the RX transverter offset,
  selected CW reference pitch, and decoded audio tone; WAV, SWL, unlinked, and
  unavailable-radio states remain labeled in audio hertz.
- Sub-bin carrier interpolation, bounded frequency/drift prediction, robust
  two-sided local noise tracking, centered-tone rejection, and automatically
  selected 60/120/240 Hz per-track filters. Filter selection holds the stable
  120 Hz acquisition path before adapting, and deterministic tests cover a
  40 Hz/s drifting tone, automatic widening, exact CW-U/CW-L RF mapping, and
  adjacent-signal rejection.
- Bounded multi-speed timing acquisition per frequency. Nine deterministic
  hypotheses spanning 8–60 WPM compete on accumulated timing quality and a
  conservative speed prior; the leader remains explicitly provisional during
  the 2.5-second evidence window, then one adaptive path is locked. A 2.5-second
  transmission gap permits safe speed reacquisition while preserving prior
  stable text. The benchmark now gates six speeds, weak/jittered inputs, WPM
  error, a 12→40 WPM transition, nine-hypothesis state size, CER, false output,
  and real-time factor (0/49 edits and zero speed failures in the deterministic
  baseline).
- Per-frequency raw-audio CW evidence in both live-input and WAV DSP workers.
  Each tracked channel now uses a phase-continuous complex mixer, a three-stage
  120 Hz narrowband filter, 500 Hz evidence updates, and adjacent-band noise
  references before timing decode. Spectrum averaging and display gain remain
  available for visualization/candidate discovery but can no longer directly
  assert key-down. Deterministic tests cover simultaneous independent tones,
  adjacent-tone rejection, the threaded live-audio decoder path, and suppression
  of numerical FFT-floor peaks outside the initial 96 dB acquisition range.
- Full-processed-passband CW detection with a bounded 24-track channel bank,
  automatic peak association, independent decoder/timing state per frequency,
  stable track IDs, bounded silent-track expiry, and distinct shared colors for
  spectrum/waterfall markers and frequency-sliced decode rows. The configurable
  700 Hz boundaries are now explicitly visual-only and absent from decoding;
  the redundant aggregate listening/key-down label was removed from the live
  control bar.
- Soft SNR-to-key probability, smoothed hysteretic transitions, likelihood-
  weighted timing adaptation, common punctuation/prosigns, and separate amber
  provisional versus append-only stable decoder text.
- A deterministic decoder accuracy/resource benchmark reporting CER, no-CW
  false output, processed duration, throughput/real-time factor, and a
  conservative bounded-state estimate with a 256 KiB corpus ceiling across
  slow, weak, jittered, and faster replay cases.
- Backlog/requirements for left-click local CW-slice selection, distinct
  right-click RX CAT centering, actual-RF anchored decoded tracks, in-place
  callsign refinement, loss/reacquisition, and synchronized overlay/list expiry.

- Receive-only SWL station profiles that skip CAT and key/PTT wizard pages,
  positively identified online-radio selection through Windows OmniRig status,
  safe refresh without speculative serial probing, and an explicit manual
  template path for radios that cannot be detected.
- Native Windows, macOS, and Linux audio-input enumeration with system-default
  following, hot-plug refresh, unavailable-device indication, and per-profile
  selection in an always-present wizard step and Settings → Audio page.
- Live sound-card RX with operator-controlled start/stop, microphone-permission
  handling, format conversion/downmix, a bounded allocation-free capture queue,
  overrun telemetry, and a separate DSP worker feeding the real spectrum and
  waterfall. WAV input remains available as an explicit replay mode.
- Per-profile audio conditioning with default DC rejection, optional bounded
  automatic gain and tunable dBFS target, exact manual gain, and automatic or
  user-entered processing bandwidth. Automatic display scaling is now clearly
  identified as visualization-only rather than audio gain.
- A two-tab live spectrum control panel directly below the visualization for
  immediate signal/display tuning and explicit profile saving, plus a clear
  decoder-unavailable state until the decoding milestone is implemented.
- Stable automatic display span, smoothed noise-floor/ceiling tracking,
  adjustable waterfall-only noise suppression, and live noise-floor telemetry.
- A profile-persisted constant waterfall time window that remains stable while
  history fills or the pane is resized, preserves source-time gaps as dark
  rows, and decouples represented seconds from selectable line density.
- Overlapping FFT hops tied to the selected waterfall line rate for real
  high-resolution dit/dah timing updates without sacrificing the 2,048-sample
  frequency window.
- New-profile waterfall defaults of 60 timing lines/s over a constant 10-second
  view, with immediate history control for magnifying short elements.
- A toggleable red CW passband guide with configurable 700 Hz center/width and
  a seven-point frequency scale across the spectrum/waterfall X axis.
- A normalized per-profile own station callsign field under Settings → Station,
  ready for station logging and exact own-call notification matching.
- A modern CW Morse-key application mark with native Windows executable/MSI,
  macOS bundle, Linux desktop, and Qt window icon assets. The Windows installer
  creates the **CW Assistant** Start-menu program group and desktop shortcut.
- Backlog scope for configurable own-callsign decode notification and an
  optional guarded QSO-closing macro.
- Backlog scope for optional real-time callsign prediction/validation using a
  versioned, provenance-visible callsign list while preserving raw decoder text.
- A researched high-accuracy decoder strategy combining a low-resource
  explainable timing baseline, an optional compact causal likelihood model,
  calibrated confidence, and bounded multiple-pass weak-signal refinement.
- Decoder backlog and acceptance gates for co-channel operator fingerprinting,
  joint timing separation, conservative interference cancellation, optional
  coherent receive diversity, data provenance, false-output measurement, and
  CPU/memory budgets.

- GPL-3.0-or-later license text and dependency/contribution licensing policy.
- Editable Yaesu FT-450D and FT-818/FT-818ND reference profiles with tested CAT
  framing, Hamlib IDs, compatible OmniRig command descriptions, and direct-COM
  RTS/PTT plus DTR/KEY starting values.
- Optional Qt Quick desktop target with a modern expandable receiver workspace,
  persistent Settings pane, passive serial-port enumeration, and configurable
  CAT framing, polling, timeout, keying polarity, and display rates/range.
- Windows native OmniRig configuration-dialog invocation from the Settings pane.
- Named, isolated station configuration profiles, a startup profile selector,
  create-profile helper, per-profile first-run setup wizard, and `--profile`
  override for parallel instances and unattended launches.

- Testable compatibility scope for band maps, callsign/watch/validation policy,
  operational DSP controls, I/Q calibration and recording, pointer/keyboard
  tuning, spot/spectrum exports, band plans, auto-start, and health indicators.
- Split-capable CAT domain contract and checked integer-Hz frequency resolution
  with independent signed RX/TX transverter offsets, persisted in station
  profiles and editable from Settings and guided setup.
- ADIF 3.1.7 satellite/split logging fields with exact actual-RF `FREQ`,
  `FREQ_RX`, `BAND`, `BAND_RX`, `PROP_MODE`, `SAT_NAME`, and `SAT_MODE`, plus
  current band-enumeration mapping and a conformance-readiness policy.
- Native Qt 6.11.2 desktop build automation for Windows 11 x64, Ubuntu x64,
  macOS ARM64, and macOS x64 alongside the dependency-free core matrix.
- Ordered ADIF-band station-equipment rules that select radio, transverter, and
  antenna from actual RF frequencies and emit `MY_RIG`/`MY_ANTENNA`, including
  explicit TX/RX descriptions for cross-band operation.
- Project-record verification now runs on the older Bash bundled with macOS as
  well as on Linux CI.
- GitHub-hosted desktop builds now stage Qt runtime/QML dependencies and publish
  downloadable Windows 11 x64, Linux x64, macOS ARM64, and macOS x64 artifacts.
- Native CAT4OM 1.x Control-channel client foundation with read-only monitoring,
  password-proof handshake, pushed multi-VFO/split state, sequence-gap recovery,
  bounded reconnect, explicit ownership requests, and capability-gated frequency
  controls. CAT4OM PTT/CW commands are deliberately excluded.
- Debian/Ubuntu `.deb` generation in hosted CI, including desktop integration,
  license, deployed Qt/QML runtime files, and operator manuals.
- User-facing manuals for first launch, profiles, configuration examples,
  online artifacts, Debian/Ubuntu installation, and CAT4OM operation.
- Persistent repository and CI rules requiring user-manual, changelog, and
  backlog updates to accompany implementation and delivery changes.
- Portable GitHub matrix expressions for conditional Debian package jobs.
- Root binary-download index and machine-readable manifest with exact stable
  platform URLs, plus automatic continuous-prerelease publication and SHA-256
  checksums after the complete hosted matrix succeeds.
- Application About page and Linux metadata identifying Alessio Bravi
  (IU0LFQ / AD2FC) as author and linking to the author website.
- Dependency-free deterministic WAV replay for PCM 8/16/24/32-bit and IEEE
  float32 input, with bounded blocks, multichannel mono downmix, sample-derived
  timestamps, restart behavior, and malformed-format diagnostics.
- Hann-windowed radix-2 audio/IQ spectrum analyzer with coherent-gain dBFS
  normalization, configurable exponential power averaging, exact frequency
  coordinates, and deterministic tone/replay tests.
- Receiver-workspace WAV selection and paced replay through a functional 2D Qt
  scene-graph spectrum/waterfall, with profile-persisted FPS, waterfall rate,
  automatic/manual dBFS range, DSP averaging, grid, progress, and gap metrics.
- Continuous verification marker: the `continuous` tag advances only after the
  full cross-platform desktop/core matrix and release publication succeed.
- Temporary per-platform annotated CI status tags expose exact failed-step
  outcomes and bounded Qt-installer/build diagnostics to authorized Git-only
  automation and are removed after a green run.
- Windows 11 x64 now publishes an upgrade-capable WiX/MSI installer with stable
  upgrade identity, per-run package revision, Start-menu/desktop shortcuts, and
  normal repair/uninstall registration instead of a `.tar.gz` binary archive.
- macOS Sonoma 14 or newer is now the explicit compiled baseline for both Apple
  silicon and Intel x64 artifacts, with hosted Mach-O deployment-target checks.

- Cross-platform C++20/CMake project foundation.
- Dependency-free sample block and bounded SPSC ring-buffer primitives.
- Channel scheduling by signal strength, arrival queue, or operator selection.
- Hardware-neutral interfaces for audio/SDR sources, Hamlib CAT, serial keying,
  and logger adapters.
- Human-confirmed transmission safety state machine.
- ADIF record serialization for the planned Log4OM 2 UDP integration.
- Architecture, requirements, test-data, and delivery-roadmap documentation.
- Native GitHub Actions builds for Windows x64, Linux x64, macOS ARM64, and
  macOS x64.
- Pull-request policy check requiring the changelog and backlog to accompany
  source, test, build, or workflow changes.
- Qt Quick scene-graph rendering decision, including shader palette, waterfall
  ring, clickable overlays, fallbacks, and modularity rules.
- Functional 2D visualization scope, explicitly excluding 3D spectrum and
  ornamental rendering effects.
- Configurable visualization model with independent FPS and waterfall speed,
  bounded manual dB range, averaging, and operational overlays.
- Normalized exact-match callsign ignore policy, enforced again by the TX guard,
  with unit coverage.
- Network receiver directory contracts and a receive-only KiwiSDR-first
  integration policy with safe browser handoff for unsupported protocols.
- Secure standalone/station-server/remote-client architecture with station-local
  CW timing, bandwidth profiles, reconnect snapshots, and authenticated control.
- Dependency-free exclusive per-rig control lease manager with bounded TTL and
  expiry/ownership tests.
- Windows 11 x64 or newer established as the supported Windows baseline.

### Fixed

- Prevented flat receiver noise and radio-AGC level movement from pumping the
  waterfall through bright yellow by anchoring automatic levels to a stable
  minimum span and slowing downward ceiling/floor movement.
- Removed persistent sound-card DC bias from the leftmost spectrum bin by
  default and stopped full-Nyquist audio from compressing CW activity against
  the left edge through a configurable 100–3000 Hz automatic view.
- Fixed blank live-audio spectrum/waterfall output accompanied by rising input
  overruns. The DSP drain timer now follows its worker onto the processing
  thread, with a cross-thread regression test that requires a real FFT frame.
- Kept the standalone spectrum-render startup regression target linked to the
  complete receiver source implementation and made live-audio sample-format
  handling warning-clean across current Qt platform SDKs.
- Widened the Settings drawer and added consistent horizontal content margins,
  preventing Station and device controls from sitting against the window edge.
- Kept the setup wizard's Back/Next/Finish controls in a fixed footer and made
  oversized setup pages scroll, preventing navigation from being clipped on
  small or scaled displays; the complete hosted QML compiler now validates the
  restructured dialog before packaging, including portable Qt/C++ declarations
  used by its runtime navigation smoke test.
- Fixed the cross-platform first-launch crash in the spectrum renderer. The
  waterfall now uses a backend-native image node created only after a valid
  texture exists; it never passes a null texture into the Qt scene graph.
- Added direct empty-render and full-QML startup regression tests to every
  hosted desktop build, plus a native-graphics launch test of each staged
  package with deterministic spectrum injection, covering both the empty launch
  state and waterfall texture creation.
- Windows Qt 6.11 SDK installation uses an immutable upstream downloader commit
  containing the new repository-layout correction until that correction ships
  in a released downloader. This build-only pin does not change the bundled Qt
  runtime, MSI version, or stable Windows upgrade identity.
- Windows MSI generation now supplies the canonical GPL text through CPack's
  supported UTF-8 `.txt` license input and publishes bounded WiX diagnostics on
  packaging failure.
- Corrected invalid setup-wizard QML that stopped hosted desktop compilation.
- macOS packaging now creates and deploys a self-contained `.app` bundle rather
  than archiving a non-bundle executable without its Qt/QML runtime.
- Hosted Qt installation now includes the task-tree dependency required by the
  Qt 6.11 QML plugin metadata, eliminating an incomplete-SDK warning.
- Corrected the Qt 6.11 Linux architecture identifier and added a clean,
  uncached Qt-install retry using the hosted runner's external 7-Zip binary for
  archive failures in the Python extractor.

### Security

- Transmission begins disarmed and cannot be initiated directly by decoder
  output.

[Unreleased]: https://github.com/alessiobravi/CW-Assistant/compare/HEAD
