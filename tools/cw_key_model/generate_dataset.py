#!/usr/bin/env python3
"""Generate deterministic, independently synthesized CW acoustic fixtures.

This module uses only the Python standard library. It emits mono PCM WAV files,
exact sample-run labels, and a checksummed manifest. It does not ingest or
derive from receiver recordings, third-party datasets, or trained models.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import random
import struct
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

GENERATOR_ID = "cw-assistant-synthetic-key-v1"
LICENSE = "GPL-3.0-or-later"

MORSE = {
    "A": ".-", "B": "-...", "C": "-.-.", "D": "-..", "E": ".",
    "F": "..-.", "G": "--.", "H": "....", "I": "..", "J": ".---",
    "K": "-.-", "L": ".-..", "M": "--", "N": "-.", "O": "---",
    "P": ".--.", "Q": "--.-", "R": ".-.", "S": "...", "T": "-",
    "U": "..-", "V": "...-", "W": ".--", "X": "-..-", "Y": "-.--",
    "Z": "--..", "0": "-----", "1": ".----", "2": "..---",
    "3": "...--", "4": "....-", "5": ".....", "6": "-....",
    "7": "--...", "8": "---..", "9": "----.", "/": "-..-.",
    "?": "..--..", ".": ".-.-.-", ",": "--..--", "=": "-...-",
}

WORDS = (
    "CQ", "TEST", "DE", "TU", "UP", "PSE", "K", "R", "QTH", "NAME",
    "RST", "599", "5NN", "AGN", "QRZ", "73", "DX", "ANT", "RIG",
)
PREFIXES = ("IU", "IK", "EA", "F", "DL", "G", "K", "W", "JA", "VK",
            "ZL", "PY", "OH", "SM", "SP", "HB9", "VE", "ZS")


@dataclass(frozen=True)
class GeneratorConfig:
    sample_rate: int = 8_000
    duration_seconds: float = 8.0
    examples: int = 100
    seed: int = 20_260_903
    profile_count: int = 30
    positive_fraction: float = 0.72


def _stable_split(profile_id: str) -> str:
    bucket = int(hashlib.sha256(profile_id.encode("ascii")).hexdigest()[:8], 16) % 100
    if bucket < 70:
        return "train"
    if bucket < 85:
        return "validation"
    return "test"


def _callsign(rng: random.Random) -> str:
    prefix = rng.choice(PREFIXES)
    if any(ch.isdigit() for ch in prefix):
        call = prefix + "".join(rng.choice("ABCDEFGHIJKLMNOPQRSTUVWXYZ")
                                 for _ in range(rng.randint(1, 3)))
    else:
        call = prefix + str(rng.randint(0, 9)) + "".join(
            rng.choice("ABCDEFGHIJKLMNOPQRSTUVWXYZ")
            for _ in range(rng.randint(1, 3)))
    if rng.random() < 0.12:
        call += rng.choice(("/P", "/M", "/QRP"))
    return call


def _message(rng: random.Random) -> str:
    caller = _callsign(rng)
    runner = _callsign(rng)
    templates = (
        f"CQ CQ DE {runner} {runner} K",
        f"{caller} DE {runner} 5NN TU",
        f"{caller} {caller} 599 001",
        f"QRZ DE {runner} UP",
        f"{caller} DE {runner} RST 599 QTH TEST",
    )
    if rng.random() < 0.2:
        return " ".join(rng.choice(WORDS) for _ in range(rng.randint(3, 8)))
    return rng.choice(templates)


def _clipped_gauss(rng: random.Random, sigma: float, limit: float) -> float:
    return max(-limit, min(limit, rng.gauss(0.0, sigma)))


def _append_run(runs: list[list[int]], start: int, end: int, value: int) -> None:
    if end <= start:
        return
    if runs and runs[-1][2] == value and runs[-1][1] == start:
        runs[-1][1] = end
    else:
        runs.append([start, end, value])


def key_runs(message: str, sample_rate: int, wpm: float,
             farnsworth: float, jitter: float, weighting: float,
             rng: random.Random, maximum_samples: int) -> list[list[int]]:
    """Return exact half-open [start, end, key] sample runs."""
    dot = sample_rate * 1.2 / wpm
    runs: list[list[int]] = []
    cursor = 0

    def emit(keyed: bool, units: float, spread: float) -> bool:
        nonlocal cursor
        varied = units * (1.0 + _clipped_gauss(rng, spread, 0.38))
        length = max(1, int(round(dot * varied)))
        end = min(maximum_samples, cursor + length)
        _append_run(runs, cursor, end, int(keyed))
        cursor = end
        return cursor < maximum_samples

    for word_index, word in enumerate(message.split()):
        for char_index, char in enumerate(word):
            pattern = MORSE.get(char)
            if pattern is None:
                continue
            for element_index, element in enumerate(pattern):
                mark_units = (1.0 if element == "." else 3.0) * weighting
                if not emit(True, mark_units, jitter):
                    return runs
                if element_index + 1 < len(pattern) and not emit(False, 1.0, jitter):
                    return runs
            if char_index + 1 < len(word) and not emit(False, 3.0 * farnsworth, jitter):
                return runs
        if word_index + 1 < len(message.split()) and not emit(
                False, 7.0 * farnsworth, jitter):
            return runs
    if cursor < maximum_samples:
        _append_run(runs, cursor, maximum_samples, 0)
    return runs


def _key_vector(runs: Sequence[Sequence[int]], length: int) -> bytearray:
    result = bytearray(length)
    for start, end, value in runs:
        if value:
            result[start:end] = b"\x01" * (end - start)
    return result


def _soft_edges(keys: Sequence[int], sample_rate: int,
                rise_ms: float, fall_ms: float) -> list[float]:
    attack = math.exp(-1.0 / max(1.0, rise_ms * sample_rate / 1_000.0))
    release = math.exp(-1.0 / max(1.0, fall_ms * sample_rate / 1_000.0))
    envelope: list[float] = []
    current = 0.0
    for key in keys:
        coefficient = attack if key else release
        current = coefficient * current + (1.0 - coefficient) * float(key)
        envelope.append(current)
    return envelope


def _noise(rng: random.Random) -> float:
    # Explicit Box-Muller sampling avoids an implementation-private Gaussian
    # cache while retaining deterministic Random seed semantics.
    first = max(rng.random(), 1.0e-15)
    second = rng.random()
    return math.sqrt(-2.0 * math.log(first)) * math.cos(2.0 * math.pi * second)


def synthesize(config: GeneratorConfig, example_index: int) -> tuple[list[float], dict]:
    example_seed = config.seed + 1_000_003 * example_index
    rng = random.Random(example_seed)
    profile_index = example_index % max(1, config.profile_count)
    profile_id = f"profile-{config.seed}-{profile_index:04d}"
    split = _stable_split(profile_id)
    sample_count = int(round(config.sample_rate * config.duration_seconds))
    positive = rng.random() < config.positive_fraction
    scenario = "target-cw" if positive else rng.choice(
        ("noise", "steady-carrier", "am-carrier", "impulses", "adjacent-cw"))
    wpm = rng.uniform(8.0, 55.0)
    farnsworth = rng.uniform(1.0, 2.3)
    jitter = rng.uniform(0.0, 0.20)
    weighting = rng.uniform(0.82, 1.18)
    tone_hz = rng.uniform(480.0, 920.0)
    drift_hz_per_second = rng.uniform(-5.0, 5.0)
    flutter_hz = rng.uniform(0.0, 1.8)
    snr_db = rng.uniform(-8.0, 18.0)
    message = _message(rng) if positive else ""
    runs = key_runs(message, config.sample_rate, wpm, farnsworth, jitter,
                    weighting, rng, sample_count) if positive else [[0, sample_count, 0]]
    keys = _key_vector(runs, sample_count)
    edge_rise_ms = rng.uniform(1.5, 9.0)
    edge_fall_ms = rng.uniform(1.5, 10.0)
    envelope = _soft_edges(keys, config.sample_rate, edge_rise_ms, edge_fall_ms)

    # Slow QSB plus a correlated random gain walk.
    qsb_depth = rng.uniform(0.0, 0.78)
    qsb_rate = rng.uniform(0.04, 1.2)
    qsb_phase = rng.uniform(0.0, 2.0 * math.pi)
    gain_walk = 1.0
    phase = rng.uniform(0.0, 2.0 * math.pi)
    adjacent_phase = rng.uniform(0.0, 2.0 * math.pi)
    adjacent_offset = rng.choice((-1.0, 1.0)) * rng.uniform(24.0, 240.0)
    adjacent_gain = rng.uniform(0.0, 0.8)
    hum_gain = rng.uniform(0.0, 0.08)
    hum_hz = rng.choice((50.0, 60.0))
    compression_drive = rng.uniform(0.8, 2.0)
    signal_gain = 10.0 ** (snr_db / 20.0) * 0.055
    samples: list[float] = []

    adjacent_keys = bytearray(sample_count)
    if scenario == "adjacent-cw" or rng.random() < 0.42:
        adjacent_message = _message(rng)
        adjacent_runs = key_runs(
            adjacent_message, config.sample_rate, rng.uniform(10.0, 48.0),
            rng.uniform(1.0, 1.8), rng.uniform(0.0, 0.18),
            rng.uniform(0.85, 1.15), rng, sample_count)
        adjacent_keys = _key_vector(adjacent_runs, sample_count)

    for index in range(sample_count):
        time_s = index / config.sample_rate
        gain_walk = max(0.18, min(1.5, gain_walk + rng.uniform(-0.0015, 0.0015)))
        fading = max(0.05, 1.0 - qsb_depth *
                     (0.5 + 0.5 * math.sin(2.0 * math.pi * qsb_rate * time_s + qsb_phase)))
        instantaneous_hz = (tone_hz + drift_hz_per_second * time_s +
                            flutter_hz * math.sin(2.0 * math.pi * 2.7 * time_s))
        phase += 2.0 * math.pi * instantaneous_hz / config.sample_rate
        target = envelope[index] * signal_gain * fading * gain_walk * math.sin(phase)
        if not positive:
            target = 0.0
            if scenario == "steady-carrier":
                target = 0.12 * math.sin(phase)
            elif scenario == "am-carrier":
                target = (0.04 + 0.06 * (1.0 + math.sin(2.0 * math.pi * 17.0 * time_s))) * math.sin(phase)
        adjacent_phase += 2.0 * math.pi * (tone_hz + adjacent_offset) / config.sample_rate
        adjacent = adjacent_gain * 0.12 * adjacent_keys[index] * math.sin(adjacent_phase)
        impulse = 0.0
        if (scenario == "impulses" or rng.random() < 0.00012) and rng.random() < 0.02:
            impulse = rng.uniform(-0.9, 0.9)
        hum = hum_gain * math.sin(2.0 * math.pi * hum_hz * time_s)
        value = target + adjacent + 0.055 * _noise(rng) + impulse + hum
        # Random receiver gain/compression keeps clipping represented without
        # applying any non-causal information to labels.
        value = math.tanh(value * compression_drive)
        samples.append(max(-1.0, min(1.0, value)))

    annotation = {
        "schema": 1,
        "generator": GENERATOR_ID,
        "license": LICENSE,
        "provenance": "generated-from-scratch",
        "exampleSeed": example_seed,
        "profileId": profile_id,
        "split": split,
        "scenario": scenario,
        "sampleRate": config.sample_rate,
        "sampleCount": sample_count,
        "targetPresent": positive,
        "targetToneHz": tone_hz,
        "driftHzPerSecond": drift_hz_per_second,
        "wpm": wpm if positive else None,
        "farnsworth": farnsworth if positive else None,
        "timingJitterFraction": jitter if positive else None,
        "weighting": weighting if positive else None,
        "snrDbNominal": snr_db if positive else None,
        "message": message,
        "keyRuns": runs,
        "impairments": {
            "qsbDepth": qsb_depth,
            "qsbRateHz": qsb_rate,
            "edgeRiseMs": edge_rise_ms,
            "edgeFallMs": edge_fall_ms,
            "adjacentOffsetHz": adjacent_offset,
            "adjacentGain": adjacent_gain,
            "humGain": hum_gain,
        },
    }
    return samples, annotation


def _write_wav(path: Path, samples: Iterable[float], sample_rate: int) -> None:
    pcm = bytearray()
    for value in samples:
        pcm.extend(struct.pack("<h", int(round(max(-1.0, min(1.0, value)) * 32767.0))))
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(sample_rate)
        output.writeframes(bytes(pcm))


def _canonical_json(value: object) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def generate(output: Path, config: GeneratorConfig) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    audio_dir = output / "audio"
    annotation_dir = output / "annotations"
    audio_dir.mkdir(exist_ok=True)
    annotation_dir.mkdir(exist_ok=True)
    records = []
    for index in range(config.examples):
        example_id = f"synthetic-{index:06d}"
        wav_path = audio_dir / f"{example_id}.wav"
        annotation_path = annotation_dir / f"{example_id}.json"
        samples, annotation = synthesize(config, index)
        _write_wav(wav_path, samples, config.sample_rate)
        annotation_path.write_text(_canonical_json(annotation) + "\n",
                                   encoding="utf-8")
        records.append({
            "id": example_id,
            "split": annotation["split"],
            "profileId": annotation["profileId"],
            "scenario": annotation["scenario"],
            "audio": wav_path.relative_to(output).as_posix(),
            "annotation": annotation_path.relative_to(output).as_posix(),
            "audioSha256": _sha256(wav_path),
            "annotationSha256": _sha256(annotation_path),
        })
    index_path = output / "index.jsonl"
    index_path.write_text("".join(_canonical_json(record) + "\n"
                                  for record in records), encoding="utf-8")
    manifest = {
        "schema": 1,
        "generator": GENERATOR_ID,
        "license": LICENSE,
        "provenance": {
            "kind": "generated-from-scratch",
            "externalAudioUsed": False,
            "externalModelsUsed": False,
            "description": "Deterministic algorithmic Morse timing and acoustic impairments.",
        },
        "configuration": config.__dict__,
        "exampleCount": len(records),
        "index": "index.jsonl",
        "indexSha256": _sha256(index_path),
        "splitCounts": {name: sum(record["split"] == name for record in records)
                        for name in ("train", "validation", "test")},
    }
    (output / "manifest.json").write_text(_canonical_json(manifest) + "\n",
                                           encoding="utf-8")
    return manifest


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--examples", type=int, default=100)
    parser.add_argument("--seed", type=int, default=20_260_903)
    parser.add_argument("--sample-rate", type=int, default=8_000)
    parser.add_argument("--duration-seconds", type=float, default=8.0)
    parser.add_argument("--profile-count", type=int, default=30)
    parser.add_argument("--positive-fraction", type=float, default=0.72)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.examples < 1 or args.sample_rate < 2_000 or args.duration_seconds <= 0:
        raise SystemExit("examples, sample rate, and duration must be positive")
    if not 0.0 <= args.positive_fraction <= 1.0:
        raise SystemExit("positive fraction must be between zero and one")
    manifest = generate(args.output, GeneratorConfig(
        sample_rate=args.sample_rate,
        duration_seconds=args.duration_seconds,
        examples=args.examples,
        seed=args.seed,
        profile_count=max(1, args.profile_count),
        positive_fraction=args.positive_fraction,
    ))
    print(_canonical_json(manifest))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
