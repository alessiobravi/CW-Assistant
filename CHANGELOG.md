# Changelog

All notable changes to CW Assistant are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and releases will use
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

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

- Corrected invalid setup-wizard QML that stopped hosted desktop compilation.
- macOS packaging now creates and deploys a self-contained `.app` bundle rather
  than archiving a non-bundle executable without its Qt/QML runtime.
- Hosted Qt installation now includes the task-tree dependency required by the
  Qt 6.11 QML plugin metadata, eliminating an incomplete-SDK warning.

### Security

- Transmission begins disarmed and cannot be initiated directly by decoder
  output.

[Unreleased]: https://github.com/alessiobravi/CW-Assistant/compare/HEAD
