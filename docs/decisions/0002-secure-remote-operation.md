# ADR 0002: Secure remote client/server operation

Status: accepted foundation

Date: 2026-08-30

## Context

The same application must work locally, as a station-side server, or as a
remote operator client. Remote operation controls transmitting equipment, so
packet loss, latency, reconnects, stale clients, credential theft, and server
failure are safety concerns rather than ordinary UI errors.

## Roles

- `Standalone`: UI, DSP, radio, keying, and logging run in one process.
- `StationServer`: owns receivers, radios, CAT, keying, decoding, workflows,
  logging, audit records, and all physical safety enforcement. It may be
  headless or show a local status/emergency-stop UI.
- `RemoteClient`: presents state and media, requests control, and submits
  operator actions. It never accesses station serial lines through the protocol.

One executable supports all roles through a startup profile. A station server
starts disarmed after process restart, device reconnect, configuration change,
or safety fault.

## Transport

The control/event channel uses versioned binary messages over secure WebSockets
(`wss`). TLS peer verification is mandatory. Plain `ws` is compiled only for
loopback integration tests and cannot bind a non-loopback address.

The initial deployment model is LAN or a user-managed VPN. Direct raw exposure
to the public internet is unsupported. Reverse-proxy support requires explicit
documentation for TLS termination, original client identity, connection limits,
timeouts, and WebSocket forwarding.

Receive audio uses Opus frames with sequence numbers, station monotonic
timestamps, a bounded jitter buffer, loss counters, and selectable latency.
Spectrum, waterfall rows, decodes, and state snapshots are independent streams
with bandwidth profiles. Raw IQ is opt-in and unavailable when server capacity
or policy disallows it.

## Pairing and authorization

- First pairing is a local/physical station action that creates a named client
  identity and certificate.
- The server authenticates clients and clients pin/trust the station identity.
- Roles are `observer`, `operator`, and `administrator`; least privilege is the
  default.
- Credentials are stored with the OS credential/key store where available.
- Revocation, certificate rotation, failed-auth throttling, connection limits,
  message-size limits, and an audit trail are required before internet use.
- TLS verification errors are never ignored automatically.

## Control and TX safety

Each rig has at most one controlling client lease. A short lease is renewed by
authenticated heartbeats and expires automatically. Observers never receive a
lease. A lease permits requests but cannot itself key hardware.

The client sends complete CW text plus speed/weight and an operator-confirmed
callsign. It never sends dot, dash, PTT, or KEY edge timing. The station validates
the protocol version, authenticated identity, role, lease, rig state, band/mode,
callsign ignore policy, confirmation nonce, request idempotency key, message
length, WPM/weight limits, and local TX interlocks. It then schedules Morse
timing locally against a monotonic clock.

Before and during TX, the station owns these independent stops:

- maximum continuous key-down timer;
- maximum message duration and queued-message count;
- PTT lead/tail limits;
- heartbeat/control-lease expiry;
- local emergency stop and physical inhibit input where available;
- radio/device disconnect and CAT frequency/mode mismatch;
- client cancel and server shutdown.

Loss of the client connection cancels queued messages and releases KEY then PTT.
Completed messages are not automatically repeated after reconnect. Requests use
idempotency IDs so replayed network packets cannot transmit twice.

## Reconnect and state

Every stream has an epoch and monotonically increasing sequence. On reconnect,
the client authenticates, receives a full station snapshot, and then subscribes
to deltas from that epoch. It does not restore control or TX state implicitly;
the operator must reacquire the lease and arm/confirm again.

Server state includes receiver/rig inventory, tuned frequencies, decoder
channels, current callsign observations, ignore-list revision, logging outbox,
TX safety state, lease owners, stream health, and audit cursor. Client clocks
are never used for station key timing.

## Consequences

- Remote manual paddle edge streaming is out of initial scope because network
  jitter cannot provide trustworthy element timing.
- Station-side decoding continues if the client or network fails.
- A low-bandwidth client can operate from decoded events and sparse spectrum
  updates without audio or IQ.
- The networking layer cannot be considered complete until authentication,
  lease-loss hardware release, malformed-message tests, and network fault
  injection all pass.
