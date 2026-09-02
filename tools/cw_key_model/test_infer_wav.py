#!/usr/bin/env python3
"""Signal-level tests for deterministic WAV inference preprocessing."""

from __future__ import annotations

import math
import unittest

import torch

from infer_wav import resample


def sine(sample_rate: int, frequency: float, seconds: float) -> torch.Tensor:
    count = int(sample_rate * seconds)
    time = torch.arange(count, dtype=torch.float64) / sample_rate
    return torch.sin(2.0 * math.pi * frequency * time).to(torch.float32)


class ResampleTest(unittest.TestCase):
    def test_48k_to_8k_preserves_cw_band_and_rejects_alias(self) -> None:
        wanted = resample(sine(48_000, 1_000.0, 0.5), 48_000, 8_000)
        rejected = resample(sine(48_000, 10_000.0, 0.5), 48_000, 8_000)
        # Ignore FIR start/end transients when measuring steady-state energy.
        wanted_rms = torch.sqrt(torch.mean(wanted[100:-100] ** 2)).item()
        rejected_rms = torch.sqrt(torch.mean(rejected[100:-100] ** 2)).item()
        self.assertGreater(wanted_rms, 0.68)
        self.assertLess(rejected_rms, 0.01)

    def test_conversion_is_repeatable_and_has_expected_length(self) -> None:
        source = sine(48_000, 1_535.156, 0.125)
        first = resample(source, 48_000, 8_000)
        second = resample(source, 48_000, 8_000)
        self.assertTrue(torch.equal(first, second))
        self.assertEqual(first.numel(), 1_000)

    def test_equal_rates_preserve_samples(self) -> None:
        source = torch.tensor((-0.25, 0.0, 0.75), dtype=torch.float32)
        self.assertTrue(torch.equal(resample(source, 8_000, 8_000), source))


if __name__ == "__main__":
    unittest.main()
