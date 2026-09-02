#pragma once

#include <QVariantList>
#include <QVariantMap>

#include <span>

#include "cwassistant/core/cw_channel_bank.hpp"

namespace cwassistant::desktop {

[[nodiscard]] QVariantList decoderChannelModel(
    std::span<const cwassistant::core::CwChannelSnapshot> channels);

// Aggregate pre-verification pipeline diagnostics (candidate/morse-likely
// counts and rejection-reason tally) for operator troubleshooting. Never
// includes automatically discovered candidate identity, frequency, or overlay
// data. An explicit operator-selected manual probe is the sole exception in
// the channel model; it remains neutral and redacts text/callsign evidence
// until the ordinary verification gates pass.
[[nodiscard]] QVariantMap verificationDiagnosticsModel(
    const cwassistant::core::CwVerificationDiagnostics& diagnostics);

}  // namespace cwassistant::desktop
