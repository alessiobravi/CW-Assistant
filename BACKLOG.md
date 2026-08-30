# Product backlog

Updated: 2026-08-30

This is the canonical prioritized backlog. Status values are `todo`, `active`,
`blocked`, and `done`. Every source, test, build, or automation change must
review this file and update affected items or the “Last reviewed” note.

Last reviewed: 2026-08-30 — added secure remote station/client architecture,
station-local CW timing, exclusive leases, reconnect, media, and security work.

## P0 — project decisions and safety

| ID | Status | Item | Acceptance |
|---|---|---|---|
| DEC-001 | blocked | Select an OSI-approved project license | `LICENSE` exists and dependency/license policy is documented. Owner decision required. |
| REQ-001 | blocked | Fix supported WPM, prosigns, character sets, and break-in scope | Requirements contain testable ranges. Owner input required. |
| HW-001 | blocked | Select the first reference rig and serial keying interface | Model, CAT settings, cable, line polarity, and safe test procedure are recorded. Owner input required. |
| SAFE-001 | todo | Implement independent maximum-key-down watchdog | KEY and PTT release on timeout, device error, process shutdown, and emergency stop; loopback tests pass. |
| ARCH-001 | done | Select graphical rendering architecture | ADR 0001 records the scene-graph approach, modular boundaries, 2D scope, and fallback policy. |
| ARCH-002 | done | Define secure remote-operation boundaries | ADR 0002 records roles, transports, authentication, leases, reconnect, and station-local TX rules. |

## P1 — M1 receive and visualize

| ID | Status | Item | Acceptance |
|---|---|---|---|
| BUILD-001 | active | Establish dependency-free C++20 core build | Core builds and tests on Windows x64, Linux x64, macOS ARM64, and macOS x64 CI. Mark done after the first full remote matrix passes. |
| PROC-001 | done | Keep changelog and backlog current | Repository guidance and PR automation enforce records for material implementation changes. |
| CI-001 | todo | Add full desktop dependency/build matrix | Qt UI and adapter targets compile on all four native runners, in addition to dependency-free core CI. |
| CI-002 | todo | Add Windows 11 x64 runtime acceptance | Self-hosted or release-candidate testing launches the packaged app and verifies graphics/audio/serial discovery on Windows 11. |
| AUDIO-001 | todo | Add PortAudio device discovery and capture | User can select device/channel/rate/block size; callback performs no allocation or blocking. |
| REPLAY-001 | todo | Add WAV replay source and deterministic clock | Repeated runs emit identical timestamped sample blocks and hashes. |
| DSP-001 | todo | Implement windowing, FFT, and spectral averaging | Golden-vector tests pass within documented numerical tolerance. |
| UI-001 | todo | Create Qt Quick desktop shell | Settings, receiver controls, diagnostics, and empty QSO panel run on all targets. |
| UI-002 | todo | Implement modular 2D scene-graph spectrum/waterfall | ADR 0001 boundaries are preserved; shader and fallback paths maintain selected 30/60 FPS target without blocking DSP; no 3D or ornamental effects. |
| UI-003 | todo | Add clickable channel/callsign overlays | Hit testing maps labels and traces to the same frequency source of truth and emits operator-selection events. |
| UI-004 | todo | Add render-backend diagnostics and fallback tests | Active API, frame/upload metrics, and fallback reason are visible; replay smoke tests cover shader and CPU fallback paths. |
| UI-005 | active | Model configurable visualization | FPS, waterfall line rate, range bounds/mode, averaging, peak hold, grid, labels, palette/levels, zoom, and pan are independently configurable and persisted. Core bounds are implemented; UI/persistence remain. |
| CALL-002 | todo | Add delayed callsign detail card | Hover delay and press-hold show live signal/context plus asynchronous log and prefix enrichment without initiating QSO. |
| CALL-003 | active | Persist and enforce exact callsign ignore list | Core normalization and TX denial are implemented; persistence and filtering in display/queue models remain. |
| OBS-001 | todo | Add pipeline telemetry | UI exposes overruns, sequence gaps, queue depths, DSP latency, and dropped display frames. |

## P1 — M2 multichannel CW decode

| ID | Status | Item | Acceptance |
|---|---|---|---|
| DATA-001 | todo | Register the located CC0 pileup WAV | Manifest records source, CC0, checksum, audio format, preprocessing, and storage location. |
| DATA-002 | todo | Build deterministic synthetic CW corpus | Covers agreed WPM/SNR/drift/fading/jitter matrix with exact annotations. |
| DSP-002 | todo | Detect and track candidate CW tones | Meets documented false-channel and track-continuity targets on corpus. |
| DSP-003 | todo | Add bounded per-channel DSP worker pool | Preserves channel order, sheds lowest-priority work, and passes overload soak tests. |
| CW-001 | todo | Implement adaptive timing and Morse decoder | Publishes text, WPM, confidence, and latency; benchmark report is reproducible. |
| CALL-001 | todo | Extract and rank callsign candidates | Precision/recall targets are documented and tested, including portable calls. |

## P2 — M3 radio and guarded transmission

| ID | Status | Item | Acceptance |
|---|---|---|---|
| CAT-001 | todo | Implement Hamlib serial CAT adapter | Enumerates supported models; connects, reads, and sets frequency on reference rig. |
| RIG-001 | todo | Persist multiple named rig profiles | CAT and keying ports/lines/polarities are independent and safely switchable. |
| KEY-001 | todo | Implement cross-platform RTS/DTR adapter | Line loopback tests pass on Windows, macOS, and Linux without discovery toggles. |
| QSO-001 | todo | Define declarative workflow/panel schema | Ordinary, DX-pileup, and contest panels validate without executable scripts. |
| QSO-002 | todo | Implement operator-confirmed QSO workflow | Exact callsign confirmation is required before first TX and emergency stop is always available. |

## P2 — M4 logging and SDR

| ID | Status | Item | Acceptance |
|---|---|---|---|
| LOG-001 | todo | Implement durable logging outbox | Records survive restart and retry state is visible. |
| LOG-002 | todo | Implement Log4OM 2 UDP ADIF sink | A test QSO is accepted by configurable Log4OM inbound ADIF service. |
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
| PKG-001 | todo | Produce signed Win64 installer | Clean-machine install, upgrade, and uninstall tests pass. |
| PKG-002 | todo | Produce macOS bundle and Linux packages | Runtime dependencies and hardware-plugin diagnostics are documented. |
| DOC-001 | todo | Write operator and hardware manuals | Covers setup, safe keying tests, workflows, diagnostics, and compatibility matrix. |

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
