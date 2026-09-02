#!/usr/bin/env python3
"""Regression tests for physically scaled causal acoustic features."""

from __future__ import annotations

import math
import unittest

import torch
from torch import nn

from dataset import FEATURE_NAMES, extract_features
from model import CausalKeyModel


class FeatureStabilityTest(unittest.TestCase):
    def test_contrast_integrator_is_causal_and_gradual(self) -> None:
        sample_rate = 8_000
        frame_samples = 80
        time = torch.arange(8_000, dtype=torch.float32) / sample_rate
        samples = torch.zeros_like(time)
        samples[4_000:] = torch.sin(2.0 * math.pi * 700.0 * time[4_000:])
        features = extract_features(samples, sample_rate, 700.0,
                                    frame_samples)
        instant_index = FEATURE_NAMES.index("center_minus_sides_db")
        causal_index = FEATURE_NAMES.index("causal_contrast_ema")
        # No future energy leaks into the last frame before key-down.
        self.assertAlmostEqual(float(features[49, causal_index]), 0.0,
                               places=6)
        # The causal estimate moves toward the new evidence over several
        # frames instead of reproducing its frame-scale discontinuity.
        self.assertGreater(float(features[50, instant_index]),
                           float(features[50, causal_index]))
        self.assertGreater(float(features[51, causal_index]),
                           float(features[50, causal_index]))

    def test_model_does_not_apply_per_frame_layer_normalization(self) -> None:
        model = CausalKeyModel()
        self.assertFalse(any(isinstance(module, nn.LayerNorm)
                             for module in model.modules()))


if __name__ == "__main__":
    unittest.main()
