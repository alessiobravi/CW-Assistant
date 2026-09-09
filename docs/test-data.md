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

The implemented bounded TSV sidecar binds exactly one WAV by SHA-256 and
records its integer sample rate. Version 1 remains accepted for compatibility
and treats the complete WAV as reviewed. Version 2 requires one or more sorted,
non-overlapping `coverage` sample intervals. Coverage is exhaustive: an
interval without an event explicitly asserts that no CW is present there.
Every event must lie wholly inside reviewed coverage.

Each `event` contains start/end sample indices, audio-tone frequency in hertz,
literal and canonical normalized text, a comma-separated exact callsign set,
and a `0`/`1` uncertainty marker. Lines are canonical UTF-8-compatible text
with LF endings so their review history is portable. They are limited to 4,096
bytes and manifests to 256 records; malformed, duplicate, overlapping,
out-of-order, noncanonical, checksum-mismatched, or out-of-range data fails
closed. An uncertain event is frequency-matched so its real track is not called
a false publication, but it is excluded from CER, WER, and callsign scores.

Run an annotated receiver report with:

```sh
cwa_capture_replay --annotations reviewed.tsv audio.wav
```

The report matches a track using its frequency during the annotated interval,
then prints character/word error, timestamped published-callsign
precision/recall, separate transcript callsign extractability, first
provisional/stable latency, non-append provisional revisions, and unmatched
publication/callsign episodes inside reviewed coverage only. Co-channel
operators at the same frequency are not yet truthfully attributable. Completed
turn output includes an exact-run timing fingerprint when the retained lattice
is complete, or explicitly reports it unavailable; the fingerprint is a
measurement, not an operator identity. No reviewed receiver recording is
bundled.

## Synthetic matrix

Generate deterministic cases across WPM, weighting, tone frequency, SNR,
frequency drift, fading, impulsive noise, overlapping callers, and timing
jitter. Generated callsigns must include portable and compound forms. The exact
range will be fixed after the supported WPM/prosign requirements are agreed.
