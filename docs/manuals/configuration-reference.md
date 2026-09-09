# Configuration reference

Settings are stored separately for every named station profile. All supplied
radio values are starting points and remain editable.

## Audio page

Select the operating-system audio input carrying receiver audio. The list is
populated from native Windows, macOS, or Linux audio services and refreshes when
devices are added or removed.

- **System default input (recommended):** follows the current OS default rather
  than binding the profile to one device identifier.
- **Named input:** binds the profile to that specific device.
- **Unavailable:** preserves a disconnected device selection visibly instead of
  silently changing the profile to another input. Reconnect it or select a new
  device, then use **Refresh audio inputs** if necessary.

Audio selection applies equally to a CAT-controlled radio and receive-only SWL
operation. Use **Start live RX** in the Receiver workspace to begin capture.
The application requests 48 kHz mono float when supported and otherwise uses
the device's preferred PCM format, converts it, and downmixes it to mono. Live
audio and WAV replay are explicit, separate receiver modes.

Enable **This input carries RX audio from the configured radio** only when that
physical association is true. This per-profile confirmation is required before
the receiver may combine live audio tones with a controlled radio frequency;
leaving it disabled keeps every marker explicitly labeled **AF**. It is disabled
by default so a system microphone or another receiver is never assigned a
guessed RF frequency.

## SDR page

- **Receiver source** stores whether the profile normally starts from
  sound-card audio or a directly connected SDR. Selecting it never starts the
  device.
- **SoapySDR backend**, **Installed modules**, and **Discovery** distinguish an
  SDR-disabled build, a missing receiver module, no attached device, and a
  successful scan. Opening this page requests one deferred scan. **Refresh
  devices** repeats discovery without opening an RX stream.
- **Center frequency** is absolute RF in whole hertz, from 1 Hz through
  99 GHz subject to the selected hardware.
- **IQ sample rate** requests 25 kS/s through 64 MS/s. The adapter selects the
  nearest rate advertised by the receiver and reports the actual value after
  start. The selector is populated from the selected device when possible.
  This is the effective IQ output rate: drivers such as SDRplay may use a
  higher internal converter rate and their supported hardware decimation to
  supply it. The default is 250 kS/s; increase it only when the wider overview
  is operationally useful.
- **Hardware RF bandwidth** requests the receiver's analogue or baseband
  filter width independently of the IQ sample rate. **Automatic** leaves that
  choice to the driver. Unsupported values are mapped to the nearest advertised
  width and actual hardware readback remains authoritative.
- **Antenna / tuner input** lists the inputs exposed by the selected receiver
  operating mode. A disabled selector means the driver exposes no choice.
- **Decoder window center** and **Decoder bandwidth** select the bounded RF
  region sent to CW detection and the independent stream decoders. The full
  acquired passband remains visible. The default window is 24 kHz and choices
  range from 6 to 96 kHz; use the narrowest width that contains the stations of
  interest to reduce CPU use and decoding latency.
- **Follow authoritative RX VFO readback** keeps the SDR decoder window centred
  on the RX frequency reported by the configured radio-control provider. The
  route is provider-neutral and never infers a value from operator intent.
  **SDR LO offset** adds a signed offset to the SDR hardware centre for
  transverter or independently tuned receiver arrangements. CW Buddy bounds it
  so the decoder window stays inside the acquired passband.
- **Automatic gain** requests the receiver's hardware gain mode. A receiver
  without that capability fails explicitly until manual gain is selected.
  **Manual gain** is requested in dB and the actual readback remains
  authoritative.

Official packages compile direct SDR support on and provide the RTL-SDR module.
Windows packages also provide the SoapySDRPlay3 bridge; the independently
installed SDRplay Hardware API 3.15/service remains required. macOS and Linux
require both that vendor API/service and a compatible external SoapySDRPlay3
module. Custom builds use `CWA_ENABLE_SOAPY_SDR=ON` and matching development/
runtime modules. When a backend or device dependency is absent, discovery
reports the module load failure, the controls fail closed, and normal audio/WAV
reception is unchanged. The SDR boundary is RX-only.
CW Buddy does not probe SDR hardware during application or profile startup.
Opening Settings > SDR requests one scan after the page renders; use **Refresh
devices** to repeat it after a hot-plug or reconnect.

An RSPduo can appear several times because its driver advertises alternative
operating modes, not because several physical receivers were discovered.
Choose **Single Tuner** for CW Buddy's current one-channel receive path. **Dual
Tuner** and **Master** entries are advanced coordinated modes; only channel 0 is
currently consumed, so they do not yet provide two CW Buddy receiver panes.
Close SDRUno, SDRconnect, or any other owner before probing or starting the RSP.

The status bar exposes bounded-queue input overruns. Standard device
sample-rate, RF-bandwidth, antenna/input and gain controls are available when
advertised. Driver-specific options, additional channels, buffer-size,
calibration, and level-meter controls remain under implementation.

### Audio conditioning and bandwidth

- **Remove input DC offset** is enabled by default. It subtracts the constant
  component before each FFT and prevents a sound-card bias from appearing as a
  permanent peak at the left edge.
- **Automatic gain** is optional software DSP gain and disabled by default. It
  does not change the operating-system mixer or a receiver's hardware gain. When enabled,
  **Automatic target** selects the desired peak level from -40 to -1 dBFS. Gain
  changes are bounded to ±40 dB and smoothed between FFT frames.
- When automatic gain is disabled, **Manual gain** applies the exact selected
  value from -40 to +40 dB. Start at 0 dB and increase only if the receiver
  level is genuinely low.
- **Automatic from audio sample rate** is the default processing bandwidth. It
  selects 100–3000 Hz where the input Nyquist limit permits. This is a stable,
  CW-oriented range derived from the source format; it does not chase an
  individual signal.
- Disable automatic bandwidth to set **Lower frequency** and **Upper
  frequency** manually. Values outside the source Nyquist limit are safely
  clipped when spectrum bins are produced.

Example for a receiver whose CW pitch is 700 Hz:

```text
DC rejection: enabled
Automatic gain: disabled
Manual gain: 0 dB
Automatic bandwidth: disabled
Lower frequency: 300 Hz
Upper frequency: 1500 Hz
```

The **Display level range** on the Display page controls only how dBFS values
are mapped to the trace and waterfall colors. Its automatic mode is not audio
gain and cannot create or remove a spectral peak.

## Live spectrum controls

The compact panel immediately below the spectrum mirrors the operational
settings that need adjustment while listening. It collapses automatically to
its header when the pointer leaves, expands on hover, and can be held open with
**Pin**:

- **Signal**: DC rejection, automatic/manual software gain, gain target, and
  automatic/manual bandwidth.
- **Display**: Audio spectrum/CW symbols view, automatic/manual dBFS levels, automatic span, waterfall noise
  suppression, suppression margin, measured noise floor, FPS, line density,
constant history seconds, and the CW frequency guide.

Changes are applied immediately to active live audio and WAV replay. Select
**Save profile** to persist them. The default 60 dB automatic span anchors the
palette well above the measured floor. Audio-spectrum noise suppression
compares every bin with a slow baseline and nearby side frequencies, darkening broad receiver
texture while retaining locally prominent narrowband signals. Raw spectrum bins are retained
for future detection and decoding.

Numeric visualization controls are labeled sliders with live value readouts.
The receiver workspace arranges them over multiple responsive rows, and the
Settings page uses the same controls for precise profile editing.

Hovering the spectrum or waterfall displays the pointer contract: left-click
opens a detected stream, right-click creates a neutral manual probe, and the
Ctrl+click TX-VFO action is unavailable until a guarded provider route exists.
Action buttons in the receiver, settings, setup, and profile views also expose
contextual hover help, including why an action is disabled where applicable.

**Waterfall history** selects a constant 5–30 second vertical time window. The
default is 10 seconds. **Lines / second** changes temporal sampling density, not
the displayed duration. Higher values request overlapping FFT hops, so the
additional rows contain new timing observations instead of copies; 60–120
lines/s makes high-speed dit/dah edges easier to inspect at a higher CPU/render
cost. Resizing and initial fill retain the chosen duration. Capture timestamp
gaps are rendered as dark rows instead of being compressed. Set **Avg** to 1–2
for the sharpest element boundaries; higher averaging deliberately smooths time.

**View** selects **Audio spectrum** or **CW symbols** and is saved per profile.
Audio spectrum uses the configured FFT power averaging. CW symbols uses the
same timestamps but draws only active verified channels' carrier-on states as
sharp three-bin marks on a neutral background; carrier-off intervals remain
blank gaps. Full-passband and unverified receiver noise is intentionally absent
and remains available in Audio spectrum. A retained marker is not active unless
the detector currently matches its peak, so residual noise during the identity
hold cannot draw symbol rows after the 750 ms word-gap bridge. **Margin** controls Audio spectrum
suppression, not this keyed raster. CW symbols is acoustic keying evidence
rather than decoded characters. Switching is immediate and does not clear
tracking, timing hypotheses, or decoded sessions.

**Visual guide** draws two dashed red vertical boundaries at the configured
center minus/plus half-width, with no fill, so it is never mistaken for an
identified signal (active verified CW tracks are the ones shown as a colored
area). It defaults to a 700 Hz center and 200 Hz width and can be adjusted to
match the receiver's preferred sidetone and CW passband.
It does not select a decoder, limit channel detection, change decoder
bandwidth, or retune the radio. Seven X-axis labels show the actual
displayed audio or RF frequency.

The decoder scans the complete processed bandwidth selected under **Signal**.
It acquires sub-bin local spectral peaks privately. A peak must exceed its local
near shoulder and hertz-scaled far references, repeat in at least three
spectral observations across normal key-up gaps, and exhibit at least six keyed
transitions,
three spacing observations, and narrowband coherence. It must then decode at
least three known symbols with no more than 30% unknown output in the bounded
recent evidence window and meet the independent cadence (0.42), pure timing
(0.55), and blended mean-character-confidence (0.40) floors before it becomes a
published CW track with a stable color. Each track
maintains separate soft key evidence, timing, provisional text, stable text,
WPM, SNR, verification/rejection reason, and bounded per-character evidence.
Key evidence is calculated from original input samples
through phase-continuous 60, 120, and 240 Hz narrowband paths at 500
updates/second. The key decision itself is a likelihood ratio
between an estimated mark level and an estimated space level, taken in the
linear power domain. A responsive per-frame estimate follows fading and manual
weighting; a bounded 512 ms history may anchor it to robust low/high populations
only when they have adequate support, separation, and bimodality. Each level
also carries its own measured scatter, and
because a mark carries signal plus noise while a space carries noise alone the
decision settles nearer the mark than half way between them, which is what
stops noise excursions from producing marks on a weak signal. The ratio is
handed to the timing decoder in the form its own logistic inverts exactly, so
the probability that decoder works from is calibrated rather than a second
shaping of an already shaped number; on a channel whose two levels do not
separate, the ratio falls to zero and reports no information rather than an
implied key-up. The smoothing applied before it scales with the element length
rather than being a fixed constant, and impulsive noise is rejected by a
minimum run duration rather than by widening the decision band. Gaps are then
classified against the element length, with the boundary between an element
gap and a character gap placed nearer the character gap than half way. A gap
is not measured in clean conditions: a noise excursion inside one registers as
a mark and eats into it from both ends, so measured gaps run short, and a
boundary placed half way between the two nominal lengths breaks apart
characters that were never spaced. Raising the audio sample rate or the internal
evidence rate does not improve copy and measurably degrades it: element timing
is already oversampled at these settings, while a higher sample rate coarsens
the fixed-size FFT and a higher evidence rate shortens the integration behind
each measurement. Acquisition starts at 120 Hz. The width then follows the keying bandwidth the
signal needs, roughly 3.5 times its element rate, so 120 Hz serves speeds to
about 41 WPM and 240 Hz beyond; a drifting carrier is widened regardless. The
narrowest 60 Hz path is used only below about 15 WPM, because its settling time
is a quarter of a 20 WPM element and rounds real elements together. None of
this changes the visual guide. **Avg**,
display bounds, waterfall suppression, and the visual guide do not alter it.
Detection is deliberately separated from presentation: it reads the unaveraged
spectrum and applies its own smoothing over a fixed time constant, and it runs
on its own fixed cadence rather than once per displayed frame. Changing **Avg**
therefore produces an identical decode, and so does any **Lines / second**
setting at or above that cadence. A line rate below it supplies the detector
with fewer observations and can delay acquisition, so keep **Lines / second**
at 60 or higher while decoding matters. Changing a display control also no
longer restarts decoding: only a change to the audio reaching the detector —
processing bandwidth, DC rejection, or gain — resets tracks and transcripts.
The verified stream marker also remains at a stable 120 Hz presentation width;
adaptive filter changes are diagnostic and do not resize its clickable area.
When retained but inactive, its filled area clears and only a short horizontal
identity-color mark remains on the frequency axis. Its frequency/callsign label
uses an 18 px font and magnifies to 32 px on hover.
Up to 24 tracks are retained; nearby peaks inside the initial 45 Hz separation
are treated as one track, numerical peaks more than 96 dB below the strongest
current bin are excluded, and decoded tracks remain visible for the
Display-page **Decoded signal timeout** (default 30 seconds) after their
signal disappears. Silence does not demote an already-verified observation;
after its marker expires, the frequency-to-color assignment remains leased for
at least five minutes and is reused if that carrier returns. Decoder filter
width/evidence rate and these other
starting limits are not yet exposed as profile controls. Click a colored marker
to open only that decoded session in its larger scrollable/selectable wrapped text
window. It follows appended text unless a selection is active, bolds the
confirmed remote call, and highlights an exact own-callsign match while flashing
the card five times. Close with **×** without stopping decode, reopen from the
marker, then drag a card's handle to reorder it. With the handle focused,
Up/Down provides the keyboard equivalent.
If the retained carrier is reacquired with
a new internal ID, the open session follows the same frequency/color identity.
The decoder pane preserves a bounded 2,048-character presentation transcript
across that replacement, but clears the confirmed callsign until the new
acoustic source establishes it independently. Callsign extraction
requires a complete stable word, rejects noise-like separated digit runs while
retaining contiguous multi-digit special-event calls, and
requires decoded `DE`/`CQ`/`TU`/`UP` context or exact repetition before
automatically naming a stream.
At a sustained-silence or explicit end-of-input boundary, the decoder retains
up to sixteen completed transmission turns and separates them with `|` in the
presentation transcript. Context may select only an acoustically competitive
path with the same non-whitespace characters and may repair only a bounded set
of word gaps. Raw and phase-consensus text are unchanged. **CURRENT SENDER**
and its cadence appear only after explicit two-call or calling-station handover
evidence; ambiguous and conflicting evidence leave them blank. Up to eight
identified senders retain independent cadence summaries, used only as a
bounded prior when a later live timing estimate already agrees.
Right-clicking an unmarked spectrum/waterfall position creates a neutral
manual probe at that center and opens its card without moving the independent
CW guide. Measured carrier evidence can move its DSP and presentation centers
through the ordinary bounded stream tracker. It is not included in the detected
count, exposes no decoded content before ordinary verification, reuses only
another manual center within 12 Hz, and expires after the configured decoded
stream timeout if it cannot verify. Successful verification promotes the same
session normally.
Left-click opens an existing detected stream and never creates a manual probe.
Unverified candidates expire after 750 ms and never appear in the signal count.
A callsign label additionally requires stable text and a completed word gap;
partial, provisional, and unsupported one-off candidates remain hidden.

Timing acquisition evaluates nine fixed starting hypotheses at 8, 12, 16, 20,
25, 32, 40, 50, and 60 WPM. The current leader is provisional for at least 2.5
seconds of signal evidence and locks only after enough decoded symbols and score
separation. After a locked track has been silent for 2.5 seconds, its stable
text is retained and timing acquisition restarts for the next transmission.
These acquisition thresholds are internal measured defaults in this build;
profile controls will be added only with benchmark-backed safe ranges.
The parallel acoustic lattice is evaluated at most every 500 ms and at a
completed gap, retains at most four alternatives within 1.0 cost of the best
path, and requires at least 0.40 timing evidence before appending consensus.
The consensus is append-only and is the card's preferred deterministic text
once available; the literal acquisition path remains its fallback.

The current deterministic qualification target is acquisition within six
simulated seconds for clean, 30 WPM, and weak/fading/drifting CW, zero published
tracks for steady carriers, speech-like amplitude modulation, irregular
impulses, and pumping broadband noise, and less than 0.20 processing seconds
per simulated second. The timing corpus also caps its conservative decoder
state estimate at 256 KiB. These are regression limits for the included corpus,
not universal RF accuracy claims; real recording coverage remains backlog work.

## Decoder page

### Keying model

**Keying model** chooses how the decoder decides where the key goes down and
comes back up. Everything after that decision is shared, so the choice affects
only that one stage.

| | when to use it |
|---|---|
| **Adaptive threshold** (default) | Hand and bug sending, and anything where the sender's timing wanders. Decides key-up and key-down from the envelope moment by moment, and shows text soonest. |
| **Semi-Markov (HSMM)** | Machine-sent, heavily weighted, or Farnsworth-spaced sending. Weighs each mark and gap against the lengths Morse expects, at the cost of about one character of delay. |

Neither is better in general, which is why both are offered. Measured at 20 WPM
and 20 dB, the duration model roughly halves character error on heavy weighting
(0.058 against 0.133) and improves Farnsworth spacing (0.075 against 0.100),
while the threshold is three to four times better under 10% timing jitter
(0.075 against 0.283). Across the general accuracy surface and on receiver
recordings the two are level, so pick on the sender rather than expecting one to
be better everywhere.

Changing the model restarts the decoders but keeps every track, so a station can
be compared under both while it is still sending. The setting is stored by name,
so it survives future additions to the list.

### Debug capture

The **Debug capture** control also appears here, not only in the decoder panel
header, together with a button that opens the capture folder in the file
manager and a **Stop automatically after** value between 30 and 1800 seconds
(default 300). Increase it for a signal that only misbehaves occasionally;
reduce it for a quick reproduction, so there is less to review before sharing.
The JSON lines include bounded completed turns plus explicit current-sender and
supported sender-cadence fields. See the operator guide's Debug capture section
for the complete recorded-field description.

### Local character model

Native builds that include the optional local-model backend expose **Settings
→ Decoder**. Enable **Local character refinement**, then select both a local
`.onnx` model and its matching JSON metadata file. The application never
downloads or bundles a character model. Files are validated when **Apply** is
selected; an incompatible model, tensor contract, or metadata file leaves the
normal deterministic decoder running and shows an error instead.

The supported feature contract is 3200 Hz audio, FFT length 256, hop length 48,
and 65 frequency bins covering 400–1200 Hz. Inference is CPU-only and limited
to four active verified, Morse-likely, or manually selected lanes. A strong
character normally needs two overlapping eight-second windows; a moderately
confident character needs three time-aligned windows and remains provisional
with only two. This is a delayed refinement rather than an instant character
display. When processing
falls behind, older pending windows for the same lane are replaced instead of
allowing an unbounded queue to interfere with live reception.
Automatically qualified lanes continue through a bounded two-second silence
grace so slow manual word gaps do not reset their feature history; an
operator-selected lane receives up to ten seconds for initial acquisition.
Bursts that never fill one complete eight-second window produce no local-model
text; the deterministic decoder remains available for those shorter signals.

**Local callsign suggestions** can use either an operator-selected
N1MM-compatible `master.scp` or Call History text export, or a managed copy of
`MASTER.SCP` downloaded directly from the Super Check Partial (SCP) Database.
Managed use and automatic updates are independently optional. Automatic checks
run no more than once per day, honor the provider's cache validators, and
download only a changed file. **Check for updates** performs the same safe check
on demand. The source, release, last-check time, and current state remain
visible in Settings.

Downloaded and selected files must be readable, non-empty, and no larger than
32 MiB; import is capped at one million unique calls and ignores comments,
directives, duplicates, overlong lines, and structurally invalid calls. A
managed download is checked against the provider's advertised SHA-256 identity,
parsed into a fresh bounded database, and atomically installed only after every
check passes. A failed, interrupted, malformed, oversized, or implausible
download never replaces the last valid copy, so decoding can continue offline.
Until the first managed copy is installed, an already-enabled operator-selected
file remains the active fallback.
**Reload local file** rereads an operator-selected file after an external
update.

Super Check Partial is maintained by W9KKN and is an activity-derived
contesting aid, not an official callsign register. CW Buddy downloads it at
runtime from `supercheckpartial.com`; the database is not bundled or
redistributed with CW Buddy. Update requests identify CW Buddy and its
version but never send received audio, decoded text, station identity, or a
callsign query. Absence from SCP never makes a decoded call invalid.

A directory match is eligible only for an already verified CW channel, a
completed uncertain call-shaped transcript span, and agreement by at least two
current competitive acoustic alternatives on the strongest complete call.
The acoustic candidate may differ from that span by no more than two
wildcard-aware substitutions, insertions, or deletions. An ambiguous result
causes abstention. The card and marker prefix an eligible result with `≈`; an
exact list hit shows **DB**, while an absent stronger winner shows **AUDIO**
and cannot be displaced by a weaker database candidate. This is an
advisory hypothesis: it does not alter decoded text, confirm the callsign,
verify a stream, trigger an own-call alert, or control transmission.
An **AUDIO** suggestion uses only current decoder alternatives and therefore
does not require a local list. Enabling a list adds **DB** provenance when that
same acoustic winner is present; it does not replace the acoustic choice.

Local-model output appears in a separate **LOCAL MODEL** section in the decoder
card. A structurally valid call confirmed across overlapping windows may
complete verification only after the carrier has independently reached
Morse-likely through spectral, keying, cadence, and coherence checks; the
ordinary sustained-entry interval remains. It is displayed with a **MODEL**
badge. The model never creates a carrier, replaces raw text, keeps a silent
stream active, or controls transmission. Builds without the optional runtime show
the setting as unavailable while retaining all deterministic decoding.

## Station page

**Own station callsign** is stored separately in every station profile. Input is
trimmed and normalized to uppercase using the same exact callsign policy as the
ignore list and transmit guard. Portable suffixes may use a single `/`.

Example values:

```text
IU0LFQ
AD2FC
IU0LFQ/P
```

The value populates station logging data and is the exact-match source for the
open decoder card's highlighted **YOUR CALL HEARD** visual notification. The
match must appear as a complete token in stable decoded text; partial calls and
provisional elements do not alert. Audio and remote notifications remain future
optional additions. The future optional closing
macro remains subject to explicit configuration, arming, QSO-context checks,
and cancellation before transmission.

### Operating role

**Operating role** tells the decoder whose callsign a monitored stream is
expected to carry. Exchange context alone cannot always say: `TU` precedes a
runner identifying itself and equally the station it has just worked.

| role | meaning |
|---|---|
| **Monitoring** (default) | No assumption. Exchange context alone ranks callsign candidates. |
| **Search and pounce** | You are hunting stations that are calling, so the stream you are listening to is a runner and its own call is the label. |
| **Running** | You are calling and others answer, so the stream is somebody answering you. |

In every role your own callsign is removed from callsign candidates for other
stations' streams, so a transmission that mentions you is still labelled with
the station actually being heard. Your call being heard is a separate thing, and
still raises the **YOUR CALL HEARD** notification described above.

The role only changes which candidate is ranked highest. It never creates,
rewrites, or corrects decoded characters.

## Radio page

### Radio participation and detection

- **No radio — receive-only (SWL):** process audio without CAT, PTT, or KEY.
  The first-run wizard skips the CAT and Keying pages.
- **Detected online radio:** lists only a device that a supported integration
  positively identifies as online. The initial Windows implementation reads
  the two OmniRig slots. It never treats a COM-port name as a radio model and
  does not issue speculative CAT commands.
- **Manual radio template:** exposes the supplied reference templates only when
  the operator deliberately chooses manual setup. All resulting serial values
  remain editable.

Use **Refresh detection** after starting or reconfiguring the frequency service.
An installed but disabled, busy, unresponsive, or unconfigured radio is not
shown in the detected-radio list. Hamlib model discovery remains planned, but
macOS, Linux, and Windows can connect to an already configured local rigctld
service.

### Reference radio

- **Yaesu FT-450D:** starts at 4800 baud, 8 data bits, no parity, 1 stop bit.
- **Yaesu FT-818/FT-818ND:** starts at 4800 baud, 8 data bits, no parity,
  2 stop bits.

Confirm these values against the radio menu and the interface cable. Loading a
reference profile does not guess a physical port.

### Frequency provider

- **OmniRig (Windows):** select radio slot 1 or 2. The Configure button opens
  its native setup. Direct key/PTT remains a separate COM connection.
- **Hamlib:** connect to `rigctld` started with `--vfo`. Configure the loopback
  host (normally `127.0.0.1`), port (default `4532`), and distinct RX/TX VFO
  names (normally `VFOA`/`VFOB`). Start read-only, or explicitly enable writes
  for frequency, mode, and split. Raw rigctld has no authentication or TLS, so
  CW Buddy refuses non-loopback hosts. For a remote radio, terminate an
  authenticated encrypted tunnel locally and point CW Buddy at its loopback
  endpoint. Hamlib PTT and KEY commands are never used.
- **CAT4OM network service:** connect to a group-specific Control WebSocket and
  select a radio ID. See [CAT4OM setup](cat4om-setup.md).

**RX tuning step** is stored per station profile as a whole-kHz value from 1 to
100 kHz (default 1 kHz). It controls the waterfall-edge `<` / `>` RX buttons;
changing it does not tune the radio until one of those buttons is activated.
Exact readout entry is not rounded to this step.

When authoritative radio state is available, the decoder pane shows a compact
faceplate with grouped whole-hertz RX/TX digits, ON AIR, VFO, SIMPLEX/SPLIT,
and provider-reported RX/TX modes. Each RX/TX frequency, mode, and split action
is independently enabled only when the provider advertises that exact write;
unknown state remains visibly unavailable rather than being inferred.

The TX row represents the independent standby/transmit VFO even in simplex;
the effective transmitted frequency remains the RX frequency until split is
enabled. CAT4OM can report both VFO modes independently. OmniRig exposes only
one mode value, so CW Buddy remembers a mode for A or B only after that VFO has
actually been observed as the receiver. An inactive VFO whose mode has never
been observed remains `?`; CW Buddy never copies the other VFO's mode into it.

### Serial CAT values

The port, baud rate, data bits, parity, stop bits, RTS flow-control mode,
polling interval, and timeout are configurable. These values apply to a direct
serial provider; CAT4OM owns its physical radio connection on the server.

### Split and offsets

Enable split when reception and transmission use independent frequencies.
Offsets accept positive or negative whole hertz values.

Examples:

```text
Direct HF radio: RX offset 0, TX offset 0
Up-converter:    RX offset +116000000
Down-converter:  RX offset -116000000
Cross-band SAT:  independent RX and TX offsets, split enabled
```

**CW audio-to-RF mapping** chooses whether an audio tone above the configured
CW pitch lies above (**CW-U / USB**) or below (**CW-L / LSB**) the radio's
actual RX reference. Actual-RF marker labels require live capture, the explicit
Audio-page radio association, and valid live state from Windows OmniRig or
CAT4OM. RX transverter offset is applied first. WAV, SWL, unlinked, and unknown
states remain labeled **AF**.

## Keying page

Select a dedicated serial port and assign different lines to PTT and KEY.
Defaults are RTS for PTT and DTR for KEY, both active high. Change polarity only
to match an electrically verified interface. Port enumeration is passive.

**Run measured loopback** is an electrical safety gate, not an acknowledgement
checkbox. Disconnect the radio physically, connect RTS→CTS and DTR→DSR on the
selected interface, confirm that disconnected state, then run the probe. CW
Buddy opens only that exact port, establishes an inactive baseline, observes
each loop separately, releases KEY before PTT, and closes the port on success
or failure. A successful result is bound to a SHA-256 fingerprint of the exact
port, line assignments, polarities, and platform; changing enablement or any
fingerprinted value requires a new measurement. CAT and keying ports must be
different.

The direct serial adapter and worker-thread Morse scheduler are connected to
the guarded application controller. Opening initializes KEY and then PTT to
inactive. Transmission asserts PTT before KEY; cancellation, error, shutdown,
and emergency release deassert KEY before PTT. The scheduler uses the fixed TX
speed when configured, otherwise the selected stream's bounded RX estimate.

The **QSO** drawer requires explicit arming, an exactly decoded and retyped
target callsign, and a second exact confirmation of the normalized outgoing
message. Own-call, editable report/exchange, profile-configurable quick macros,
and free-text actions all use the same boundary. The progress panel reports
elapsed and remaining time from the worker's monotonic clock while a message or
TUNE is active and clears at every terminal state. **Auto-QSO** can propose one
of those messages from decoded context
but cannot confirm or send it. **TUNE** is an operator-only toggle with a hard,
non-extendable 15-second hardware deadline. Ordinary Morse elements have a
separate three-second continuous-KEY guard. A measured loopback permits safe
port opening but is not on-air acceptance: always complete the documented
message, cancel, watchdog, and emergency-release checks into a dummy load at
minimum power first.

## Display page

- **Spectrum view:** profile-persisted **Audio spectrum** (smoothed) or **CW
  symbols** (verified-channel keying raster on a neutral background).
- **Target FPS:** UI redraw target from 10 to 120.
- **Waterfall lines/second:** independent scroll/update rate from 1 to 120.
- **Automatic range:** adapts the visible dBFS range using smoothed robust
  spectrum levels. Disable it to use the editable lower and upper bounds.
- **Spectrum averaging:** applies exponential power averaging in DSP from 1 to
  32 frames; higher values steady the trace but react more slowly.
- **Reference grid:** shows or hides functional frequency/level guide lines.
- **Lower/upper dB:** manual bounds; at least 10 dB of span is enforced.
- **Decoded signal timeout:** from 5 to 300 seconds, default 30. Controls how
  long a verified track's marker and session stay visible after its signal
  disappears before being removed. Applies immediately to a running decoder
  session on both the live-audio and WAV-replay paths, without restarting RX.
  A carrier recognized again within at least five minutes reuses its previous
  color even if this shorter marker/session timeout has already elapsed.

The current receiver canvas is an honest empty state and does not draw simulated
radio data.

## Profile examples

### HF desk

```text
Radio: Yaesu FT-450D
Frequency provider: OmniRig slot 1 (Windows) or Hamlib
CAT: radio-specific COM port, 4800 8-N-1, hardware RTS flow control
Key/PTT: dedicated interface port, RTS PTT, DTR KEY
Offsets: 0 / 0
```

### Portable radio

```text
Radio: Yaesu FT-818
Frequency provider: OmniRig slot 2 (Windows) or Hamlib
CAT: radio-specific port, 4800 8-N-2
Key/PTT: dedicated interface port
Offsets: 0 / 0
```

### Network-controlled station

```text
Frequency provider: CAT4OM network service
Control URL: ws://127.0.0.1:5001/
Radio ID: run
CAT serial fields: managed by the CAT4OM server, not this client
Key/PTT: remains local and independently guarded unless a future remote-server
         profile explicitly owns transmission
```
