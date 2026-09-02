#!/usr/bin/env python3
"""Train the causal key/noise probability model on a generated dataset."""

from __future__ import annotations

import argparse
import hashlib
import json
import random
from pathlib import Path

import torch
from torch.nn import functional as F

from dataset import FEATURE_NAMES, extract, load_index
from model import CausalKeyModel


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sequence_loss(model: CausalKeyModel, features: torch.Tensor,
                  labels: torch.Tensor, chunk_frames: int,
                  training: bool, key_positive_weight: float = 1.5,
                  stability_weight: float = 0.35) -> torch.Tensor:
    state = model.initial_state(1, device=features.device)
    losses = []
    for start in range(0, features.shape[0], chunk_frames):
        chunk = features[start:start + chunk_frames].unsqueeze(0)
        target = labels[start:start + chunk_frames].unsqueeze(0)
        logits, state = model(chunk, state)
        element = F.binary_cross_entropy_with_logits(logits, target,
                                                       reduction="none")
        weights = torch.ones_like(element)
        weights[..., 0] += (key_positive_weight - 1.0) * target[..., 0]
        data_loss = (element * weights).mean()
        # Frame scores must not invent extra mark/space boundaries. Penalize
        # probability movement only where the exact synthetic schedule is
        # stable, leaving true transitions unconstrained and causal.
        probability = torch.sigmoid(logits)
        if probability.shape[1] > 1:
            stable = (torch.abs(target[:, 1:] - target[:, :-1]) < 0.25).float()
            movement = torch.abs(probability[:, 1:] - probability[:, :-1])
            stability_loss = torch.sum(movement * stable) / torch.clamp(
                torch.sum(stable), min=1.0)
        else:
            stability_loss = torch.zeros((), device=features.device)
        losses.append(data_loss + stability_weight * stability_loss)
        if training:
            state = state.detach()
    return torch.stack(losses).mean()


def evaluate_loss(model: CausalKeyModel, root: Path,
                  records: list[dict], frame_samples: int,
                  chunk_frames: int, device: torch.device) -> float:
    model.eval()
    total = 0.0
    with torch.no_grad():
        for record in records:
            features, labels = extract(root, record, frame_samples)
            total += float(sequence_loss(model, features.to(device),
                                         labels.to(device), chunk_frames,
                                         False))
    return total / max(1, len(records))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--epochs", type=int, default=12)
    parser.add_argument("--learning-rate", type=float, default=2.0e-3)
    parser.add_argument("--frame-samples", type=int, default=80)
    parser.add_argument("--chunk-frames", type=int, default=400)
    parser.add_argument("--seed", type=int, default=20260903)
    parser.add_argument("--key-positive-weight", type=float, default=1.5)
    parser.add_argument("--stability-weight", type=float, default=0.35)
    args = parser.parse_args()
    if args.epochs < 1 or args.frame_samples < 8 or args.chunk_frames < 1:
        parser.error("epochs, frame samples, and chunk frames must be positive")

    random.seed(args.seed)
    torch.manual_seed(args.seed)
    torch.use_deterministic_algorithms(True)
    device = torch.device("cpu")
    train_records = load_index(args.dataset, "train")
    validation_records = load_index(args.dataset, "validation")
    if not train_records:
        raise SystemExit("dataset contains no train split records")
    if not validation_records:
        raise SystemExit("dataset contains no validation split records")

    model = CausalKeyModel(feature_count=len(FEATURE_NAMES)).to(device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.learning_rate,
                                  weight_decay=1.0e-4)
    history = []
    best_validation = float("inf")
    best_state = None
    order_rng = random.Random(args.seed)
    for epoch in range(args.epochs):
        model.train()
        order = list(train_records)
        order_rng.shuffle(order)
        train_total = 0.0
        for record in order:
            features, labels = extract(args.dataset, record,
                                       args.frame_samples)
            optimizer.zero_grad(set_to_none=True)
            loss = sequence_loss(model, features.to(device), labels.to(device),
                                 args.chunk_frames, True,
                                 args.key_positive_weight,
                                 args.stability_weight)
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 5.0)
            optimizer.step()
            train_total += float(loss.detach())
        validation_loss = evaluate_loss(model, args.dataset,
                                        validation_records,
                                        args.frame_samples,
                                        args.chunk_frames, device)
        row = {"epoch": epoch + 1,
               "trainLoss": train_total / len(order),
               "validationLoss": validation_loss}
        history.append(row)
        print(json.dumps(row, sort_keys=True))
        if validation_loss < best_validation:
            best_validation = validation_loss
            best_state = {key: value.detach().cpu().clone()
                          for key, value in model.state_dict().items()}

    manifest_path = args.dataset / "manifest.json"
    dataset_manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    checkpoint = {
        "schema": 1,
        "modelKind": "causal-key-and-cw-probability",
        "trainingCodeLicense": "GPL-3.0-or-later",
        "featureNames": list(FEATURE_NAMES),
        "frameSamples": args.frame_samples,
        "sampleRate": dataset_manifest["configuration"]["sample_rate"],
        "architecture": {"featureCount": len(FEATURE_NAMES),
                         "hiddenSize": model.hidden_size,
                         "layerCount": model.layer_count,
                         "inputNormalization": "fixed-feature-scaling"},
        "stateDict": best_state,
        "seed": args.seed,
        "trainingObjective": {
            "keyPositiveWeight": args.key_positive_weight,
            "stableFrameProbabilityMovementWeight": args.stability_weight,
        },
        "datasetManifestSha256": file_sha256(manifest_path),
        "datasetGenerator": dataset_manifest.get("generator"),
        "datasetLicense": dataset_manifest.get("license"),
        "datasetProvenance": dataset_manifest.get("provenance"),
        "bestValidationLoss": best_validation,
        "history": history,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    torch.save(checkpoint, args.output)
    print(json.dumps({"checkpoint": str(args.output),
                      "sha256": file_sha256(args.output),
                      "bestValidationLoss": best_validation}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
