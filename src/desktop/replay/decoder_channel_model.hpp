#pragma once

#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <span>

#include "cwassistant/core/cw_channel_bank.hpp"

namespace cwassistant::desktop {

enum class LocalDecoderPresentationState {
  Unavailable,
  Disabled,
  Loading,
  Ready,
  Error,
};

// Presentation-only output from an optional local decoder. Stable text must
// be append-only for a channel ID. It is deliberately kept outside the core
// verification, callsign, and publication evidence paths.
struct LocalDecoderChannelPresentation {
  std::uint64_t channel_id{0};
  LocalDecoderPresentationState state{
      LocalDecoderPresentationState::Unavailable};
  QString stable_text;
  QString status;
};

[[nodiscard]] QVariantList decoderChannelModel(
    std::span<const cwassistant::core::CwChannelSnapshot> channels,
    std::span<const LocalDecoderChannelPresentation> local_decoder = {});

// Aggregate pre-verification pipeline diagnostics (candidate/morse-likely
// counts and rejection-reason tally) for operator troubleshooting. Never
// includes automatically discovered candidate identity, frequency, or overlay
// data. An explicit operator-selected manual probe is the sole exception in
// the channel model; it remains neutral and redacts text/callsign evidence
// until the ordinary verification gates pass.
[[nodiscard]] QVariantMap verificationDiagnosticsModel(
    const cwassistant::core::CwVerificationDiagnostics& diagnostics);

}  // namespace cwassistant::desktop
