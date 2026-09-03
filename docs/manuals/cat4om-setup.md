# CAT4OM setup

CW Assistant uses the native CAT4OM 1.x JSON WebSocket Control channel. This is
a network frequency provider: CAT4OM owns the physical radio connection and CW
Assistant consumes its high-level state. It is not configured as a serial port
inside CW Assistant.

Protocol baseline: [CAT4OM Integration Manual, wire protocol 1.0.0,
revision 2026-08-21](https://www.cat4om.com/cat4om/resources/Cat4OM_IntegrationManual.pdf).
CW Assistant compares only the protocol major version and ignores unknown
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
Frequency control: CAT4OM network service
Control URL:       ws://127.0.0.1:5001/
Radio ID:          run
Password:          leave empty if this Control endpoint is open
```

Select **Test read-only**. The client connects as an observer, does not join the
ownership election, and displays the selected radio's pushed RX/TX frequency
and split state. Leaving Radio ID empty selects the first visible radio, which
is convenient for a one-radio group but not recommended for a stable multi-radio
profile.

## Control connection and ownership

Select **Connect control** only when CW Assistant is meant to participate as an
interactive controller. If the group requires a password, enter it immediately
before connecting. The password is used to create the documented minute-based
SHA-256 proof and is then erased from the settings object; it is never persisted
in the profile.

The first normal Control client may become master automatically. A later client
is a slave and cannot write until the operator selects **Request ownership**.
Ownership covers every radio in that group. Keep unrelated operators/radios in
separate CAT4OM groups when independent authority is required.

Frequency and split writes are capability-gated by the selected radio's
`availableCommands`. A successful command response means accepted; the next
pushed `stateUpdate` remains the authoritative displayed value. VFO identifiers
are treated as opaque names rather than assuming only A and B.

With a control connection holding master ownership and `SetFrequency`
advertised, the main RX readout becomes editable and the waterfall-edge tuning
buttons appear. CW Assistant explicitly names the pushed active RX VFO in each
frequency request. A split radio's TX VFO and mode are not changed; split
control remains a separate operation.

CW Assistant's CAT4OM adapter intentionally does not expose PTT or CW commands.
Transmission remains behind the application's independent confirmation,
interlock, watchdog, and station-server architecture.

## Remote example

```text
Control URL: wss://station.example.internal/radio-group/
Radio ID:    hf-main
```

Use `wss://` only when a trusted TLS reverse proxy maps that URL to the CAT4OM
Control listener. Otherwise connect through a VPN and use the server's private
address. Do not expose a plain `ws://` listener directly to the internet: the
published password proof avoids sending the password itself but can be replayed
briefly and can be attacked offline if the password is weak.

## Troubleshooting

- **Connection refused:** confirm the group is running, the Control port is
  correct, and the server bind-address policy permits this client.
- **Wrong endpoint:** a Management `managementWelcome` is rejected; use the
  group's Control port.
- **Authentication rejected:** check the group password and both computers'
  UTC clocks. The proof uses the current UTC minute.
- **No radio state:** enter the exact case-sensitive radio ID, or clear it to
  inspect the first visible radio.
- **Cannot write:** connect as a normal control client, then request ownership.
- **Frequency write unavailable:** the radio state must advertise the relevant
  command and report a connected state.
- **Reconnect after server restart:** the client compares server instance IDs,
  discards stale cached state, performs a new handshake, and does not assume it
  still owns control.
