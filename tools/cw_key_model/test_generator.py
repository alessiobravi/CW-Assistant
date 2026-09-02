#!/usr/bin/env python3
"""Dependency-free smoke tests for the synthetic CW dataset generator."""

from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
import wave
from pathlib import Path

from generate_dataset import GeneratorConfig, generate, synthesize


def tree_hash(root: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        digest.update(path.relative_to(root).as_posix().encode("utf-8"))
        digest.update(path.read_bytes())
    return digest.hexdigest()


class GeneratorTest(unittest.TestCase):
    def test_same_seed_is_byte_reproducible(self) -> None:
        config = GeneratorConfig(sample_rate=4000, duration_seconds=0.12,
                                 examples=8, seed=41, profile_count=8)
        with tempfile.TemporaryDirectory() as first_name, \
                tempfile.TemporaryDirectory() as second_name:
            first, second = Path(first_name), Path(second_name)
            self.assertEqual(generate(first, config), generate(second, config))
            self.assertEqual(tree_hash(first), tree_hash(second))

    def test_manifest_checksums_shapes_and_profile_splits(self) -> None:
        config = GeneratorConfig(sample_rate=4000, duration_seconds=0.08,
                                 examples=24, seed=19, profile_count=7)
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            manifest = generate(root, config)
            index_path = root / manifest["index"]
            self.assertEqual(hashlib.sha256(index_path.read_bytes()).hexdigest(),
                             manifest["indexSha256"])
            profile_splits: dict[str, set[str]] = {}
            for line in index_path.read_text(encoding="utf-8").splitlines():
                record = json.loads(line)
                profile_splits.setdefault(record["profileId"], set()).add(
                    record["split"])
                annotation_path = root / record["annotation"]
                annotation = json.loads(annotation_path.read_text(
                    encoding="utf-8"))
                self.assertEqual(annotation["provenance"],
                                 "generated-from-scratch")
                cursor = 0
                for start, end, value in annotation["keyRuns"]:
                    self.assertEqual(start, cursor)
                    self.assertIn(value, (0, 1))
                    self.assertGreater(end, start)
                    cursor = end
                self.assertEqual(cursor, annotation["sampleCount"])
                with wave.open(str(root / record["audio"]), "rb") as audio:
                    self.assertEqual(audio.getnchannels(), 1)
                    self.assertEqual(audio.getsampwidth(), 2)
                    self.assertEqual(audio.getnframes(), annotation["sampleCount"])
            self.assertTrue(all(len(splits) == 1
                                for splits in profile_splits.values()))

    def test_positive_and_negative_labels_are_explicit(self) -> None:
        positive = GeneratorConfig(sample_rate=4000, duration_seconds=0.5,
                                   examples=1, seed=2, positive_fraction=1.0)
        negative = GeneratorConfig(sample_rate=4000, duration_seconds=0.5,
                                   examples=1, seed=2, positive_fraction=0.0)
        _, positive_annotation = synthesize(positive, 0)
        _, negative_annotation = synthesize(negative, 0)
        self.assertTrue(positive_annotation["targetPresent"])
        self.assertTrue(any(run[2] for run in positive_annotation["keyRuns"]))
        self.assertFalse(negative_annotation["targetPresent"])
        self.assertFalse(any(run[2] for run in negative_annotation["keyRuns"]))


if __name__ == "__main__":
    unittest.main()
