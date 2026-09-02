"""Manifest verification and causal feature extraction for generated audio."""

from __future__ import annotations

import array
import hashlib
import json
import math
import sys
import wave
from pathlib import Path

import torch

FEATURE_NAMES = (
    "center_log_power", "lower_log_power", "upper_log_power",
    "center_minus_sides_db", "causal_contrast_ema", "phase_delta_sin",
    "phase_delta_cos", "absolute_level_db",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_index(root: Path, split: str | None = None) -> list[dict]:
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    index_path = root / manifest["index"]
    if sha256(index_path) != manifest["indexSha256"]:
        raise ValueError("dataset index checksum does not match manifest")
    records = [json.loads(line) for line in index_path.read_text(
        encoding="utf-8").splitlines() if line]
    selected = []
    for record in records:
        if split is not None and record["split"] != split:
            continue
        audio_path = root / record["audio"]
        annotation_path = root / record["annotation"]
        if sha256(audio_path) != record["audioSha256"]:
            raise ValueError(f"audio checksum mismatch: {record['id']}")
        if sha256(annotation_path) != record["annotationSha256"]:
            raise ValueError(f"annotation checksum mismatch: {record['id']}")
        selected.append(record)
    return selected


def _read_pcm16(path: Path) -> tuple[int, torch.Tensor]:
    with wave.open(str(path), "rb") as source:
        if source.getnchannels() != 1 or source.getsampwidth() != 2:
            raise ValueError(f"expected mono PCM16 WAV: {path}")
        rate = source.getframerate()
        values = array.array("h", source.readframes(source.getnframes()))
    if sys.byteorder != "little":
        values.byteswap()
    return rate, torch.tensor(values, dtype=torch.float32) / 32768.0


def _labels(annotation: dict, sample_count: int) -> torch.Tensor:
    labels = torch.zeros(sample_count, dtype=torch.float32)
    for start, end, value in annotation["keyRuns"]:
        if value:
            labels[start:end] = 1.0
    return labels


def extract_features(samples: torch.Tensor, sample_rate: int,
                     center_hz: float, frame_samples: int = 80,
                     side_offset_hz: float = 180.0) -> torch.Tensor:
    """Extract causal frame features from an unannotated waveform."""
    if center_hz - side_offset_hz <= 0.0 or center_hz + side_offset_hz >= sample_rate / 2:
        raise ValueError("target and side-reference tones must be below Nyquist")
    frame_count = len(samples) // frame_samples
    if frame_count < 1:
        raise ValueError("audio is shorter than one analysis frame")
    samples = samples[:frame_count * frame_samples].reshape(frame_count,
                                                             frame_samples)
    times = torch.arange(frame_samples, dtype=torch.float32) / sample_rate
    frequency_bank = (center_hz, center_hz - side_offset_hz,
                      center_hz + side_offset_hz)
    powers = []
    center_iq = None
    absolute_offset = torch.arange(frame_count, dtype=torch.float32) * frame_samples / sample_rate
    for frequency in frequency_bank:
        phase = 2.0 * math.pi * frequency * (absolute_offset[:, None] +
                                              times[None, :])
        iq = torch.sum(samples * torch.exp(-1j * phase), dim=1) / frame_samples
        powers.append(torch.clamp(torch.abs(iq) ** 2, min=1.0e-12))
        if center_iq is None:
            center_iq = iq
    log_power = torch.stack([10.0 * torch.log10(power) for power in powers], dim=1)
    side_level = 0.5 * (log_power[:, 1] + log_power[:, 2])
    contrast = (log_power[:, 0] - side_level) / 30.0
    smoothed_contrast = torch.empty_like(contrast)
    smoothed_contrast[0] = contrast[0]
    # A 30 ms one-pole integration is long enough to suppress frame-scale
    # scintillation while remaining shorter than ordinary hand-sent dits.
    contrast_alpha = 1.0 - math.exp(-frame_samples / sample_rate / 0.030)
    for index in range(1, frame_count):
        smoothed_contrast[index] = (contrast_alpha * contrast[index] +
                                    (1.0 - contrast_alpha) *
                                    smoothed_contrast[index - 1])
    phase_delta = torch.zeros(frame_count)
    if frame_count > 1:
        phase_delta[1:] = torch.angle(center_iq[1:] * torch.conj(center_iq[:-1]))
    return torch.stack((
        log_power[:, 0] / 60.0,
        log_power[:, 1] / 60.0,
        log_power[:, 2] / 60.0,
        contrast,
        smoothed_contrast,
        torch.sin(phase_delta),
        torch.cos(phase_delta),
        20.0 * torch.log10(torch.clamp(torch.sqrt(torch.mean(samples * samples,
                                                               dim=1)), min=1.0e-6)) / 60.0,
    ), dim=1)


def extract(root: Path, record: dict, frame_samples: int = 80,
            side_offset_hz: float = 180.0) -> tuple[torch.Tensor, torch.Tensor]:
    annotation = json.loads((root / record["annotation"]).read_text(
        encoding="utf-8"))
    sample_rate, samples = _read_pcm16(root / record["audio"])
    if sample_rate != annotation["sampleRate"] or len(samples) != annotation["sampleCount"]:
        raise ValueError(f"audio/annotation shape mismatch: {record['id']}")
    center_hz = float(annotation["targetToneHz"])
    features = extract_features(samples, sample_rate, center_hz, frame_samples,
                                side_offset_hz)
    frame_count = features.shape[0]
    sample_labels = _labels(annotation, annotation["sampleCount"])
    key_label = sample_labels[:frame_count * frame_samples].reshape(
        frame_count, frame_samples).mean(dim=1)
    cw_label = torch.full_like(key_label,
                               float(bool(annotation["targetPresent"])))
    return features, torch.stack((key_label, cw_label), dim=1)
