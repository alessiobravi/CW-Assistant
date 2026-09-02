#!/usr/bin/env python3
"""Regression tests for fragmentation-sensitive evaluation metrics."""

from __future__ import annotations

import unittest

import torch

from evaluate import add, counters, finalize


class TemporalMetricsTest(unittest.TestCase):
    def test_islands_are_counted_despite_equal_frame_error_count(self) -> None:
        labels = torch.zeros((10, 2))
        fragmented = torch.tensor(
            [[0.1, 0.1], [0.9, 0.1], [0.1, 0.1], [0.9, 0.1],
             [0.1, 0.1], [0.1, 0.1], [0.1, 0.1], [0.1, 0.1],
             [0.1, 0.1], [0.1, 0.1]])
        result = counters()
        add(result, fragmented, labels)
        report = finalize(result)
        self.assertEqual(report["predictedKeyTransitions"], 4)
        self.assertEqual(report["referenceKeyTransitions"], 0)
        self.assertGreaterEqual(report["predictedShortKeyRuns"], 4)


if __name__ == "__main__":
    unittest.main()
