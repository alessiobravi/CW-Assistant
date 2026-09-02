# Causal CW key-probability tooling

This directory contains an independently implemented, reproducible experiment
for estimating two frame-level probabilities from narrowband receiver audio:

- whether the selected channel's key is down;
- whether the selected channel contains keyed CW rather than a hard negative.

The model is deliberately not a character recognizer. It produces no text and
does not use decoded characters as input. Its probabilities are intended to be
one acoustic input to the existing timing and confidence gates, never evidence
that bypasses those gates.

## Dataset generation

`generate_dataset.py` uses only the Python standard library. Given the same
Python version, arguments, and seed, it produces byte-identical PCM16 audio,
sample-exact key runs, annotations, and checksums:

```sh
python3 tools/cw_key_model/generate_dataset.py \
  --output /tmp/cwa-synthetic --examples 1000 --seed 20260903
python3 tools/cw_key_model/test_generator.py
```

The synthesis range includes 8–55 WPM, manual timing jitter, weighting,
Farnsworth spacing, shaped key edges, fading, receiver gain walk, frequency
drift/flutter, Gaussian noise, hum, impulses, compression, steady/AM carriers,
and nearby independently keyed carriers. A stable operator-profile identifier
is assigned wholly to train, validation, or test to prevent profile leakage.

Labels are created directly from the synthetic key schedule before acoustic
impairments. No received audio, third-party corpus, decoded transcript, or
pretrained model is read by the generator.

## Training, evaluation, and export

Install the exact direct dependency versions in an isolated environment, then
run the complete vertical slice:

```sh
python3 -m venv /tmp/cwa-key-model-env
/tmp/cwa-key-model-env/bin/pip install -r tools/cw_key_model/requirements-lock.txt
/tmp/cwa-key-model-env/bin/python tools/cw_key_model/train.py \
  --dataset /tmp/cwa-synthetic --output /tmp/cwa-key-model.pt
/tmp/cwa-key-model-env/bin/python tools/cw_key_model/evaluate.py \
  --dataset /tmp/cwa-synthetic --checkpoint /tmp/cwa-key-model.pt \
  --split test --output /tmp/cwa-key-model-evaluation.json
/tmp/cwa-key-model-env/bin/python tools/cw_key_model/export_onnx.py \
  --checkpoint /tmp/cwa-key-model.pt --output /tmp/cwa-key-model.onnx
/tmp/cwa-key-model-env/bin/python tools/cw_key_model/infer_wav.py \
  --checkpoint /tmp/cwa-key-model.pt --wav receiver.wav --tone-hz 700 \
  --output-jsonl /tmp/receiver-probabilities.jsonl
```

The network is a small frame-causal GRU. Its explicit recurrent state permits
streaming inference without future audio. Export compares PyTorch and ONNX
Runtime outputs and fails if the maximum absolute error exceeds `1e-5`.
Evaluation reports frame-level precision, recall, false-positive rate, Brier
score, predicted/reference key transitions, and implausibly short runs overall
and by acoustic scenario. Training penalizes probability movement away from
true schedule boundaries, so a fragmented envelope cannot be hidden by good
aggregate frame accuracy. It makes no character accuracy claim.

Feature scaling is fixed and physically defined. The model does not normalize
the heterogeneous feature channels against one another per frame, because that
would discard absolute-level evidence and exaggerate weak-frame noise. A causal
30 ms contrast integrator supplies short-term evidence without future samples.

`infer_wav.py` is read-only and does not require annotations. It accepts mono
PCM16 audio and reports causal probabilities at the selected tone. When rates
differ, it deterministically converts to the checkpoint rate. Downsampling uses
a 191-tap Hann-windowed sinc low-pass at 94% of the destination Nyquist limit
before linear clock conversion, preventing out-of-band energy from aliasing
into the model's CW band. The source and model rates and whether conversion was
applied are recorded in the inference summary.

Synthetic performance is not an acceptance result for live radio. Before a
model can ship, evaluate it on separately governed receiver captures across
rigs, operators, bands, signal-to-noise ratios, and interference types. Record
the frozen generator configuration, dataset manifest hash, checkpoint hash,
evaluation report, and exported-model hash. Existing deterministic decoding
must remain available as a fallback.

Checkpoints and exported-model metadata also carry the dataset generator,
license, provenance object, manifest hash, and training-code license. Preserve
that sidecar with every candidate artifact; a model file without matching
provenance metadata is not eligible for packaging.

## Licensing and provenance

All source in this directory is part of CW Assistant and is licensed under
GPL-3.0-or-later. Generated manifests and annotations explicitly state
`generated-from-scratch`, `externalAudioUsed: false`, and
`externalModelsUsed: false`.

Pinned optional dependencies and their upstream licenses are:

- PyTorch 2.14.0 — BSD-3-Clause;
- ONNX 1.22.0 — Apache-2.0;
- ONNX Runtime 1.29.0 — MIT;
- NumPy 2.5.2 — BSD-3-Clause.

Review the packages' bundled license files when constructing a redistributable
training environment. The exported model's redistribution decision must also
include the recorded dataset and training provenance.
