#pragma once

#include <QVariantList>

#include <span>

#include "cwassistant/core/cw_channel_bank.hpp"

namespace cwassistant::desktop {

[[nodiscard]] QVariantList decoderChannelModel(
    std::span<const cwassistant::core::CwChannelSnapshot> channels);

}  // namespace cwassistant::desktop
