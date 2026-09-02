# Operator guide

## Current development status

CW Assistant is pre-release software. The current desktop shell can create and
select isolated station profiles, guide first-time setup, run in receive-only
SWL mode, discover serial ports without opening them, identify online radios
through a supported integration, save radio/keying/display settings, open the Windows
frequency-provider configuration, monitor a CAT4OM radio, process a selected
live sound-card input, and replay a WAV recording through the real spectrum and
waterfall, and run a receive-only decoder across the complete processed
passband. Each tracked frequency now obtains keying evidence from a narrowband
filter over the original audio rather than the display spectrum. Bounded
multi-speed acquisition is active; weak-signal refinement, direct keying
output, logging connection, SDR capture, and remote-station runtime remain
under implementation.
A saved profile does not arm or key a transmitter.

The replay core accepts little-endian RIFF/WAVE PCM at 8, 16, 24, or 32 bits and
IEEE float32. Multichannel input is averaged to mono for this audio-analysis
path. Compressed WAV codecs are rejected with a clear diagnostic.

Every supported-platform build runs an empty-receiver render regression test
and loads the complete QML desktop shell before it can be published. This
specifically covers first launch before a WAV, audio device, or SDR source has
produced spectrum data. The staged application is also launched with the hosted
runner's native graphics path before its installer or archive is uploaded, with
a deterministic test spectrum to exercise waterfall texture creation.

## Receive live radio audio

1. Open **Settings → Audio**, select the sound-card input connected to the
   receiver, and select **Apply**.
2. In the Receiver workspace, choose **Live audio** instead of **WAV replay**.
3. Select **Start live RX**. On macOS, approve microphone/audio-input access the
   first time; the application requests this only when live RX is started.
4. Confirm the status line names the device and sample rate. Spectrum and
   waterfall frames now come from that device.
5. Select **Stop live RX** before changing cables or audio routing.

Capture requests 48 kHz mono floating-point audio when supported. Otherwise it
uses the input's preferred PCM format, safely averages channels to mono, and
normalizes integer or float samples. Capture places fixed sample blocks in a
bounded queue without allocating or blocking; FFT work runs on a separate DSP
worker. **Input overruns** should remain zero. A rising value indicates the DSP
cannot keep pace and blocks are being deliberately dropped rather than allowing
unbounded latency.

The spectrum and waterfall visualize all audio energy; they do not wait for a
Morse signal. Receiver noise alone should begin filling the waterfall after
live RX starts. If the display remains blank while **Input overruns** rises,
stop live RX and install a newer build because the processing worker is not
draining captured blocks correctly.

If a stationary peak fills the far-left edge, open **Settings → Audio** and
leave **Remove input DC offset** enabled. This is normally sound-card DC bias,
not gain. Keep **Automatic gain** off for a calibrated receiver and tune
**Manual gain** from 0 dB; or enable it and choose the automatic dBFS target.
Use automatic 100–3000 Hz bandwidth for a general CW view, or disable it and
enter lower/upper frequencies around the receiver passband. Select **Apply** to
save the values in the active station profile.

The same operational controls now sit immediately below the spectrum. Changes
to DC rejection, software gain, bandwidth, display levels, automatic span, and
waterfall noise suppression take effect while RX is running. **Save profile**
persists the current values; the Settings pages remain available for complete
profile configuration. The panel collapses to a slim header when the pointer
leaves it; hover over the header to reveal it, or select **Pin** while making
several adjustments.

The waterfall always represents the selected fixed number of **History**
seconds from top to bottom. At startup, unavailable older time stays dark rather
than stretching the first received rows over the pane. Resizing changes only
the pixel height, and capture gaps remain visible as dark time. **Lines/s**
controls genuine overlapping analysis updates without changing the window
duration; use 60–120 lines/s when inspecting high-speed dit/dah traces, subject
to available CPU. Reduce **Avg** to 1–2 frames for crisper element edges; raise
it only when a steadier but less time-sharp display is more useful.

For timing inspection, select **CW symbols**. Unlike **Audio spectrum**, this
is intentionally sparse: only active, verified channels' carrier-on states are
drawn as high-contrast marks on a neutral background. Carrier-off rows form
blank gaps, and full-passband/unverified noise remains exclusively in Audio
spectrum. A retained marker cannot draw marks unless its peak is currently
matched, so residual noise during the identity hold stays blank. This makes
dit/dah timing readable rather than presenting a second noise spectrogram.

The Display controls are grouped into multiple responsive rows. Drag the
labeled sliders for FPS, line rate, averaging, history, levels, CW center/width,
and Audio-spectrum noise margin; each label shows the current numeric value.
The full Settings → Display page offers the same slider interaction.

Enable **Visual guide** to draw two dashed red boundaries around the desired
receive region. Their positions are exactly the configured center minus/plus
half the configured CW width. The default is centered at 700 Hz with a 200 Hz
width; both values update in real time and are stored per profile. The region
has no fill, so it cannot be mistaken for an identified signal — that treatment
is reserved for active verified CW tracks (see below). This guide is visual
only: it does not select a decoder, limit decoding, change receiver tuning, or
change decoder bandwidth.

Pointer tuning is planned, not active in this build. The defined behavior is:
left-click a waterfall trace to move the local CW guide to it; right-click to
request an RX-only CAT retune that places the pointed actual-RF signal at the
configured CW pitch. Future decoded-call overlays will be anchored to absolute
RF Hz and a stable track ID, updated as text improves, marked lost after a short
silence, and removed from both waterfall and list after a configurable timeout.

For a quieter waterfall, open the **Display** live-control tab, leave **Suppress
noise** enabled, and start with a 6 dB margin. Automatic levels maintain a
minimum 60 dB span and follow falling peaks slowly, preventing receiver-noise
changes from repeatedly driving the palette yellow. A radio's own AGC may still
change the audio level delivered by the sound card; this application does not
yet control radio AGC through CAT.

The receiver scans every frequency inside the processed audio bandwidth for
both live audio and WAV replay. Spectral peaks begin as private candidates and
do not immediately receive a line. A candidate must show local prominence,
repeat across spectrum observations within the retained track, contain coherent
narrowband energy and keyed edges, and progress through a Morse-likely state. It
then must
produce at least three known Morse symbols with bounded unknown output plus
adequate spacing cadence, timing, and character confidence. Only then does
it receive a stable color and a colored vertical area with a fixed 120 Hz
presentation width, or increment **signals detected**. The internal adaptive
filter remains independent and is reported in the tooltip/session diagnostics,
so its normal 60/120/240 Hz changes cannot resize or flicker the marker. A
thinner line inside that area flashes
with the live keying state. Click anywhere in that colored area to open its
decoded session in the right-hand panel. Its larger decoded-text window wraps
the latest output and remains selected if the same frequency/color is
reacquired under a replacement tracker ID. Up to 2,048 stable text characters
and an already-confirmed structurally plausible callsign remain visible across
that replacement. Closing a card with **×** does not
stop its DSP; click the marker to reopen it. Drag cards over one another to set the
operator's preferred order. Stable text, amber provisional text/elements,
adaptive WPM, SNR, confidence, drift, and selected filter width update in place.
A keyed gap does not immediately discard a track; decoded tracks are retained
for a configurable timeout (Settings → Display → **Decoded signal timeout**,
default 30 seconds, configurable up to 300 seconds) so normal word and message
gaps preserve identity. Silence retains rather than invalidates the verified
observation. The filled area and center line clear
while inactive, leaving a short horizontal identity-color mark on the frequency
axis. The larger stream label magnifies further while the marker is hovered.
Retention preserves identity and text only: without a current matched peak it
cannot keep the area active or generate CW-symbol rows from residual noise.
The area bridges ordinary word gaps for 750 ms, but then clears; this visual
hold is independent of the longer six-second verification-exit and configured
marker-retention timers. The label stays at its acquired carrier center while
internal drift tracking continues, and grows to 26 px on hover.
If the marker eventually expires, its carrier keeps
the same reserved color for at least five minutes and reuses it when recognized
again; a new track ID therefore does not make the same frequency look like a
different station merely because one pass ended.

Peak shape is measured in hertz rather than a fixed number of FFT bins. A small
near-shape requirement rejects broad shoulders, while farther references allow
the wider peak produced by a real receiver/audio path to enter private
tracking. Therefore a visible narrow trace may take several characters before
its marker appears, but a steady carrier or broadband level change should not
be presented as a decoded station. A genuinely broad spectral feature
(adjacent SSB audio, AGC pumping, a receiver-filter skirt) is expected to
never appear as a candidate at all, because its shape fails the
local-prominence check before any track is created.

Frequency text is written vertically beside the matching colored area inside
the upper spectrum region, never over waterfall history. Callsign text remains
hidden until the track is verified, the decoder has promoted the text to stable,
and a word gap confirms that the structurally valid token is complete. This is
signal/timing confirmation, not external directory validation. The marker
tooltip exposes the same confirmed call, frequency, audio tone, filter width,
and measured drift.

The FFT-bin tracker discovers candidate frequencies, but it does not provide
the key-up/key-down evidence. For both live audio and WAV replay, the DSP worker
uses the original samples to mix each tracked tone to baseband, evaluates
three-stage 60, 120, and 240 Hz paths, compares their energy with independently
smoothed references on both sides, and supplies new soft evidence 500 times per
second to that track's adaptive timing decoder. The 120 Hz path remains fixed
during initial acquisition; the decoder can then narrow a clean slow signal or
widen a fast/drifting signal. This is deliberately independent of spectrum averaging,
waterfall levels, display gain, and the 700 Hz visual guide.

Each new track evaluates nine timing starts from 8 through 60 WPM. During at
least the first 2.5 seconds after keyed evidence begins, the best current path
is intentionally shown as amber provisional text because it may change as
slower hypotheses gain enough evidence. After the timing score and
decoded-symbol threshold pass, one path becomes the presentation leader while
all nine fixed speed anchors keep processing. A gap of at least 2.5 seconds
reselects the best complete path, preserves stable text, and starts a fresh
speed acquisition, so an early choice cannot permanently disable alternatives
and another sender can use a substantially different speed. Quality is judged
over bounded recent evidence, allowing a rough acquisition to recover. Short
or ambiguous fragments may remain provisional rather than being presented as
certain.

A colored verified marker is intentionally delayed until the complete evidence
set remains valid for roughly half a second. Brief fades then receive a longer
hold before removal, reducing both transient false markers and visible flapping.
No-signal evidence cannot demote a verified marker before the configured
decoded-signal timeout; contradictory evidence from an active carrier can.
Cadence, pure timing, and blended character confidence remain separate gates;
one strong metric cannot substitute for a failing one.
An extremely short fragment can therefore end while still provisional; this is
an abstention, not evidence that its carrier was absent from the symbols view.
Development builds also preserve the final verification summary when the
hosted live-audio acceptance fixture fails, so a release is not advanced on an
opaque or unexplained decoder result.

The baseline handles letters, digits, common punctuation, selected prosigns,
sub-bin drift tracking, and automatic filter width selection, but does not yet
provide calibrated confidence, multiple-pass weak-signal recovery, or separation of
callers occupying the same frequency. Signals closer than about 45 Hz may
therefore appear as one track, and noise or non-CW
carriers may produce `?` or incorrect text. Changing the audio source or
processing bandwidth clears decoder state. Decoder output cannot arm TX, key a
radio, or initiate a QSO.

## Debug capture

When a visible signal will not decode and the on-demand **Diagnostics**
readout is not enough to explain why, use **Debug capture** in the decoder
panel header. It is only available while live RX is running. Selecting it
starts a bounded recording:

- The exact raw audio feeding the decoder, written to `audio.wav`.
- A private per-track diagnostic log, `diagnostics.jsonl`, with one line per
  second listing every currently tracked frequency — including tracks that
  never become visible — with its SNR, narrowband coherence, filter width,
  verification state and reason, spectral observations, key transitions,
  decoded/unknown symbol counts, timing/cadence quality, WPM, and both
  provisional and stable decoded text. Each line also records the linked
  radio's RX/TX frequency and split state at that instant, so reviewing the
  file shows whether (and exactly when) the VFO moved during the capture —
  a common explanation for a signal that stops decoding partway through.

Both files are written to a timestamped folder under the application's
standard per-user data location; the panel shows the exact path while
recording and after it stops. Capture is capped at five minutes and always
requires an explicit click to start; it is never silent or automatic. Select
**Stop capture** to end it early. Because the WAV file is exactly what the
selected audio input picked up, review its contents before sharing the
capture folder with anyone.

## Replay a receiver recording

1. Choose **WAV replay**, select **Open WAV**, and choose a local recording.
2. Review the detected filename, sample rate, and duration.
3. Select **Play**. The upper trace is the current Hann-windowed FFT; the lower
   panel is the scrolling waterfall. Select **Audio spectrum** for smoothed
   spectral history or **CW symbols** for crisp verified-channel acoustic
   dit/dah/gap rows on a neutral background. The symbols view is keyed envelope
   evidence, not reconstructed decoder text.
4. Use **Pause** to retain the current display or **Stop** to return to the
   beginning. Opening another file clears the previous display.

The progress bar and elapsed time follow sample-derived recording time rather
than wall-clock guesses. The first integration does not yet provide seeking or
looping. Frequency labels cover 0 Hz through half the WAV sample rate because
ordinary WAV replay is treated as real-valued audio, not complex I/Q.

Open **Settings → Display** to select the redraw target, waterfall row rate,
automatic or manual dBFS range, DSP averaging from 1 to 32 frames, and the
profile-persisted spectrum view. Switching views while live or replaying does
not reset the decoder. Automatic range uses a smoothed robust estimate so an isolated
strong bin does not repeatedly rescale the entire view. These values are saved
independently in each station profile.

## About and author

Open **Settings → About** to see the application version, license, author, and
author website. The displayed author is **Alessio Bravi (IU0LFQ / AD2FC)** and
the **Author Website** button opens [https://iu0lfq.it/](https://iu0lfq.it/) in
the system browser. The application, taskbar/dock entry, and installed shortcuts
use the same Morse-key dot/dash mark.

Continuous downloads are published only after every supported-platform build,
test, staged-layout check, and native startup smoke test succeeds. A failed
matrix does not replace the last fully verified download.

The displayed version is the same `major.minor.revision` value embedded in the
native installer/package and application metadata. Continuous builds use the
GitHub workflow run number as the revision, so a hosted build may show, for
example, `0.1.245`; a default local development build shows `0.1.0`.

## Checking for updates

The same **Settings → About** page checks for updates. A background check
runs a few seconds after every launch (uncheck **Automatically check for
updates** to disable it), and **Check for updates** runs one on demand,
showing when it last ran and whether a newer version is published.
During the short interval in which continuous-release files are being replaced,
the previous manifest remains available and the application retries transient
404, timeout, and server errors. A persistent failure is still reported after
three attempts; it is never treated as an available or verified update.

When an update is available, **Download update** fetches this platform's
installer/package to your Downloads folder and verifies its checksum against
the published `SHA256SUMS` before keeping it — a failed or mismatched
download is discarded automatically, never silently kept. Once verified,
**Open installer** hands it to the OS's own installer or package manager
(the Windows MSI installer, the Linux package tool, or an archive tool on
the portable builds) so you complete the install the normal way; **Show in
folder** reveals it instead if you would rather run it yourself. The
application never downloads or installs anything without you clicking
these buttons, and never silently replaces itself while running.

## First launch

1. Start `cw-assistant-desktop`.
2. Enter a descriptive station profile name, such as `HF desk` or
   `Satellite station`.
3. For audio-only decoding, select **No radio — receive-only audio decoding
   (SWL)**. The wizard skips CAT and Keying after the Audio step.
4. For radio operation, select a positively identified online radio. On
   Windows, the initial detector reads the online state and model name from the
   installed OmniRig service; it does not send probe commands to arbitrary COM
   ports.
5. If the radio cannot be identified, select **Set up a radio manually**, choose
   the nearest reference template, then edit every value to match the radio and
   cable.
6. On **Audio**, select the sound-card input carrying receiver audio. **System
   default input** follows the operating-system default when devices change.
   This step is always present for radio and SWL profiles. For a controlled
   radio, also confirm **This input carries RX audio from this radio** if the
   selected device is physically connected to that receiver.
7. Select a physically separate direct-COM key/PTT interface. Port enumeration
   never toggles RTS or DTR.
8. Review the display defaults and finish the wizard.

The Back and Next controls live in a fixed wizard footer and remain visible when
a setup page must scroll on a small or scaled display.

Finishing the wizard saves settings only. Hardware ownership, a keying loopback
test, and the transmit guard will be required before transmission is enabled in
a later milestone.

Selecting SWL mode persists that choice per profile, disables radio/keying
validation, and labels the workspace as receive-only. Previously entered radio
values are retained so switching the profile back to radio operation does not
discard configuration.

The current build discovers, displays, and saves audio-input selection, including
an unavailable marker when a previously selected device is disconnected. Live
sound-card capture, input level metering, channel/sample-rate selection, and
buffer controls are still under implementation; use **Open WAV** for the active
signal-processing path in this build.

## Multiple radios and application instances

Create one named profile for each independent station chain. If more than one
profile exists, the startup helper asks which profile to open. A shortcut or
automation can bypass the helper:

```text
cw-assistant-desktop --profile "HF desk"
cw-assistant-desktop --profile "Satellite station"
```

Two application processes may use different profiles. Future device locks will
prevent both processes from opening the same serial, audio, or SDR device.

## Frequency, split, and transverter terminology

- **RX dial frequency** is the frequency reported to or requested from the
  radio for reception.
- **TX dial frequency** is independent when split is enabled.
- **RX/TX transverter offset** is a signed integer in hertz. It is used to
  calculate actual RF for display and logging; the offset is never sent to a
  direct CAT radio by accident.
- **CW audio-to-RF mapping** selects CW-U/USB or CW-L/LSB direction. For a live
  linked input, each signal is calculated as `actual RX RF + direction ×
  (decoded audio tone − configured CW pitch)`.

Actual-RF marker labels are enabled only while live capture is running, the
profile explicitly links that audio input to the radio, and the selected
frequency provider has a valid state. Windows OmniRig is polled for its online
RX frequency; CAT4OM uses its pushed radio state. The RX transverter offset is
applied before tone mapping. Recordings, SWL profiles, unlinked inputs, and
unavailable frequency providers deliberately show **AF** rather than guessing.

The **CW Decoder** panel shows the same resolved frequency as a large
VFO-style readout above the signal list, sized to match the panel around
it: RX in green, and — once split is active — TX in yellow alongside a
SPLIT badge, both shown with the same decimal/centesimal precision a real
rig's display has (for example `7016.45 kHz` on 40 m). It disappears
entirely for receive-only SWL setups, WAV replay, and whenever no radio is
currently linked, rather than showing a stale or meaningless value. Note
that showing this readout at all requires **both** Settings → Radio
**Radio enabled** and the **audio input linked to radio** toggle — enabling
the radio alone is not enough.

Retuning the linked radio's RX VFO while live audio is running follows any
already-identified signal rather than losing it: every tracked signal is
re-centered by the exact amount the RX dial moved (translated to audio Hz
using the configured CW-U/CW-L sideband direction), so its decoded text and
verification carry over across the retune instead of restarting.

Next to the VFO readout is an **ON AIR** indicator. It is a placeholder
only: it never lights, because no currently supported radio backend
reports live transmit/PTT state (OmniRig's own status property is not yet
polled for it, and CAT4OM's protocol has no such field). It will start
reflecting real state once that telemetry is added.

Example satellite station:

```text
RX dial:        29,900,000 Hz
RX offset:     116,000,000 Hz
Actual RX RF:  145,900,000 Hz

TX dial:        28,300,000 Hz
TX offset:     407,000,000 Hz
Actual TX RF:  435,300,000 Hz
Split: enabled
```

The log record derives `FREQ_RX`, `FREQ`, `BAND_RX`, and `BAND` from those
actual RF values. Satellite operation also supplies `PROP_MODE`, `SAT_NAME`,
and `SAT_MODE`. Station equipment rules select `MY_RIG` and `MY_ANTENNA` from
the actual TX/RX bands.

## Safety principles

- Decoder output never starts transmission.
- The first transmission of a QSO requires operator confirmation of the exact
  selected callsign.
- Ignored callsigns are excluded from display, queueing, QSO selection, and TX
  authorization.
- Direct key/PTT is separate from frequency control.
- Network receivers are receive-only and cannot own TX.
- Remote operation keeps final interlocks and CW timing at the station server.
