#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "cwassistant/core/cw_character_decoder.hpp"
#include "cwassistant/core/cw_character_lane_frontend.hpp"

namespace cwassistant::desktop {

enum class CwCharacterBackendStatus {
  unavailable,
  ready,
  invalid_metadata,
  model_load_failed,
  invalid_input,
  inference_failed,
  invalid_output,
};

struct CwCharacterBackendConfig {
  std::string model_path;
  std::string metadata_path;
  std::size_t maximum_input_frames{2'048};
  std::size_t maximum_output_steps{4'096};
  std::size_t maximum_characters{256};
  // Predictions whose acoustic centers fall in these window edges remain
  // provisional when overlapping windows are merged.
  std::size_t edge_context_frames{34};
};

struct CwCharacterBackendDiagnostics {
  CwCharacterBackendStatus status{CwCharacterBackendStatus::unavailable};
  std::string message;
  std::uint64_t inference_count{0};
  std::uint64_t failure_count{0};
  std::size_t last_input_frames{0};
  std::size_t last_output_steps{0};
  std::size_t last_emitted_characters{0};
};

// Optional desktop adapter from a dependency-free core feature window to a
// timestamped CTC hypothesis. The model and its metadata are always supplied
// by the operator; this class neither bundles nor downloads either file.
class CwOnnxCharacterDecoderBackend {
 public:
  explicit CwOnnxCharacterDecoderBackend(CwCharacterBackendConfig config);
  ~CwOnnxCharacterDecoderBackend();

  CwOnnxCharacterDecoderBackend(CwOnnxCharacterDecoderBackend&&) noexcept;
  CwOnnxCharacterDecoderBackend& operator=(
      CwOnnxCharacterDecoderBackend&&) noexcept;
  CwOnnxCharacterDecoderBackend(const CwOnnxCharacterDecoderBackend&) = delete;
  CwOnnxCharacterDecoderBackend& operator=(
      const CwOnnxCharacterDecoderBackend&) = delete;

  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] const CwCharacterBackendDiagnostics& diagnostics() const
      noexcept;
  [[nodiscard]] bool infer(
      const cwassistant::core::CwCharacterFeatureWindow& window,
      cwassistant::core::CwCharacterHypothesis& destination) noexcept;
  void requestCancellation() noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cwassistant::desktop
