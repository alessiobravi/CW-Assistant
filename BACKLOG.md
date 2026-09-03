# Product backlog

Updated: 2026-09-03

This is the canonical prioritized backlog. Status values are `todo`, `active`,
`blocked`, and `done`. Every source, test, build, or automation change must
review this file and update affected items or the “Last reviewed” note.

Last reviewed: 2026-09-03 — added capability-gated RX-frequency entry and
waterfall-edge stepping for linked writable OmniRig/CAT4OM providers. Checked
actual-RF-to-dial conversion preserves transverter offsets, and the active RX
VFO is changed without touching split TX, mode, PTT, or KEY. UI-003, CAT-002,
CAT-003, and CAT-004 remain active for the wider scope recorded below.

Last reviewed: 2026-09-03 — a new field capture confirmed correct carrier lock
and roughly 20 WPM cadence but exposed two downstream losses: competitive
timing suffixes were discarded at closed transmission boundaries, and repeated
moderate-confidence character-model overlaps split/duplicated a callsign. The
timing path now finalizes a bounded MAP suffix only at explicit flush, the card
prefers its spaced append-only consensus, and three aligned model windows can
confirm moderate characters. Ordinary-QSO PSE K/K/KN/AR/SK context may rank an
already complete call. Diagnostics record presented-call provenance. CW-001,
CW-002, CALL-006, and OBS-003 remain active for held-out calibration,
contextual N-best rescoring, per-sender cadence, and model qualification.

Last reviewed: 2026-09-03 — added explicit IC-7300 and IC-7610 support scope
under CAT-005. The implementation must stay behind the provider-neutral radio
boundary, retain configurable CI-V addressing, and pass mocked plus documented
hardware acceptance before either rig is advertised as supported.

Last reviewed: 2026-09-03 — added CAT-006 for a runtime, data-driven radio
catalog. Hamlib's own model/status/capability enumeration is the canonical
direct-CAT list; network control programs such as rigctld and Flrig remain
provider entries rather than duplicated radio models.

Last reviewed: 2026-09-03 — disabled the plot-level signal picker until a
receive/replay source is active, restoring the central empty-state start action
and normal cursor. UI-003 remains active for its documented wider scope.

Last reviewed: 2026-09-03 — added a bounded optional local character-refinement
path for operator-supplied ONNX models. It isolates up to four verified,
Morse-likely, or manually selected lanes at 30–50 Hz, performs asynchronous
overlapping-window inference with latest-window load shedding, and exposes
append-only consensus separately from the deterministic transcript. A
structurally valid callsign confirmed across overlapping model windows may now
complete verification only for an already Morse-likely carrier; spectral,
keying, cadence, coherence, and sustained-entry gates remain mandatory. The
model cannot create a carrier, replace raw text, or control transmission.
Character lanes use the robust presentation center so adaptive DSP-center
excursions cannot move the narrow model input off the carrier. Native runtime packaging and
strict model/metadata validation are covered on every desktop architecture.
Windows consumes a checksum-pinned official runtime distribution and disables
runtime telemetry in the application before creating a session; POSIX builds
retain the telemetry-disabled source build and package its upstream privacy
notice plus the loader-required major-version runtime alias. No character model
is bundled or downloaded. CW-002 remains active for corpus qualification,
measured error/resource gates, noise rejection, and a fully independent trained
artifact.

Last reviewed: 2026-09-03 — added the first independently generated synthetic
CW corpus/tooling slice and a dependency-free streaming probability-to-event
decoder boundary. The initial causal GRU experiment predicts only key-down and
target-channel-CW probabilities; temporal metrics exposed excessive transition
fragmentation that aggregate frame scores concealed. Fixed physical feature
scaling, a causal contrast integrator, and stable-region loss materially reduce
that failure, but no model/runtime is shipped until a locked receiver corpus,
character-level gain, runtime budget, provenance, and per-platform packaging
all pass. Established stream ridges are also reserved before global peak
ranking, with stable-center reassociation preventing a returning carrier from
being published as a duplicate beside an internally drifted track.
DATA-002, CW-002, DSP-002, CW-001, PERF-001, and UI-003 remain active.

Last reviewed: 2026-09-02 — connected the bounded timing lattice to live
envelope runs, exposing append-only acoustic consensus separately from literal
text, and added fixed-center manual probes that promote only through ordinary
verification. Right-click creates a manual probe while left-click remains
dedicated to detected streams; a stable plot-level pointer router prevents live
model refreshes from destroying a marker between press and release. Operator
labels now use the stable presentation center. CW-001,
UI-003, DSP-002, and CW-004 retain the calibrated confidence, cancellation,
weak-signal filtering, close-carrier, and true co-channel work documented below.

Last reviewed: 2026-09-02 — implemented the capture-driven CW recovery slice:
bounded recent decoder evidence, continuous processing of every fixed WPM
hypothesis with safe-boundary winner selection, saturated-bank replacement,
track identity jump rejection, adaptive key-envelope normalization, bounded
coherence, and verification enter/exit hysteresis. Added a native private-WAV
replay audit and selectable profile-persisted Audio spectrum/CW symbols views;
the latter displays verified-channel acoustic keying envelopes on a neutral
background rather than inventing decoded glyphs. CW-001 and DSP-002 remain active for the full semi-Markov path,
held-out calibration, and legally reusable real corpus.
Last reviewed: 2026-09-02 — bounded decoder input by spectral association:
after the existing 750 ms normal-gap hold, an unmatched verified or private
track receives one forced key-up/flush and its decoder remains frozen until a
real candidate matches again. A deterministic alternating-carrier fixture uses
a stronger station 85 Hz away and also preserves a 600 ms same-frequency gap.
DSP-002 remains active for calibrated multi-signal association, and CW-004
still owns true overlapping/co-channel separation.
Last reviewed: 2026-09-02 — decoder cards now follow live text, emphasize the
confirmed station call, and provide a bounded flashing visual alert for an
exact profile own-callsign match. Card close no longer competes with whole-card
interaction, explicit up/down controls provide refresh-safe ordering, and
unidentified vertical markers show only frequency. QSO-003 is
active for configurable visual behavior plus its unimplemented audio/remote
notification scope; CW-001 remains active because a new capture confirms that
compressed character/word gaps and digits require ambiguity-preserving timing,
not a global threshold reduction.
Last reviewed: 2026-09-02 — stabilized the decoder transcript viewport under
live updates: appends scroll only the viewport while it is following the
bottom, never move the text cursor, and do not override text selection or an
operator's upward scroll. Plain text plus a permanently reserved scrollbar
gutter prevents rich-text and scrollbar-driven line reflow; confirmed calls
remain prominent in the card header. Its contract test normalizes platform
line endings. UI-003 remains active for its wider interaction scope.
Last reviewed: 2026-09-02 — corrected the live-view selector auto-hide
regression and replaced indirect marker tapping with a direct pointer target.
Decoded text is now scrollable/selectable, and automatic stream naming requires
callsign structure plus exchange-role or repetition evidence. A same-frequency
field capture confirms that this role evidence cannot substitute for the
remaining CW-001 acoustic timing work or CW-004 operator separation.
Last reviewed: 2026-09-02 — corrected the macOS bundle's required identity
metadata and final resource-sealing order after a clean downloaded ARM64 build
was rejected as damaged. PKG-002 remains active for Developer ID signing,
notarization, and clean-machine acceptance; CI now rejects empty plist identity
fields or any invalid staged bundle resource envelope.
Last reviewed: 2026-09-02 — a new live capture confirmed stream acquisition but
showed repeated passes on one carrier receiving new IDs/colors. Silence now
keeps a verified observation for the actual configured timeout instead of
demoting it after the short failure hold; expired tracks reuse a bounded
frequency-color lease for at least five minutes. CALL-006 remains active for
callsign-level identity and stronger same-carrier session continuity.
Last reviewed: 2026-09-02 — anchored the five-minute color lease to the
frequency that established it so a stale verified tracker cannot walk the
remembered identity through nearby noise. Stabilized verified-marker geometry
at a fixed presentation width; the adaptive decoder filter remains diagnostic.
Inactive observations now reduce to an axis mark, stream labels enlarge on
hover, and the CW receive guide uses two unfilled dashed width boundaries.
Display values now use labeled sliders in a responsive multi-row layout rather
than an overflowing row of number boxes.
Last reviewed: 2026-09-02 — made trace activation open a larger decoded-text
window reliably and reconcile an operator-opened session across same-frequency
tracker reacquisition. Retention now preserves identity without allowing
unmatched residual noise to present the carrier as active or draw CW symbols.
The bottom live controls auto-collapse to their header and can be pinned open.
Gap prediction is capped, stale drift decays, verified-exit hysteresis is six
seconds, and a fixed presentation anchor plus short word-gap activity hold
reduce contest-stream churn and area flicker. Bounded session text and a
structurally plausible callsign persist across same-identity reacquisition.
UI-003, UI-005, and CALL-006 remain active for their documented larger scope.
Last reviewed: 2026-09-02 — added independent bounded 1:3 mark / 1:3:7 gap
cadence fitting and guarded decoder reacquisition for a cadence-confirmed
carrier stuck in implausible unverified text. Replaced global waterfall gating
with per-bin/local-side conditioning so narrow CW marks remain visible without
broad passband texture, and made continuous manifest publication last with
bounded client retries for transient release-asset errors. CW-001, DSP-002,
UI-005, OBS-003, and PKG-004 remain active for their documented larger scope.
Last reviewed: 2026-09-02 — the live integration fixture now exercises the
sustained-verification interval with five keyed repetitions, validates both
averaged and instantaneous spectrum output, and emits bounded failure
diagnostics. The independent character-confidence floor is 0.40 while cadence,
pure timing, and the complete hard-negative corpus remain separate gates.
Last reviewed: 2026-09-01 — added AUDIO-002 (selected-track audio monitor
output: play back only the selected decoded CW track's isolated,
700 Hz-repitched narrowband audio to a PC output, like an operator-enabled
bandpass filter tied to the identified trace); no implementation yet.
Last reviewed: 2026-09-01 — fixed `timing_quality` and
`mean_character_confidence` being mathematically forced identical (traced
directly to real contest debug-capture data: a track with a legible `TEST`
in its text never verified because the combined metric never crossed
threshold); see the `CW-001` note for the fix and its remaining known gap
(lifetime-cumulative rather than windowed averaging). Also added CW-006
(recognize well-known CW/contest patterns like CQ, TEST, 599, 5NN, TU, UP
as independent verification evidence, motivated by the same finding) as a
not-yet-implemented follow-up.
Last reviewed: 2026-09-01 — retuning the linked radio's VFO previously lost a
signal's tracking identity, so the receive path added
`CwChannelBank::shiftTrackedFrequencies()`: a retune while live audio is
running now re-centers every currently tracked signal by the exact
audio-domain shift implied (accounting for CW-U/CW-L sideband direction)
and resynchronizes each track's narrowband filter, without discarding
decoded text or verification state. Also gave the VFO readout rig-display
decimal precision (e.g. 7016.45 kHz), and added RX/TX frequency and split
state to every debug-capture diagnostics line so a VFO move during a
capture is visible after the fact.
Last reviewed: 2026-09-01 — implemented the check/download/verify/guided-
install slice of PKG-004: background + manual update
checks against the published manifest's version field, SHA-256-verified
download to the Downloads folder, and handoff to the OS installer/package
handler rather than a silent self-install (deferred until PKG-001/PKG-002
signing lands). Also enlarged the VFO readout (green RX / yellow TX / SPLIT
badge) to match the decoder panel's visual weight, added a styled but
intentionally unwired "ON AIR" placeholder (no backend reports real PTT/
transmit state yet — see CAT-002, CAT-004), and moved the CW guide's axis
line to sit on the spectrum/waterfall boundary rather than the bottom of
the waterfall.
Last reviewed: 2026-09-01 — added DOC-002 (render documentation diagrams,
e.g. Mermaid, instead of the ASCII art currently in docs/architecture.md,
docs/decoder-strategy.md, and docs/decisions/0001-qt-quick-spectrum-renderer.md)
with no implementation yet.
Last reviewed: 2026-09-01 — added PKG-004 (application update checking and
guided install: periodic/manual update checks against the published release
manifest, operator-confirmed download/checksum-verify/install/cleanup) per
the documented safety policy; no implementation yet.
Last reviewed: 2026-09-01 — swapped which spectrum overlay reads as an
"area" to remove visual ambiguity: the CW
pitch guide is now an unfilled pair of dashed vertical width boundaries, and
an active verified CW track is identified primarily by a
stable-width colored vertical area, with the keying-state line drawn thinner
on top. The adaptive filter width remains available as decoder diagnostics.
Last reviewed: 2026-09-01 — renamed the decoder panel from "Full-spectrum CW
decoder" to "CW Decoder" and added a VFO frequency readout showing the
connected radio's actual RX dial frequency (and TX dial frequency when split
is active), hidden entirely unless a live radio/CAT source is actually
linked and driving the current audio input in live-audio mode — never shown
for receive-only SWL setups or WAV replay, which have no radio state to
show. Reuses the existing `resolve_frequencies` core logic; `AppSettings`
gained `controlledTxRfHz()`/`controlledSplitActive()` alongside the existing
RX-only accessor.
Last reviewed: 2026-09-01 — fixed the root cause of the field-reported
"visible CW never decodes" case, isolated using a real operator debug
capture: falsely verified tracks decoded to text overwhelmingly made of the
two single-element characters E and T (the statistical signature of timing
noise, not genuine text), which the existing unknown-symbol-fraction gate did
not catch since it stayed under threshold throughout. Added a
character-distribution plausibility gate
(`CwVerificationReason::ImplausibleCharacterDistribution`) that re-checks
even an already-verified track once enough decoded text has accumulated to
judge it, calibrated directly against the real capture's numbers (0.35
threshold; the three real false positives measured 0.59/0.45/0.76, the
benchmark's legitimate text measures 0.25). Exposed as a standalone testable
function; full `ctest` suite and `cwa_verification_benchmark` hard negatives
confirmed clean, plus a real-GCC local compile (designated-initializer
strict) as an extra portability check.
Last reviewed: 2026-09-01 — implemented an initial OBS-003 slice: an
operator-started, bounded "Debug capture" that records raw live audio (WAV)
and a per-track private diagnostic log (JSON lines, once per second) to a
timestamped folder, capped at 5 minutes, never silent. Added a
dependency-free WavWriter (round-trip tested against WavReplaySource) and
CwChannelBank::allTrackDiagnostics() exposing full per-track state for every
track regardless of verification. Verified end to end with an extended
cwa_live_audio_pipeline_test driving a real decode and checking both output
files. This is the requested path to real field data now that synthetic
reproduction of the reported "visible CW not decoding" case has not
succeeded (see the entry below).
Last reviewed: 2026-09-01 — restored the pre-verification diagnostics as an
opt-in, once-per-second "Diagnostics" toggle in the decoder panel (off by
default) instead of a continuously live-updating label, after a direct
instrumented investigation into a still-unresolved field report of visible
CW streams not decoding. That investigation confirmed the core algorithm
itself reliably verifies a clean or realistically-noisy single tone within a
few seconds and creates no spurious tracks against a synthetic bumpy noise
floor, so the reported field case (many simultaneous candidate/Morse-likely
tracks saturating the 24-track cap) was not reproduced synthetically; real
diagnostic data from the operator's environment is needed to isolate it
further, which the restored toggle is intended to provide.
Last reviewed: 2026-09-01 — fixed a verification-state/reason inconsistency
where a track could stay reported as Morse-likely after later failing an
earlier gate again, confirmed against two operator screenshots showing this
exact contradiction; added `CwChannelBank::configure()` and a Settings →
Display "Decoded signal timeout" control (default 30 s, replacing a fixed
8 s) so an already-running decoder session can have its retention changed
without restarting; and removed the initial always-visible decoder-panel
diagnostics readout after confirming it was constantly flickering and not
useful in practice (`narrowband_coherence` is naturally noisy per-instant
evidence even for a clean tone), while keeping the underlying diagnostics
data on the model for a future dedicated view. Also reverted a first attempt
at bank-capacity eviction (letting a stronger candidate replace the weakest
unverified track when full) after a deterministic test showed it could evict
a genuine intermittent CW signal's own track during its normal key-up gaps;
that starvation theory (the bank saturating at its 24-track cap with
marginal candidates) remains plausible and worth a more careful design, but
is not fixed yet.
Last reviewed: 2026-09-01 — added a decoder-panel diagnostics readout exposing
pre-verification candidate/Morse-likely counts and a rejection-reason tally
without overlaying unverified candidates, and a deterministic
broad-spectral-hump hard-negative benchmark case confirming the local-
prominence guard rejects genuinely wide non-CW spectral features (adjacent
SSB audio, AGC pumping, a receiver-filter skirt) before any track is created,
in response to an operator screenshot showing two broad spectral features
that were not decoding.
Last reviewed: 2026-09-01 — reproduced and fixed hosted QML compilation,
validated the Qt desktop build/install locally, added functional WAV spectrum
replay, corrected macOS bundle deployment and Linux Qt architecture selection,
set and verify the macOS Sonoma 14+ deployment baseline, documented the
temporary Windows SDK-downloader source pin, added remotely queryable CI
outcomes/log diagnostics plus extractor retry, corrected WiX license input and
added MSI failure diagnostics, and replaced the Windows archive plan with an
upgrade-capable MSI delivery flow. The first fully green packaged matrix then
exposed a native first-launch render crash; the null-texture path is now removed
and guarded by empty-render, full-QML, and staged native-graphics startup tests
with deterministic texture creation on every platform. Setup now distinguishes
receive-only SWL operation, positively identified online radios, and explicit
manual templates; the wizard footer is also guarded against clipping. Bounded
compiler diagnostics are now available through the Git-only CI status markers,
which identified and closed the initial setup-dialog QML syntax failure and
the subsequent cross-platform Qt macro/declaration errors. Native audio-input
enumeration, per-profile selection, and bounded live RX now run for both radio
and SWL setup. Station settings include the normalized own callsign and wider
content margins. A CW operating mark is integrated across application packages,
and the Windows shortcut/program-group contract has been rechecked. The
standalone render regression remains linked against the complete receiver
source after live-audio integration and handles every current Qt sample-format
enumerator without compiler warnings. Live DSP timer affinity is now covered by
a cross-thread FFT regression that prevents blank output with queue overruns.
Audio conditioning now separates default DC rejection, optional automatic or
manual gain, visualization scaling, and automatic/manual bandwidth with
deterministic coordinate and gain tests. Decoder planning now includes an
operator-toggleable, provenance-visible callsign prediction/validation service
that never overwrites raw decoded text. Live signal/display controls now sit
below the spectrum; stable automatic levels and waterfall-only noise
suppression reduce color pumping without altering raw decoder input.
The M2 plan now defines a measured hybrid decoder, bounded multi-pass weak-signal
refinement, and same-frequency pileup separation using operator fingerprints,
joint timing inference, conservative cancellation, and optional receive
diversity.
The waterfall now has a constant profile-selected time span independent of pane
size, startup fill, and line density, preserves timestamp gaps, derives genuine
high-rate timing frames with overlapping FFT hops, and provides a configurable
CW guide plus X-axis frequency scale.
The receive-only decoder now scans the complete processed passband, maintains a
bounded independently colored state per detected frequency, publishes soft key
evidence plus provisional/stable text, and removes silent tracks consistently
from both overlay and decode-list models. The configurable 700 Hz guide is
visual-only. Confidence calibration, same-frequency separation, and
multiple-pass stages remain active backlog work.
Sub-bin interpolation and bounded drift prediction now feed automatic
60/120/240 Hz filters with asymmetric local-noise tracking. Decoder cards are
operator-opened from vertical colored markers, independently closable and
reorderable with explicit up/down controls while every track continues decoding. Conservative callsign
tokens appear vertically on their trace. Explicit profile audio/radio pairing,
CW-U/CW-L mapping, RX transverter resolution, live Windows OmniRig polling, and
CAT4OM state now produce RF labels only when the complete evidence chain is
valid; every other source remains explicitly AF.
The first hosted build correction normalized Qt's platform-sized session-list
index before clamping so GCC, Apple Clang, and MSVC share the same bound.
Versioning now derives application, About, native package/bundle metadata,
network identity, installed version record, and release manifest from one
effective CMake value; the hosted workflow revision is the patch component.
Raw spectral candidates are now private DSP state until local prominence,
repeated observation, known-symbol ratio, and timing-quality gates verify a CW
trace. Shaped broadband noise is a deterministic hard negative. Callsigns are
withheld until a stable completed word passes the same evidence gate, and their
vertical annotations now remain in the upper spectrum. The CW guide is a
translucent band rather than two signal-like lines.

## P0 — project decisions and safety

| ID | Status | Item | Acceptance |
|---|---|---|---|
| DEC-001 | done | Select an OSI-approved project license | GPL-3.0-or-later text and dependency/license policy are committed. |
| REQ-001 | blocked | Fix supported WPM, prosigns, character sets, and break-in scope | Requirements contain testable ranges. Owner input required. |
| HW-001 | active | Validate initial Yaesu radios and direct serial keying | FT-450D and FT-818 editable defaults and preliminary safety notes are recorded; physical adapter polarity and disconnected/dummy-load procedures still require validation. |
| SAFE-001 | todo | Implement independent maximum-key-down watchdog | KEY and PTT release on timeout, device error, process shutdown, and emergency stop; loopback tests pass. |
| ARCH-001 | done | Select graphical rendering architecture | ADR 0001 records the scene-graph approach, modular boundaries, 2D scope, and fallback policy. |
| ARCH-002 | done | Define secure remote-operation boundaries | ADR 0002 records roles, transports, authentication, leases, reconnect, and station-local TX rules. |

## P1 — M1 receive and visualize

| ID | Status | Item | Acceptance |
|---|---|---|---|
| BUILD-001 | done | Establish dependency-free C++20 core build | Core builds and tests passed on Windows x64, Linux x64, macOS ARM64, and macOS x64 in the first complete hosted matrix. |
| PROC-001 | done | Keep manuals, changelog, and backlog current | Repository guidance and PR automation require user manuals plus both project records for implementation and delivery changes. |
| CI-001 | done | Add full desktop dependency/build matrix | Qt 6.11.2 desktop and core tests pass on Windows x64, Linux x64, macOS ARM64, and macOS x64; successful jobs publish artifacts, stable continuous-release assets/checksums, and a verified-commit tag. |
| CI-002 | todo | Add Windows 11 x64 runtime acceptance | Self-hosted or release-candidate testing launches the packaged app and verifies graphics/audio/serial discovery on Windows 11. |
| VER-001 | done | Keep application and package versions consistent | One effective `major.minor.revision` value drives About, Qt identity, Windows executable/MSI metadata, macOS bundle metadata and in-bundle VERSION record, Debian package, CAT4OM identity, and the continuous manifest; hosted checks compare native metadata and installed records before publication. |
| AUDIO-001 | active | Add native audio device discovery and capture | Qt Multimedia discovery, hot-plug/default/unavailable state, per-profile selection, permission-gated live capture, PCM conversion/downmix, allocation-free bounded capture queue, DSP worker, overrun count, DC rejection, bounded manual/automatic gain, selectable/automatic bandwidth, and deterministic pipeline tests are implemented; add operator channel/rate/block controls, level meter, signal-driven bandwidth recommendation, disconnect/reconnect soak tests, and clean-machine hardware validation. |
| AUDIO-002 | todo | Add selected-track audio monitor output | Play the captured/processed audio to a chosen PC output device; when a decoded CW track is selected/opened, the monitor output isolates only that track's narrowband signal (reusing the decoder's own per-track tracking mixer, not a separate filter) and re-pitches it to a fixed reference tone (e.g. 700 Hz), so what the operator hears matches exactly what is being decoded rather than the full noisy passband — effectively a bandpass-filter monitor tied to the selected trace. Output device selection, level control, and mute/bypass when nothing is selected remain to design. |
| REPLAY-001 | active | Add WAV replay source and deterministic clock | Dependency-free PCM/float parsing, deterministic timestamps/restart, downmix, paced UI selection/play/pause/stop, and core tests pass; hash manifests, seek, looping, and repeat-run integration remain. |
| DSP-001 | active | Implement windowing, FFT, and spectral averaging | Hann-windowed radix-2 audio/IQ analysis, dBFS normalization, averaging, frequency mapping, and deterministic tone tests pass; golden fixtures, overlap, calibration, and performance benchmarks remain. |
| UI-001 | active | Create Qt Quick desktop shell | Modern expandable receiver workspace, Settings/About panes, author metadata, profile chooser, guided setup, cross-platform offscreen QML tests, and staged native-graphics startup tests are implemented; clean-machine hardware validation remains. |
| UI-002 | active | Implement modular 2D scene-graph spectrum/waterfall | Public Qt scene-graph line/grid geometry and a backend-native, valid-texture-only waterfall image node render real replay FFT frames with bounded history; empty startup/reset regression tests pass; add palette shader/ring uploads, peak hold, overlays, metrics, and performance validation. |
| UI-003 | active | Add clickable channel/callsign overlays | A stable-center/width colored area is the primary identification cue for each verified CW track, independent of adaptive carrier/filter changes, with a thinner keying-state line on top and an 18 px label enlarged to 32 px on hover. A stable plot-level left-button router opens/reopens a larger scrollable/selectable decoded-text card; close leaves DSP active, explicit press-stable up/down controls reorder cards, and an open card follows same-frequency/color reacquisition under a replacement internal ID. A context-confirmed callsign becomes the prominent marker/card label; otherwise the marker shows only stabilized frequency. Right-button trace-area hit testing moves the guide and opens a fixed-center neutral manual probe: measured weak evidence receives priority, text/count/color remain withheld until ordinary verification, centers outside a 12 Hz reuse boundary stay distinct, and an unverified probe expires after 30 seconds. Successful probes promote in place. A linked writable radio supports exact RX entry from an LCD-style readout with reliable Enter/Escape/focus-loss dismissal and fixed waterfall-edge stepping by a persisted 1–100 kHz setting, without changing split TX or mode. Add explicit manual-probe cancellation, direct pointed-signal radio retune/confirmation, keyboard tuning, and signal/band navigation. |
| UI-004 | todo | Add render-backend diagnostics and fallback tests | Active API, frame/upload metrics, and fallback reason are visible; replay smoke tests cover shader and CPU fallback paths. |
| UI-005 | active | Model configurable visualization | FPS, waterfall line rate, constant 5–30 second history, range bounds/mode, stable automatic span, waterfall noise suppression/margin, averaging, grid, red CW center/width guide, and seven-point frequency scale are configurable and persisted. A live profile-persisted selector switches between averaged Audio spectrum and a crisp CW symbols raster without resetting decoding; its popup keeps the auto-hiding controls open while in use. Audio suppression uses a slow per-bin baseline plus local side references; CW symbols instead draws only currently matched, active verified channels' keying envelopes on a neutral background, leaving retained identity and full-passband/unverified noise out of the raster. Startup, resize, and timestamp gaps cannot collapse or stretch time, and overlapping FFT hops provide real timing samples at the selected line rate; immediate Signal/Display controls auto-collapse below the spectrum, can be pinned, and retain explicit profile saving; peak hold, time ticks, palette selection, zoom, and pan remain. |
| UI-006 | done | Distinguish the CW guide from detected traces | The CW pitch guide is an unfilled pair of dashed vertical boundaries at the configured center ± half-width, so it cannot be mistaken for an identified signal; active verified traces use a colored vertical area, while inactive retained traces reduce to a short identity-color axis mark. Stream labels are larger and magnify again on hover; every trace remains independently colored and clickable. |
| CALL-002 | todo | Add delayed callsign detail card | Hover delay and press-hold show live signal/context plus asynchronous log and prefix enrichment without initiating QSO. |
| CALL-003 | active | Persist and enforce exact callsign ignore list | Core normalization and TX denial are implemented; persistence and filtering in display/queue models remain. |
| OBS-001 | active | Add pipeline telemetry | Pre-verification candidate/Morse-likely counts and a rejection-reason tally are exposed as an opt-in, once-per-second "Diagnostics" toggle (off by default) in the decoder panel after an always-visible version proved too flickering; overruns, sequence gaps, queue depths, DSP latency, and dropped display frames remain to add. |
| OBS-003 | active | Add operator-controlled diagnostic capture bundles | A "Debug capture" control records raw live audio (WAV) and per-track private diagnostics (JSON lines, 1 Hz, every track including unverified, now also carrying RX/TX radio frequency and split state on every line so a VFO move during the capture is visible after the fact) to a timestamped folder, capped at 5 minutes, requiring explicit start and never silent. Remaining: move the control into the Settings pane (currently only in the decoder panel header), add a button to open the capture folder directly, make the auto-stop duration a configurable Settings value (default 5 minutes, currently fixed), plus conditioned/spectrum frames, overruns, a review step, and credential/private-identifier redaction before export. |
| OBS-002 | todo | Add operator-accessible native crash diagnostics | Windows minidumps and macOS/Linux crash-report guidance identify build/profile/backend without exposing station secrets; diagnostic export is documented and tested. |
| CFG-001 | active | Implement named station profiles and setup helper | Versioned isolated persistence, UI create/select helper, per-profile wizard, and `--profile` selection exist; audio/logger/remote pages and migrations remain. |
| CFG-002 | todo | Enforce cross-process hardware ownership | Named OS locks prevent serial/audio/SDR devices from being opened by two active profiles and report the owning profile. |

## P1 — M2 multichannel CW decode

| ID | Status | Item | Acceptance |
|---|---|---|---|
| DATA-001 | todo | Register the located CC0 pileup WAV | Manifest records source, CC0, checksum, audio format, preprocessing, and storage location. |
| DATA-002 | active | Build deterministic synthetic CW corpus | A reproducible generated-from-scratch PCM corpus now covers 8–55 WPM, manual timing variation, Farnsworth spacing, shaped edges, fading, drift/flutter, receiver gain/compression, hum, impulses, nearby CW, steady/AM carriers, exact key-run annotations, checksums, and profile-grouped leakage-safe splits. Add broader receiver/audio-path simulation, exact and near-exact co-channel pileups, legally reusable labeled recordings, a locked blind receiver pack, and versioned corpus releases. |
| DSP-002 | active | Detect and track candidate CW tones | The bounded full-passband bank provides sub-bin peaks, bounded gap/drift prediction, nearby-candidate suppression, numerical-floor and FFT-resolution-aware near/far prominence guards, and private-candidate expiry. Each track separates an immutable association origin, adaptive DSP center, and fixed-width presentation center: robust evidence corrects first-verification bias, while sustained coherent motion follows through deadband, slew, dispersion/drift, and absolute-origin guards without moving identity/color. Saturated admission replaces only weak unmatched unverified occupancy, established identities reject large or cumulatively walking innovations, decoded/Morse-likely candidates survive normal word gaps, and automatic candidates reuse one identity per configured separation cell across frames so keyed FFT sidelobes cannot clone a carrier. Verified/operator identities reserve first. Two-sided noise references feed an adaptive per-track key envelope; coherence is a bounded spectral-concentration measure. Candidate → Morse-likely → verified → lost transitions combine recent spectral, edge, cadence, known-symbol, timing, confidence, and character-distribution evidence with separate enter/six-second exit hysteresis. A cadence-confirmed track held in implausible unverified text reacquires only its timing decoder instead of poisoning a later transmission at the same carrier. Only verified tracks publish IDs/colors. Deterministic saturation/identity/recovery/presentation tests and hard-negative verification benchmarks pass; native capture replay and expanded frequency/match/activity diagnostics support field audits. Add legally reusable recordings, held-out threshold/confidence calibration, stronger quantile/envelope estimation, and measured publication/character-error targets. |
| DSP-003 | todo | Add bounded per-channel DSP worker pool | Preserves channel order, sheds lowest-priority work, and passes overload soak tests. |
| CW-001 | active | Implement explainable adaptive timing baseline | Every detected passband track converts adaptive per-track key-envelope evidence to smoothed key probability and evaluates nine bounded 8–60 WPM timing hypotheses. The leader remains provisional during acquisition; every fixed anchor continues processing afterward, and silence/flush boundaries reselect the best complete path rather than making the early choice irreversible. A separate bounded run-length fit estimates acoustic WPM directly from 1:3 marks and 1:3:7 gaps; it is reported independently and guards reacquisition of cadence-confirmed tracks trapped in implausible text without broadly overriding a valid decoder leader. Character/timing/cadence quality and unknown fractions use bounded recent windows. The dependency-free event lattice now consumes immutable envelope runs at bounded completed-gap checkpoints and exposes up to four observation-scoped alternatives plus a separate append-only consensus for competitive paths, including compressed character/word gaps and explicit unknowns, without language or provider input. Callsign labeling may use this consensus only through the existing context/repetition policy; raw text is never rewritten. Remaining: held-out confidence calibration, capture-derived jitter/edge fixtures, safe fixed-lag correction across longer interrupted segments, and automatic close-carrier/co-channel separation. Validate every extension against `PERF-001` CPU/state budgets and native capture replay. |
| CW-002 | active | Add compact causal learned likelihood path | The independent experimental causal GRU still predicts only key-down and target-channel-CW probabilities, with explicit recurrent state, deterministic training/evaluation, temporal-fragmentation metrics, anti-aliased WAV inference, and checked ONNX export. A separate optional desktop refinement boundary now accepts an operator-supplied character model: strict metadata/tensor validation, per-architecture ONNX Runtime packaging, bounded 30–50 Hz stable-center lanes, asynchronous latest-window load shedding, timestamped CTC hypotheses, stale-generation rejection, and append-only overlap consensus are implemented. At most four verified, Morse-likely, or manually selected tracks are refined. A structurally valid callsign confirmed across overlapping windows may complete verification only after the same track independently passes carrier, keyed-edge, cadence, coherence, and sustained-entry gates. Model output remains separately labeled and cannot create a carrier, rewrite raw text, keep silence active, or control TX. No character model is bundled or downloaded. Remaining: qualify models on a locked legally reusable receiver corpus, publish CER/callsign/no-CW/resource gates, improve segment/noise abstention, train a fully independent artifact, and retain the deterministic fallback. |
| CW-003 | todo | Add bounded multiple-pass weak-signal refinement | Live, rolling 2–5 second, and completed-segment passes rescore alternative filters/tracks/timing with explicit revision rules; refinement is load-shed before capture and publishes pass provenance. |
| CW-004 | todo | Separate co-channel pileup operators and cancel interference | A bounded two-then-three-source factorial timing model fingerprints sub-bin carrier/phase drift, WPM, dit/dah and spacing cadence, keying edges, and fading; confidently reconstructed tracks may be subtracted only when residual/decode scores improve, original evidence is retained, and unidentifiable overlaps are reported as ambiguous. |
| CW-005 | todo | Add optional coherent receive diversity | Synchronized receiver/antenna inputs can contribute spatial or confidence diversity, but bad alignment or a weak source must never degrade the best single-input held-out result. |
| CW-006 | todo | Recognize well-known CW patterns as verification evidence | A recognized prosign/Q-code/contest token (`CQ`, `TEST`, `599`, `5NN`, `TU`, `UP`, and similarly distinctive ones — deliberately excluding short/common ones like `K`/`DE` that noise can hit by chance) appearing in a track's accumulated text is strong independent evidence of genuine Morse, distinct from the aggregate character-confidence score. Motivated directly by real debug-capture data: a real contest track's text contained a legible `TEST` yet never verified because `timingQuality` (see `DSP-002`'s known defect below) stayed under threshold for the track's entire lifetime. Add as an additional verification path (pattern found + minimal supporting evidence → verify) rather than replacing the existing gates, and calibrate/test the token list against real noise captures so it cannot reopen the noise-verification problem `DSP-002`'s plausibility gate closed. |
| PERF-001 | active | Build decoder accuracy/resource benchmark gate | Deterministic gates cover zero primary and consensus edits across 8–55 WPM, a compressed-gap callsign repaired by consensus, append-only long-stream truncation, speed acquisition/change, no-CW false characters, clean/30 WPM/weak verified-track acquisition, five interference hard negatives (including a broad-spectral-hump case distinguishing wide non-CW energy from a narrowband CW carrier), a maximum 0.20 real-time resource factor, and a conservative decoder-state estimate capped at 256 KiB across the timing corpus. Raw-bank tests cover simultaneous tones and adjacent rejection; the threaded live fixture requires keyed Morse. Extend with WER, call precision/recall, calibrated SNR curves, co-channel separation, provisional/stable latency/revisions, true platform peak memory, real recordings, and per-platform overload behavior. |
| CALL-001 | active | Extract and rank callsign candidates | A conservative normalized letter+digit token is exposed only after its track is verified, a stable word gap confirms the complete token, and exchange context (`DE`, `CQ`, `TU`, callsign-before-`UP`) or exact repetition supports it. Runner-identifying context outranks a repeated standalone caller; lone call-shaped noise/report fragments remain hidden. The timing layer now preserves bounded `?`/gap alternatives and exposes append-only acoustic consensus separately; a complete call from that consensus may pass the same context/repetition label policy without rewriting raw text. Add per-character alternative alignment, frequency-scoped repetition, segment roles, ranked callsign suggestions, optional external validation, and measured precision/recall targets including portable calls. Raw acoustic text must remain available and a suggestion must never silently replace it. |
| CALL-006 | active | Maintain frequency-anchored decoded observation lifecycle | Tracks retain stable IDs/colors through keyed gaps and silence, update overlays and operator-selected sessions in place, and expire after a profile-configurable hold (Settings → Display, default 30 s, maximum 300 s). Retention preserves identity/text but cannot assert active/keyed state; explicit source/prefix provenance prevents simultaneous nearby tracks from overwriting one observation and carries a bounded 2,048-character transcript plus confirmed callsign exactly once across a genuine replacement. Concurrent published identities own distinct colors, while a later reacquisition reuses its unoccupied five-minute frequency-color lease. Composed presentation text is never rescored as raw callsign repetition. Leases follow known RX retunes. Linked live radio audio can show checked actual RF using provider state, transverter offset, CW pitch, and sideband direction; add a visible/configurable lost state, viewport-independent RF reacquisition, and callsign-level identity. |
| DSP-004 | todo | Add operational DSP conditioning | Configurable noise blanker, AGC, key-click suppression, mute, and 20–700 Hz monitor filter have replay tests and bypass paths. |
| DSP-005 | todo | Add frequency and I/Q calibration | Manual/automatic correction, reset, diagnostics, and deterministic imbalance fixtures pass. |
| CALL-004 | todo | Add validation, watch, and band-plan policies | Configurable validation levels, allocation/pattern checks, master-call data, watch list, and CW-segment filtering are independently testable. |
| CALL-005 | active | Add optional provider-based callsign prediction and validation | The dependency-free core models immutable raw hypotheses and a bounded local `master.scp`/Call History index. Settings load up to 32 MiB/one million unique records without network access; a separately marked `≈`/`DB` suggestion appears only when at least two competitive acoustic paths independently contain the database call and it matches a completed uncertain call-shaped span. It never changes transcript text or verifies CW. Remaining: checksummed source/version metadata, richer character-difference rationale, indexed fuzzy lookup outside the refresh path, and then bounded authenticated directory lookup where terms permit it. Database absence never penalizes a valid acoustic candidate. |
| CALL-007 | todo | Correlate read-only DX-cluster/RBN evidence | A profile-configured receive-only provider ingests documented DXSpider-style spots or a documented HTTPS activity API, normalizes callsign/frequency/time/mode, expires stale data, and ranks a decoder candidate only when checked RF frequency, age, mode, and acoustic edit distance agree. Show source, spotter, age, frequency delta, and confidence beside—not inside—the immutable raw transcript. Cluster evidence cannot by itself verify CW, confirm a callsign, distinguish two iso-frequency senders, post a spot, or initiate TX. Use TLS where the provider supports it; legacy Telnet requires explicit opt-in, bounded reconnect/rate limits, credential-safe diagnostics, and no commands beyond login/read filtering. |

## P2 — M3 radio and guarded transmission

| ID | Status | Item | Acceptance |
|---|---|---|---|
| CAT-001 | todo | Implement Hamlib serial CAT adapter | Enumerates supported models; connects, reads, and sets frequency on both reference rigs. It must implement the same provider-neutral RX-frequency control boundary used by the operating-panel readout and waterfall-edge step controls; no provider-specific duplicate UI. |
| CAT-002 | active | Implement Windows OmniRig frequency adapter | Settings select Rig 1/2, open native configuration, and poll online RX frequency through COM for evidence-gated live RF labels. RX tuning now requires online/receive state plus the advertised writable mask and selects active `FreqA`/`FreqB` where available, falling back to writable `Freq`; it never invokes simplex/split/mode/TX operations. Richer authoritative split/TX/PTT diagnostics and both-radio hardware tests remain. |
| CAT-003 | active | Implement split and transverter frequency domain | Checked integer-Hz RX/TX resolution, signed offsets, profile persistence, and CAT split contract are implemented; a large decoder-panel VFO readout shows resolved actual RF. Exact RX entry and configurable stepping use a checked inverse RX offset before sending the provider dial frequency; split TX and mode remain unchanged. Retuning live RX re-centers tracked signals with sideband-aware mapping. OmniRig still lacks authoritative split/TX display, and setup preview, Doppler/satellite tracking, and hardware tests remain. |
| CAT-004 | active | Implement CAT4OM network frequency provider | Native 1.x handshake, observer/control connection, password proof, pushed state, ownership, capability checks, reconnect, Settings fields, and core protocol tests exist. Operating-panel RX tuning now requires master ownership plus advertised `SetFrequency` and explicitly targets the opaque active RX VFO while pushed state remains authoritative. Split command UI/sequencing, live service integration tests, and a protocol extension for actual transmit/PTT state remain. |
| CAT-005 | todo | Add Icom IC-7300 and IC-7610 radio support | Add both rigs through the provider-neutral CAT boundary (Hamlib/direct CI-V and compatible external providers), with configurable CI-V address/baud, USB audio-link guidance, online/capability discovery, RX/TX frequency, mode and split readback/control, and safe inactive PTT/KEY initialization. Unit tests use protocol mocks; documented hardware acceptance verifies reconnect, VFO selection, split operation, and read/write behavior without unintended transmission before either model is listed as supported. |
| CAT-006 | todo | Populate a data-driven catalog of well-known radios | Enumerate manufacturer, model, backend version, support status, and advertised capabilities from the bundled/selected Hamlib release at runtime, with searchable selection and stable saved model identity. Treat Hamlib NET rigctl, Flrig, OmniRig, CAT4OM, and future control programs as provider backends rather than duplicating static rig lists. Never claim that catalog presence proves every command works: capability-gate frequency/mode/split/PTT/KEY, preserve safe inactive serial lines, show backend status, allow tested per-model overrides, and maintain mocked plus representative hardware acceptance results. |
| RIG-001 | active | Persist multiple named rig profiles | CAT/keying/framing/poll/display settings are isolated by station profile; full device settings and safe live switching remain. |
| KEY-001 | todo | Implement cross-platform RTS/DTR adapter | Line loopback tests pass on Windows, macOS, and Linux without discovery toggles. |
| QSO-001 | active | Define declarative workflow/panel schema | A dependency-free validated profile model now separates neutral monitoring, open-ended ordinary/general CW, and individually defined rule-derived contest exchanges, with typed fields, role transitions, field-scoped aliases, and inert macro metadata rather than executable scripts. Remaining profiles include DX pileup, special events, and beacons; application/UI state and runtime trust must keep acoustic, provider-suggested, operator-accepted, and exact-TX-confirmed calls distinct. Suggested/automatic replies require explicit per-profile enablement, exact-call/context confirmation, armed TX, cancellable preview, maximum-key-down, and emergency release. |
| QSO-002 | todo | Implement operator-confirmed QSO workflow | Exact callsign confirmation is required before first TX and emergency stop is always available. |
| QSO-003 | active | Notify when the operator's own callsign is decoded | An exact normalized match in stable decoded text is highlighted, labels **YOUR CALL HEARD**, and flashes the open decoder card for five bounded pulses. Add configurable visual behavior plus opt-in audio/remote notifications and repeat/rate limiting. An optional closing macro may be queued only when the QSO context matches, auto-reply is explicitly enabled and armed, all TX guards pass, and the operator can cancel before transmission. |

## P2 — M4 logging and SDR

| ID | Status | Item | Acceptance |
|---|---|---|---|
| LOG-001 | todo | Implement durable logging outbox | Records survive restart and retry state is visible. |
| LOG-002 | todo | Implement Log4OM 2 UDP ADIF sink | A test QSO is accepted by configurable Log4OM inbound ADIF service. |
| LOG-003 | active | Maintain ADIF conformance readiness | ADIF 3.1.7 satellite/split fields, exact frequency calculation, full band mapping, and policy exist; validated ADI/ADX import/export, official pinned fixtures, independent parser, and release report remain. |
| LOG-004 | active | Resolve station equipment by actual-RF band | Ordered ADIF-band rules and `MY_RIG`/`MY_ANTENNA` cross-band serialization are tested; profile rule editor, persistence, overlap diagnostics, and logger acceptance remain. |
| INT-001 | todo | Add read-only DX-cluster spot service | Verified calls can be served with CQ-only filtering, authentication option, bounded clients, and loopback-safe defaults. |
| INT-002 | todo | Add UDP spectrum export | Versioned timestamped spectrum frames interoperate with a documented logger/contest consumer fixture. |
| REC-001 | active | Add interoperable audio/IQ recorder | A dependency-free PCM16 WAV writer exists (round-trip tested against the existing WAV reader) and is used by the OBS-003 debug capture; RF64, IQ, metadata, rotation, looping, and a dedicated operator-facing recorder UI (independent of debug capture) remain. |
| SDR-001 | todo | Add SoapySDR stream adapter | Enumerates modules and produces timestamped IQ blocks with overflow telemetry. |
| SDR-002 | todo | Validate RTL-SDR | Installation diagnostics and replay/live acceptance test pass. |
| SDR-003 | todo | Validate SDRplay 3 | External vendor API is detected and live acceptance test passes. |
| NET-001 | todo | Implement cached network receiver directory | Normalized entries filter by band/frequency, location, protocol, and availability; provider terms and refresh limits are documented. |
| NET-002 | todo | Implement KiwiSDR WebSocket sample source | Receives permitted audio/IQ/waterfall with identity, capacity handling, sequence telemetry, and bounded reconnect. |
| NET-003 | todo | Add browser/virtual-audio receiver handoff | Browser-only receiver entries tune via supported URL parameters and guide audio-device selection without private protocol use. |
| NET-004 | todo | Evaluate OpenWebRX adapter | Implement only against a documented stable interface with replayable protocol fixtures. |
| NET-005 | todo | Guard remote-RX/local-TX frequency linking | Local rig retune requires explicit action and confirmation; network sources can never acquire TX ownership. |

## P3 — release engineering

| ID | Status | Item | Acceptance |
|---|---|---|---|
| PKG-001 | active | Produce signed Win64 installer | Hosted WiX/MSI generation, stable major-upgrade identity, numeric build revisions, branded executable/product icon, `CW Assistant` Start-menu program group, desktop shortcut, and stable download naming are implemented. The finish page offers to launch the app, leaving the option unchecked on clean installs and selecting it for interactive upgrades. Running-process closure uses WiX's standard execute sequence with a bounded wait; CI rejects UI-sequence invocation or the wrong WiX binary and verifies the close contract, conditional default, and launch target through PowerShell 7-compatible reflected COM access. Clean Windows 11 install/upgrade/repair/uninstall runtime tests, migration from the out-of-support WiX v3 toolchain, Authenticode signing, and signed update metadata remain. |
| PKG-002 | active | Produce macOS bundle and Debian/Ubuntu package | Hosted builds deploy Qt/QML runtime files and publish portable Sonoma 14+ Apple silicon/Intel artifacts plus a CPack `.deb`; CI verifies required macOS plist identity/version fields, the complete bundle resource seal, the Mach-O 14.0 deployment target, and stable filenames; validate clean Sonoma and supported Debian/Ubuntu installs, then add Developer ID signing and notarization before release. |
| PKG-003 | todo | Publish signed Debian/Ubuntu APT repository | Signed Release/InRelease metadata, protected key rotation, version promotion, retention, and documented repository enrollment pass clean-machine tests. |
| PKG-004 | active | Add application update checking and guided install | A background check (disableable, ~4 s after startup) and Settings → About **Check for updates** compare the running version against the published manifest. **Download update** fetches this platform's artifact and verifies SHA-256 before saving. Continuous publication replaces binaries/checksums before publishing the manifest pointer, while the client retries transient 404/timeout/server failures with bounded backoff. Once verified, **Open Installer** and the platform-specific reveal action replace the download control in place and hand the file to the OS rather than installing silently. Remaining: silent self-install-and-relaunch is intentionally deferred until Windows Authenticode and macOS notarization signing land (`PKG-001`, `PKG-002`). |
| DOC-001 | active | Maintain operator and hardware manuals | A user-manual index plus setup, settings, hosted-build, Debian/Ubuntu, and CAT4OM guides exist; every implementation change is CI-gated on manual/changelog/backlog updates; safe keying, workflows, diagnostics, and compatibility manuals remain. |
| DOC-002 | todo | Render documentation diagrams instead of ASCII art | `docs/architecture.md`, `docs/decoder-strategy.md`, and `docs/decisions/0001-qt-quick-spectrum-renderer.md` currently draw box/flow diagrams as ASCII art; replace them with rendered diagrams (e.g. Mermaid, kept as reviewable source text in the same Markdown files rather than committed binary images) and establish that convention for new architecture/decision documentation going forward. |

## P2 — secure remote operation

| ID | Status | Item | Acceptance |
|---|---|---|---|
| REM-001 | active | Implement remote roles and per-rig lease domain | Role/message contracts and bounded exclusive lease manager pass dependency-free expiry tests; persistence remains. |
| REM-002 | todo | Define and generate versioned wire schema | Compatibility tests reject unknown major versions and preserve unknown optional fields. |
| REM-003 | todo | Implement secure WebSocket station/client adapters | Verified TLS only outside loopback tests; size/rate/connection limits and malformed-frame tests pass. |
| REM-004 | todo | Implement local pairing, roles, revocation, and key storage | Observer/operator/admin permissions and credential lifecycle pass integration tests on every OS. |
| REM-005 | todo | Add full snapshot/delta reconnect protocol | Epoch/sequence gaps trigger resnapshot; control, arming, confirmation, and queued TX never resume implicitly. |
| REM-006 | todo | Stream Opus receive audio | Jitter buffer exposes latency/loss; audio degrades independently of control and decoder events. |
| REM-007 | todo | Add remote spectrum/event/IQ subscriptions | Bandwidth profiles are enforced with bounded queues; IQ is opt-in and capacity-controlled. |
| REM-008 | todo | Implement station-local idempotent CW scheduler | Complete messages retain timing under network jitter; duplicates, disconnects, lease loss, and limits are safe. |
| REM-009 | todo | Add remote audit and fault-injection suite | Loss, delay, reorder, duplicate, reconnect, crashes, and device removal never duplicate TX or leave lines asserted. |
| REM-010 | todo | Document VPN/reverse-proxy deployments | LAN/VPN setup is supported; public raw port forwarding is explicitly rejected. |
