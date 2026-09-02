#!/usr/bin/env python3
"""Run a causal key/CW probability checkpoint on an unannotated mono WAV."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

import torch
from torch.nn import functional as F

from dataset import FEATURE_NAMES, _read_pcm16, extract_features
from model import CausalKeyModel


def resample(samples: torch.Tensor, source_rate: int,
             destination_rate: int) -> torch.Tensor:
    """Deterministically convert sample rate with an anti-alias FIR.

    Downsampling first applies a 191-tap, Hann-windowed sinc low-pass at 94%
    of the destination Nyquist frequency. Linear interpolation then samples
    the filtered waveform on the destination clock. Calculations use float64
    and the result is returned as float32.
    """
    if source_rate <= 0 or destination_rate <= 0:
        raise ValueError("sample rates must be positive")
    if samples.ndim != 1:
        raise ValueError("resampler expects a mono sample vector")
    if source_rate == destination_rate:
        return samples.clone()
    if samples.numel() < 2:
        raise ValueError("audio is too short to resample")

    working = samples.to(dtype=torch.float64)
    if destination_rate < source_rate:
        half_width = 95
        offsets = torch.arange(-half_width, half_width + 1,
                               dtype=torch.float64)
        cutoff = 0.47 * destination_rate / source_rate
        kernel = (2.0 * cutoff * torch.sinc(2.0 * cutoff * offsets) *
                  (0.5 + 0.5 * torch.cos(math.pi * offsets / half_width)))
        kernel /= torch.sum(kernel)
        padded = F.pad(working.reshape(1, 1, -1),
                       (half_width, half_width))
        working = F.conv1d(padded, kernel.reshape(1, 1, -1)).reshape(-1)

    output_count = max(1, int(round(working.numel() *
                                    destination_rate / source_rate)))
    positions = (torch.arange(output_count, dtype=torch.float64) *
                 source_rate / destination_rate)
    lower = torch.floor(positions).to(dtype=torch.int64)
    lower = torch.clamp(lower, max=working.numel() - 1)
    upper = torch.clamp(lower + 1, max=working.numel() - 1)
    fraction = positions - lower.to(dtype=torch.float64)
    return (working[lower] + fraction * (working[upper] - working[lower])).to(
        dtype=torch.float32)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--wav", type=Path, required=True)
    parser.add_argument("--tone-hz", type=float, required=True)
    parser.add_argument("--output-jsonl", type=Path)
    parser.add_argument("--chunk-frames", type=int, default=400)
    args = parser.parse_args()
    if args.tone_hz <= 0.0 or args.chunk_frames < 1:
        parser.error("tone frequency and chunk frames must be positive")

    checkpoint = torch.load(args.checkpoint, map_location="cpu",
                            weights_only=False)
    if checkpoint["featureNames"] != list(FEATURE_NAMES):
        raise SystemExit("checkpoint feature schema does not match extractor")
    sample_rate, samples = _read_pcm16(args.wav)
    source_sample_rate = sample_rate
    sample_rate = checkpoint["sampleRate"]
    samples = resample(samples, source_sample_rate, sample_rate)
    architecture = checkpoint["architecture"]
    model = CausalKeyModel(architecture["featureCount"],
                           architecture["hiddenSize"],
                           architecture["layerCount"])
    model.load_state_dict(checkpoint["stateDict"])
    model.eval()
    features = extract_features(samples, sample_rate, args.tone_hz,
                                checkpoint["frameSamples"])
    state = model.initial_state(1)
    probabilities = []
    with torch.no_grad():
        for start in range(0, features.shape[0], args.chunk_frames):
            logits, state = model(
                features[start:start + args.chunk_frames].unsqueeze(0), state)
            probabilities.append(torch.sigmoid(logits.squeeze(0)))
    probability = torch.cat(probabilities, dim=0)
    frame_seconds = checkpoint["frameSamples"] / sample_rate
    if args.output_jsonl:
        args.output_jsonl.parent.mkdir(parents=True, exist_ok=True)
        with args.output_jsonl.open("w", encoding="utf-8") as output:
            for index, values in enumerate(probability.tolist()):
                output.write(json.dumps({
                    "frame": index,
                    "startSeconds": index * frame_seconds,
                    "endSeconds": (index + 1) * frame_seconds,
                    "targetToneHz": args.tone_hz,
                    "keyDownProbability": values[0],
                    "targetChannelCwProbability": values[1],
                }, sort_keys=True) + "\n")
    summary = {
        "frames": probability.shape[0],
        "sourceSampleRate": source_sample_rate,
        "modelSampleRate": sample_rate,
        "resampled": source_sample_rate != sample_rate,
        "frameSeconds": frame_seconds,
        "durationSeconds": probability.shape[0] * frame_seconds,
        "targetToneHz": args.tone_hz,
        "meanKeyDownProbability": float(probability[:, 0].mean()),
        "meanTargetChannelCwProbability": float(probability[:, 1].mean()),
        "outputJsonl": str(args.output_jsonl) if args.output_jsonl else None,
    }
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
