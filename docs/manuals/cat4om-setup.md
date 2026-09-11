# CAT4OM setup

CW Buddy uses the native CAT4OM 1.x JSON WebSocket Control channel. This is
a network frequency provider: CAT4OM owns the physical radio connection and CW
Buddy consumes its high-level state. It is not configured as a serial port
inside CW Buddy.

Protocol baseline: [CAT4OM Integration Manual, wire protocol 1.0.0,
revision 2026-08-21](https://www.cat4om.com/cat4om/resources/Cat4OM_IntegrationManual.pdf).
CW Buddy compares only the protocol major version and ignores unknown
optional fields, messages, events, and error codes as required for 1.x forward
compatibility.

## Server preparation

1. Configure and start the radio group in CAT4OM.
2. Note that group's **Control port** (commonly 5001 or higher), not the global
   Management port (commonly 5000).
3. Note the stable radio ID shown inside the group, for example `run`.
4. For a different computer, configure the server bind address deliberately and
   use a private LAN reached through a VPN or another trusted encrypted tunnel.

## Local example

In **Settings → Radio**:

```text
Frequency control:  CAT4OM network service
CAT4OM Control URL: ws://127.0.0.1:5001/
CAT4OM radio ID:    run
CAT4OM password:    leave empty if this Control endpoint is open
```

Select **Test read-only**. The client connects as an observer without sending
any password, so the read-only test succeeds on an open endpoint even when the
password field is blank. It does not join the ownership election, and it
displays the selected radio's pushed RX/TX frequency and split state. Leaving
the radio ID empty selects the first visible radio, which is convenient for a
one-radio group but not recommended for a stable multi-radio profile.

## Control connection and ownership

Select **Connect control** only when CW Buddy is meant to participate as an
interactive controller. If the group requires a password, enter it immediately
before connecting. The password is used to create the documented minute-based
SHA-256 proof and is then erased from the settings object; it is never persisted
in the profile.

The first normal Control client may become master automatically. A later client
is a slave and cannot write until the operator selects **Request ownership**.
Ownership covers every radio in that group. Keep unrelated operators/radios in
separate CAT4OM groups when independent authority is required.

Frequency, mode, and split writes are capability-gated by the selected radio's
`availableCommands`. A successful command response means accepted; the next
pushed `stateUpdate` remains the authoritative displayed value. VFO identifiers
are treated as opaque names rather than assuming only A and B.

With a control connection holding master ownership and `SetFrequency`
advertised, the main RX/TX readouts become editable as their corresponding VFO
state becomes available, and the waterfall-edge RX tuning buttons appear. CW
Buddy explicitly names the pushed RX or TX VFO in every request. `SetMode` and
`SetSplit` are exposed only when advertised; each accepted request still waits
for the next pushed state before updating the authoritative display.

CW Buddy's CAT4OM adapter intentionally does not expose PTT or CW commands.
Transmission remains behind the application's independent confirmation,
interlock, watchdog, and station-server architecture.

## Remote example

```text
CAT4OM Control URL: wss://station.example.internal/radio-group/
CAT4OM radio ID:    hf-main
```

Use `wss://` only when a trusted TLS reverse proxy maps that URL to the CAT4OM
Control listener. Otherwise connect through a VPN and use the server's private
address. Do not expose a plain `ws://` listener directly to the internet: the
published password proof avoids sending the password itself but can be replayed
briefly and can be attacked offline if the password is weak.

## Troubleshooting

- **Connection refused:** confirm the group is running, the Control port is
  correct, and the server bind-address policy permits this client.
- **Wrong endpoint:** a welcome that does not name the `control` endpoint is
  rejected, as is a first message that is not a welcome at all; use the group's
  Control port.
- **Authentication rejected:** check the group password and both computers'
  UTC clocks. The proof uses the current UTC minute.
- **No radio state:** enter the exact case-sensitive radio ID, or clear it to
  inspect the first visible radio.
- **Cannot write:** connect as a normal control client, then request ownership.
- **Frequency write unavailable:** the radio state must advertise the relevant
  command and report a connected state.
- **Reconnect after server restart:** unless the operator selected
  **Disconnect**, the client retries on its own with a bounded, increasing
  delay. It compares server instance IDs, discards stale cached state, performs
  a new handshake, and does not assume it still owns control.
