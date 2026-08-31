# Operator guide

## Current development status

CW Assistant is pre-release software. The current desktop shell can create and
select isolated station profiles, guide first-time setup, discover serial ports
without opening them, save radio/keying/display settings, open the Windows
frequency-provider configuration, and monitor a CAT4OM radio. The decoder,
waterfall renderer, direct keying output, logging connection, SDR capture, and
remote-station runtime are still under implementation. A saved profile does not
arm or key a transmitter.

## About and author

Open **Settings → About** to see the application version, license, author, and
author website. The displayed author is **Alessio Bravi (IU0LFQ / AD2FC)** and
the **Author Website** button opens [https://iu0lfq.it/](https://iu0lfq.it/) in
the system browser.

## First launch

1. Start `cw-assistant-desktop`.
2. Enter a descriptive station profile name, such as `HF desk` or
   `Satellite station`.
3. Choose one of the reference radios as a safe starting point.
4. Select the frequency provider and edit every connection value to match the
   radio and cable.
5. Select a physically separate direct-COM key/PTT interface. Discovery never
   toggles RTS or DTR.
6. Review the display defaults and finish the wizard.

Finishing the wizard saves settings only. Hardware ownership, a keying loopback
test, and the transmit guard will be required before transmission is enabled in
a later milestone.

## Multiple radios and application instances

Create one named profile for each independent station chain. If more than one
profile exists, the startup helper asks which profile to open. A shortcut or
automation can bypass the helper:

```text
cw-assistant-desktop --profile "HF desk"
cw-assistant-desktop --profile "Satellite station"
```

Two application processes may use different profiles. Future device locks will
prevent both processes from opening the same serial, audio, or SDR device.

## Frequency, split, and transverter terminology

- **RX dial frequency** is the frequency reported to or requested from the
  radio for reception.
- **TX dial frequency** is independent when split is enabled.
- **RX/TX transverter offset** is a signed integer in hertz. It is used to
  calculate actual RF for display and logging; the offset is never sent to a
  direct CAT radio by accident.

Example satellite station:

```text
RX dial:        29,900,000 Hz
RX offset:     116,000,000 Hz
Actual RX RF:  145,900,000 Hz

TX dial:        28,300,000 Hz
TX offset:     407,000,000 Hz
Actual TX RF:  435,300,000 Hz
Split: enabled
```

The log record derives `FREQ_RX`, `FREQ`, `BAND_RX`, and `BAND` from those
actual RF values. Satellite operation also supplies `PROP_MODE`, `SAT_NAME`,
and `SAT_MODE`. Station equipment rules select `MY_RIG` and `MY_ANTENNA` from
the actual TX/RX bands.

## Safety principles

- Decoder output never starts transmission.
- The first transmission of a QSO requires operator confirmation of the exact
  selected callsign.
- Ignored callsigns are excluded from display, queueing, QSO selection, and TX
  authorization.
- Direct key/PTT is separate from frequency control.
- Network receivers are receive-only and cannot own TX.
- Remote operation keeps final interlocks and CW timing at the station server.
