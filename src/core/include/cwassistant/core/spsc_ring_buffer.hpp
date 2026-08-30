#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace cwassistant::core {

// Bounded single-producer/single-consumer queue. It performs no allocation,
// locking, or blocking and is therefore suitable between an audio callback and
// the DSP dispatcher. Capacity is the usable element count.
template <typename T, std::size_t Capacity>
class SpscRingBuffer {
 public:
  static_assert(Capacity > 0);
  static_assert(std::is_copy_assignable_v<T>);

  [[nodiscard]] bool try_push(const T& value) noexcept(
      std::is_nothrow_copy_assignable_v<T>) {
    const auto head = head_.load(std::memory_order_relaxed);
    const auto next = increment(head);
    if (next == tail_.load(std::memory_order_acquire)) {
      return false;
    }

    storage_[head] = value;
    head_.store(next, std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool try_pop(T& value) noexcept(
      std::is_nothrow_copy_assignable_v<T>) {
    const auto tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) {
      return false;
    }

    value = storage_[tail];
    tail_.store(increment(tail), std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool empty() const noexcept {
    return head_.load(std::memory_order_acquire) ==
           tail_.load(std::memory_order_acquire);
  }

 private:
  static constexpr std::size_t kStorageSize = Capacity + 1;

  static constexpr std::size_t increment(std::size_t index) noexcept {
    return (index + 1) % kStorageSize;
  }

  std::array<T, kStorageSize> storage_{};
  alignas(64) std::atomic<std::size_t> head_{0};
  alignas(64) std::atomic<std::size_t> tail_{0};
};

}  // namespace cwassistant::core
