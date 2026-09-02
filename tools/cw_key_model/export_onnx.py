#!/usr/bin/env python3
"""Export and numerically verify the causal model's streaming ONNX graph."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy
import onnx
import onnxruntime
import torch

from dataset import FEATURE_NAMES, sha256
from model import CausalKeyModel


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    checkpoint = torch.load(args.checkpoint, map_location="cpu",
                            weights_only=False)
    architecture = checkpoint["architecture"]
    model = CausalKeyModel(architecture["featureCount"],
                           architecture["hiddenSize"],
                           architecture["layerCount"])
    model.load_state_dict(checkpoint["stateDict"])
    model.eval()
    torch.manual_seed(checkpoint["seed"])
    features = torch.randn(1, 23, architecture["featureCount"])
    state = model.initial_state(1)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    torch.onnx.export(
        model, (features, state), args.output, opset_version=17,
        dynamo=False,
        input_names=("features", "hidden_in"),
        output_names=("logits", "hidden_out"),
        # Live inference owns one recurrent state per tracked channel. Keep the
        # batch dimension fixed at one and vary only causal chunk length; this
        # avoids ambiguous recurrent batching in portable runtimes.
        dynamic_axes={"features": {1: "frames"},
                      "logits": {1: "frames"}},
    )
    onnx.checker.check_model(onnx.load(str(args.output)))
    session = onnxruntime.InferenceSession(str(args.output),
                                           providers=["CPUExecutionProvider"])
    def verification_error(test_features, test_state) -> float:
        with torch.no_grad():
            expected_logits, expected_state = model(test_features, test_state)
        actual_logits, actual_state = session.run(
            None, {"features": test_features.numpy(),
                   "hidden_in": test_state.numpy()})
        return max(float(numpy.max(numpy.abs(
                       actual_logits - expected_logits.numpy()))),
                   float(numpy.max(numpy.abs(
                       actual_state - expected_state.numpy()))))

    # Verify two different causal chunk lengths so an export that accidentally
    # freezes the sequence axis cannot pass on its tracing example alone.
    error = max(verification_error(features, state),
                verification_error(
                    torch.randn(1, 7, architecture["featureCount"]),
                    torch.randn_like(state)))
    if error > 1.0e-5:
        raise SystemExit(f"ONNX verification error {error} exceeds 1e-5")
    metadata = {
        "schema": 1,
        "modelKind": checkpoint["modelKind"],
        "modelSha256": sha256(args.output),
        "sourceCheckpointSha256": sha256(args.checkpoint),
        "trainingCodeLicense": checkpoint.get("trainingCodeLicense"),
        "datasetManifestSha256": checkpoint["datasetManifestSha256"],
        "datasetGenerator": checkpoint.get("datasetGenerator"),
        "datasetLicense": checkpoint.get("datasetLicense"),
        "datasetProvenance": checkpoint.get("datasetProvenance"),
        "sampleRate": checkpoint["sampleRate"],
        "frameSamples": checkpoint["frameSamples"],
        "featureNames": list(FEATURE_NAMES),
        "inputs": {"features": f"float32[1,frames,{architecture['featureCount']}]",
                   "hidden_in": (f"float32[{architecture['layerCount']},1,"
                                 f"{architecture['hiddenSize']}]")},
        "outputs": {"logits": ["targetKeyDown", "targetChannelCw"],
                    "hidden_out": "streaming recurrent state"},
        "causal": True,
        "characterDecoderIncluded": False,
        "maximumVerificationError": error,
    }
    metadata_path = args.output.with_suffix(args.output.suffix + ".metadata.json")
    metadata_path.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n",
                             encoding="utf-8")
    print(json.dumps({"model": str(args.output), "metadata": str(metadata_path),
                      "maximumVerificationError": error}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
