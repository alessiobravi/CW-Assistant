# High-accuracy CW decoder strategy

## Status and objective

This document is the implementation proposal for the complete M2 decoder. A
receive-only full-processed-passband peak tracker and bounded channel bank are
now implemented. Every frequency track owns a phase-continuous complex mixer,
a three-stage raw-sample filter selected from 60, 120 and 240 Hz paths and
starting at 120 Hz, adjacent-band noise references, independent
soft SNR likelihood, adaptive timing decoder, provisional/stable text contract,
stable color, and expiry lifecycle. Parabolic sub-bin peak interpolation, a
bounded carrier and drift predictor, hysteretic width selection, and a first
segment-pass slice that re-decodes a completed turn's retained run lattice are
all delivered; calibrated confidence and the remaining refinement passes are
still planned. Bounded multi-speed timing is delivered: nine 8–60 WPM paths are
seeded together and every one of them keeps processing for the whole segment,
so the early acquisition window fixes only which path is presented, and the
best complete path is selected again at silence or flush. The complete decoder's goal
is high weak-signal accuracy with bounded CPU, memory, and latency on ordinary
desktop hardware.
Every claimed improvement must survive the same held-out replay corpus and must
publish its accuracy, latency, false-output, CPU, and memory results.

The recommended design is a confidence-fused hybrid, not a single opaque model.
It retains the observed tone/envelope evidence, exposes uncertainty, and can
fall back to a deterministic decoder when the optional learned model is absent
or unsupported.

## Processing graph

```mermaid
flowchart TB
  FFT["shared wide FFT"] --> TRK["candidate tone tracker"]
  TRK --> MIX["complex mixer, narrow multirate filter bank,<br/>adaptive noise estimate"]
  MIX --> FEAT["physical narrowband features"]
  FEAT --> DET["deterministic key likelihood<br/>(two-level envelope model)"]
  FEAT -.-> MODEL["tiny causal likelihood model<br/>(planned)"]
  DET --> SEARCH["semi-Markov Morse n-best search"]
  DET -.-> FUSE["calibrated probability fusion<br/>(planned)"]
  MODEL -.-> FUSE
  FUSE -.-> SEARCH
  SEARCH --> OUT["provisional text, stable text,<br/>confidence, evidence"]
  OUT --> RANK["context and callsign re-ranking<br/>(always labelled separately)"]
```

One wide FFT feeds a candidate tone tracker. Each candidate is mixed down
through a narrow multirate filter bank against an adaptive noise estimate to
yield physical narrowband features. Those features drive a deterministic key
likelihood, which today is the two-level envelope model described below and is
what the semi-Markov Morse n-best search consumes; that search produces
provisional text, stable text, confidence and evidence. The small causal
likelihood model and the calibrated fusion that would combine it with the
deterministic likelihood are drawn dashed because neither is built: nothing in
the tree fuses two likelihoods, and no learned artifact ships. Context and
callsign re-ranking happens after the search and is always labelled separately.

A tracked signal is state, not a dedicated operating-system thread. One FFT is
shared by all candidates and bounded worker-pool jobs process active tracks.
Inactive or low-value tracks do not invoke the optional learned path.

Decode cost is deliberately tied to the number of signals being copied rather
than to the number being followed. A track whose strongest measured level has
stayed below the minimum decode level, twelve decibels by default, is still
acquired, followed and drawn in the spectrum, but it is not fed to its decoder,
and its baseband chain of three oscillators and five three-stage complex
cascades per sample is skipped before it runs. It is deliberately not
suspended, because suspension is the association-loss path and a track
suspended for being quiet would never be decoded again once it grew loud. The
gate reads the peak level the track has reached rather than the level of the
moment, since a signal is weak while it is still being acquired, and a fixed
warm-up of a hundred and fifty blocks filters every new track regardless, so
that nothing is judged before it has had time to show its level. A monitored
track and an operator-selected track are always filtered and always decoded,
and an operator setting decodes everything when it is turned on. The margin
behind the default is measured: the weakest track that carried a correctly
recovered callsign across the capture corpus sat at 19.5 dB. This is where the
pipeline's ceiling is set, because the per-track chain grows at roughly nine
tenths of a second per track per twenty seconds of audio while building the
spectrum costs the same for one signal as for twenty-four.

## Selectable keying models

Deciding where the key goes down and comes back up is one stage, and there is
more than one reasonable technique for it. That stage is therefore a selection
rather than a fixed part of the decoder. Everything after it -- element
assembly, the event lattice, the verification gate, callsign policy -- consumes
only the same run description, so a technique is an implementation of one
interface and an entry in one enumeration.

```mermaid
flowchart TB
  LLR["per-frame keying log-likelihood<br/>(from the two-level envelope model)"]
  LLR --> SEL{"keying model<br/>Settings, Decoder page"}
  SEL -->|"Adaptive threshold (default)"| THR["per-frame hysteresis,<br/>then element ratios"]
  SEL -->|"Semi-Markov (HSMM)"| SMK["explicit duration model over<br/>dot, dash, element/character/word gap"]
  SEL -.->|"future"| ML["learned model<br/>(operator-supplied)"]
  THR --> RUNS["runs: kind, length, evidence"]
  SMK --> RUNS
  ML -.-> RUNS
  RUNS --> REST["element assembly, event lattice,<br/>verification, callsign policy"]
```

One per-frame keying log-likelihood, produced by the two-level envelope model,
feeds whichever keying model the operator has selected in Settings. The adaptive
threshold decides each frame with hysteresis and classifies elements afterwards
by duration ratio; the semi-Markov model scores whole runs against explicit
duration distributions. A future learned model plugs in at the same point. All
of them emit the same run description -- kind, length and evidence -- which is
what element assembly, the event lattice, verification and callsign policy
consume, so nothing downstream knows or cares which technique produced it.

### Adaptive threshold (default)

Hysteresis on the smoothed keying probability decides key-up and key-down one
frame at a time; a completed run is classified afterwards by comparing its
length against the tracked element length. It is cheap, it emits text with the
least delay, and it degrades gracefully when the sender's timing wanders,
because no single run has to fit a model for the rest to survive.

### Semi-Markov (HSMM)

Dot, dash, and the element, character and word gaps each get an explicit
log-normal length distribution centred on their true multiple of the element
length. A transition *is* a whole segment, scored as the evidence accumulated
across it plus the log density of its duration under that class; accumulated
evidence comes from a running prefix sum, so a segment of any length costs one
subtraction. Because every competing segmentation covers the same frames, their
scores compare directly.

This is what a two-state hidden Markov model cannot express. Giving key-down its
own state and letting it self-loop makes its length geometric -- mode at zero,
no characteristic scale -- so such a model can only be more or less reluctant to
switch, never encode "a mark lasts about one element, or about three".

Two properties are worth knowing before extending it. Morse timing is
self-similar at a factor of three: read three times too fast, every dash becomes
a dot and every character gap becomes an element gap, producing legal Morse of
entirely known symbols that fits its own duration model perfectly. Speed
hypotheses are therefore also scored on how much it costs them to describe the
signal, because a hypothesis three times too fast must break solid marks apart
to make the interval tile at all. Second, silence between transmissions is
unbounded, so word gaps may follow one another; without that, silence longer
than the widest single gap has no legal parse and the search invents marks
inside it to make the interval tile.

### Choosing between them

Neither is better everywhere, which is why both ship. Measured on the keying
style bench at 20 WPM and 20 dB:

| keying style | adaptive threshold | semi-Markov |
|---|---|---|
| machine | **0.117** | 0.242 |
| heavy weighting 1.15 | 0.133 | **0.058** |
| light weighting 0.85 | 0.133 | **0.108** |
| Farnsworth gap 5 | 0.100 | **0.075** |
| jitter 10% | **0.075** | 0.283 |
| jitter 20% | **0.267** | 0.458 |
| bug-like 0.8 weight + 15% jitter | **0.092** | 0.167 |

Lower is better; the figures are mean character error. The duration model wins
where the sender is systematically off the textbook ratios, which is what
weighted keying and Farnsworth spacing are, and loses where their timing
wanders. Hand and bug sending produce exactly that wander, so the adaptive
threshold remains the default. Across the synthetic accuracy surface the two are
level (0.2884 against 0.2946, against a spread of 0.03) and equal on receiver
captures, so the choice is about the sender, not about general accuracy.

### Adding another technique

Implement `CwKeyingSegmenter` (`cw_keying_segmenter.hpp`), add an entry to
`CwKeyingModel`, and add the option to Settings. The interface is deliberately
narrow: frames of calibrated log-likelihood in, committed runs out, plus a score
used to compare speed hypotheses and a state-size report. Note that
`cwEvidenceBoundNats` selects how much per-frame likelihood a model is given --
a per-frame threshold gains nothing past about three nats and is destabilised by
more, while a model integrating across a run gains a great deal.

## First pass: fast causal decode

The always-on path mixes each tracked tone to baseband and decimates it to a
small working rate. A small bank of nearby filter widths provides evidence for
weak, drifting, and slightly mistuned signals without repeatedly computing a
wide FFT. Phase/frequency continuity, robust noise quantiles, and a CFAR-like
threshold produce a probability of key-down rather than a hard on/off decision.

The current measured baseline implements parabolic sub-bin peak interpolation,
a bounded carrier/drift predictor, and parallel 60/120/240 Hz three-stage paths.
Initial acquisition stays at 120 Hz. Selection afterwards follows the keying
bandwidth the signal actually needs, about 3.5 element rates, so 120 Hz serves
speeds to roughly 41 WPM and 240 Hz beyond, while the 60 Hz path is further
restricted to signals at or below 15 WPM because its three cascaded sections
cost 15.9 ms of group delay, a sixth of a 12 WPM element but a quarter of a
20 WPM one. Drift of 30 Hz per second or more forces the widest path whatever
the speed, and a change of width takes effect only once twenty consecutive
observations agree. The per-width SNR and the centered-tone localization
measured alongside are reported evidence and a verification gate respectively;
neither chooses the width, because selecting on comparative filter power put
ordinary signals into a filter too narrow to resolve their own dits.

Lower and upper noise references
are smoothed independently and combined geometrically so neither one quiet
side nor one adjacent interferer dominates the local threshold. A per-track
responsive two-level envelope model supplies the keying evidence, as the
log-likelihood ratio between its mark and space hypotheses in linear amplitude,
each level carrying its own measured scatter. Unequal scatter moves the
decision off half amplitude deliberately: a mark carries signal plus noise and
a space carries noise alone, so the boundary sits nearer the mark and noise
excursions stop producing marks, which is the whole weak-signal gain. The slope
is measured rather than chosen, so it flattens toward zero -- meaning no
information -- on a channel carrying no CW. A bounded
recent amplitude history regularizes its space and mark levels only when a
robust split has support, at least 6 dB power separation, and at least 70%
explained variation; this supplies separation without the level collapse
measured with plain soft assignment. A bounded 0–1 narrow/wide concentration
metric replaces the former unbounded ratio.

The bounded track bank uses explicit admission control: a strong new carrier
may replace only the weakest unmatched unverified occupancy, never a verified
track. Once a track has enough persistence, an identity-breaking frequency
innovation starts a fresh track rather than transferring decoder history.
The association origin remains immutable except for a known receiver retune.
The adaptive DSP center follows accepted peaks, while a separate display center
is robustly reanchored at first verification and thereafter follows only
sustained coherent, low-dispersion motion through bounded deadband and slew
guards. Display correction never feeds classical association, decoding, or
color leases; the optional narrow character-refinement lane uses this robust
center specifically to reject short DSP-center excursions onto keyed
sidelobes.

Discovery and publication are separate. Broad shoulders fail a local-prominence
test combining a permissive near check with hertz-scaled far references;
surviving tracks move from candidate to Morse-likely only after repeated
spectral persistence, keyed edges, narrowband coherence, and spacing cadence.
At least three known symbols, a bounded recent unknown fraction (30% default), recent
mark-timing quality, and recent mean character confidence then verify the
trace. Passing evidence must remain valid for an entry interval; a longer
failure hold demotes a verified trace, with every gate continuously
re-evaluated. Every pending track has an inspectable rejection reason. These
defaults favor a delayed real signal over immediate false colored lines.
An optional overlap-confirmed local-model callsign can satisfy the final
symbol/timing/character checks for a track that already reached Morse-likely;
it cannot bypass spectral, edge, cadence, coherence, or sustained-entry gates.

Nine fixed 8–60 WPM timing anchors remain active for the entire segment. The
initial leader remains stable for presentation, but every alternative keeps
processing and the best complete path is selected again at silence or flush.
This avoids irrevocable early speed lock without rewriting stable text in the
middle of a transmission. Mid-segment switching remains future work and must
first define an append-only consensus boundary.

Alongside those character paths, a bounded decoder-independent cadence fit
records recent key-down and key-up run lengths. It searches candidate dot
durations from observed marks divided by 1/3 and gaps divided by 1/3/7, scores
them with a clipped robust residual, and reports acoustic WPM plus fit
confidence. A parallel score pairs each mark with its immediately following
gap: ordinary hand-key weighting moves their shared edge but preserves their
total. That paired estimate is used only for reported and per-sender cadence.
The original independent mark/gap estimate continues to control filter width,
rejection recovery, and lattice timing, so a display-quality improvement cannot
silently change decoded text or callsign publication. Neither estimate uses
decoded words. A sustained implausible-character rejection may reset an
unverified timing decoder only when the control estimate confirms regular Morse
cadence; carrier identity, noise tracking, and verified text remain untouched.

The dependency-free event lattice now receives every immutable key-envelope
mark/gap transition. At completed-gap checkpoints (bounded to at most once per
500 ms) its beam search emits up to four time/observation-scoped acoustic
alternatives. Characters and gap decisions common to all competitive paths are
committed into a separate append-only refinement after sufficient timing
evidence. The newest suffix remains provisional until at least one second and
six later physical observations support it, allowing later spacing to resolve
a recent character or word-gap ambiguity. Brief spectrum-association loss does
not force that suffix final; sustained silence or explicit end-of-input does.
The primary live transcript is not rewritten. The lattice tracks dit
length, mark/gap duration distributions, character and word spacing, and
manual-keying variance. This gives a low-resource baseline that can explain why
a character was selected and can abstain instead of inventing text.

Sustained silence or an explicit end-of-input boundary now retains a bounded
transmission-turn record. A shorter spectrum-association loss drains the
acoustic state but does not complete a turn; reacquisition must still satisfy
the decoder's longer silence rule, preserving slow-CW word gaps. An explicit
`CALL1 DE CALL2` or calling-station self-identification may attribute the sender,
while conflicting final paths abstain. Up to eight identified senders retain
separate cadence summaries. A prior can nudge a later WPM candidate by 30
percent only when independent cadence confidence is sufficient and the live
acoustic estimate already lies within a 0.75–1.35 ratio.

At turn completion, a separate context rescorer may choose only an alternative
inside the acoustic competitive margin whose non-whitespace character sequence
is identical to the acoustic best path. It can reconstruct a bounded set of
missing exchange-word boundaries, never change a character or rescue a rejected
path. This contextual text is presentation data; primary text, append-only
consensus, callsign confirmation, and verification remain unchanged.

The primary learned-likelihood path is deliberately limited to acoustic evidence: compare
a causal depthwise temporal convolution network and compact causal recurrent
models. Its inputs are physically scaled narrowband log energy,
phase/frequency error, side-channel contrast, and causal envelope features; it
emits key-down and target-channel-CW probabilities, never characters. The same
explainable semi-Markov timing lattice converts either deterministic or learned
probabilities into Morse alternatives. This keeps timing, UNKNOWN decisions,
stable-prefix rules, and callsign/context policy outside the model and avoids a
second opaque text decoder.

A first experimental vertical slice was built as reproducible local tooling and
has since been removed from this repository. It generated checksummed synthetic
PCM and exact key runs from scratch, used profile-grouped train/validation/test
splits, trained a small stateful causal GRU, reported frame calibration plus
transition excess and implausibly short runs, checked streaming ONNX export
equivalence, and could infer from resampled mono PCM16 WAV input. Its durable
result is a constraint on any successor rather than an artifact: aggregate frame
metrics were shown to hide unusable envelope fragmentation, so temporal topology
and downstream character accuracy are mandatory gates and not optional
reporting. No training pipeline ships in this tree today, and no
learned-likelihood artifact is bundled with the application; the generic
optional runtime below carries no model.
The first target is at most two million INT8 parameters, a bounded state cache,
and CPU-only operation. This is a design budget to benchmark, not a performance
claim. Model choice will be made from the character-error-rate/resource Pareto
frontier rather than architecture popularity.

An additional optional character-refinement boundary is now implemented for
local operator-supplied ONNX models. It does not replace that primary design.
At most four verified, Morse-likely, or manually selected tracks are translated
into independent 30–50 Hz lanes, resampled to a strictly validated feature
contract, and submitted as overlapping eight-second windows to a dedicated
CPU inference thread. Pending work is bounded and coalesced per track, so a
slow model cannot back up capture or DSP queues. Timestamp-aware consensus
accepts only repeated overlap evidence, rejects stale track/frontend
generations, and appends stable text without revising its current generation.

This refinement remains downstream of carrier qualification. Its transcript is
labeled separately and cannot create a track, keep a silent track alive,
replace raw acoustic output, or reach TX. A structurally valid callsign
confirmed by overlapping model windows may complete verification only after
the same track has independently reached Morse-likely through spectral,
keying, cadence, and coherence gates; the ordinary sustained-entry interval
still applies. The confirmed model callsign is shown with explicit provenance. No model
is bundled or downloaded; model accuracy, license, and provenance remain the
operator's responsibility until an independently trained artifact passes the
published corpus gates.

## Multiple-pass weak-signal decode

Ordinary CW does not contain the fixed framing or forward-error-correction bits
of a structured weak-signal digital mode. Multiple passes therefore cannot
create missing information, but they can make better use of observations that a
low-latency causal pass could not yet interpret. Of the five passes below, the
live pass and a first slice of the segment pass are implemented, as described
at the end of this section; the other three are design.

1. **Live pass:** updates element and provisional-character hypotheses with the
   lowest latency. It never waits for a complete transmission.
2. **Rolling refinement pass:** reprocesses a bounded 2–5 second narrowband
   buffer with limited future context, alternative filter widths, nearby tone
   tracks, and competing WPM/timing hypotheses. It may revise only text still
   marked provisional.
3. **Segment pass:** after a word, callsign exchange, or transmission gap, a
   longer bounded segment is rescored in both directions. Acoustic/timing
   evidence remains dominant; QSO or callsign context may re-rank n-best
   candidates but is displayed as a separate inferred suggestion.
4. **Interference-cancellation pass:** for overlapping tracks, reconstruct the
   confidently decoded stronger keyed carrier and subtract it conservatively,
   then retry the residual for weaker signals. The result is accepted only when
   residual and decoding scores improve; the original samples and first-pass
   result remain available.
5. **Optional diversity pass:** when two synchronized receive sources cover the
   same RF signal, align and combine confidence or narrowband evidence. A poor
   source must be rejected rather than reducing the stronger source's result.

No rolling narrowband store exists yet. When it is built it should hold
decimated narrowband samples or features, not a
duplicate full-rate stream for every channel. At 3.2 kHz mono int16, 30 seconds
is about 192 kB per track before metadata; its total size and track count remain
hard-limited. Refinement work runs only inside a configured CPU budget and is
discardable before live capture is allowed to overrun.

The first segment-pass slice is implemented. At a completed transmission only,
the retained bounded physical mark/gap lattice is decoded at up to nine
distinct WPM values already maintained by the live hypotheses. A pass can
replace the live baseline only when it covers the same observations, preserves
confidence and symbol-count bounds, materially lowers normalized acoustic cost,
cleanly extends the committed observation boundary, and has no contradictory
near-tied physical explanation. Otherwise it abstains. Live and provisional
text are untouched. Alternative filter-width passes remain pending because the
receiver does not yet retain replayable per-width features; retrying a cleared
filter envelope would not be a valid acoustic comparison.

## Same-frequency pileup separation

Several callers may occupy the same displayed frequency at a runner station.
They must be modeled as a mixture of operators inside one channel, not as one
malformed keyer. No separator is built: what follows is the design, and only
the timing fingerprint described further down exists in the tree today. The
separator would first maintain soft identity fingerprints from:

- sub-bin carrier offset, phase evolution, chirp, and short-term drift;
- WPM plus separate dit, dah, intra-character, character, and word-spacing
  distributions;
- rise/fall shape, key-click signature, element weighting, and timing jitter;
- slowly varying amplitude, QSB trajectory, and receiver-path observations;
- decoded-prefix compatibility only as a separately weighted contextual clue.

The first diagnostic foundation retains an exact physical-run timing
fingerprint with each completed turn when the event lattice has a contiguous,
untruncated sequence. It measures dit/dah and gap counts/medians, weighting,
normalized mark residual, evidence interval, and confidence. Replay and debug
capture expose the record—or explicitly mark it unavailable—but it deliberately
contains no callsign, carrier identity, or inferred sender. Carrier/phase
features still require a separately timestamp-aligned source before the
fingerprint can participate in operator association.

A bounded factorial semi-Markov model would jointly estimate the key-up/key-down
and
timing state of two callers first, expanding to three only when evidence and CPU
budget justify it. The strongest high-confidence hypothesis is reconstructed as
a complex keyed carrier, including its measured envelope and phase trajectory.
Successive interference cancellation then retries the residual, while a joint
beam keeps alternative assignments when operator identities could swap. None of
that is implemented, and neither is the interference-cancellation pass listed
among the multiple passes above.

The separator is to be evaluated across relative power, sub-bin frequency difference,
speed difference, cadence similarity, overlap percentage, fading, and number of
callers. Operator lock lets a human seed or retain one fingerprint without
forcing decoded characters. Contextual callsign completion can re-rank an
acoustically plausible alternative, never manufacture one.

There is a physical identifiability boundary: two callers that are simultaneous
and indistinguishable in carrier, phase, envelope, and timing cannot be uniquely
recovered from one mono mixture. In that case the decoder reports competing
hypotheses/unknown intervals. Phase-coherent antennas or receivers can add
spatial evidence, and non-coherent receiver diversity can add independent
fading evidence, but both require measured alignment and a safe single-source
fallback.

### Operator role

Exchange context alone cannot always say which station a call belongs to. `TU`
precedes a runner identifying itself (`TU IU0LFQ`) and equally the station it
has just worked (`TU DL1NKB`), and both score the same, so a run can be labelled
with the station that was worked rather than the one being listened to.

What settles it is knowing what the operator is doing, which is configured under
Settings -> Station:

```mermaid
flowchart LR
  R{"operating role"}
  R -->|"Monitoring (default)"| M["no assumption:<br/>exchange context alone ranks candidates"]
  R -->|"Search and pounce"| SP["the monitored stream is a runner:<br/>an unambiguous runner context<br/>outranks the ambiguous TU"]
  R -->|"Running"| RU["the monitored stream is answering you:<br/>a repeated bare call outranks<br/>one introduced by someone else's CQ"]
  M --> OWN["in every role, the operator's own call<br/>is removed from candidate scoring"]
  SP --> OWN
  RU --> OWN
```

Monitoring makes no assumption and ranks candidates on exchange context alone,
as the decoder always has. Searching and pouncing, the stream being listened to
is a runner, so a call introduced as the sender's own outranks one merely
mentioned after `TU`. Running, the stream is somebody answering, so a call sent
bare and repeated outranks one introduced by a `CQ` that belongs to another
transmission. In all three the operator's own callsign is removed from candidate
scoring rather than blanked after the fact, so a transmission that mentions the
operator is still labelled with the station actually being heard.

Measured on exactly that ambiguity, `5NN TU DL1NKB OK5OO UP K` labels DL1NKB --
the station just worked -- with no role, and OK5OO, the split runner, when
hunting. Role knowledge changes only which candidate is ranked highest; it never
creates, rewrites, or corrects decoded characters.

### Exchange-role inference

The first segmentation slice now retains completed transmission turns and can
name a sender from explicit handover evidence. Broader role inference operates
on those segments and n-best decoded tokens; it must not concatenate every
operator on a frequency into one asserted identity. It selects an explicit
conversation profile rather than assuming all
traffic is a contest. The profile kinds that exist are an ordinary directed
QSO, a DX pileup, special-event operation, a contest-specific exchange, and a
neutral monitoring profile that makes no assumption and supplies no language
prior; monitoring is the default. A beacon profile has not been written. The
following is the
initial bounded runner/pileup state machine, not a universal grammar:

1. a runner solicitation (`CQ`, optionally a contest qualifier, `DE`, a
   repeated self-callsign, or callsign followed by `UP`);
2. a short caller response, usually one callsign repeated once or twice;
3. a runner response containing the selected caller's call plus report/exchange;
4. the caller's report/exchange; and
5. runner acknowledgement (`TU`) followed by the stable runner callsign, `CQ`,
   or another solicitation.

Each segment first receives an acoustic operator assignment from the carrier,
keying-envelope, speed, weighting, spacing, and jitter fingerprint above. Text
then supplies only soft role likelihoods. `DE`, `CQ`, `TU`, and callsign-before-
`UP` strongly support runner self-identification; a standalone repeated
callsign immediately following a solicitation supports a caller; a callsign
followed by a report supports the runner addressing that caller. Reports,
serials, abbreviations, and isolated callsign-shaped tokens never establish an
identity by themselves. Evidence decays over a bounded observation window, but
the recurring runner identity is expected to outscore changing callers.

The marker label represents only a sufficiently supported persistent station
identity. Caller alternatives and their role confidence belong in the decoded
session, not in a rapidly changing marker label. If acoustic assignments swap,
the exchange sequence is incomplete, or two simultaneous operators remain
indistinguishable, the role stays unknown and competing calls remain visibly
provisional. Context can rank acoustically possible hypotheses; it cannot split
an inseparable waveform or replace incorrectly decoded dits and dahs.

Each contest profile is a separately revisioned data file carrying the address
of its published rules and the dates the exchange applies between. It declares
the fields each side sends, their kinds and lengths, the whole-token aliases
and character-by-character cut-number readings that count as legal, and the
order in which the runner and the caller send them. It declares nothing else:
the conversation flow, its states, and every transmit safety gate are built by
the application, and no profile file can name them, arm a transmitter, change
a key-down timeout, or relax callsign confirmation. Ordinary-QSO profiles allow
open-ended name/QTH/rig/weather/conversation text and must avoid forcing it into
a contest exchange. A suggested reply is a separate guarded action: exact
counterpart identity and current context must be confirmed, TX must be armed,
the maximum-key-down and emergency-release paths remain active, and the
operator receives a cancellable preview and must still take an explicit send
action. Those gates are invariants in code rather than profile settings, and a
decoder event has no route to transmission at all, so decoder confidence can
never trigger a reply.

## Stable text and uncertainty

Decoder output has three distinct forms:

- **raw evidence:** tone, key-down probability, timing, frequency, and SNR;
- **provisional text:** may be revised by a bounded later pass and is styled as
  uncertain in the UI;
- **stable text:** is append-only once its confirmation delay and confidence
  criteria pass, except for an explicit operator correction.

A fourth, derived **contextual presentation** separates completed turns and may
repair only word boundaries while preserving the exact non-whitespace decoded
character sequence. It is never fed back into raw/stable evidence, callsign
confirmation, verification, or transmit control.

One further presentation rule applies to both the primary and the refined
transcript as they are published. A run of six or more of the one- and
two-element characters — E, T, I, A, N and M, with any spaces inside the run
counted as part of the same damage — is replaced by a single space. Such a run
is what an envelope broken into fragments reads as, and a single space says
plainly that something here was not copyable, where deleting it would join
unrelated text together. Six is the shortest safe length: real copy does reach
runs of four and five. The same measure cannot be applied to a whole track,
because tracks that recovered a correct callsign themselves reach runs of ten
between the parts they copied. Nothing upstream sees this; the decoded record
and every gate that reads it are unchanged.

Every character carries confidence, pass number, time interval, selected track,
and acoustic-versus-context contribution. Low-confidence intervals produce a
visible unknown/alternative rather than a plausible-looking fabrication.
Confidence must be calibrated on held-out data, not treated as trustworthy just
because a model emits a large probability.

Callsign lists and QSO grammar are optional search priors. They never replace
raw text, never turn absence into “invalid,” and never independently authorize
transmission. The operator can disable the callsign lists, managed and
operator-supplied alike, on the decoder page of Settings.

## Training and test data

The reproducible generator needs exact sample-level labels and domain
randomization over:

- speed, Farnsworth spacing, acceleration, timing jitter, and missing or
  extended elements;
- paddle, straight-key, bug, cootie, and imperfect human timing distributions;
- carrier offset, chirp, drift, phase discontinuity, key clicks, and oscillator
  instability;
- calibrated noise, QSB/fading, AGC pumping, clipping, hum, impulses, speech,
  carriers, adjacent CW, and exact/near-exact co-channel multi-operator pileups;
- receiver filter shapes, sample-rate error, sound-card paths, lossy network
  audio, and SDR demodulator artifacts;
- no-CW hard negatives so silence, noise, data modes, and speech do not create
  convincing text.

Real recordings with clear redistribution rights complement synthetic data.
Training, validation, and test sets must be separated by message, callsign,
operator/keying profile, noise recording, and propagation seed. The acoustic
test set must not gain an unfair advantage from the callsign list used by a
context pass.

## Benchmarks and acceptance gates

The deterministic executable gate (`cwa_decoder_benchmark`) reports character
edits/CER, acquired-WPM error, false characters during a fixed no-CW minute,
processed updates, simulated duration, wall time, real-time factor, bounded
hypothesis count, and a conservative allocated-state estimate. Its cases cover
8, 12, 20, 25, 40, and 55 WPM with fixed weak-SNR and timing-jitter sequences,
plus a 12→40 WPM change across a transmission gap. The state estimate includes
bounded character-evidence buffers and must stay below 256 KiB across this
corpus. The deterministic baseline and acoustic consensus have zero edits on
the 8--55 WPM timing corpus, the consensus repairs the included compressed-gap
callsign case where the conservative primary path retains an unknown, and both
remain append-only across the long-stream truncation case. There are no
speed-limit failures and no false characters in the synthetic noise minute. This is a
regression floor, not the final corpus or a claim of
calibrated over-the-air performance. Separate core regressions drive two
simultaneous original-sample tones through the channel bank, verify independent
decodes and stable identity, and reject an adjacent non-tracked tone. The Qt
pipeline regression verifies that the live DSP worker publishes both a spectrum
frame and a raw-narrowband channel result.

A second generated production-path gate (`cwa_decoder_quality_gate`) enforces
normalized CER and WER, exact set-based callsign precision/recall, time to first
provisional and stable text, verified-stream acquisition latency, and zero false
stream or callsign publications during a one-minute no-carrier fixture. Its
clean and moderately jittered messages are a deterministic regression floor,
not receiver calibration: weak-SNR curves, revision rate, co-channel overlap,
and annotated real-audio accuracy remain required.

A disjoint generated receiver-path gate (`cwa_receiver_holdout_benchmark`) adds
manual weighting, timing jitter,
drift, fading, publication/revision measurements, exact callsign scoring, and a
30-second no-CW hard negative. Its current production-path baseline is CER
0.458, WER 0.786, exact-call precision 1.0, recall 0.333, maximum publication
latency 7.13 seconds, and zero hard-negative publications. The limits it
actually enforces sit above those figures on purpose: character error at or
below 0.55, word error at or below 0.95, exact callsign precision of one with
no wrong callsign at all, recall of at least a third, publication latency
within eight seconds, and not one publication on the hard negative. These
deliberately
weak accuracy figures are a regression ceiling, not a receiver-quality claim.

Two further gates measure what those cannot. A generated speed-and-noise
surface (`cwa_decoder_surface_benchmark`) synthesises keyed audio and drives it
through the spectrum analyzer and the channel bank, so unlike the injection
benchmarks it measures the front end as well as the timing decoder; it reports
mean character error per seed set over a grid of speeds and signal-to-noise
ratios, counts callsigns asserted wrongly as well as correctly, and prints the
spread across its seed sets as the resolution below which a difference is not a
result. The registered run covers 16, 25 and 40 WPM at 20 and 12 dB over three
seed sets; a `--full` argument widens it to 12 through 50 WPM at 30, 20, 15 and
12 dB, and a second argument selects the semi-Markov keying model so that a
regression in the model an operator can choose is not invisible. It
deliberately gates on a paired comparison between two paths run on
the same audio in the same process rather than on any absolute error figure,
because that absolute figure moves with the noise draw. A spacing benchmark
(`cwa_spacing_benchmark`) covers word-boundary placement, which is the only
thing the context vocabulary is permitted to change and which neither the
synthetic surface nor the capture corpus can see; it is compiled against the
repository dictionaries and allowed ten minutes.

Two more tools are built but deliberately not registered as tests. A receive
profiler (`cwa_receive_profile`) reports per-stage wall time for the live
receive path against the number of tracked signals; it reports rather than
gates, because its absolute figures depend on the machine, and it exists so
that an optimisation is accepted only after a measurement. A capture replay
(`cwa_capture_replay`) replays a private recording and, when given an
annotation sidecar whose SHA-256 matches the audio, scores the decoder against
it; operator captures are private and are therefore not CI fixtures. That
checksum-bound annotation replay is the path to
replacing generated limits with reviewed over-the-air evidence.

Receiver reports distinguish two callsign questions: whether the application
actually published a call by the end of the annotated event, and whether a call
could be extracted from that event's transcript. Publication and callsign
episodes retain later withdrawals. False-publication rates use only explicitly
reviewed coverage; uncertain events protect a matched signal from false-positive
accounting but do not contribute accuracy scores.

The timing hypotheses retain one update each. Scalar evidence remains current
at the 500 Hz decoder cadence, while strings and character vectors refresh only
at Morse boundaries. This removes nine unchanged deep copies per track; the
outer presentation snapshot remains a bounded future optimization.

Each experiment publishes:

- character error rate and word error rate;
- exact callsign precision/recall and complete-call accuracy;
- false characters and false callsigns per minute on no-CW recordings;
- weak-signal detection probability versus SNR, always stating the SNR
  measurement bandwidth;
- time to first provisional character, time to stable character, and revision
  rate;
- accuracy by speed, keying style, drift, fading, interference, and number of
  simultaneous signals;
- real-time factor, CPU utilization, peak memory, queue depth, and overload
  behavior on each reference platform.

The benchmark first establishes the deterministic semi-Markov baseline. Small
learned front ends, multi-pass rescoring, and interference cancellation are
then added independently. A feature is enabled by default only when the held-out
gain is repeatable and its resource cost is within the published budget.

## Delivery sequence

1. Build deterministic synthetic/noise fixtures and the benchmark runner.
   (Delivered, including the end-to-end audio surface and the annotation-bound
   capture replay.)
2. Implement tone tracking, initial raw-sample narrowband evidence, and the
   explainable timing baseline. (Initial path and bounded multi-speed delivered.)
3. Add provisional/stable text and calibrated confidence contracts. (The
   provisional/stable contract is delivered; calibrated confidence is not.)
4. Extend the delivered bounded recent evidence and continuous fixed-anchor
   evaluation with safe mid-segment consensus switching and multi-pass
   refinement. (The completed-turn segment pass is delivered; mid-segment
   switching is not.)
5. Re-establish the learned-likelihood experiment, compare compact causal
   candidates, and ship one only after receiver character gain, hard-negative,
   license, provenance, checksum, fallback, and resource checks pass.
6. Add conservative strongest-track cancellation and optional diversity input.
7. Add two-source then bounded three-source joint co-channel separation.
8. Add separately labeled QSO/callsign re-ranking and operator controls. (The
   completed-turn context rescorer and the operating-role setting are
   delivered; the cluster and spot priors below are not.)

The re-ranking stage operates only after acoustic alternatives exist. Unknown
symbols and uncertain gap boundaries remain explicit lattice branches; a
versioned callsign list can score compatible branches, while a recent
frequency/mode-matched cluster or beacon spot supplies an additional prior.
The UI must preserve the acoustic transcript and identify every proposed
replacement, source, age/version, frequency delta, and score. A database hit is
not acoustic proof, and a cluster spot cannot distinguish two stations sending
on the same mono-audio carrier.

## Research basis

- [Morse Code Datasets for Machine Learning](https://arxiv.org/abs/1807.04239)
  motivates parameterized synthetic data and repeatable evaluation.
- [A CNN/BiLSTM/CTC Morse decoder study](https://jeit.ac.cn/article/doi/10.11999/JEIT190658?pageType=en&viewType=HTML)
  evaluates learned decoding across speed, drift, SNR, and timing variation.
- [A simulation model for CW contest interference](https://arxiv.org/abs/2402.04742)
  supports explicit multi-signal and interference test scenarios.
- [Gain-adapted factorial hidden Markov source separation](https://arxiv.org/abs/1901.07604)
  demonstrates joint temporal inference for two unknown-gain sources in one
  observed channel; its speech result is a methodological reference to test,
  not evidence that CW separation is already solved.
- [Multichannel factorial hidden Markov source separation](https://www.isca-archive.org/interspeech_2014/higuchi14_interspeech.pdf)
  motivates using spatial evidence when synchronized receiver channels exist.
- [Stateful Conformer for cache-based streaming inference](https://arxiv.org/abs/2312.17279)
  describes bounded cached context for streaming sequence models.
- [Efficient Conformer](https://arxiv.org/abs/2109.01163) studies progressive
  downsampling and grouped attention under constrained inference budgets.
- [ONNX Runtime quantization guidance](https://onnxruntime.ai/docs/how-to/quantization.html)
  documents CPU quantization formats and the need to measure accuracy and
  hardware-specific performance.

Public implementation repositories are useful experimental references, but
their self-reported results are not acceptance evidence. CW Buddy will not
import a model, training data, or code until its license and data provenance are
compatible with GPL-3.0-or-later and independently reproduced benchmarks.
