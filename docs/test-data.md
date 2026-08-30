# Decoder test data

Real recordings are required because synthesized Morse does not reproduce AGC
pumping, oscillator drift, multipath, clicks, adjacent signals, QRM, or operator
timing. Tests should use both generated fixtures with exact ground truth and
off-air recordings with reviewed annotations.

## Located public recording

Freesound sound 243528, “Hams on CW multiple frequencies & pile-up 7005.0kHz
LSB.wav,” is a 110-second, 7,119 Hz, 16-bit mono recording containing several CW
signals and a pileup. Its page identifies the sound as Creative Commons Zero:

https://freesound.org/people/kb7clx/sounds/243528/

Freesound currently requires an account to download it. We may redistribute a
verified download under CC0, but should store its original page URL, creator,
license, SHA-256, sample format, and any preprocessing in the fixture manifest.
No third-party recording has been committed yet.

## Fixture layout (planned)

```text
test-data/
  manifest.json
  audio/
  iq/
  annotations/
```

Large captures should use release assets or external object storage rather than
normal Git history. `manifest.json` records acquisition source, license,
checksum, sample format, center frequency when known, and annotation revision.

## Annotation format

Each expected event needs start/end sample indices, channel/tone frequency,
literal text, normalized text, callsign regions, and an uncertainty marker.
Metrics are character error rate, callsign precision/recall, channel tracking
continuity, false-channel rate, and end-of-character latency.

## Synthetic matrix

Generate deterministic cases across WPM, weighting, tone frequency, SNR,
frequency drift, fading, impulsive noise, overlapping callers, and timing
jitter. Generated callsigns must include portable and compound forms. The exact
range will be fixed after the supported WPM/prosign requirements are agreed.
