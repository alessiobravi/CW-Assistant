# Configuration reference

Settings are stored separately for every named station profile. All supplied
radio values are starting points and remain editable.

## Audio page

Select the operating-system audio input carrying receiver audio. The list is
populated from native Windows, macOS, or Linux audio services and refreshes when
devices are added or removed.

- **System default input (recommended):** follows the current OS default rather
  than binding the profile to one device identifier.
- **Named input:** binds the profile to that specific device.
- **Unavailable:** preserves a disconnected device selection visibly instead of
  silently changing the profile to another input. Reconnect it or select a new
  device, then use **Refresh audio inputs** if necessary.

Audio selection applies equally to a CAT-controlled radio and receive-only SWL
operation. Use **Start live RX** in the Receiver workspace to begin capture.
The application requests 48 kHz mono float when supported and otherwise uses
the device's preferred PCM format, converts it, and downmixes it to mono. Live
audio and WAV replay are explicit, separate receiver modes.

The status bar exposes bounded-queue input overruns. Advanced channel,
sample-rate, buffer-size, calibration, and level-meter controls remain under
implementation.

## Station page

**Own station callsign** is stored separately in every station profile. Input is
trimmed and normalized to uppercase using the same exact callsign policy as the
ignore list and transmit guard. Portable suffixes may use a single `/`.

Example values:

```text
IU0LFQ
AD2FC
IU0LFQ/P
```

The value will populate station logging data and is the exact-match source for
the planned notification when your call is decoded. The future optional closing
macro remains subject to explicit configuration, arming, QSO-context checks,
and cancellation before transmission.

## Radio page

### Radio participation and detection

- **No radio — receive-only (SWL):** process audio without CAT, PTT, or KEY.
  The first-run wizard skips the CAT and Keying pages.
- **Detected online radio:** lists only a device that a supported integration
  positively identifies as online. The initial Windows implementation reads
  the two OmniRig slots. It never treats a COM-port name as a radio model and
  does not issue speculative CAT commands.
- **Manual radio template:** exposes the supplied reference templates only when
  the operator deliberately chooses manual setup. All resulting serial values
  remain editable.

Use **Refresh detection** after starting or reconfiguring the frequency service.
An installed but disabled, busy, unresponsive, or unconfigured radio is not
shown in the detected-radio list. macOS and Linux currently use SWL or manual
setup until live Hamlib discovery is implemented.

### Reference radio

- **Yaesu FT-450D:** starts at 4800 baud, 8 data bits, no parity, 1 stop bit.
- **Yaesu FT-818/FT-818ND:** starts at 4800 baud, 8 data bits, no parity,
  2 stop bits.

Confirm these values against the radio menu and the interface cable. Loading a
reference profile does not guess a physical port.

### Frequency provider

- **OmniRig (Windows):** select radio slot 1 or 2. The Configure button opens
  its native setup. Direct key/PTT remains a separate COM connection.
- **Hamlib:** portable serial CAT path; the live adapter is planned.
- **CAT4OM network service:** connect to a group-specific Control WebSocket and
  select a radio ID. See [CAT4OM setup](cat4om-setup.md).

### Serial CAT values

The port, baud rate, data bits, parity, stop bits, RTS flow-control mode,
polling interval, and timeout are configurable. These values apply to a direct
serial provider; CAT4OM owns its physical radio connection on the server.

### Split and offsets

Enable split when reception and transmission use independent frequencies.
Offsets accept positive or negative whole hertz values.

Examples:

```text
Direct HF radio: RX offset 0, TX offset 0
Up-converter:    RX offset +116000000
Down-converter:  RX offset -116000000
Cross-band SAT:  independent RX and TX offsets, split enabled
```

## Keying page

Select a dedicated serial port and assign different lines to PTT and KEY.
Defaults are RTS for PTT and DTR for KEY, both active high. Change polarity only
to match an electrically verified interface. Port enumeration is passive.

The direct keying adapter and line test are not implemented yet, so no profile
can transmit through this path in the current build.

## Display page

- **Target FPS:** UI redraw target from 10 to 120.
- **Waterfall lines/second:** independent scroll/update rate from 1 to 120.
- **Automatic range:** adapts the visible dBFS range using smoothed robust
  spectrum levels. Disable it to use the editable lower and upper bounds.
- **Spectrum averaging:** applies exponential power averaging in DSP from 1 to
  32 frames; higher values steady the trace but react more slowly.
- **Reference grid:** shows or hides functional frequency/level guide lines.
- **Lower/upper dB:** manual bounds; at least 10 dB of span is enforced.

The current receiver canvas is an honest empty state and does not draw simulated
radio data.

## Profile examples

### HF desk

```text
Radio: Yaesu FT-450D
Frequency provider: OmniRig slot 1 (Windows) or Hamlib
CAT: radio-specific COM port, 4800 8-N-1, hardware RTS flow control
Key/PTT: dedicated interface port, RTS PTT, DTR KEY
Offsets: 0 / 0
```

### Portable radio

```text
Radio: Yaesu FT-818
Frequency provider: OmniRig slot 2 (Windows) or Hamlib
CAT: radio-specific port, 4800 8-N-2
Key/PTT: dedicated interface port
Offsets: 0 / 0
```

### Network-controlled station

```text
Frequency provider: CAT4OM network service
Control URL: ws://127.0.0.1:5001/
Radio ID: run
CAT serial fields: managed by the CAT4OM server, not this client
Key/PTT: remains local and independently guarded unless a future remote-server
         profile explicitly owns transmission
```
