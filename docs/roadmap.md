# Delivery roadmap

## M0 — foundation (current)

- C++20/CMake project and platform-neutral core
- Fixed-allocation sample blocks and bounded SPSC handoff
- Candidate selection policies and per-rig configuration contracts
- Human-confirmed TX permission state machine
- Basic ADIF serialization and hardware-free tests

## M1 — receive and visualize

- PortAudio device enumeration and capture adapter
- WAV input and deterministic replay clock
- Windowing, FFT, spectrum averaging, waterfall row generation
- Qt Quick application shell and accelerated spectrum/waterfall
- Runtime counters and capture-soak tests

Acceptance: select an audio port on Win64, see a smooth waterfall/spectrum, and
replay the same recording to identical spectral results without overruns.

## M2 — multichannel decode

- Peak detector and channel lifecycle tracker
- Narrowband filter/decimator and adaptive CW envelope/timing decoder
- Worker pool with per-channel ordering and load shedding
- Callsign extraction, confidence, channel list, and waterfall selection
- Corpus annotations and decoder benchmark report

Acceptance: locate and independently decode multiple annotated signals from the
CC0 pileup fixture with published accuracy and latency metrics.

## M3 — radio, guarded transmit, and QSO panels

- Hamlib serial CAT adapter and multiple saved rig profiles
- Cross-platform serial RTS/DTR key/PTT adapter
- Maximum-key-down watchdog and emergency stop
- Ordinary, DX-pileup, and contest workflow panels
- Explicit callsign confirmation and CW message scheduling

Acceptance: pass loopback line tests, then complete a human-confirmed QSO on the
chosen reference rig without any decoder event directly controlling TX.

## M4 — Log4OM and SDR

- Durable logging outbox and Log4OM 2 UDP ADIF adapter
- SoapySDR device discovery and stream adapter
- RTL-SDR and SDRplay setup diagnostics
- IQ tuning, frequency mapping, and CAT/SDR frequency synchronization

Acceptance: log a completed QSO into Log4OM and repeat the M2 decoder benchmark
from both an RTL-SDR and an SDRplay receiver.

## M5 — packaging and release

- Signed Win64 installer, macOS application bundle, and Linux packages
- CI build/test matrix, dependency/license manifest, crash diagnostics
- User manual, hardware compatibility table, and reproducible release process
