# High-accuracy CW decoder strategy

## Status and objective

This document is the implementation proposal for the complete M2 decoder. A
receive-only full-processed-passband peak tracker and bounded channel bank are
now implemented. Every frequency track owns a phase-continuous complex mixer,
three-stage 120 Hz raw-sample filter, adjacent-band noise references, independent
soft SNR likelihood, adaptive timing decoder, provisional/stable text contract,
stable color, and expiry lifecycle. Sub-bin drift tracking, alternative-width
selection, multi-speed timing, confidence calibration, and multiple-pass
weak-signal recovery remain planned. The complete decoder's goal
is high weak-signal accuracy with bounded CPU, memory, and latency on ordinary
desktop hardware.
Every claimed improvement must survive the same held-out replay corpus and must
publish its accuracy, latency, false-output, CPU, and memory results.

The recommended design is a confidence-fused hybrid, not a single opaque model.
It retains the observed tone/envelope evidence, exposes uncertainty, and can
fall back to a deterministic decoder when the optional learned model is absent
or unsupported.

## Processing graph

```text
shared wide FFT
      |
candidate tone tracker
      |
complex mixer + narrow multirate filter bank + adaptive noise estimate
      |
soft key-down likelihoods and frequency/timing observations
      |                         |
semi-Markov timing path         tiny causal learned path (optional)
      |                         |
      +--------- confidence fusion ---------+
                                               |
                                 Morse-constrained n-best search
                                               |
                       provisional text, stable text, confidence, evidence
                                               |
                optional context/callsign re-ranking (always labeled separately)
```

A tracked signal is state, not a dedicated operating-system thread. One FFT is
shared by all candidates and bounded worker-pool jobs process active tracks.
Inactive or low-value tracks do not invoke the optional learned path.

## First pass: fast causal decode

The always-on path mixes each tracked tone to baseband and decimates it to a
small working rate. A small bank of nearby filter widths provides evidence for
weak, drifting, and slightly mistuned signals without repeatedly computing a
wide FFT. Phase/frequency continuity, robust noise quantiles, and a CFAR-like
threshold produce a probability of key-down rather than a hard on/off decision.

An explicit hidden semi-Markov timing model tracks dit length, key-down and
key-up duration distributions, character spacing, word spacing, drift, and
keying style. Viterbi/beam search produces several Morse-valid hypotheses with
timestamps. This gives a low-resource baseline that can explain why a character
was selected and can abstain instead of inventing text.

The optional learned path should be deliberately small: compare a causal
depthwise temporal convolution network, a compact CNN-GRU, and a compact causal
Conformer. Its inputs are narrowband log energy, phase/frequency error, noise
estimate, and envelope features; it emits element/spacing or CTC likelihoods.
The first target is at most two million INT8 parameters, a bounded state cache,
and CPU-only operation. This is a design budget to benchmark, not a performance
claim. Model choice will be made from the character-error-rate/resource Pareto
frontier rather than architecture popularity.

## Multiple-pass weak-signal decode

Ordinary CW does not contain the fixed framing or forward-error-correction bits
of a structured weak-signal digital mode. Multiple passes therefore cannot
create missing information, but they can make better use of observations that a
low-latency causal pass could not yet interpret.

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

The rolling store should hold decimated narrowband samples or features, not a
duplicate full-rate stream for every channel. At 3.2 kHz mono int16, 30 seconds
is about 192 kB per track before metadata; its total size and track count remain
hard-limited. Refinement work runs only inside a configured CPU budget and is
discardable before live capture is allowed to overrun.

## Same-frequency pileup separation

Several callers may occupy the same displayed frequency at a runner station.
They must be modeled as a mixture of operators inside one channel, not as one
malformed keyer. The separator first maintains soft identity fingerprints from:

- sub-bin carrier offset, phase evolution, chirp, and short-term drift;
- WPM plus separate dit, dah, intra-character, character, and word-spacing
  distributions;
- rise/fall shape, key-click signature, element weighting, and timing jitter;
- slowly varying amplitude, QSB trajectory, and receiver-path observations;
- decoded-prefix compatibility only as a separately weighted contextual clue.

A bounded factorial semi-Markov model jointly estimates the key-up/key-down and
timing state of two callers first, expanding to three only when evidence and CPU
budget justify it. The strongest high-confidence hypothesis is reconstructed as
a complex keyed carrier, including its measured envelope and phase trajectory.
Successive interference cancellation then retries the residual, while a joint
beam keeps alternative assignments when operator identities could swap.

The separator is evaluated across relative power, sub-bin frequency difference,
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

## Stable text and uncertainty

Decoder output has three distinct forms:

- **raw evidence:** tone, key-down probability, timing, frequency, and SNR;
- **provisional text:** may be revised by a bounded later pass and is styled as
  uncertain in the UI;
- **stable text:** is append-only once its confirmation delay and confidence
  criteria pass, except for an explicit operator correction.

Every character carries confidence, pass number, time interval, selected track,
and acoustic-versus-context contribution. Low-confidence intervals produce a
visible unknown/alternative rather than a plausible-looking fabrication.
Confidence must be calibrated on held-out data, not treated as trustworthy just
because a model emits a large probability.

Callsign lists and QSO grammar are optional search priors. They never replace
raw text, never turn absence into “invalid,” and never independently authorize
transmission. The operator can disable them immediately from the decoding view.

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

The first deterministic executable gate (`cwa_decoder_benchmark`) reports
character edits/CER, false characters during a fixed no-CW minute, processed
updates, simulated duration, wall time, real-time factor, and decoder object
size. Its initial cases cover 12, 20, and 25 WPM with fixed weak-SNR and timing-
jitter sequences. This is a regression floor, not the final corpus or a claim
of calibrated over-the-air performance. Separate core regressions drive two
simultaneous original-sample tones through the channel bank, verify independent
decodes and stable identity, and reject an adjacent non-tracked tone. The Qt
pipeline regression verifies that the live DSP worker publishes both a spectrum
frame and a raw-narrowband channel result.

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
2. Implement tone tracking, initial raw-sample narrowband evidence, and the
   explainable timing baseline. (Initial path delivered; multi-speed next.)
3. Add provisional/stable text and calibrated confidence contracts.
4. Add bounded rolling refinement and multi-hypothesis passes.
5. Train and evaluate compact learned likelihood models; ship one only after
   license, provenance, checksum, fallback, and resource checks pass.
6. Add conservative strongest-track cancellation and optional diversity input.
7. Add two-source then bounded three-source joint co-channel separation.
8. Add separately labeled QSO/callsign re-ranking and operator controls.

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
their self-reported results are not acceptance evidence. CW Assistant will not
import a model, training data, or code until its license and data provenance are
compatible with GPL-3.0-or-later and independently reproduced benchmarks.
