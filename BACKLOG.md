# Product backlog

Updated: 2026-09-08

This is the canonical prioritized backlog. Status values are `todo`, `active`,
`blocked`, and `done`. Every source, test, build, or automation change must
review this file and update affected items or the “Last reviewed” note.

Last reviewed: 2026-09-08 (twentieth entry) -- implemented bounded
transmission-turn segmentation at sustained-silence/end-of-input boundaries,
strong-evidence current-sender attribution, and separate retained cadence
summaries for explicitly identified senders. A prior cadence can only nudge an
already compatible live timing estimate. Completed-turn context may repair
word boundaries only among acoustically competitive paths with identical
decoded characters; it cannot rewrite raw or phase-consensus evidence. The
alternating-simplex regression now requires two correctly attributed turns at
distinct 18/30 WPM cadences, and an association-suspension regression prevents
a slow word gap from becoming a false turn. The VFO presentation and hover
guidance were also consolidated without adding provider TX/mode write support.

Previous review: 2026-09-08 (nineteenth entry) -- a clean ordinary-QSO capture
proved that one frequency observation can contain two alternating operators.
The completed `CALL1 DE CALL2` handover now retains and presents both
participants on one card. Per-transmission silence boundaries, current-sender
inference, and conversation-aware timing reset remain explicitly separate so
the application does not invent two frequency tracks or guess a speaker.

Previous review: 2026-09-08 (eighteenth entry) -- implemented bounded full-window
and selected-track audio monitoring plus the first hardware-inert guarded TX
workflow: free text/own call/report Morse plans, exact confirmation, advisory
Auto-QSO proposals, emergency release, and a tested 15-second TUNE watchdog.
Defined the next pileup operating slice: keep the runner at the 700 Hz guide,
show its receive-side pileup to the right, and rank operator-confirmed split-TX
slots only after authoritative CAT split state and selected-track monitoring.

Previous review: 2026-09-08 (seventeenth entry) -- the owner-supplied CW Buddy
mark now has a genuine transparent exterior around its rounded-square edge.
The alpha-aware master is propagated to Qt/Linux PNG, Windows ICO, macOS ICNS,
and README artwork. This local packaging polish is held for the next
source-bearing publication rather than spending a release on artwork alone.

Previous review: 2026-09-08 (sixteenth entry) -- the public product identity is
now CW Buddy across the UI, executables, installers, release assets, update
manifest, documentation, and repository links, with the owner-supplied mark as
the cross-platform application icon and README artwork. The established macOS
bundle identifier and Windows upgrade GUID remain stable so the rename is an
upgrade, not a second product; the Debian package replaces the previous package,
and first launch imports existing profiles and the managed SCP cache. Hosted
packaging remains the required cross-platform acceptance gate.

Previous review: 2026-09-08 (fifteenth entry) -- DSP-006's first bounded slice is
implemented. The responsive hard-assignment tracker remains for fast attack,
fading, and manual weighting, but a 512 ms allocation-free amplitude history
now anchors it to two robust modes only when both populations have support, at
least 6 dB power separation, and at least 70% explained variation. This is the
separation prior the rejected soft/EM experiment lacked. Paired on identical
cross-platform reproducible generated audio, mean full-surface character error
improves from 0.3070 to 0.2579, with every seed set improving; wrong callsign
assertions fall from 40 to 36. The receiver corpus holds at 8/9
corroborated calls and 0/4 false calls on no-CW recordings. Captures now record
the split evidence and whether the anchor was accepted. Full corpus replay rises
from 20.13 to 25.75 seconds, so eliminating per-frame deep decoder snapshots is
the next performance task before wider multi-track scaling. Remaining DSP-006
work is a fully probabilistic separated-state estimator with held-out annotated
receiver CER, not further threshold-only tuning.

Previous review: 2026-09-08 (fourteenth entry) -- the keying technique is now a
choice rather than a fixed part of the decoder, and a duration-explicit model is
offered beside the shipped threshold. Measured paired against the previous
build, the two are level on the synthetic surface (0.2946 against 0.2884, inside
the +/-0.03 spread that surface has) and equal on captures (8/9, 0/4 false), and
they differ sharply by keying style rather than in general, so neither replaces
the other. The default is unchanged and reproduces its previous figures exactly.

Two things this measurement settled and that should not be re-derived. First,
the duration model is four times better than the threshold in isolation on
matched evidence (0.061 against 0.236) yet only level end to end, which places
the remaining limit upstream, in the two-level envelope model that produces the
keying likelihood: it assigns each frame to whichever level it is nearer, and at
low SNR that is close to a coin toss that drags the levels together. Replacing
that with plain soft assignment is worse, not better -- 0.3884 against 0.2946,
collapsing outright at 12 WPM and 12 dB -- because near the middle both
responsibilities sit near a half and an ambiguous frame pulls the levels
together. Whatever replaces it needs a separation prior. Second, the calibrated
likelihood was clamped to three nats per frame, a bound a per-frame threshold
does not notice and an integrating model pays for; widening it for the latter
moved capture recovery from seven of nine to eight.

Previous review: 2026-09-07 (thirteenth entry) — tracks are verified as CW on
recordings that contain none: eight across the four such captures, one to three
each, published with text like "BL E EEHWE IH K I E". Nothing measured this. The
quality checks count false callsigns, so a track wrongly accepted as a signal was
invisible, which is the same blind spot callsign precision had.

Tightening the verification timing-quality threshold does not fix it and is
recorded so it is not retried: at 0.55 there are eight false tracks and eight of
nine callsigns recovered; at 0.70, still eight and seven; at 0.80, four and six;
at 0.88, one and four. Reaching one false track costs half the real stations,
because noise reaches the same timing quality as CW -- measured values overlap,
noise 0.62 to 0.80 against real 0.75 to 0.94. The threshold stays where it is.
What discriminates on structure rather than timing is CW-006's recognisable
pattern evidence, and that is the honest next attempt.

The same sweep found that the threshold had been left parameterised by a
build-time define since the keying-decision work earlier in the day, surviving
eight commits and every verification run: the sweep silently changed nothing,
and three identical rows were what exposed it. The define is gone and the
verification battery now fails on any stray build-time define or leftover
standard-error output anywhere in the shipped sources, because the previous
check listed macro names by hand and could only catch the ones already known.

Last reviewed: 2026-09-07 (twelfth entry) — the local model reported an error
for a model that was never configured. Enabling it without selecting files still
attempted a load, and an empty path fails the metadata check by the same branch
as a file of the wrong kind or past the size limit, so the card showed a size
complaint for a model the operator had not chosen. An enabled but unfinished
setup is now its own state, loads nothing and says what to select, and the panel
is hidden while the feature is unused.

Also measured and not adopted: choosing between the consensus and the literal
transcript when they disagree. Neither available signal discriminates. The
lattice's evidence confidence moves only from 0.830 to 0.821 across a seventeen
fold change in the consensus's own character error, and its acoustic cost per
symbol is anti-correlated at the extremes -- the best consensus measured, at
0.016 error, carried the highest cost per symbol at 0.41, while one at 0.391
error carried 0.18. A four point sample suggested cost tracked quality and an
eighteen point sample destroyed it. Any preference rule written on these signals
would be arbitrary, so none was written; calibrating the lattice's confidence so
that it means something is the real work, and it is its own piece.

Last reviewed: 2026-09-07 (eleventh entry) — CW-001's Farnsworth item is closed.
The event lattice scored gaps against fixed centres of one, three and seven
element lengths. Farnsworth sending holds element timing at the operator's speed
and stretches the character and word gaps by a common factor, so a stretched
character gap sat nearer the word-gap centre and was read as a word gap: the
consensus transcript split every character apart while the literal transcript
beside it was perfect, and the card prefers the consensus. The factor is now
recovered from the observed gaps, since ordinary text holds far more character
gaps than word gaps and the median gap clearly longer than an element gap is
therefore a character gap. Character and word centres scale together, element
timing is untouched, and nothing adapts without at least six confident gaps or
outside the range real sending occupies.

Measured on a controlled fixture with the word gap stretched proportionally, as
Farnsworth does: consensus character error falls from 0.562 to 0.031, 0.547 to
0.062 and 0.516 to 0.094 as spacing stretches, while the standard-spacing case
is unchanged. The paired surface benchmark and the keying-style figures are
byte-identical to baseline and captures hold at eight of nine with none asserted
on the four containing no CW. On receiver capture 20260907-175150 a callsign
that decoded as "R 7 K B ?" now reads "R7KBB"; the same capture also begins
asserting R7KBTI, joining its one corrupted repetition where previously nothing
was asserted, which is a real cost of joining what had been fragments.

Two things measured along the way and not adopted. The lattice's evidence
confidence does not track its own correctness -- 0.845 when its transcript was
0.016 character error and 0.847 when it was 0.594 -- so it cannot be used to
decide whether the consensus or the literal transcript should be shown. And a
fixed stretched character-gap centre improved only the operating point it was
placed at, leaving the gap between centres untouched, which is fitting a fixture
rather than modelling the sender.

Last reviewed: 2026-09-07 (tenth entry) — PKG-004's startup update notice now
shares the verified-download presentation contract used by Settings → About:
progress and checksum status remain visible, then **Open Installer** and the
platform reveal action replace **Download update** in place. CALL-001/CALL-005
also keep a missing word gap from turning `DE`, `CQ`, `TU`, or `QRZ` into part
of the selected callsign when the remainder is independently plausible, and
place the correction control beside the loaded-list state.

Last reviewed: 2026-09-07 (tenth entry) — receiver captures 20260907-125614 and
20260907-141450 were added to the local corpus and drove three fixes. A station
sending CQ CQ CQ DE SV7BIO was labelled DESV7BIO: a missing word gap merged the
prosign onto the callsign, and the merged token then collected the CQ context
credit while the real callsign, which stood alone twice in the same text, got
only repetition. Splitting the pair is now done at tokenisation and only where
the remainder is itself a plausible callsign, which leaves a genuine
DE-prefixed German call intact.

The same capture explained two display complaints. Signal level was reported
from an instantaneous reading, which on a keyed carrier is about +28 dB inside
a mark and below zero inside a gap; the recorded diagnostics for that station
swing between -17 and +30 dB frame to frame, and the -6.3 dB the operator saw
was simply a gap sample on a strong signal. The presented figure is now the
estimated mark level, +31 dB for that station. Confidence was reported from the
instantaneous value, which falls to zero between characters and read zero per
cent while text arrived; it now reports the character-averaged figure.

The wandering speed from those captures is addressed at the presentation. The
estimator is unchanged and still adapts freely, but a speed is withheld until
at least three symbols support one: before that the value is the seeded default
rather than a measurement, which is why a 27 WPM station read 20 WPM with
nothing decoded and 40 WPM on its fourth key transition. Six of fourteen
sampled readings across that recording were unsupported and are now withheld,
narrowing the displayed range from 20-28 WPM to 25-28. The independent cadence
estimate still spikes to 40.9 on thin evidence and is not yet gated. The transcript away from the callsign remains
poor at this signal strength, which is the retention limit already recorded
above rather than anything specific to this recording.

An adaptive gap-timing scheme clustering intra-element, character and word gaps
per segment was evaluated against the paired benchmark and not adopted: mean
character error was identical on all three seed sets, one additional wrong
callsign was asserted, and the refined transcript on 20260907-141450 was worse
than without it. The idea of learning spacing from the observed gap population
is sound and better founded than adapting to signal quality, which has no
usable signal; it needs to show a paired gain before it ships.

Last reviewed: 2026-09-07 (ninth entry) — callsign precision was never measured
and is poor. The quality checks counted callsigns recovered and false callsigns
on recordings containing no CW; a wrong station named on a live signal was
invisible. Measured against the corroborated receiver captures, six of nine
assert some other callsign at the true station's own frequency, usually a near
miss of it: EG1PDA also asserted as EG1PEIE, DB26YLBB as UR5BV, EA1EYL as A1E,
4X1MM as K1NE, EM90ZMV as T90ZTTV. The decoder surface benchmark now reports
the same thing on synthetic signals, where six assertions in thirty-three name a
station that was never sent.

It cannot be fixed by scoring, and two attempts to do so are recorded here so
they are not repeated. Raising the acceptance threshold from 3 to 5 removes
every wrong assertion but drops correct ones from six to four and, worse, kills
the exact-repetition path that identifies a pileup caller sending only its own
call -- which is a case the application must keep. Weakening the bare closing
prosign weight changes nothing at any value from 4 down to 1, because these
tokens are followed by PSE K, a separate and much stronger rule. The reason is
structural: the false tokens sit in positions where a callsign genuinely
belongs, so context scoring cannot separate a correctly placed wrong token from
a correctly placed right one. Only better decoding or external corroboration
can, which is what the offline-list badge and the near-miss correction provide.

Last reviewed: 2026-09-07 (eighth entry) — CW-001. Refining where element
boundaries are placed was proposed as the remaining weak-signal work and is
rejected on measurement, before any decoder code was written for it. The idea
was to keep the threshold for deciding that a transition happened and place the
boundary itself by likelihood, on the reasoning that one threshold crossing
cannot serve both mark retention and timing precision.

Placement is already accurate. Measured against known signals at 20 WPM, the
median boundary lands within 3 per cent of a dot of the truth at 20, 15 and 12
dB. Refining that would gain a percent or two of a dot, and the error surface
says a quarter of a dot of jitter costs only 0.045 character error, so there is
nothing there to win.

The tail is a different failure. The ninetieth percentile sits near half a dot
at every signal-to-noise ratio, and an edge that far out is a mark detected in
the wrong place or not at all rather than one mistimed: it is the retention
problem again, and placement refinement does not touch it. Retention remains
what the error surface says is expensive -- losing a tenth of the marks costs
0.455 against 0.125 for inventing a tenth -- and it cannot be bought with the
keying thresholds without lengthening every mark, which the cadence estimator
and the lattice's evidence confidence both detect. What is left is genuinely
structural: the lattice would have to hypothesise marks and gaps from the
evidence stream instead of being handed runs a threshold already extracted.
That is a large piece of work and should be scoped deliberately rather than
approached as a tuning change.

Last reviewed: 2026-09-07 (seventh entry) — the remaining unrecovered receiver
capture, 20260903-165900, was investigated and two proposed explanations were
disproved by measurement before anything was built on them.

It is not an acquisition transient. The theory was that opening characters are
always lost because element boundaries must be committed before a speed
estimate exists. A clean synthetic signal decodes the same callsign correctly
from its first repetition at 16, 20 and 30 WPM, so no such general defect
exists; the leading-token corruption seen in keying-style output is real but
does not generalise to a mechanism.

It is not track splitting either. Eleven of nineteen captures hold track pairs
within 5 Hz, some 0.1 Hz apart, which looked like one carrier held under two
identities. They are sequential rather than simultaneous: at 1015 Hz in capture
20260902-132323 the acquisitions are 110, 128, 156 and 232 seconds apart. That
is a station transmitting intermittently with tracks expiring between overs,
which is the documented identity lifecycle.

What is actually wrong is narrower. The callsign decodes as "EM ?0ZMV": a
spurious gap splits it in two, and the character distinguishing the station is
unknown. Joining the fragments would not settle it -- a wildcard lookup for
EM?0ZMV matches EM80ZMV and EM90ZMV at distance zero, and the new directory
correction refuses an ambiguous neighbourhood by design rather than guessing
between two real stations. Recovery therefore depends on the operator's own
list: unique there, a span that bridges one spurious gap would recover it;
holding both, the decode genuinely does not determine which station sent.
Bridging a single gap when forming the callsign span is worth trying for that
reason, and must stay behind the existing correction setting.

Last reviewed: 2026-09-07 (sixth entry) — the offline callsign list's fuzzy
lookup was implemented and covered by tests but never called by the application;
its only use was an exact-membership check that labelled a suggestion's source.
It now backs an opt-in correction of near-miss callsigns, which is the directory
half of the acquisition-transient problem recorded in the fifth entry: capture
20260903-165900 decodes EM90ZMV as T90ZMV, losing only the opening characters,
and a bounded edit-distance lookup recovers exactly that. Correction stays off
by default and cannot promote a candidate where more than one entry is equally
close, because two listed stations can differ by one character. The acoustic
half is still open: boundaries are committed before any speed estimate exists,
and the same signature appears across the corpus.

Startup now reports pending application and callsign-list updates once per
launch. UI-005 covers it.

Last reviewed: 2026-09-07 (fifth entry) — CW-001. Two findings, one of which
changes how this decoder must be measured at all.

First, measurement. The character-error figure used to judge decoder changes is
averaged over five noise seeds, and its value moves by about 0.03 with the draw
alone -- larger than most differences that were being read as results. It is
deterministic per seed, so the variance is entirely across draws rather than
between runs, and a comparison of two builds on the same seeds cancels it
almost completely. Every decoder comparison must therefore be paired against
identical draws and reported as the paired difference. An absolute figure
compared against a remembered baseline from a different build means nothing at
this scale, and several conclusions reached that way were wrong.

Second, where the remaining error lives. Feeding the event lattice exact run
boundaries decodes a message perfectly at every speed, so the sequence decoder
is not a limit; segmentation is the whole of it. Degrading those boundaries the
way noise does shows the costs are strongly asymmetric: displacing every edge
by a quarter of a dot costs 0.045 character error, losing a tenth of the marks
costs 0.455, and inventing a tenth costs 0.125. Losing a mark is roughly three
and a half times worse than inventing one.

That asymmetry cannot be exploited by moving the keying thresholds, and the
attempt is recorded so it is not repeated: shifting the hysteresis band down
retains marks but lengthens every one of them, which the independent cadence
estimator and the lattice's evidence confidence both detect, and narrowing the
band restores timing while losing more copy than the original. One threshold
crossing has to serve both retention and timing and they pull opposite ways.
Making the decision adapt to signal quality instead is currently impossible:
the keying evidence is a calibrated posterior that saturates by design, and
neither the decoder's own confidence nor the detector's level separation nor
its mark level tracks the input signal-to-noise ratio -- all three were
measured and all three are flat or saturated across a 30 dB to 12 dB range. A
quality-adaptive rule needs a signal that does not exist yet.

Also rejected on measurement this session: weighting anchor selection by a
searched element length (recorded in the third entry), and an evidence-scaled
speed prior, which bought no copy on its own and broke the timing benchmark.

What ships from it is one constant. Gap classification now sits nearer the
nominal character gap, worth a paired 0.018, 0.039 and 0.013 across three
independent seed sets. The staging fixture that had to move for it was
asserting a two-and-a-half dot gap, neither an element gap nor a character gap
but between them, and now uses unambiguous spacing -- the same correction this
tree already applied once to the replacement fixture, and it passes under the
old threshold as well as the new one.

Still open under CW-001: copy below about 15 dB, Farnsworth spacing and 50 WPM,
and the acquisition transient -- capture 20260903-165900 decodes EM90ZMV as
T90ZMV, losing only the first two characters, because element boundaries must
be committed before any speed estimate exists. DSP-002 and UI-005 unchanged.

Last reviewed: 2026-09-07 (fourth entry) — UI-005: fixed the decoded transcript
shuddering as text arrived. The cause was that every update reassigned the whole
string, rebuilding the text document and resetting the viewport, so one frame in
every decoded character was drawn at a stale offset. Appending only the suffix
and pinning the tail in the same frame the content grows removes it; the local
model transcript in the same card shared the defect and is fixed with it. The
decoder work in this session is unchanged by it.

Last reviewed: 2026-09-07 (third entry) — tested and rejected the element-length
search that CW-001 had proposed as its next step. The proposal was to replace
the nine fixed speed anchors with a searched element length. Implementing it
turned out to need little new machinery, because the event lattice already is a
duration-explicit decoder: it scores a whole segment's run durations against
the dot, dash and three gap classes with a beam search, and merely took the
element length as an input supplied by whichever anchor was leading. Searching
that one parameter, coarse then local and always including the anchor bank's
own answer as a candidate, was therefore the whole change.

It does not pay. The search alone leaves mean character error at 0.307
unchanged, because the lattice feeds the refined transcript rather than the
primary text, and it roughly doubles decode time. Feeding the searched length
back into anchor selection makes copy monotonically worse as its weight rises:
0.307 at zero weight, then 0.317, 0.332 and 0.331. The reason is that the
anchors are not choosing wrongly in the first place. Measured against known
synthetic signals, the leading anchor's speed is within 1.3 per cent of truth
at 20, 30, 40 and 50 WPM at both 20 dB and 12 dB. The premise came from
diagnosing an earlier acceptance-test failure, where a wrong anchor genuinely
did capture a whole segment, and it did not survive the element-timing
corrections that followed; it should not be re-attempted on that reasoning.

What remains under CW-001 is therefore not a speed problem. Copy at 12 dB sits
at 0.719 with the speed already correct, so the residue is noise corrupting
individual elements. The lattice decides against run durations produced by a
threshold, which discards the per-frame margin the detector now measures;
letting it place its own boundaries from that evidence is the remaining
soft-decision step, and it should be justified by measurement before being
built. Farnsworth spacing and 50 WPM stay open. DSP-002 and UI-005 are
unchanged.

Last reviewed: 2026-09-07 (second entry) — replaced the heuristic keying slicer
with a soft decision, the first stage of the weak-signal work. The detector now
emits a calibrated log-likelihood ratio from a two-level model that tracks each
level's scatter separately, so the decision slope is measured rather than set
by hand and an unkeyed channel reports no information instead of being pushed
toward key-up. Mean character error falls from 0.507 to 0.307, concentrated in
the weak columns (20 dB 0.389 to 0.136, 15 dB 0.619 to 0.338, 12 dB 0.915 to
0.719), and keying-style error falls from 0.296 to 0.108. Receiver captures and
level invariance are unchanged. Two intermediate forms were measured and
rejected on the way: a single pooled variance reached only 0.498 because it
cannot express that a mark is noisier than a space, and forcing the decision
back onto half amplitude reached 0.520, which established that the boundary
shift is the gain rather than a defect to be corrected. CW-001 therefore stays
open but narrows: what remains is the duration-explicit sequence decoder that
would search dot length directly rather than choosing among nine fixed speed
anchors, which is the piece that should carry Farnsworth spacing and 50 WPM.
The debounce, the E/T veto and the anchor bank all remain compensations for a
hard slicer that no longer exists, and should retire with it.

Last reviewed: 2026-09-07 — restored the display/detection separation that was
withdrawn on 2026-09-05. The element-timing corrections made since removed the
acquisition sensitivity that had destabilised the hosted live-audio acceptance
test, and with it the change improves every measure: mean character error falls
from 0.545 to 0.507 and receiver-capture recovery rises from six of eight to
eight of nine corroborated callsigns with no callsign asserted on any of the
four captures containing no CW. Two captures previously treated as empty were
found to carry traffic that the earlier detector lost; one is corroborated by
the application's own capture-time diagnostics. A boxcar integrator matched to
the element length was also prototyped for weak signals and rejected: it
improved mean error but smeared element edges by its own window, degrading the
timing corpus and turning consensus SOS into SYS. Weak-signal copy below about
15 dB therefore still needs soft-decision decoding rather than a better filter,
and remains CW-001 with Farnsworth spacing and 50 WPM. DSP-002 and UI-005 stay
active for the wider detection and visualization scope.

Last reviewed: 2026-09-06 — removed the keying-weight bias from the element
estimate after measuring that operator sending style, not just speed and
signal-to-noise ratio, was an untested decoder dimension. Bug-style sending was
previously undecodable. Added keying style (weighting, Farnsworth spacing,
timing jitter) and absolute-level invariance to the local quality gate, and
began scoring receiver captures directly: six of eight externally corroborated
callsigns are recovered, and none of the five captures established to contain
no CW asserts a callsign. Confirmed the keying decision is level invariant -
scaling a whole scene across 52 dB at fixed signal-to-noise ratio produces
identical output - so strength dependence is confined to the deliberately
contrast-adaptive decision band. Damping the paired estimate to 70% of the former
adaptation rate removes the light-weighting regression the first attempt
introduced, taking that case below its original value. An adaptive word-gap
classifier was prototyped and rejected: better on synthetic Farnsworth timing,
but worse on the receiver captures and it asserted a callsign on a capture
containing no CW. CW-001's weak-signal band, Farnsworth spacing and 50 WPM all
remain open.

Last reviewed: 2026-09-06 — narrowband width is now selected from required
keying bandwidth rather than comparative filter power, with the 60 Hz path
restricted to slow signals because its 15.9 ms group delay is a quarter of a
20 WPM element; the keying decision band also adapts to measured contrast.
Mean character error over the audio-driven surface falls from 0.653 to 0.621
and receiver-capture copy improves. A diagnostic run confirmed that spectral
acquisition is not the weak-signal limit — narrowband coherence stays flat at
about 0.386 at every signal-to-noise ratio — and that copy collapses between
15 and 12 dB because the per-interval level slicer fragments elements: 17% of
marks fragment at 12 dB, rising to 90% at 6 dB. Recovering that band needs
soft-decision decoding rather than threshold tuning, and remains CW-001 along
with 50 WPM. PERF-001 and DSP-002 remain active.

Last reviewed: 2026-09-05 — corrected the acoustic front end and element
timing. The keying decision was being taken on a decibel-domain span at a fixed
fraction, placing it far below half amplitude, so marks measured long and gaps
short with the error growing as signal strength grew. Keying now resolves in
linear power at half amplitude, evidence smoothing scales with element length,
impulses are rejected by duration instead of by hysteresis width, and open-gap
timing accounts for the detection delay. Mean character error over an
audio-driven speed/noise surface falls from 0.713 to 0.653 and from 0.363 to
0.053 at 30 dB; acquired-speed error falls from up to 23% to at most 4.2%. The
verification timing floor moves from 0.45 to 0.55 now that real CW and
irregularly keyed noise separate cleanly. CW-001 remains active: weak-signal
copy below about 12 dB is unchanged, 50 WPM is limited by narrowband filter
selection rather than timing, and the full bounded semi-Markov path with
held-out capture calibration is still outstanding. PERF-001 and DSP-002 remain
active.

Last reviewed: 2026-09-03 — added capability-gated RX-frequency entry and
waterfall-edge stepping for linked writable OmniRig/CAT4OM providers. Checked
actual-RF-to-dial conversion preserves transverter offsets, and the active RX
VFO is changed without touching split TX, mode, PTT, or KEY. UI-003, CAT-002,
CAT-003, and CAT-004 remain active for the wider scope recorded below.

Last reviewed: 2026-09-03 — a receiver capture exposed stale callsign
inheritance and showed that exact wildcard matching rejected acoustically
supported substitutions and boundary errors. Replacement trackers now retain
only transcript/color continuity and must establish their own callsign.
Advisory matching accepts at most two wildcard-aware edits only when two
current N-best paths agree on the acoustic winner; ambiguity abstains and an
SCP miss remains visibly acoustic-only. Captures record pre-existing decoder
state and callsign/model presentation diagnostics only while recording.
CALL-005, CALL-006, CW-001, and OBS-003 remain active for indexed candidate
lookup, acoustic accuracy, and full observation-aligned lattice scoring. The
hosted failure marker now retains compiler errors independently of interleaved
parallel-build tail output after the Windows leg hid its failing target. That
diagnostic exposed and the follow-up fixes the missing direct standard-library
include in the callsign-evidence MSVC test.

Last reviewed: 2026-09-03 — a new field capture confirmed correct carrier lock
and roughly 20 WPM cadence but exposed two downstream losses: competitive
timing suffixes were discarded at closed transmission boundaries, and repeated
moderate-confidence character-model overlaps split/duplicated a callsign. The
timing path now finalizes a bounded MAP suffix only at explicit flush, the card
prefers its spaced append-only consensus, and three aligned model windows can
confirm moderate characters. Ordinary-QSO PSE K/K/KN/AR/SK context may rank an
already complete call. Diagnostics record presented-call provenance. CW-001,
CW-002, CALL-006, and OBS-003 remain active for held-out calibration,
contextual N-best rescoring, per-sender cadence, and model qualification.

Last reviewed: 2026-09-03 — added explicit IC-7300 and IC-7610 support scope
under CAT-005. The implementation must stay behind the provider-neutral radio
boundary, retain configurable CI-V addressing, and pass mocked plus documented
hardware acceptance before either rig is advertised as supported.

Last reviewed: 2026-09-03 — added CAT-006 for a runtime, data-driven radio
catalog. Hamlib's own model/status/capability enumeration is the canonical
direct-CAT list; network control programs such as rigctld and Flrig remain
provider entries rather than duplicated radio models.

Last reviewed: 2026-09-03 — disabled the plot-level signal picker until a
receive/replay source is active, restoring the central empty-state start action
and normal cursor. UI-003 remains active for its documented wider scope.

Last reviewed: 2026-09-03 — added a bounded optional local character-refinement
path for operator-supplied ONNX models. It isolates up to four verified,
Morse-likely, or manually selected lanes at 30–50 Hz, performs asynchronous
overlapping-window inference with latest-window load shedding, and exposes
append-only consensus separately from the deterministic transcript. A
structurally valid callsign confirmed across overlapping model windows may now
complete verification only for an already Morse-likely carrier; spectral,
keying, cadence, coherence, and sustained-entry gates remain mandatory. The
model cannot create a carrier, replace raw text, or control transmission.
Character lanes use the robust presentation center so adaptive DSP-center
excursions cannot move the narrow model input off the carrier. Native runtime packaging and
strict model/metadata validation are covered on every desktop architecture.
Windows consumes a checksum-pinned official runtime distribution and disables
runtime telemetry in the application before creating a session; POSIX builds
retain the telemetry-disabled source build and package its upstream privacy
notice plus the loader-required major-version runtime alias. No character model
is bundled or downloaded. CW-002 remains active for corpus qualification,
measured error/resource gates, noise rejection, and a fully independent trained
artifact.

Last reviewed: 2026-09-03 — added the first independently generated synthetic
CW corpus/tooling slice and a dependency-free streaming probability-to-event
decoder boundary. The initial causal GRU experiment predicts only key-down and
target-channel-CW probabilities; temporal metrics exposed excessive transition
fragmentation that aggregate frame scores concealed. Fixed physical feature
scaling, a causal contrast integrator, and stable-region loss materially reduce
that failure, but no model/runtime is shipped until a locked receiver corpus,
character-level gain, runtime budget, provenance, and per-platform packaging
all pass. Established stream ridges are also reserved before global peak
ranking, with stable-center reassociation preventing a returning carrier from
being published as a duplicate beside an internally drifted track.
DATA-002, CW-002, DSP-002, CW-001, PERF-001, and UI-003 remain active.

Last reviewed: 2026-09-02 — connected the bounded timing lattice to live
envelope runs, exposing append-only acoustic consensus separately from literal
text, and added fixed-center manual probes that promote only through ordinary
verification. Right-click creates a manual probe while left-click remains
dedicated to detected streams; a stable plot-level pointer router prevents live
model refreshes from destroying a marker between press and release. Operator
labels now use the stable presentation center. CW-001,
UI-003, DSP-002, and CW-004 retain the calibrated confidence, cancellation,
weak-signal filtering, close-carrier, and true co-channel work documented below.

Last reviewed: 2026-09-02 — implemented the capture-driven CW recovery slice:
bounded recent decoder evidence, continuous processing of every fixed WPM
hypothesis with safe-boundary winner selection, saturated-bank replacement,
track identity jump rejection, adaptive key-envelope normalization, bounded
coherence, and verification enter/exit hysteresis. Added a native private-WAV
replay audit and selectable profile-persisted Audio spectrum/CW symbols views;
the latter displays verified-channel acoustic keying envelopes on a neutral
background rather than inventing decoded glyphs. CW-001 and DSP-002 remain active for the full semi-Markov path,
held-out calibration, and legally reusable real corpus.
Last reviewed: 2026-09-02 — bounded decoder input by spectral association:
after the existing 750 ms normal-gap hold, an unmatched verified or private
track receives one forced key-up/flush and its decoder remains frozen until a
real candidate matches again. A deterministic alternating-carrier fixture uses
a stronger station 85 Hz away and also preserves a 600 ms same-frequency gap.
DSP-002 remains active for calibrated multi-signal association, and CW-004
still owns true overlapping/co-channel separation.
Last reviewed: 2026-09-02 — decoder cards now follow live text, emphasize the
confirmed station call, and provide a bounded flashing visual alert for an
exact profile own-callsign match. Card close no longer competes with whole-card
interaction, explicit up/down controls provide refresh-safe ordering, and
unidentified vertical markers show only frequency. QSO-003 is
active for configurable visual behavior plus its unimplemented audio/remote
notification scope; CW-001 remains active because a new capture confirms that
compressed character/word gaps and digits require ambiguity-preserving timing,
not a global threshold reduction.
Last reviewed: 2026-09-02 — stabilized the decoder transcript viewport under
live updates: appends scroll only the viewport while it is following the
bottom, never move the text cursor, and do not override text selection or an
operator's upward scroll. Plain text plus a permanently reserved scrollbar
gutter prevents rich-text and scrollbar-driven line reflow; confirmed calls
remain prominent in the card header. Its contract test normalizes platform
line endings. UI-003 remains active for its wider interaction scope.
Last reviewed: 2026-09-02 — corrected the live-view selector auto-hide
regression and replaced indirect marker tapping with a direct pointer target.
Decoded text is now scrollable/selectable, and automatic stream naming requires
callsign structure plus exchange-role or repetition evidence. A same-frequency
field capture confirms that this role evidence cannot substitute for the
remaining CW-001 acoustic timing work or CW-004 operator separation.
Last reviewed: 2026-09-02 — corrected the macOS bundle's required identity
metadata and final resource-sealing order after a clean downloaded ARM64 build
was rejected as damaged. PKG-002 remains active for Developer ID signing,
notarization, and clean-machine acceptance; CI now rejects empty plist identity
fields or any invalid staged bundle resource envelope.
Last reviewed: 2026-09-02 — a new live capture confirmed stream acquisition but
showed repeated passes on one carrier receiving new IDs/colors. Silence now
keeps a verified observation for the actual configured timeout instead of
demoting it after the short failure hold; expired tracks reuse a bounded
frequency-color lease for at least five minutes. CALL-006 remains active for
callsign-level identity and stronger same-carrier session continuity.
Last reviewed: 2026-09-02 — anchored the five-minute color lease to the
frequency that established it so a stale verified tracker cannot walk the
remembered identity through nearby noise. Stabilized verified-marker geometry
at a fixed presentation width; the adaptive decoder filter remains diagnostic.
Inactive observations now reduce to an axis mark, stream labels enlarge on
hover, and the CW receive guide uses two unfilled dashed width boundaries.
Display values now use labeled sliders in a responsive multi-row layout rather
than an overflowing row of number boxes.
Last reviewed: 2026-09-02 — made trace activation open a larger decoded-text
window reliably and reconcile an operator-opened session across same-frequency
tracker reacquisition. Retention now preserves identity without allowing
unmatched residual noise to present the carrier as active or draw CW symbols.
The bottom live controls auto-collapse to their header and can be pinned open.
Gap prediction is capped, stale drift decays, verified-exit hysteresis is six
seconds, and a fixed presentation anchor plus short word-gap activity hold
reduce contest-stream churn and area flicker. Bounded session text and a
structurally plausible callsign persist across same-identity reacquisition.
UI-003, UI-005, and CALL-006 remain active for their documented larger scope.
Last reviewed: 2026-09-02 — added independent bounded 1:3 mark / 1:3:7 gap
cadence fitting and guarded decoder reacquisition for a cadence-confirmed
carrier stuck in implausible unverified text. Replaced global waterfall gating
with per-bin/local-side conditioning so narrow CW marks remain visible without
broad passband texture, and made continuous manifest publication last with
bounded client retries for transient release-asset errors. CW-001, DSP-002,
UI-005, OBS-003, and PKG-004 remain active for their documented larger scope.
Last reviewed: 2026-09-02 — the live integration fixture now exercises the
sustained-verification interval with five keyed repetitions, validates both
averaged and instantaneous spectrum output, and emits bounded failure
diagnostics. The independent character-confidence floor is 0.40 while cadence,
pure timing, and the complete hard-negative corpus remain separate gates.
Last reviewed: 2026-09-01 — added AUDIO-002 (selected-track audio monitor
output: play back only the selected decoded CW track's isolated,
700 Hz-repitched narrowband audio to a PC output, like an operator-enabled
bandpass filter tied to the identified trace); no implementation yet.
Last reviewed: 2026-09-01 — fixed `timing_quality` and
`mean_character_confidence` being mathematically forced identical (traced
directly to real contest debug-capture data: a track with a legible `TEST`
in its text never verified because the combined metric never crossed
threshold); see the `CW-001` note for the fix and its remaining known gap
(lifetime-cumulative rather than windowed averaging). Also added CW-006
(recognize well-known CW/contest patterns like CQ, TEST, 599, 5NN, TU, UP
as independent verification evidence, motivated by the same finding) as a
not-yet-implemented follow-up.
Last reviewed: 2026-09-01 — retuning the linked radio's VFO previously lost a
signal's tracking identity, so the receive path added
`CwChannelBank::shiftTrackedFrequencies()`: a retune while live audio is
running now re-centers every currently tracked signal by the exact
audio-domain shift implied (accounting for CW-U/CW-L sideband direction)
and resynchronizes each track's narrowband filter, without discarding
decoded text or verification state. Also gave the VFO readout rig-display
decimal precision (e.g. 7016.45 kHz), and added RX/TX frequency and split
state to every debug-capture diagnostics line so a VFO move during a
capture is visible after the fact.
Last reviewed: 2026-09-01 — implemented the check/download/verify/guided-
install slice of PKG-004: background + manual update
checks against the published manifest's version field, SHA-256-verified
download to the Downloads folder, and handoff to the OS installer/package
handler rather than a silent self-install (deferred until PKG-001/PKG-002
signing lands). Also enlarged the VFO readout (green RX / yellow TX / SPLIT
badge) to match the decoder panel's visual weight, added a styled but
intentionally unwired "ON AIR" placeholder (no backend reports real PTT/
transmit state yet — see CAT-002, CAT-004), and moved the CW guide's axis
line to sit on the spectrum/waterfall boundary rather than the bottom of
the waterfall.
Last reviewed: 2026-09-01 — added DOC-002 (render documentation diagrams,
e.g. Mermaid, instead of the ASCII art currently in docs/architecture.md,
docs/decoder-strategy.md, and docs/decisions/0001-qt-quick-spectrum-renderer.md)
with no implementation yet.
Last reviewed: 2026-09-01 — added PKG-004 (application update checking and
guided install: periodic/manual update checks against the published release
manifest, operator-confirmed download/checksum-verify/install/cleanup) per
the documented safety policy; no implementation yet.
Last reviewed: 2026-09-01 — swapped which spectrum overlay reads as an
"area" to remove visual ambiguity: the CW
pitch guide is now an unfilled pair of dashed vertical width boundaries, and
an active verified CW track is identified primarily by a
stable-width colored vertical area, with the keying-state line drawn thinner
on top. The adaptive filter width remains available as decoder diagnostics.
Last reviewed: 2026-09-01 — renamed the decoder panel from "Full-spectrum CW
decoder" to "CW Decoder" and added a VFO frequency readout showing the
connected radio's actual RX dial frequency (and TX dial frequency when split
is active), hidden entirely unless a live radio/CAT source is actually
linked and driving the current audio input in live-audio mode — never shown
for receive-only SWL setups or WAV replay, which have no radio state to
show. Reuses the existing `resolve_frequencies` core logic; `AppSettings`
gained `controlledTxRfHz()`/`controlledSplitActive()` alongside the existing
RX-only accessor.
Last reviewed: 2026-09-01 — fixed the root cause of the field-reported
"visible CW never decodes" case, isolated using a real operator debug
capture: falsely verified tracks decoded to text overwhelmingly made of the
two single-element characters E and T (the statistical signature of timing
noise, not genuine text), which the existing unknown-symbol-fraction gate did
not catch since it stayed under threshold throughout. Added a
character-distribution plausibility gate
(`CwVerificationReason::ImplausibleCharacterDistribution`) that re-checks
even an already-verified track once enough decoded text has accumulated to
judge it, calibrated directly against the real capture's numbers (0.35
threshold; the three real false positives measured 0.59/0.45/0.76, the
benchmark's legitimate text measures 0.25). Exposed as a standalone testable
function; full `ctest` suite and `cwa_verification_benchmark` hard negatives
confirmed clean, plus a real-GCC local compile (designated-initializer
strict) as an extra portability check.
Last reviewed: 2026-09-01 — implemented an initial OBS-003 slice: an
operator-started, bounded "Debug capture" that records raw live audio (WAV)
and a per-track private diagnostic log (JSON lines, once per second) to a
timestamped folder, capped at 5 minutes, never silent. Added a
dependency-free WavWriter (round-trip tested against WavReplaySource) and
CwChannelBank::allTrackDiagnostics() exposing full per-track state for every
track regardless of verification. Verified end to end with an extended
cwa_live_audio_pipeline_test driving a real decode and checking both output
files. This is the requested path to real field data now that synthetic
reproduction of the reported "visible CW not decoding" case has not
succeeded (see the entry below).
Last reviewed: 2026-09-01 — restored the pre-verification diagnostics as an
opt-in, once-per-second "Diagnostics" toggle in the decoder panel (off by
default) instead of a continuously live-updating label, after a direct
instrumented investigation into a still-unresolved field report of visible
CW streams not decoding. That investigation confirmed the core algorithm
itself reliably verifies a clean or realistically-noisy single tone within a
few seconds and creates no spurious tracks against a synthetic bumpy noise
floor, so the reported field case (many simultaneous candidate/Morse-likely
tracks saturating the 24-track cap) was not reproduced synthetically; real
diagnostic data from the operator's environment is needed to isolate it
further, which the restored toggle is intended to provide.
Last reviewed: 2026-09-01 — fixed a verification-state/reason inconsistency
where a track could stay reported as Morse-likely after later failing an
earlier gate again, confirmed against two operator screenshots showing this
exact contradiction; added `CwChannelBank::configure()` and a Settings →
Display "Decoded signal timeout" control (default 30 s, replacing a fixed
8 s) so an already-running decoder session can have its retention changed
without restarting; and removed the initial always-visible decoder-panel
diagnostics readout after confirming it was constantly flickering and not
useful in practice (`narrowband_coherence` is naturally noisy per-instant
evidence even for a clean tone), while keeping the underlying diagnostics
data on the model for a future dedicated view. Also reverted a first attempt
at bank-capacity eviction (letting a stronger candidate replace the weakest
unverified track when full) after a deterministic test showed it could evict
a genuine intermittent CW signal's own track during its normal key-up gaps;
that starvation theory (the bank saturating at its 24-track cap with
marginal candidates) remains plausible and worth a more careful design, but
is not fixed yet.
Last reviewed: 2026-09-01 — added a decoder-panel diagnostics readout exposing
pre-verification candidate/Morse-likely counts and a rejection-reason tally
without overlaying unverified candidates, and a deterministic
broad-spectral-hump hard-negative benchmark case confirming the local-
prominence guard rejects genuinely wide non-CW spectral features (adjacent
SSB audio, AGC pumping, a receiver-filter skirt) before any track is created,
in response to an operator screenshot showing two broad spectral features
that were not decoding.
Last reviewed: 2026-09-01 — reproduced and fixed hosted QML compilation,
validated the Qt desktop build/install locally, added functional WAV spectrum
replay, corrected macOS bundle deployment and Linux Qt architecture selection,
set and verify the macOS Sonoma 14+ deployment baseline, documented the
temporary Windows SDK-downloader source pin, added remotely queryable CI
outcomes/log diagnostics plus extractor retry, corrected WiX license input and
added MSI failure diagnostics, and replaced the Windows archive plan with an
upgrade-capable MSI delivery flow. The first fully green packaged matrix then
exposed a native first-launch render crash; the null-texture path is now removed
and guarded by empty-render, full-QML, and staged native-graphics startup tests
with deterministic texture creation on every platform. Setup now distinguishes
receive-only SWL operation, positively identified online radios, and explicit
manual templates; the wizard footer is also guarded against clipping. Bounded
compiler diagnostics are now available through the Git-only CI status markers,
which identified and closed the initial setup-dialog QML syntax failure and
the subsequent cross-platform Qt macro/declaration errors. Native audio-input
enumeration, per-profile selection, and bounded live RX now run for both radio
and SWL setup. Station settings include the normalized own callsign and wider
content margins. A CW operating mark is integrated across application packages,
and the Windows shortcut/program-group contract has been rechecked. The
standalone render regression remains linked against the complete receiver
source after live-audio integration and handles every current Qt sample-format
enumerator without compiler warnings. Live DSP timer affinity is now covered by
a cross-thread FFT regression that prevents blank output with queue overruns.
Audio conditioning now separates default DC rejection, optional automatic or
manual gain, visualization scaling, and automatic/manual bandwidth with
deterministic coordinate and gain tests. Decoder planning now includes an
operator-toggleable, provenance-visible callsign prediction/validation service
that never overwrites raw decoded text. Live signal/display controls now sit
below the spectrum; stable automatic levels and waterfall-only noise
suppression reduce color pumping without altering raw decoder input.
The M2 plan now defines a measured hybrid decoder, bounded multi-pass weak-signal
refinement, and same-frequency pileup separation using operator fingerprints,
joint timing inference, conservative cancellation, and optional receive
diversity.
The waterfall now has a constant profile-selected time span independent of pane
size, startup fill, and line density, preserves timestamp gaps, derives genuine
high-rate timing frames with overlapping FFT hops, and provides a configurable
CW guide plus X-axis frequency scale.
The receive-only decoder now scans the complete processed passband, maintains a
bounded independently colored state per detected frequency, publishes soft key
evidence plus provisional/stable text, and removes silent tracks consistently
from both overlay and decode-list models. The configurable 700 Hz guide is
visual-only. Confidence calibration, same-frequency separation, and
multiple-pass stages remain active backlog work.
Sub-bin interpolation and bounded drift prediction now feed automatic
60/120/240 Hz filters with asymmetric local-noise tracking. Decoder cards are
operator-opened from vertical colored markers, independently closable and
reorderable with explicit up/down controls while every track continues decoding. Conservative callsign
tokens appear vertically on their trace. Explicit profile audio/radio pairing,
CW-U/CW-L mapping, RX transverter resolution, live Windows OmniRig polling, and
CAT4OM state now produce RF labels only when the complete evidence chain is
valid; every other source remains explicitly AF.
The first hosted build correction normalized Qt's platform-sized session-list
index before clamping so GCC, Apple Clang, and MSVC share the same bound.
Versioning now derives application, About, native package/bundle metadata,
network identity, installed version record, and release manifest from one
effective CMake value; the hosted workflow revision is the patch component.
Raw spectral candidates are now private DSP state until local prominence,
repeated observation, known-symbol ratio, and timing-quality gates verify a CW
trace. Shaped broadband noise is a deterministic hard negative. Callsigns are
withheld until a stable completed word passes the same evidence gate, and their
vertical annotations now remain in the upper spectrum. The CW guide is a
translucent band rather than two signal-like lines.

## P0 — project decisions and safety

| ID | Status | Item | Acceptance |
|---|---|---|---|
| DEC-001 | done | Select an OSI-approved project license | GPL-3.0-or-later text and dependency/license policy are committed. |
| REQ-001 | blocked | Fix supported WPM, prosigns, character sets, and break-in scope | Requirements contain testable ranges. Owner input required. |
| HW-001 | active | Validate initial Yaesu radios and direct serial keying | FT-450D and FT-818 editable defaults and preliminary safety notes are recorded; physical adapter polarity and disconnected/dummy-load procedures still require validation. |
| SAFE-001 | active | Implement independent maximum-key-down watchdog | The dependency-free guard rejects out-of-state KEY, limits message elements to three continuous seconds, gives operator-only TUNE a distinct hard 15-second limit, and latches emergency release/fault until an explicit reset. Remaining: connect the physical adapter, release KEY then PTT on its own monotonic deadline, and pass device-error/process-shutdown/physical-loopback tests. |
| ARCH-001 | done | Select graphical rendering architecture | ADR 0001 records the scene-graph approach, modular boundaries, 2D scope, and fallback policy. |
| ARCH-002 | done | Define secure remote-operation boundaries | ADR 0002 records roles, transports, authentication, leases, reconnect, and station-local TX rules. |

## P1 — M1 receive and visualize

| ID | Status | Item | Acceptance |
|---|---|---|---|
| BUILD-001 | done | Establish dependency-free C++20 core build | Core builds and tests passed on Windows x64, Linux x64, macOS ARM64, and macOS x64 in the first complete hosted matrix. |
| PROC-001 | done | Keep manuals, changelog, and backlog current | Repository guidance and PR automation require user manuals plus both project records for implementation and delivery changes. |
| CI-001 | done | Add full desktop dependency/build matrix | Qt 6.11.2 desktop and core tests pass on Windows x64, Linux x64, macOS ARM64, and macOS x64; successful jobs publish artifacts, stable continuous-release assets/checksums, and a verified-commit tag. |
| CI-002 | todo | Add Windows 11 x64 runtime acceptance | Self-hosted or release-candidate testing launches the packaged app and verifies graphics/audio/serial discovery on Windows 11. |
| VER-001 | done | Keep application and package versions consistent | One effective `major.minor.revision` value drives About, Qt identity, Windows executable/MSI metadata, macOS bundle metadata and in-bundle VERSION record, Debian package, CAT4OM identity, and the continuous manifest; hosted checks compare native metadata and installed records before publication. |
| AUDIO-001 | active | Add native audio device discovery and capture | Qt Multimedia discovery, hot-plug/default/unavailable state, per-profile selection, permission-gated live capture, PCM conversion/downmix, allocation-free bounded capture queue, DSP worker, overrun count, DC rejection, bounded manual/automatic gain, selectable/automatic bandwidth, and deterministic pipeline tests are implemented; add operator channel/rate/block controls, level meter, signal-driven bandwidth recommendation, disconnect/reconnect soak tests, and clean-machine hardware validation. |
| AUDIO-002 | active | Add selected-track audio monitor output | Off/full-receiver/selected-stream modes, PC output-device selection, live level control, and bounded low-latency Qt audio output are implemented. Opening a stream now activates its monitor directly; selecting Stream with one open card adopts that card rather than publishing an empty channel ID. Stream mode follows the opened track, reuses its decoder tracking mixer/adaptive filter, rejects adjacent audio, and re-pitches it to the configured reference tone; All RX preserves the complete receiver audio. Remaining: output-device hot-unplug recovery, format conversion for devices rejecting source-rate mono float, underrun/drop diagnostics, listening tests, and a continuously adjustable monitor filter. |
| REPLAY-001 | active | Add WAV replay source and deterministic clock | Dependency-free PCM/float parsing, deterministic timestamps/restart, downmix, paced UI selection/play/pause/stop, and core tests pass; hash manifests, seek, looping, and repeat-run integration remain. |
| DSP-001 | active | Implement windowing, FFT, and spectral averaging | Hann-windowed radix-2 audio/IQ analysis, dBFS normalization, averaging, frequency mapping, and deterministic tone tests pass; golden fixtures, overlap, calibration, and performance benchmarks remain. |
| UI-001 | active | Create Qt Quick desktop shell | Modern expandable receiver workspace, Settings/About panes, author metadata, profile chooser, guided setup, maximized startup with a larger decoder pane, contextual action tooltips backed by a QML source-contract test, cross-platform offscreen QML tests, and staged native-graphics startup tests are implemented; clean-machine hardware validation remains. |
| UI-002 | active | Implement modular 2D scene-graph spectrum/waterfall | Public Qt scene-graph line/grid geometry and a backend-native, valid-texture-only waterfall image node render real replay FFT frames with bounded history; empty startup/reset regression tests pass; add palette shader/ring uploads, peak hold, overlays, metrics, and performance validation. |
| UI-003 | active | Add clickable channel/callsign overlays | A stable-center/width colored area is the primary identification cue for each verified CW track, independent of adaptive carrier/filter changes, with a thinner keying-state line on top and an 18 px label enlarged to 32 px on hover. A stable plot-level left-button router opens/reopens a larger scrollable/selectable decoded-text card and activates filtered Stream monitoring; close leaves DSP active, drag plus focused Up/Down keys reorder cards, and an open card follows same-frequency/color reacquisition under a replacement internal ID. Hover explains left-open/monitor, right-manual-probe, and the unavailable Ctrl TX-VFO action. A context-confirmed callsign becomes the prominent marker/card label; otherwise the marker shows only stabilized frequency. Right-button trace-area hit testing leaves the independent CW guide unchanged and opens a temporary neutral manual region: measured weak evidence receives priority, text/count/color remain withheld until ordinary verification, qualified carrier movement follows the normal bounded tracker, centers outside a 12 Hz click-reuse boundary stay distinct, and an unverified region expires after the configured decoded-stream timeout. Successful probes promote in place. A linked writable radio supports exact RX entry from a grouped radio-style readout with reliable Enter/Escape/focus-loss dismissal and fixed waterfall-edge stepping by a persisted 1–100 kHz setting; the provider-neutral faceplate exposes authoritative independent RX/TX frequency, mode, VFO and split state without fabricating unavailable values. Add explicit manual-probe cancellation, direct pointed-signal radio retune/confirmation, keyboard tuning, and signal/band navigation. |
| UI-004 | todo | Add render-backend diagnostics and fallback tests | Active API, frame/upload metrics, and fallback reason are visible; replay smoke tests cover shader and CPU fallback paths. |
| UI-005 | active | Model configurable visualization | FPS, waterfall line rate, constant 5–30 second history, range bounds/mode, stable automatic span, waterfall noise suppression/margin, averaging, grid, red CW center/width guide, and seven-point frequency scale are configurable and persisted. A live profile-persisted selector switches between averaged Audio spectrum and a crisp CW symbols raster without resetting decoding; its popup keeps the auto-hiding controls open while in use. Audio suppression uses a slow per-bin baseline plus local side references; CW symbols instead draws only currently matched, active verified channels' keying envelopes on a neutral background, leaving retained identity and full-passband/unverified noise out of the raster. Startup, resize, and timestamp gaps cannot collapse or stretch time, and overlapping FFT hops provide real timing samples at the selected line rate; immediate Signal/Display controls auto-collapse below the spectrum, can be pinned, and retain explicit profile saving; peak hold, time ticks, palette selection, zoom, and pan remain. |
| UI-006 | done | Distinguish the CW guide from detected traces | The CW pitch guide is an unfilled pair of dashed vertical boundaries at the configured center ± half-width, so it cannot be mistaken for an identified signal; active verified traces use a colored vertical area, while inactive retained traces reduce to a short identity-color axis mark. Stream labels are larger and magnify again on hover; every trace remains independently colored and clickable. |
| CALL-002 | todo | Add delayed callsign detail card | Hover delay and press-hold show live signal/context plus asynchronous log and prefix enrichment without initiating QSO. |
| CALL-003 | active | Persist and enforce exact callsign ignore list | Core normalization and TX denial are implemented; persistence and filtering in display/queue models remain. |
| OBS-001 | active | Add pipeline telemetry | Pre-verification candidate/Morse-likely counts and a rejection-reason tally are exposed as an opt-in, once-per-second "Diagnostics" toggle (off by default) in the decoder panel after an always-visible version proved too flickering; overruns, sequence gaps, queue depths, DSP latency, and dropped display frames remain to add. |
| OBS-003 | active | Add operator-controlled diagnostic capture bundles | A "Debug capture" control records raw live audio (WAV) and per-track private diagnostics (JSON lines, 1 Hz, every track including unverified, now also carrying RX/TX radio frequency and split state, bounded completed turns, and strong-evidence current-sender/cadence fields on every line) to a timestamped folder, capped at 5 minutes, requiring explicit start and never silent. The control now also lives in Settings → Decoder with a button that opens the capture folder in the operator's file manager, and the auto-stop duration is a persisted 30–1800 second setting rather than a fixed five minutes; the decoder-panel status line reports the configured limit instead of advertising 300 seconds. Remaining: conditioned/spectrum frames, overruns, a review step, and credential/private-identifier redaction before export. |
| OBS-002 | todo | Add operator-accessible native crash diagnostics | Windows minidumps and macOS/Linux crash-report guidance identify build/profile/backend without exposing station secrets; diagnostic export is documented and tested. |
| CFG-001 | active | Implement named station profiles and setup helper | Versioned isolated persistence, UI create/select helper, per-profile wizard, and `--profile` selection exist; audio/logger/remote pages and migrations remain. |
| CFG-002 | todo | Enforce cross-process hardware ownership | Named OS locks prevent serial/audio/SDR devices from being opened by two active profiles and report the owning profile. |

## P1 — M2 multichannel CW decode

| ID | Status | Item | Acceptance |
|---|---|---|---|
| DATA-001 | todo | Register the located CC0 pileup WAV | Manifest records source, CC0, checksum, audio format, preprocessing, and storage location. |
| DATA-002 | active | Build deterministic synthetic CW corpus | A reproducible generated-from-scratch PCM corpus now covers 8–55 WPM, manual timing variation, Farnsworth spacing, shaped edges, fading, drift/flutter, receiver gain/compression, hum, impulses, nearby CW, steady/AM carriers, exact key-run annotations, checksums, and profile-grouped leakage-safe splits. Add broader receiver/audio-path simulation, exact and near-exact co-channel pileups, legally reusable labeled recordings, a locked blind receiver pack, and versioned corpus releases. |
| DSP-002 | active | Detect and track candidate CW tones | The bounded full-passband bank provides sub-bin peaks, bounded gap/drift prediction, nearby-candidate suppression, numerical-floor and FFT-resolution-aware near/far prominence guards, and private-candidate expiry. Each track separates an immutable association origin, adaptive DSP center, and fixed-width presentation center: robust evidence corrects first-verification bias, while sustained coherent motion follows through deadband, slew, dispersion/drift, and absolute-origin guards without moving identity/color. Saturated admission replaces only weak unmatched unverified occupancy, established identities reject large or cumulatively walking innovations, decoded/Morse-likely candidates survive normal word gaps, and automatic candidates reuse one identity per configured separation cell across frames so keyed FFT sidelobes cannot clone a carrier. Verified/operator identities reserve first. Two-sided noise references feed an adaptive per-track key envelope; coherence is a bounded spectral-concentration measure. Candidate → Morse-likely → verified → lost transitions combine recent spectral, edge, cadence, known-symbol, timing, confidence, and character-distribution evidence with separate enter/six-second exit hysteresis. A cadence-confirmed track held in implausible unverified text reacquires only its timing decoder instead of poisoning a later transmission at the same carrier. Only verified tracks publish IDs/colors. Deterministic saturation/identity/recovery/presentation tests and hard-negative verification benchmarks pass; native capture replay and expanded frequency/match/activity diagnostics support field audits. Add legally reusable recordings, held-out threshold/confidence calibration, stronger quantile/envelope estimation, and measured publication/character-error targets. |
| DSP-003 | todo | Add bounded per-channel DSP worker pool | Preserves channel order, sheds lowest-priority work, and passes overload soak tests. |
| CW-001 | active | Implement explainable adaptive timing baseline | Every detected passband track converts adaptive per-track key-envelope evidence to smoothed key probability and evaluates nine bounded 8–60 WPM timing hypotheses. The leader remains provisional during acquisition; every fixed anchor continues processing afterward, and sustained-silence/end-of-input boundaries reselect the best complete path rather than making the early choice irreversible. Association loss drains acoustic state but becomes a semantic turn only when the longer silence test passes, preserving slow word gaps. A separate bounded run-length fit estimates acoustic WPM directly from 1:3 marks and 1:3:7 gaps. Up to eight explicitly identified senders retain separate cadence summaries; a supported prior may nudge only an already compatible live estimate by 30 percent. Character/timing/cadence quality and unknown fractions use bounded recent windows. The dependency-free event lattice exposes up to four observation-scoped alternatives plus a separate append-only consensus. At a completed turn, bounded context may choose only a competitive path with the exact same non-whitespace characters and repair a small set of word boundaries; callsign confirmation and raw/consensus text are unchanged. Remaining: held-out confidence calibration, capture-derived jitter/edge fixtures, safe fixed-lag correction across longer interrupted segments, and automatic close-carrier/co-channel separation. Validate every extension against `PERF-001` CPU/state budgets and native capture replay. |
| CW-002 | active | Add compact causal learned likelihood path | The independent experimental causal GRU still predicts only key-down and target-channel-CW probabilities, with explicit recurrent state, deterministic training/evaluation, temporal-fragmentation metrics, anti-aliased WAV inference, and checked ONNX export. A separate optional desktop refinement boundary now accepts an operator-supplied character model: strict metadata/tensor validation, per-architecture ONNX Runtime packaging, bounded 30–50 Hz stable-center lanes, asynchronous latest-window load shedding, timestamped CTC hypotheses, stale-generation rejection, and append-only overlap consensus are implemented. At most four verified, Morse-likely, or manually selected tracks are refined. A structurally valid callsign confirmed across overlapping windows may complete verification only after the same track independently passes carrier, keyed-edge, cadence, coherence, and sustained-entry gates. Model output remains separately labeled and cannot create a carrier, rewrite raw text, keep silence active, or control TX. No character model is bundled or downloaded. Remaining: qualify models on a locked legally reusable receiver corpus, publish CER/callsign/no-CW/resource gates, improve segment/noise abstention, train a fully independent artifact, and retain the deterministic fallback. |
| CW-003 | todo | Add bounded multiple-pass weak-signal refinement | Live, rolling 2–5 second, and completed-segment passes rescore alternative filters/tracks/timing with explicit revision rules; refinement is load-shed before capture and publishes pass provenance. |
| CW-004 | todo | Separate co-channel pileup operators and cancel interference | A bounded two-then-three-source factorial timing model fingerprints sub-bin carrier/phase drift, WPM, dit/dah and spacing cadence, keying edges, and fading; confidently reconstructed tracks may be subtracted only when residual/decode scores improve, original evidence is retained, and unidentifiable overlaps are reported as ambiguous. |
| CW-005 | todo | Add optional coherent receive diversity | Synchronized receiver/antenna inputs can contribute spatial or confidence diversity, but bad alignment or a weak source must never degrade the best single-input held-out result. |
| CW-006 | done | Recognize well-known CW patterns as verification evidence | A recognized prosign/Q-code/contest token (`CQ`, `TEST`, `599`, `5NN`, `TU`, `UP`, and similarly distinctive ones — deliberately excluding short/common ones like `K`/`DE` that noise can hit by chance) appearing in a track's accumulated text is strong independent evidence of genuine Morse, distinct from the aggregate character-confidence score. Motivated directly by real debug-capture data: a real contest track's text contained a legible `TEST` yet never verified because `timingQuality` (see `DSP-002`'s known defect below) stayed under threshold for the track's entire lifetime. Add as an additional verification path (pattern found + minimal supporting evidence → verify) rather than replacing the existing gates, and calibrate/test the token list against real noise captures so it cannot reopen the noise-verification problem `DSP-002`'s plausibility gate closed. Implemented as an additional path: a recognized whole token (`CQ`, `TEST`, `599`, `5NN`, `QRZ`, `TU`, `UP`; short common ones excluded) may satisfy the three character-quality gates, but never the requirement to have decoded enough symbols, and never before the carrier, keyed-edge, cadence and coherence gates have passed, so it cannot verify a silent channel. Measured across all 22 captures: callsign recovery unchanged at 8/9, no false callsign on the four recordings containing none, published tracks identical on every capture but one, where a garbage fragment merged into the OK5OO identity instead of standing as a separate track. The motivating failure no longer reproduces on this corpus -- the tracks whose text reads `TEST` already verify -- so the path currently changes nothing and stands as a safety net; `pattern_verified_tracks` in the verification diagnostics counts how often it is actually needed, and being non-zero on air is the evidence for keeping it. |
| CW-007 | active | Operator role modes: runner and search-and-pounce | The application has no notion of the operator's own role, which weakens stream callsign attribution. Runner: the operator calls and expects answers, needing split working (listening away from the transmit frequency) and pileup reading where many stations answer at once and each repeats only its own call. Search and pounce: the operator hunts stations that are calling, the common case, where the monitored stream is a runner whose own call is the one to label. Motivating measurement: role scoring already picks the transmitting station in nine of eleven realistic exchanges (both reply directions, contest CQ, split runner, pileup caller repeating), but fails where `TU` is ambiguous — it precedes the runner identifying itself (`TU IU0LFQ`) and equally the station just worked (`TU DL1NKB`), both scoring 6, so a run can label the worked station instead of the runner. Knowing the operator's role and own callsign resolves that directly: in search and pounce the monitored stream is the runner, and a call the operator's own station sends is never a stream label. Extends `CALL-001`'s segment-roles item; the own callsign is already configured under Settings → Station and stable text matching it is already detected. Callsign attribution is implemented: Settings → Station selects Monitoring (default, no assumption), Search and pounce, or Running, and the role reaches both decode paths. Hunting, an unambiguous runner context outranks the ambiguous `TU`; running, a repeated bare call outranks one introduced by a `CQ` that belongs to another transmission. The operator's own callsign is removed from candidate scoring rather than only blanked afterwards, so the station actually being heard still gets labelled. Measured on the ambiguous case the item describes: `5NN TU DL1NKB OK5OO UP K` labels DL1NKB -- the station just worked -- with no role, and OK5OO, the split runner, when hunting. Remaining: split working, pileup reading where many stations answer at once, and role-aware behaviour beyond callsign attribution. |
| PERF-001 | active | Build decoder accuracy/resource benchmark gate | Deterministic gates cover zero primary and consensus edits across 8–55 WPM, a compressed-gap callsign repaired by consensus, append-only long-stream truncation, speed acquisition/change, no-CW false characters, clean/30 WPM/weak verified-track acquisition, five interference hard negatives, a maximum 0.20 real-time resource factor, and a conservative decoder-state estimate capped at 256 KiB including bounded turn/cadence state. An alternating-simplex fixture requires two explicit senders with separate 18/30 WPM cadence ranges, and association suspension must preserve a slow word gap while completing a later sustained absence. Character-model scheduling consumes a trivially-copyable bounded lane snapshot instead of copying every full decoder hypothesis per update. Raw-bank tests cover simultaneous tones, adjacent rejection, and selected-track monitor isolation; the threaded live fixture requires keyed Morse. Extend with WER, call precision/recall, calibrated SNR curves, co-channel separation, latency/revisions, true platform peak memory, and overload behavior. |
| CALL-001 | active | Extract and rank callsign candidates | A conservative normalized letter+digit token is exposed only after its track is verified, a stable word gap confirms the complete token, and exchange context (`DE`, `CQ`, `TU`, callsign-before-`UP`) or exact repetition supports it. Runner-identifying context outranks a repeated standalone caller; lone call-shaped noise/report fragments remain hidden. The timing layer preserves bounded `?`/gap alternatives and append-only acoustic consensus separately. Completed-turn context can repair only missing boundaries among competitive paths with identical decoded characters. Current-sender attribution additionally requires an explicit two-call handover or calling-station self-identification and abstains on conflicting final evidence. Add per-character alternative alignment, frequency-scoped repetition, richer segment roles/provenance, ranked callsign suggestions, optional external validation, and measured precision/recall targets including portable calls. Raw acoustic text must remain available and a suggestion must never silently replace it. |
| CALL-006 | active | Maintain frequency-anchored decoded observation lifecycle | Tracks retain stable IDs/colors through keyed gaps and silence, update overlays and operator-selected sessions in place, and expire after a profile-configurable hold (Settings → Display, default 30 s, maximum 300 s). Retention preserves identity/text but cannot assert active/keyed state; explicit source/prefix provenance prevents simultaneous nearby tracks from overwriting one observation and carries a bounded 2,048-character transcript exactly once across a genuine replacement. A replacement never inherits a confirmed callsign and must establish station identity from its own acoustic suffix. Concurrent published identities own distinct colors, while a later reacquisition reuses its unoccupied five-minute frequency-color lease. Composed presentation text is never rescored as raw callsign repetition. Leases follow known RX retunes. Linked live radio audio can show checked actual RF using provider state, transverter offset, CW pitch, and sideband direction; add a visible/configurable lost state, viewport-independent RF reacquisition, and callsign-level identity. |
| DSP-004 | todo | Add operational DSP conditioning | Configurable noise blanker, AGC, key-click suppression, mute, and 20–700 Hz monitor filter have replay tests and bypass paths. |
| DSP-006 | active | Replace the hard two-level keying envelope decision | The first bounded separation-prior slice is implemented. The responsive hard-assignment tracker remains for attack, fading, and manual weighting, but a 512 ms allocation-free amplitude history periodically anchors it to robust space/mark modes only when both populations have support, at least 6 dB power separation, and at least 70% explained variation. Plain responsibility-weighted soft/EM assignment remains rejected: it measured 0.3884 against 0.2946 and collapsed at 12 WPM/12 dB because ambiguous samples pulled both levels together. On a portable waveform shared by MSVC, libc++, and libstdc++, the conservative robust anchor improves paired full-surface CER from 0.3070 to 0.2579, reduces wrong surface callsign assertions from 40 to 36, and holds receiver recovery at 8/9 with 0/4 false calls on no-CW recordings. Diagnostics expose separation, explained variation, and anchor acceptance. Full-corpus replay currently costs 25.75 seconds against 20.13 without this slice; eliminate per-frame deep decoder snapshots before wider multi-track scaling. Remaining: replace the responsive hard assignment with a fully probabilistic separated-state estimator, calibrate on annotated held-out receiver audio, and preserve these safety/resource gates. |
| DSP-005 | todo | Add frequency and I/Q calibration | Manual/automatic correction, reset, diagnostics, and deterministic imbalance fixtures pass. |
| CALL-004 | todo | Add validation, watch, and band-plan policies | Configurable validation levels, allocation/pattern checks, master-call data, watch list, and CW-segment filtering are independently testable. |
| CALL-005 | active | Add optional provider-based callsign prediction and validation | The dependency-free core models immutable raw hypotheses and a bounded local `master.scp`/Call History index. Settings support operator-selected files and an optional managed Super Check Partial `MASTER.SCP` cache. The updater discovers provider metadata, identifies CW Buddy, performs conditional HTTPS checks no more than daily plus explicit on-demand checks, validates bounded data before atomic replacement, and preserves the last valid copy on every failure. SCP data is downloaded at runtime and is not bundled. A separately marked `≈` suggestion appears only when at least two current competitive acoustic paths independently select the same strongest complete callsign and it is within two wildcard-aware substitutions, insertions, or deletions of a completed uncertain span. Ambiguity abstains. An exact list hit carries a `DB` badge; an absent stronger acoustic winner remains `AUDIO` and cannot be displaced by a weaker database candidate. It never changes transcript text, verifies CW, confirms a call, alerts on the operator's call, or influences TX. Remaining: richer character-difference rationale, indexed database-generated candidates outside the refresh path, and bounded authenticated directory lookup only where provider terms permit it. Database absence never penalizes a valid acoustic candidate. |
| CALL-007 | todo | Correlate read-only DX-cluster/RBN evidence | A profile-configured receive-only provider ingests documented DXSpider-style spots or a documented HTTPS activity API, normalizes callsign/frequency/time/mode, expires stale data, and ranks a decoder candidate only when checked RF frequency, age, mode, and acoustic edit distance agree. Show source, spotter, age, frequency delta, and confidence beside—not inside—the immutable raw transcript. Cluster evidence cannot by itself verify CW, confirm a callsign, distinguish two iso-frequency senders, post a spot, or initiate TX. Use TLS where the provider supports it; legacy Telnet requires explicit opt-in, bounded reconnect/rate limits, credential-safe diagnostics, and no commands beyond login/read filtering. |

## P2 — M3 radio and guarded transmission

| ID | Status | Item | Acceptance |
|---|---|---|---|
| CAT-001 | todo | Implement Hamlib serial CAT adapter | Enumerates supported models; connects, reads, and sets frequency on both reference rigs. It must implement the same provider-neutral RX-frequency control boundary used by the operating-panel readout and waterfall-edge step controls; no provider-specific duplicate UI. |
| CAT-002 | active | Implement Windows OmniRig frequency adapter | Settings select Rig 1/2, open native configuration, and poll authoritative RX/TX frequency, active VFO, split, and mode through COM. Writes require the matching advertised writable mask; RX/TX frequency target the resolved VFO property, RX mode uses the active mode route, TX mode remains unavailable where OmniRig cannot address it independently, and split uses only explicit split capability. Richer PTT diagnostics and both-radio hardware tests remain. |
| CAT-003 | active | Implement split and transverter frequency domain | Checked integer-Hz RX/TX resolution, independent signed offsets, profile persistence, and a dependency-free provider-neutral state/command contract are implemented. The compact faceplate shows grouped actual-RF RX/TX digits plus authoritative VFO, split, and RX/TX mode, leaving unavailable values explicit rather than inferring simplex or CW. Capability-gated RX/TX entry, mode and split controls use checked inverse offsets before provider commands; live RX retunes re-center tracked signals with sideband-aware mapping. Remaining: a real Hamlib/direct-CAT adapter, logical cross-device VFO A/RX and VFO B/TX presentation for SAT-001, setup preview, Doppler tracking, and hardware tests. |
| CAT-004 | active | Implement CAT4OM network frequency provider | Native 1.x handshake, observer/control connection, password proof, pushed state, ownership, capability checks, reconnect, Settings fields, and core protocol tests exist. Operating-panel RX/TX frequency, mode, and split writes require master ownership plus the corresponding advertised command and explicitly target the opaque provider VFO while pushed state remains authoritative. Live service integration tests and a protocol extension for actual transmit/PTT state remain. |
| CAT-005 | todo | Add Icom IC-7300 and IC-7610 radio support | Add both rigs through the provider-neutral CAT boundary (Hamlib/direct CI-V and compatible external providers), with configurable CI-V address/baud, USB audio-link guidance, online/capability discovery, RX/TX frequency, mode and split readback/control, and safe inactive PTT/KEY initialization. Unit tests use protocol mocks; documented hardware acceptance verifies reconnect, VFO selection, split operation, and read/write behavior without unintended transmission before either model is listed as supported. |
| CAT-006 | todo | Populate a data-driven catalog of well-known radios | Enumerate manufacturer, model, backend version, support status, and advertised capabilities from the bundled/selected Hamlib release at runtime, with searchable selection and stable saved model identity. Treat Hamlib NET rigctl, Flrig, OmniRig, CAT4OM, and future control programs as provider backends rather than duplicating static rig lists. Never claim that catalog presence proves every command works: capability-gate frequency/mode/split/PTT/KEY, preserve safe inactive serial lines, show backend status, allow tested per-model overrides, and maintain mocked plus representative hardware acceptance results. |
| RIG-001 | active | Persist multiple named rig profiles | CAT/keying/framing/poll/display settings are isolated by station profile; full device settings and safe live switching remain. |
| KEY-001 | active | Implement cross-platform RTS/DTR adapter | A direct serial adapter owns an explicit port, rejects unsafe same-line and active-low configurations, initializes KEY then PTT inactive, releases in KEY-then-PTT order, and has deterministic fake-backend coverage. A dedicated worker-thread scheduler copies an immutable Morse plan, uses monotonic timing, aborts on error, and releases safely. Remaining: connect it to the operator guard/controller, graceful cancellation and TUNE, OS resource locking, physical line-loopback tests on Windows/macOS/Linux, and documented dummy-load acceptance; discovery never toggles unknown ports. |
| QSO-001 | active | Define declarative workflow/panel schema | A dependency-free validated profile model now separates neutral monitoring, open-ended ordinary/general CW, and individually defined rule-derived contest exchanges, with typed fields, role transitions, field-scoped aliases, and inert macro metadata rather than executable scripts. Remaining profiles include DX pileup, special events, and beacons; application/UI state and runtime trust must keep acoustic, provider-suggested, operator-accepted, and exact-TX-confirmed calls distinct. Suggested/automatic replies require explicit per-profile enablement, exact-call/context confirmation, armed TX, cancellable preview, maximum-key-down, and emergency release. |
| QSO-002 | active | Implement operator-confirmed QSO workflow | The receiver card can select only an exact decoded callsign; the operator must retype it, then prepare and retype normalized own-call, report, or free text before a standard Morse plan exists. Auto-QSO recognizes exact own-call/listening cues, while a near own-call must repeat twice, and both paths create only a visible proposal. Emergency release is permanent in the drawer. The audited scheduler/direct serial adapter exists behind deterministic fakes but is not yet connected to controller actions, so hardware remains unavailable. Add controller integration, cancellation countdown, physical state/status UI, report/exchange editor, and end-to-end dummy-load acceptance. |
| QSO-004 | active | Add split-pileup operating view and TX-slot assistance | A selected runner carrying checked absolute RF can be explicitly anchored to the configured 700 Hz reference through the provider-neutral RX-write route; TX, split and mode remain unchanged, so an ordinary UP pileup is presented to its right. Once authoritative RX/TX VFO, mode and split control plus selected-track monitoring are available, rank genuinely quiet split frequencies and show an operator-confirmed TX suggestion. A later opt-in hint may follow the frequency of the latest station whose exchange visibly completed, but decoder ambiguity, a stale report, or an occupied slot must abstain; no hint may retune TX or key automatically. |
| QSO-003 | active | Notify when the operator's own callsign is decoded | An exact normalized match in stable decoded text is highlighted, labels **YOUR CALL HEARD**, and flashes the open decoder card for five bounded pulses. Add configurable visual behavior plus opt-in audio/remote notifications and repeat/rate limiting. An optional closing macro may be queued only when the QSO context matches, auto-reply is explicitly enabled and armed, all TX guards pass, and the operator can cancel before transmission. |
| QSO-005 | active | Model alternating operators on one simplex carrier | A completed, structurally plausible `CALL1 DE CALL2` handover records both distinct participants and labels the retained decoder card as one QSO. Sustained silence or explicit end-of-input now creates bounded transmission turns, while a shorter association loss only drains acoustic state and cannot split a slow word gap. Explicit two-call/calling-station evidence may identify the current sender; ambiguous or conflicting evidence abstains. Up to eight identified senders retain separate cadence summaries, and a high-confidence prior can influence only an already acoustically compatible timing neighborhood. The contextual card separates completed turns with `|` without changing decoded characters. Remaining: operator-confirmed attribution, richer evidence provenance, truly independent per-turn timing state, wider ordinary-ragchew/contest/pileup field validation, and co-channel separation. |

## P2 — M4 logging and SDR

| ID | Status | Item | Acceptance |
|---|---|---|---|
| LOG-001 | todo | Implement durable logging outbox | Records survive restart and retry state is visible. |
| LOG-002 | todo | Implement Log4OM 2 UDP ADIF sink | A test QSO is accepted by configurable Log4OM inbound ADIF service. |
| LOG-003 | active | Maintain ADIF conformance readiness | ADIF 3.1.7 satellite/split fields, exact frequency calculation, full band mapping, and policy exist; validated ADI/ADX import/export, official pinned fixtures, independent parser, and release report remain. |
| LOG-004 | active | Resolve station equipment by actual-RF band | Ordered ADIF-band rules and `MY_RIG`/`MY_ANTENNA` cross-band serialization are tested; profile rule editor, persistence, overlap diagnostics, and logger acceptance remain. |
| SAT-001 | todo | Add complete satellite/transverter operating profiles | A named profile stores independent signed RX/downlink and TX/uplink transverter offsets, radio dial versus actual-RF presentation, radio/control backend, antenna and converter-chain descriptions, and optional satellite defaults suitable for full-duplex operation such as QO-100. RX and TX bindings are independent: a profile can receive from one radio, SDR, audio device or audio channel while transmitting through another radio/control provider and, where applicable, another audio device/channel; it must not impose a shared-device or simplex assumption. The faceplate presents these as logical `VFO A / RX` and `VFO B / TX` endpoints even when A and B belong to two different physical devices, and names each bound device rather than implying both VFOs live in one rig. QSO logging resolves the profile at contact time and emits the applicable ADIF fields: exact `FREQ`/`FREQ_RX`, `BAND`/`BAND_RX`, `PROP_MODE=SAT`, `SAT_NAME`, `SAT_MODE`, and local-station `MY_RIG`/`MY_ANTENNA`; it never puts local equipment into contacted-station `RIG`. The editor validates frequency arithmetic, ADIF dependencies/enumerations, overlapping equipment rules, missing satellite identity, device/channel ownership, and unsafe or ambiguous RX/TX mappings before CAT, audio, TX, or logging use. Cross-link implementation with CAT-003, LOG-003, LOG-004, and the audio/SDR adapters rather than creating separate frequency or ADIF models. |
| INT-001 | todo | Add read-only DX-cluster spot service | Verified calls can be served with CQ-only filtering, authentication option, bounded clients, and loopback-safe defaults. |
| INT-002 | todo | Add UDP spectrum export | Versioned timestamped spectrum frames interoperate with a documented logger/contest consumer fixture. |
| REC-001 | active | Add interoperable audio/IQ recorder | A dependency-free PCM16 WAV writer exists (round-trip tested against the existing WAV reader) and is used by the OBS-003 debug capture; RF64, IQ, metadata, rotation, looping, and a dedicated operator-facing recorder UI (independent of debug capture) remain. |
| SDR-001 | todo | Add SoapySDR stream adapter | Enumerates modules and produces timestamped IQ blocks with overflow telemetry. |
| SDR-002 | todo | Validate RTL-SDR | Installation diagnostics and replay/live acceptance test pass. |
| SDR-003 | todo | Validate SDRplay 3 | External vendor API is detected and live acceptance test passes. |
| SDR-004 | todo | Add Analog Devices PlutoSDR receive support | Discover local or remote ADALM-Pluto contexts through the official cross-platform libiio API, select the RX streaming channel, configure center frequency/sample rate/RF bandwidth/gain without assuming TX ownership, and feed timestamped complex-IQ blocks with overflow and reconnect diagnostics into the shared SDR source boundary. Package or locate libiio per platform with license/runtime validation; test against a mock IIO context and a documented USB/IP hardware fixture before declaring support. A future explicitly armed Pluto TX path is separate and must pass the normal transmit safety gates. |
| NET-001 | todo | Implement cached network receiver directory | Normalized entries filter by band/frequency, location, protocol, and availability; provider terms and refresh limits are documented. |
| NET-002 | todo | Implement KiwiSDR WebSocket sample source | Receives permitted audio/IQ/waterfall with identity, capacity handling, sequence telemetry, and bounded reconnect. |
| NET-003 | todo | Add browser/virtual-audio receiver handoff | Browser-only receiver entries tune via supported URL parameters and guide audio-device selection without private protocol use. |
| NET-004 | todo | Evaluate OpenWebRX adapter | Implement only against a documented stable interface with replayable protocol fixtures. |
| NET-005 | todo | Guard remote-RX/local-TX frequency linking | Local rig retune requires explicit action and confirmation; network sources can never acquire TX ownership. |

## P3 — release engineering

| ID | Status | Item | Acceptance |
|---|---|---|---|
| PKG-001 | active | Produce signed Win64 installer | Hosted WiX/MSI generation, stable major-upgrade identity, numeric build revisions, branded executable/product icon, `CW Buddy` Start-menu program group, desktop shortcut, and stable download naming are implemented. The finish page offers to launch the app, leaving the option unchecked on clean installs and selecting it for interactive upgrades. Running-process closure uses WiX's standard execute sequence with a bounded wait; CI rejects UI-sequence invocation or the wrong WiX binary and verifies the close contract, conditional default, and launch target through PowerShell 7-compatible reflected COM access. Clean Windows 11 install/upgrade/repair/uninstall runtime tests, migration from the out-of-support WiX v3 toolchain, Authenticode signing, and signed update metadata remain. |
| PKG-002 | active | Produce macOS bundle and Debian/Ubuntu package | Hosted builds deploy Qt/QML runtime files and publish portable Sonoma 14+ Apple silicon/Intel artifacts plus a CPack `.deb`; CI verifies required macOS plist identity/version fields, the complete bundle resource seal, the Mach-O 14.0 deployment target, and stable filenames; validate clean Sonoma and supported Debian/Ubuntu installs, then add Developer ID signing and notarization before release. |
| PKG-003 | todo | Publish signed Debian/Ubuntu APT repository | Signed Release/InRelease metadata, protected key rotation, version promotion, retention, and documented repository enrollment pass clean-machine tests. |
| PKG-004 | active | Add application update checking and guided install | A background check (disableable, ~4 s after startup) and Settings → About **Check for updates** compare the running version against the published manifest. **Download update** fetches this platform's artifact and verifies SHA-256 before saving. Continuous publication replaces binaries/checksums before publishing the manifest pointer, while the client retries transient 404/timeout/server failures with bounded backoff. In both the startup notice and Settings → About, verification status remains visible; once verified, **Open Installer** and the platform-specific reveal action replace the download control in place and hand the file to the OS rather than installing silently. Remaining: silent self-install-and-relaunch is intentionally deferred until Windows Authenticode and macOS notarization signing land (`PKG-001`, `PKG-002`). |
| DOC-001 | active | Maintain operator and hardware manuals | A user-manual index plus setup, settings, hosted-build, Debian/Ubuntu, and CAT4OM guides exist; every implementation change is CI-gated on manual/changelog/backlog updates; safe keying, workflows, diagnostics, and compatibility manuals remain. |
| DOC-002 | done | Render documentation diagrams instead of ASCII art | Every diagram under `docs/` is currently ASCII art in a fenced block. Replace them with rendered vector figures (source checked in and generated at build or docs time, so a diagram is never a binary blob nobody can edit), covering the signal path, the decoder stages, verification state transitions, and the UI layout maps. Keep the rendered output legible in both light and dark viewers, and keep a text alternative for accessibility and for terminal readers. Done for the five existing diagrams: the layer stack, the realtime data flow and the transmit-safety states in `docs/architecture.md`, the decoder pipeline in `docs/decoder-strategy.md`, and the renderer node tree in `docs/decisions/0001`. All are Mermaid, which keeps the source in the document, diffable and editable, renders as vectors in GitHub and the common documentation viewers, and needs no build step or checked-in binary. Each carries a prose description of the same content immediately below it, so a terminal reader or screen reader loses nothing. The operator guide also gained the UI layout map it lacked. |

## P2 — secure remote operation

| ID | Status | Item | Acceptance |
|---|---|---|---|
| REM-001 | active | Implement remote roles and per-rig lease domain | Role/message contracts and bounded exclusive lease manager pass dependency-free expiry tests; persistence remains. |
| REM-002 | todo | Define and generate versioned wire schema | Compatibility tests reject unknown major versions and preserve unknown optional fields. |
| REM-003 | todo | Implement secure WebSocket station/client adapters | Verified TLS only outside loopback tests; size/rate/connection limits and malformed-frame tests pass. |
| REM-004 | todo | Implement local pairing, roles, revocation, and key storage | Observer/operator/admin permissions and credential lifecycle pass integration tests on every OS. |
| REM-005 | todo | Add full snapshot/delta reconnect protocol | Epoch/sequence gaps trigger resnapshot; control, arming, confirmation, and queued TX never resume implicitly. |
| REM-006 | todo | Stream Opus receive audio | Jitter buffer exposes latency/loss; audio degrades independently of control and decoder events. |
| REM-007 | todo | Add remote spectrum/event/IQ subscriptions | Bandwidth profiles are enforced with bounded queues; IQ is opt-in and capacity-controlled. |
| REM-008 | todo | Implement station-local idempotent CW scheduler | Complete messages retain timing under network jitter; duplicates, disconnects, lease loss, and limits are safe. |
| REM-009 | todo | Add remote audit and fault-injection suite | Loss, delay, reorder, duplicate, reconnect, crashes, and device removal never duplicate TX or leave lines asserted. |
| REM-010 | todo | Document VPN/reverse-proxy deployments | LAN/VPN setup is supported; public raw port forwarding is explicitly rejected. |
