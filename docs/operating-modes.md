# Operating modes

Status: planned product specification. The current application behavior is
**Standard** mode; the other modes below are not yet available.

CW Buddy will expose the operating mode as a first-level choice in the left
navigation rail. A mode changes receiver interpretation, presentation, and
optional TX-frequency assistance. It does not bypass the radio provider,
transmit arming, callsign confirmation, exact-message preview, KEY watchdog, or
emergency release.

## Mode summary

| Mode | Operator goal | Receiver behavior | Frequency assistance |
|---|---|---|---|
| **Standard** | General listening and manually guided QSOs | Decode every qualified stream without assuming an operating pattern | Manual VFO entry, A=B, and Ctrl+left-click only |
| **PileUp Chaser** | Work a running/DX station by learning where it listens | Identify the runner, associate completed replies with callers across the announced split range, and learn its selection pattern | Predict the next likely listened-to slice; optional explicitly enabled VFO B follow |
| **PileUp Slicer** | Call from a comparatively clear place in a pileup | Measure recent occupancy, collision likelihood, persistence, and neighboring-signal guard space inside the eligible split range | Rank clear TX slots; optional explicitly enabled VFO B placement |
| **Runner** | Call CQ and work answering stations | Treat this station as the runner, separate and rank callers, and model exchanges in either simplex or split operation | Manage the operator-defined receive range; never infer permission to transmit |

Only one primary mode is active per receiver workspace. Switching mode keeps
raw receiver history and decoder evidence, but clears mode-specific learned
predictions. It never arms TX or sends a message.

## PileUp Chaser learning and exchange cycle

The Chaser requires a visible, observation-only **Learning** phase before its
frequency-follow option can be enabled. Learning correlates the runner's stable
callsign and transmit frequency, decoded split instructions (`UP`, `DWN`,
`QSX`, or an explicit range), the selected caller's frequency and completed
reply/report, successive selection direction, occupancy, uncertainty, and
missing receiver coverage.

When the runner answers another caller by callsign, local calling is inhibited.
The Chaser follows that caller's slice, receives the caller's report and the
runner's completion, and only then adds the observed frequency to its listening
model. It must not transmit while another station is being worked. A partial,
near-match, or ambiguous selected callsign also keeps transmission inhibited.
An exact match to the operator's own confirmed callsign may create a report
proposal, but the proposal remains subject to every ordinary TX gate.

The learned state is scoped to the current runner, band, mode, and listening
session. A bounded online estimator may recognize a sweep, alternating edge,
local-step, repeated-slice, or non-predictive pattern, but must expose its
evidence and confidence. It selects **unknown** when several explanations
remain plausible and cannot infer a deterministic pattern from one contact.

Chaser decoding is asymmetric. The runner slice receives high-priority full
decoding because its callsign, split instructions, selected caller, report, and
end-of-QSO cues control the observation cycle. Pileup slices use a bounded
grammar for caller identities and the short report/acknowledgement exchange
needed to associate a completed QSO with a frequency. Other pileup energy is
tracked as occupancy rather than open-ended text. This load reduction must
preserve raw acoustic alternatives and uncertainty; the grammar cannot force a
plausible callsign or report when the signal does not support it.

Before the first transmission, the operator sees the observed contacts,
coverage, learned range, current confidence, and proposed TX slice. The
operator explicitly enables frequency follow for that session. Enabling a mode
is not equivalent to arming TX.

For half-duplex reception, learning pauses whenever local transmission makes
the receiver unavailable. The operator configures periodic relearning by
elapsed time and/or completed-QSO count. The Chaser returns to observation when
that limit expires, confidence falls, the runner changes its instruction, or
the observed pattern changes. With genuine independent full-duplex RX,
learning continues during local transmission and continuously revises the
prediction from newly completed exchanges.

Automatic VFO B positioning is permitted only when the operator enabled follow
for this session; runner identity, split range, frequency mapping, evidence,
and receiver coverage are current; the slice is eligible; the transmitter is
not keyed; and the configured provider supports the request. Only authoritative
radio readback moves the TX marker. Failure or ambiguity freezes the last
confirmed state and returns to **Learning** or **Suggestion only**. Decoder
output alone never keys the transmitter.

## PileUp Slicer

The Slicer does not claim to know where the runner is listening. It ranks
eligible slices using a bounded recent occupancy map, estimated signal width,
guard spacing, duty cycle, collision history, and receiver coverage. A
quiet-looking point outside the announced split range, inside the runner's own
signal, or in missing/stale coverage is ineligible.

The operator can use suggestions only or explicitly enable VFO B placement.
The UI shows the selected slice, alternatives, age, and reason. A new carrier,
changed instruction, lost coverage, or readback mismatch withdraws the choice.
The Slicer does not initiate a call and does not treat silence as proof that a
frequency is legal or appropriate.

## Runner

Runner mode supports simplex and split operation. It models the local station
as the calling station, recognizes CQ/listening cycles, groups callers within
each response window, and ranks complete or partial calls without silently
replacing acoustic text. Split uses the explicitly configured receive range;
simplex treats alternating stations on one carrier as one conversational
frequency session.

Automatic replies remain a separate, explicit setting governed by the complete
TX safety state machine. Runner mode by itself changes receiver interpretation
and the QSO workspace only.

## UI and auditability

The left rail shows the active mode by name. The workspace reports one of
**Standard**, **Learning**, **Suggestion only**, **Following**, or **Attention
required**, with evidence age and confidence. Every automatic VFO B request
records old frequency, requested frequency, authoritative readback, triggering
evidence, mode, and reason in local diagnostics. The operator can stop follow
immediately without stopping RX.

## Operating basis

The design follows established practice: listen before calling, copy the
runner and its split instruction, do not transmit while another station is
being worked, and observe where completed callers were heard. It also accounts
for runners who move unpredictably. These references inform heuristics; they
are not machine-readable rules and do not authorize transmission:

- [IARU Region 1 HF Manager's Handbook](https://www.iaru-r1.org/wp-content/uploads/2019/08/hf_managers_handbook_v9.pdf)
- [ARRL Ethics and Operating Procedures for the Radio Amateur](https://www.arrl.org/files/file/DXCC/Eth-operating-EN-ARRL-CORR-JAN-2011.pdf)
- [ARRL DXpeditioning Basics](https://www.arrl.org/files/file/DXCC/dx-basics.pdf)
- [RSGB DX Code of Conduct](https://rsgb.org/main/operating/dx-code-of-conduct/)
