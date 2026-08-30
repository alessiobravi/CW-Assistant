# CW Assistant

CW Assistant is a modular, cross-platform C++ application for receiving,
visualizing, decoding, and operator-assisted replying to amateur-radio CW. The
target platforms are Windows 11 or newer on x64, macOS, and Linux; Windows is
the first packaging target.

The project is at foundation stage. The current code provides dependency-free
core primitives and tests plus an optional Qt Quick desktop shell with a
profile chooser, guided setup, and persistent radio/keying/display settings.

## Planned capabilities

- Live audio input with selectable device, channel, sample rate, and buffering
- RTL-SDR and SDRplay reception through SoapySDR modules
- Selectable receive-only network SDR directory, with KiwiSDR streaming first
  and browser handoff for receiver types without an authorized client API
- Accelerated spectrum and scrolling waterfall
- Detection and stateful decoding of multiple simultaneous CW channels
- Strongest-signal, arrival-queue, and operator-selected channel scheduling
- Configurable 2D spectrum bounds, FPS, waterfall speed, averaging, peak hold,
  palettes, overlays, and delayed callsign detail cards
- Persistent callsign ignore list enforced by display, queue, and TX safety
- Standalone, station-server, and remote-client roles with secure control,
  receive audio/spectrum streaming, and station-local CW timing
- Multiple saved station profiles with a startup chooser and isolated settings,
  allowing separate application instances to operate separate radios
- Windows OmniRig, portable Hamlib, and CAT4OM network frequency-control paths
- Yaesu FT-450D and FT-818/FT-818ND editable reference configurations
- Separate configurable serial port and RTS/DTR lines for PTT and keying
- Human-confirmed QSO initiation with ordinary, DX-pileup, and contest panels
- ADIF 3 records sent to Log4OM 2 through its inbound UDP integration
- Deterministic replay of audio and IQ captures for decoder regression tests

## Architecture

The real-time capture callback only packages samples into a bounded SPSC ring.
A dispatcher performs shared spectral analysis and submits active channels to a
fixed-size DSP worker pool. Each channel has its own filter, timing, decoder,
and confidence state; it does not own an operating-system thread.

See [the architecture](docs/architecture.md), [requirements](docs/requirements.md),
[prioritized backlog](BACKLOG.md), [changelog](CHANGELOG.md), and
[roadmap](docs/roadmap.md). Operator-facing setup and examples are maintained in
the [user manuals](docs/manuals/README.md). The graphical architecture is recorded in
[ADR 0001](docs/decisions/0001-qt-quick-spectrum-renderer.md).
ADIF release gates and official-fixture verification are documented in the
[ADIF conformance policy](docs/adif-conformance.md).

## Build the current core

Requirements: a C++20 compiler and CMake 3.24 or newer.

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The development host starts in a hardware-safe state:

```sh
./build/dev/src/app/cw-assistant
```

The Qt desktop shell requires Qt 6.5 or newer with Quick, Quick Controls, QML,
SerialPort, and WebSockets:

```sh
cmake -S . -B build/desktop -DCWA_BUILD_DESKTOP=ON
cmake --build build/desktop
./build/desktop/src/desktop/cw-assistant-desktop --profile default
```

When multiple station profiles exist, the desktop opens a profile chooser.
`--profile NAME` bypasses the chooser for dedicated shortcuts, remote stations,
and parallel instances.

Runtime dependencies planned for adapter milestones are Qt 6, PortAudio,
Hamlib, SoapySDR, SoapyRTLSDR, and SoapySDRPlay3. SDRplay also requires the
vendor's platform-specific API/driver.

Every push to `main` and pull request builds and tests natively on Windows x64,
Linux x64, macOS ARM64, and macOS x64, then publishes downloadable desktop
artifacts; Linux also produces a Debian/Ubuntu `.deb`. Pull requests containing
implementation, test, build, or workflow changes must update the user manuals,
changelog, and backlog.

## Safety

Decoded text can never directly assert PTT or KEY. Transmission starts
disarmed, requires an explicit arm action, a selected callsign, a matching
operator confirmation, and a healthy keying adapter. Timeouts and emergency
release are required before hardware TX is enabled.

## License

CW Assistant is licensed under the GNU General Public License v3.0 or later
(`GPL-3.0-or-later`). See [LICENSE](LICENSE) and the
[dependency licensing policy](docs/licensing.md).
