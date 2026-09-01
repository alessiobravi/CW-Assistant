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
// includes per-candidate identity, frequency, or overlay data: unverified
// candidates still must not receive spectrum overlays or session rows.
[[nodiscard]] QVariantMap verificationDiagnosticsModel(
    const cwassistant::core::CwVerificationDiagnostics& diagnostics);

}  // namespace cwassistant::desktop
