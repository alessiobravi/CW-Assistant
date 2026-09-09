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

The implemented bounded TSV v1 sidecar binds exactly one WAV by SHA-256 and
records its integer sample rate. Each `event` contains start/end sample indices,
audio-tone frequency in hertz, literal and canonical normalized text, a
comma-separated exact callsign set, and a `0`/`1` uncertainty marker. Lines are
canonical UTF-8-compatible text with LF endings so their own review history is
portable. They are limited to 4,096 bytes and manifests to 256 events;
malformed, duplicate,
out-of-order, noncanonical, checksum-mismatched, or out-of-range data fails
closed. Uncertain events remain reviewable but are excluded from scores.

Run an annotated receiver report with:

```sh
cwa_capture_replay --annotations reviewed.tsv audio.wav
```

The report matches a track using its frequency during the annotated interval,
then prints character/word error, exact callsign precision/recall, first
provisional/stable latency, non-append provisional revisions, and unmatched
published stream/callsign rates. Co-channel operators at the same frequency are
not yet truthfully attributable, and no reviewed receiver recording is bundled.

## Synthetic matrix

Generate deterministic cases across WPM, weighting, tone frequency, SNR,
frequency drift, fading, impulsive noise, overlapping callers, and timing
jitter. Generated callsigns must include portable and compound forms. The exact
range will be fixed after the supported WPM/prosign requirements are agreed.
