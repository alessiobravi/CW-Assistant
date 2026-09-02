#!/usr/bin/env python3
"""Evaluate a causal key/noise checkpoint without character-level claims."""

from __future__ import annotations

import argparse
import json
from collections import defaultdict
from pathlib import Path

import torch

from dataset import FEATURE_NAMES, extract, load_index, sha256
from model import CausalKeyModel


def counters() -> dict[str, float]:
    return {"frames": 0, "keyTp": 0, "keyFp": 0, "keyTn": 0, "keyFn": 0,
            "cwTp": 0, "cwFp": 0, "cwTn": 0, "cwFn": 0,
            "keyBrierSum": 0.0, "cwBrierSum": 0.0,
            "predictedKeyTransitions": 0, "referenceKeyTransitions": 0,
            "predictedShortKeyRuns": 0}


def add(result: dict[str, float], probabilities: torch.Tensor,
        labels: torch.Tensor) -> None:
    result["frames"] += labels.shape[0]
    for index, prefix in ((0, "key"), (1, "cw")):
        predicted = probabilities[:, index] >= 0.5
        actual = labels[:, index] >= 0.5
        result[prefix + "Tp"] += int(torch.sum(predicted & actual))
        result[prefix + "Fp"] += int(torch.sum(predicted & ~actual))
        result[prefix + "Tn"] += int(torch.sum(~predicted & ~actual))
        result[prefix + "Fn"] += int(torch.sum(~predicted & actual))
        result[prefix + "BrierSum"] += float(torch.sum(
            (probabilities[:, index] - labels[:, index]) ** 2))
    predicted_key = probabilities[:, 0] >= 0.5
    reference_key = labels[:, 0] >= 0.5
    result["predictedKeyTransitions"] += int(torch.sum(
        predicted_key[1:] != predicted_key[:-1]))
    result["referenceKeyTransitions"] += int(torch.sum(
        reference_key[1:] != reference_key[:-1]))
    if predicted_key.numel():
        boundaries = torch.nonzero(predicted_key[1:] != predicted_key[:-1]).flatten() + 1
        points = [0] + boundaries.tolist() + [predicted_key.numel()]
        result["predictedShortKeyRuns"] += sum(
            end - start < 3 for start, end in zip(points, points[1:]))


def finalize(raw: dict[str, float]) -> dict[str, float | int]:
    result: dict[str, float | int] = {"frames": int(raw["frames"])}
    result["predictedKeyTransitions"] = int(raw["predictedKeyTransitions"])
    result["referenceKeyTransitions"] = int(raw["referenceKeyTransitions"])
    result["predictedShortKeyRuns"] = int(raw["predictedShortKeyRuns"])
    result["keyTransitionExcessRatio"] = (
        raw["predictedKeyTransitions"] /
        max(1, raw["referenceKeyTransitions"]))
    for prefix in ("key", "cw"):
        tp, fp = raw[prefix + "Tp"], raw[prefix + "Fp"]
        tn, fn = raw[prefix + "Tn"], raw[prefix + "Fn"]
        result[prefix + "Precision"] = tp / max(1, tp + fp)
        result[prefix + "Recall"] = tp / max(1, tp + fn)
        result[prefix + "FalsePositiveRate"] = fp / max(1, fp + tn)
        result[prefix + "Brier"] = raw[prefix + "BrierSum"] / max(1, raw["frames"])
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--split", choices=("train", "validation", "test"),
                        default="test")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    checkpoint = torch.load(args.checkpoint, map_location="cpu",
                            weights_only=False)
    manifest_hash = sha256(args.dataset / "manifest.json")
    if checkpoint["datasetManifestSha256"] != manifest_hash:
        raise SystemExit("checkpoint was trained against a different dataset manifest")
    if checkpoint["featureNames"] != list(FEATURE_NAMES):
        raise SystemExit("checkpoint feature schema does not match extractor")
    architecture = checkpoint["architecture"]
    model = CausalKeyModel(architecture["featureCount"],
                           architecture["hiddenSize"],
                           architecture["layerCount"])
    model.load_state_dict(checkpoint["stateDict"])
    model.eval()
    overall = counters()
    scenarios: dict[str, dict[str, float]] = defaultdict(counters)
    records = load_index(args.dataset, args.split)
    if not records:
        raise SystemExit(f"dataset contains no {args.split} split records")
    with torch.no_grad():
        for record in records:
            features, labels = extract(args.dataset, record,
                                       checkpoint["frameSamples"])
            logits, _ = model(features.unsqueeze(0), model.initial_state(1))
            probabilities = torch.sigmoid(logits.squeeze(0))
            add(overall, probabilities, labels)
            add(scenarios[record["scenario"]], probabilities, labels)
    report = {
        "schema": 1,
        "scope": "frame-level key-down and target-channel CW probabilities",
        "split": args.split,
        "checkpointSha256": sha256(args.checkpoint),
        "datasetManifestSha256": manifest_hash,
        "overall": finalize(overall),
        "scenarios": {name: finalize(value)
                      for name, value in sorted(scenarios.items())},
    }
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
