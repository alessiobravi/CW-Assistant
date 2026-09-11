#pragma once

#include <array>
#include <cstddef>

namespace cwassistant::core {

// The speeds every decoder starts from before it has measured the sender.
//
// Both the multi-speed decoder and the probabilistic one evaluate this set,
// and each kept its own copy. They are not independent choices: a speed added
// to one and not the other means the two disagree about which senders they can
// acquire, and nothing would report it.
//
// The spacing is roughly a quarter in the logarithm, which is close enough
// that an anchor and the sender it is nearest agree on element length to
// within the tolerance the lattice already allows, and coarse enough that nine
// of them span a hand key at eight words a minute through a machine at sixty.
inline constexpr std::array<double, 9> kCwSeedWpmAnchors{
    8.0, 12.0, 16.0, 20.0, 25.0, 32.0, 40.0, 50.0, 60.0};

// Where a decoder starts before any evidence. Named rather than written as a
// bare index, which silently selects a different speed if the anchors change.
inline constexpr double kCwInitialLeaderWpm = 20.0;

[[nodiscard]] constexpr std::size_t cwSeedWpmAnchorIndex(
    const double wpm) noexcept {
  for (std::size_t index = 0; index < kCwSeedWpmAnchors.size(); ++index) {
    if (kCwSeedWpmAnchors[index] == wpm) return index;
  }
  return 0;
}

}  // namespace cwassistant::core
