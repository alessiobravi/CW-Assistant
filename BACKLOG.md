# Product backlog

Updated: 2026-08-31

This is the canonical prioritized backlog. Status values are `todo`, `active`,
`blocked`, and `done`. Every source, test, build, or automation change must
review this file and update affected items or the “Last reviewed” note.

Last reviewed: 2026-08-31 — reproduced and fixed hosted QML compilation,
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
manual templates; the wizard footer is also guarded against clipping.

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
| PROC-001 | done | Keep manuals, changelog, and backlog current | Repository guidance and PR automation require user manuals plus both project records for implementation/delivery changes. |
| CI-001 | done | Add full desktop dependency/build matrix | Qt 6.11.2 desktop and core tests pass on Windows x64, Linux x64, macOS ARM64, and macOS x64; successful jobs publish artifacts, stable continuous-release assets/checksums, and a verified-commit tag. |
| CI-002 | todo | Add Windows 11 x64 runtime acceptance | Self-hosted or release-candidate testing launches the packaged app and verifies graphics/audio/serial discovery on Windows 11. |
| AUDIO-001 | todo | Add PortAudio device discovery and capture | User can select device/channel/rate/block size; callback performs no allocation or blocking. |
| REPLAY-001 | active | Add WAV replay source and deterministic clock | Dependency-free PCM/float parsing, deterministic timestamps/restart, downmix, paced UI selection/play/pause/stop, and core tests pass; hash manifests, seek, looping, and repeat-run integration remain. |
| DSP-001 | active | Implement windowing, FFT, and spectral averaging | Hann-windowed radix-2 audio/IQ analysis, dBFS normalization, averaging, frequency mapping, and deterministic tone tests pass; golden fixtures, overlap, calibration, and performance benchmarks remain. |
| UI-001 | active | Create Qt Quick desktop shell | Modern expandable receiver workspace, Settings/About panes, author metadata, profile chooser, guided setup, cross-platform offscreen QML tests, and staged native-graphics startup tests are implemented; clean-machine hardware validation remains. |
| UI-002 | active | Implement modular 2D scene-graph spectrum/waterfall | Public Qt scene-graph line/grid geometry and a backend-native, valid-texture-only waterfall image node render real replay FFT frames with bounded history; empty startup/reset regression tests pass; add palette shader/ring uploads, peak hold, overlays, metrics, and performance validation. |
| UI-003 | todo | Add clickable channel/callsign overlays | Hit testing maps labels and traces to the same frequency source of truth and emits operator-selection events. |
| UI-004 | todo | Add render-backend diagnostics and fallback tests | Active API, frame/upload metrics, and fallback reason are visible; replay smoke tests cover shader and CPU fallback paths. |
| UI-005 | active | Model configurable visualization | FPS, waterfall line rate, range bounds/mode, averaging, and grid are independently configurable and persisted; peak hold, labels, palette/levels, zoom, and pan remain. |
| CALL-002 | todo | Add delayed callsign detail card | Hover delay and press-hold show live signal/context plus asynchronous log and prefix enrichment without initiating QSO. |
| CALL-003 | active | Persist and enforce exact callsign ignore list | Core normalization and TX denial are implemented; persistence and filtering in display/queue models remain. |
| OBS-001 | todo | Add pipeline telemetry | UI exposes overruns, sequence gaps, queue depths, DSP latency, and dropped display frames. |
| OBS-002 | todo | Add operator-accessible native crash diagnostics | Windows minidumps and macOS/Linux crash-report guidance identify build/profile/backend without exposing station secrets; diagnostic export is documented and tested. |
| CFG-001 | active | Implement named station profiles and setup helper | Versioned isolated persistence, UI create/select helper, per-profile wizard, and `--profile` selection exist; audio/logger/remote pages and migrations remain. |
| CFG-002 | todo | Enforce cross-process hardware ownership | Named OS locks prevent serial/audio/SDR devices from being opened by two active profiles and report the owning profile. |

## P1 — M2 multichannel CW decode

| ID | Status | Item | Acceptance |
|---|---|---|---|
| DATA-001 | todo | Register the located CC0 pileup WAV | Manifest records source, CC0, checksum, audio format, preprocessing, and storage location. |
| DATA-002 | todo | Build deterministic synthetic CW corpus | Covers agreed WPM/SNR/drift/fading/jitter matrix with exact annotations. |
| DSP-002 | todo | Detect and track candidate CW tones | Meets documented false-channel and track-continuity targets on corpus. |
| DSP-003 | todo | Add bounded per-channel DSP worker pool | Preserves channel order, sheds lowest-priority work, and passes overload soak tests. |
| CW-001 | todo | Implement adaptive timing and Morse decoder | Publishes text, WPM, confidence, and latency; benchmark report is reproducible. |
| CALL-001 | todo | Extract and rank callsign candidates | Precision/recall targets are documented and tested, including portable calls. |
| DSP-004 | todo | Add operational DSP conditioning | Configurable noise blanker, AGC, key-click suppression, mute, and 20–700 Hz monitor filter have replay tests and bypass paths. |
| DSP-005 | todo | Add frequency and I/Q calibration | Manual/automatic correction, reset, diagnostics, and deterministic imbalance fixtures pass. |
| CALL-004 | todo | Add validation, watch, and band-plan policies | Configurable validation levels, allocation/pattern checks, master-call data, watch list, and CW-segment filtering are independently testable. |

## P2 — M3 radio and guarded transmission

| ID | Status | Item | Acceptance |
|---|---|---|---|
| CAT-001 | todo | Implement Hamlib serial CAT adapter | Enumerates supported models; connects, reads, and sets frequency on both reference rigs. |
| CAT-002 | active | Implement Windows OmniRig frequency adapter | Settings select Rig 1/2 and open the native configuration through COM; frequency read/set, state diagnostics, and both-radio hardware tests remain. |
| CAT-003 | active | Implement split and transverter frequency domain | Checked integer-Hz RX/TX resolution, signed offsets, profile persistence, and CAT split contract are implemented; adapters, actual-RF UI preview, Doppler/satellite tracking, and hardware tests remain. |
| CAT-004 | active | Implement CAT4OM network frequency provider | Native 1.x handshake, observer/control connection, password proof, pushed state, ownership, capability checks, reconnect, Settings fields, and core protocol tests exist; finish operating-panel frequency/split command sequencing and live service integration tests. |
| RIG-001 | active | Persist multiple named rig profiles | CAT/keying/framing/poll/display settings are isolated by station profile; full device settings and safe live switching remain. |
| KEY-001 | todo | Implement cross-platform RTS/DTR adapter | Line loopback tests pass on Windows, macOS, and Linux without discovery toggles. |
| QSO-001 | todo | Define declarative workflow/panel schema | Ordinary, DX-pileup, and contest panels validate without executable scripts. |
| QSO-002 | todo | Implement operator-confirmed QSO workflow | Exact callsign confirmation is required before first TX and emergency stop is always available. |
| QSO-003 | todo | Notify when the operator's own callsign is decoded | A normalized exact own-call match raises a configurable visual/audio/remote notification; an optional closing macro may be queued only when the QSO context matches, auto-reply is explicitly enabled and armed, all TX guards pass, and the operator can cancel before transmission. |

## P2 — M4 logging and SDR

| ID | Status | Item | Acceptance |
|---|---|---|---|
| LOG-001 | todo | Implement durable logging outbox | Records survive restart and retry state is visible. |
| LOG-002 | todo | Implement Log4OM 2 UDP ADIF sink | A test QSO is accepted by configurable Log4OM inbound ADIF service. |
| LOG-003 | active | Maintain ADIF conformance readiness | ADIF 3.1.7 satellite/split fields, exact frequency calculation, full band mapping, and policy exist; validated ADI/ADX import/export, official pinned fixtures, independent parser, and release report remain. |
| LOG-004 | active | Resolve station equipment by actual-RF band | Ordered ADIF-band rules and `MY_RIG`/`MY_ANTENNA` cross-band serialization are tested; profile rule editor, persistence, overlap diagnostics, and logger acceptance remain. |
| INT-001 | todo | Add read-only DX-cluster spot service | Verified calls can be served with CQ-only filtering, authentication option, bounded clients, and loopback-safe defaults. |
| INT-002 | todo | Add UDP spectrum export | Versioned timestamped spectrum frames interoperate with a documented logger/contest consumer fixture. |
| REC-001 | todo | Add interoperable audio/IQ recorder | WAV/RF64 record/replay, metadata, rotation, looping, inspection, and deterministic replay tests pass. |
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
| PKG-001 | active | Produce signed Win64 installer | Hosted WiX/MSI generation, stable major-upgrade identity, numeric build revisions, shortcuts, and stable download naming are implemented; clean Windows 11 install/upgrade/repair/uninstall tests, Authenticode signing, and signed update metadata remain. |
| PKG-002 | active | Produce macOS bundle and Debian/Ubuntu package | Hosted builds deploy Qt/QML runtime files and publish portable Sonoma 14+ Apple silicon/Intel artifacts plus a CPack `.deb`; CI verifies the Mach-O 14.0 deployment target and the root binary index points to stable filenames; validate clean Sonoma and supported Debian/Ubuntu installs and add signing before release. |
| PKG-003 | todo | Publish signed Debian/Ubuntu APT repository | Signed Release/InRelease metadata, protected key rotation, version promotion, retention, and documented repository enrollment pass clean-machine tests. |
| DOC-001 | active | Maintain operator and hardware manuals | A user-manual index plus setup, settings, hosted-build, Debian/Ubuntu, and CAT4OM guides exist; every implementation change is CI-gated on manual/changelog/backlog updates; safe keying, workflows, diagnostics, and compatibility manuals remain. |

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
