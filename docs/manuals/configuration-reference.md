# Configuration reference

Settings are stored separately for every named station profile. All supplied
radio values are starting points and remain editable.

## Radio page

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
