# ADR 0002: Secure remote client/server operation

Status: accepted foundation; not implemented

Date: 2026-08-30

## Context

The same application must work locally, as a station-side server, or as a
remote operator client. Remote operation controls transmitting equipment, so
packet loss, latency, reconnects, stale clients, credential theft, and server
failure are safety concerns rather than ordinary UI errors.

## Implementation status

This record decides the shape of remote operation; it does not describe
shipped behaviour. The whole of `cwassistant/core/remote_control.hpp` is what
exists: the `ApplicationRole` and `RemoteBandwidthProfile` enumerations, the
`RemoteProtocolVersion` structure, the `RemoteCwTransmitRequest` structure, and
`ControlLeaseManager`. There is no server, no client, no transport, no pairing
and no audit trail, and the desktop application never mentions any of those
types. Where a paragraph below reads as a description of running behaviour, it
states a requirement on work still to be done. The details are expanded in the
[remote operation specification](../remote-operation-specification.md).

## Roles

- `Standalone`: UI, DSP, radio, keying, and logging run in one process.
- `StationServer`: owns receivers, radios, CAT, keying, decoding, workflows,
  logging, audit records, and all physical safety enforcement. It may be
  headless or show a local status/emergency-stop UI.
- `RemoteClient`: presents state and media, requests control, and submits
  operator actions. It never accesses station serial lines through the protocol.

One executable is to support all three roles through a startup profile. The
`--profile` option today selects an isolated station configuration only, and
every build runs as `Standalone`. A station server starts disarmed after
process restart, device reconnect, configuration change, or safety fault.

## Transport

The control/event channel is to use versioned binary messages over secure
WebSockets (`wss`); the Qt WebSockets module the CAT4OM client already links is
the intended transport. TLS peer verification is mandatory. Plain `ws` may be
compiled only for loopback integration tests and must not bind a non-loopback
address.

The initial deployment model is LAN or a user-managed VPN. Direct raw exposure
to the public internet is unsupported. Reverse-proxy support requires explicit
documentation for TLS termination, original client identity, connection limits,
timeouts, and WebSocket forwarding.

Receive audio is to use Opus frames with sequence numbers, station monotonic
timestamps, a bounded jitter buffer, loss counters, and selectable latency; no
audio codec is linked today. Spectrum, waterfall rows, decodes, and state
snapshots are independent streams with bandwidth profiles, named by the
`RemoteBandwidthProfile` enumeration. Raw IQ is opt-in and unavailable when
server capacity or policy disallows it.

## Pairing and authorization

- First pairing is a local/physical station action that creates a named client
  identity and certificate.
- The server authenticates clients and clients pin/trust the station identity.
- Authorization roles are `observer`, `operator`, and `administrator`. They are
  a separate axis from `ApplicationRole` and have no representation in the tree
  yet; least privilege is the default.
- Credentials are stored with the OS credential/key store where available.
- Revocation, certificate rotation, failed-auth throttling, connection limits,
  message-size limits, and an audit trail are required before internet use.
- TLS verification errors are never ignored automatically.

## Control and TX safety

Each station profile has one station-wide controlling-client lease covering all
of its coupled receivers, transmitters, VFOs, audio routes and operating
workflows. A short lease is renewed by authenticated heartbeats and expires
automatically. Multiple observers may receive concurrently, but observers never
receive a lease. A lease permits requests but cannot itself key hardware.
`ControlLeaseManager` implements that ownership rule for one rig identifier at
a time, with a requested lifetime clamped between two and thirty seconds;
extending it to cover a whole station profile remains to be done.

The client sends complete CW text plus speed/weight and an operator-confirmed
callsign. It never sends dot, dash, PTT, or KEY edge timing. The station validates
the protocol version, authenticated identity, role, lease, rig state, band/mode,
callsign ignore policy, confirmation nonce, request idempotency key, message
length, WPM/weight limits, and local TX interlocks. It then schedules Morse
timing locally against a monotonic clock.

Before and during TX, the station owns these independent stops. The first
three and the emergency stop already exist for local transmission, in
`TransmitGuard` and `CwTransmitScheduler`; the remainder arrive with the
server:

- maximum continuous key-down timer, three seconds, with a separate fifteen-
  second ceiling on TUNE;
- maximum message duration, and a queued-message count once a queue exists;
- PTT lead and hang limits;
- heartbeat/control-lease expiry;
- local emergency stop, and a physical inhibit input where available;
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
