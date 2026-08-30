# CW Assistant

CW Assistant is a modular, cross-platform C++ application for receiving,
visualizing, decoding, and operator-assisted replying to amateur-radio CW. The
target platforms are Windows 11 or newer on x64, macOS, and Linux; Windows is
the first packaging target.

The project is at foundation stage. The current code provides dependency-free
core primitives and tests. Audio/CAT/SDR/UI adapters are deliberately kept out
of the core and will be added as independently replaceable modules.

## Planned capabilities

- Live audio input with selectable device, channel, sample rate, and buffering
- RTL-SDR and SDRplay reception through SoapySDR modules
- Accelerated spectrum and scrolling waterfall
- Detection and stateful decoding of multiple simultaneous CW channels
- Strongest-signal, arrival-queue, and operator-selected channel scheduling
- Multiple saved transceiver profiles using Hamlib serial CAT
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
[roadmap](docs/roadmap.md). The graphical architecture is recorded in
[ADR 0001](docs/decisions/0001-qt-quick-spectrum-renderer.md).

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

Runtime dependencies planned for adapter milestones are Qt 6, PortAudio,
Hamlib, SoapySDR, SoapyRTLSDR, and SoapySDRPlay3. SDRplay also requires the
vendor's platform-specific API/driver.

Every push to `main` and pull request builds and tests the core natively on
Windows x64, Linux x64, macOS ARM64, and macOS x64. Pull requests containing
implementation, test, build, or workflow changes must update both the changelog
and backlog.

## Safety

Decoded text can never directly assert PTT or KEY. Transmission starts
disarmed, requires an explicit arm action, a selected callsign, a matching
operator confirmation, and a healthy keying adapter. Timeouts and emergency
release are required before hardware TX is enabled.

## License

The project is intended to be open source. A license has not yet been selected;
do not publish or accept outside contributions until a `LICENSE` is added.
