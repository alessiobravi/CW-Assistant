# ADR 0001: Qt Quick scene-graph spectrum renderer

Status: accepted

Date: 2026-08-30

## Context

The project needs a responsive, zoomable spectrum and waterfall on Windows 11
x64, macOS, and Linux while DSP and multiple CW decoders run concurrently. The
UI must remain responsive under load, expose clickable channels/callsigns, and
retain a portable fallback when accelerated features are unavailable.

## Decision

Use Qt 6 Quick/QML for controls and layout, with a custom C++ `QQuickItem` for
the real-time visualization:

```text
QML controls and layout
        |
SpectrumWaterfallItem (public QQuickItem API)
        |
Qt Scene Graph nodes
  |-- spectrum line/fill geometry
  |-- waterfall ring texture + palette shader
  |-- grid/frequency markers
  `-- decoded callsign/channel overlays and hit testing
```

Qt Shader Tools will compile shader variants at build time. Qt's rendering
abstraction selects Direct3D on Windows, Metal on macOS, and Vulkan/OpenGL as
available on Linux. Diagnostics expose the active API and fallback reason.

The display is deliberately two-dimensional. GPU work must improve clarity,
latency, or efficiency of an operating feature; visual novelty by itself is not
sufficient scope.

## Module boundaries

- `SpectrumSnapshot`: immutable, renderer-neutral bins and frequency mapping.
- `SpectrumSnapshotQueue`: bounded SPSC transfer from DSP to UI; the render
  item drains available rows without sharing a long-held producer mutex.
- `SpectrumGeometryNode`: spectrum trace and optional peak-hold geometry.
- `WaterfallHistory`: fixed-capacity row ring with sequence-gap accounting.
- `WaterfallMaterial`: palette, dB range, gamma, black level, zoom, and pan.
- `ChannelOverlayNode`: tracked tones, confidence, callsign labels, and hit map.
- `SpectrumWaterfallItem`: property and interaction coordinator only.
- `RenderMetrics`: frame rate, queue gaps, upload bytes/rows, and sync/render
  latency, independent of rendering classes.

The DSP dispatcher calculates one windowed CPU FFT and publishes its result to
both detection and display. Rendering may drop presentation snapshots under
load, but DSP processing has a separate queue and priority policy. GPU compute
is deferred until profiling of wide SDR sample rates demonstrates a benefit.

## Backend policy

The baseline must build using Qt's public Quick/Scene Graph and Shader Tools
APIs. A CPU-generated 8-bit intensity texture with a GPU palette shader is the
portable fallback. A partial-row float-texture uploader or GPU compute backend
may be added later only behind a narrow interface and build option. If it needs
Qt private APIs, CI must pin the exact Qt minor version and the packaged
fallback must remain functional.

A widget plotting library is not used for the real-time waterfall. It may be
used only for low-rate diagnostic plots where a widget model is appropriate.

## Explicit visual non-goals

- 3D or perspective spectrum history
- animated backgrounds, bloom, glow, particle, or ornamental shader effects
- simulated instrument treatments that reduce data density or legibility
- duplicate visual modes without an operating or diagnostic use case

Themes, palettes, peak hold, averaging, and accessible contrast remain in scope
because they improve signal interpretation rather than decorate it.

## Threading and ownership

DSP owns sample and spectral computation. The GUI thread owns QML state. The Qt
render thread owns scene-graph nodes and GPU resources. Cross-thread transfer
uses bounded snapshots with sequence numbers; no thread receives pointers into
another thread's mutable storage.

GPU resource creation, updates, and destruction occur only at Qt-sanctioned
scene-graph phases. Cleanup is scheduled on the render thread when required.
Backend-native image nodes are created only after a valid texture exists, own
their render-thread textures, and are retained with an empty rectangle across
receiver resets. A texture node with null texture is invalid Qt scene-graph
state and must never be added or updated.

## Consequences

- The core stays independent of Qt and can be tested without a display.
- CPU spectrum bins are the single source of truth for visible frequency
  coordinates and CW channel detection.
- We accept a potentially less efficient full-texture fallback before adding a
  private graphics optimization; profiling and correctness come first.
- Accelerated and fallback rendering require replay/golden-image tests and
  native CI smoke tests.
