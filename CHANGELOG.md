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

### Security

- Transmission begins disarmed and cannot be initiated directly by decoder
  output.

[Unreleased]: https://github.com/alessiobravi/CW-Assistant/compare/HEAD
