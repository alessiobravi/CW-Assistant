# Changelog

All notable changes to CW Assistant are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and releases will use
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

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
