#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace cwassistant::core {

class CallsignPolicy {
 public:
  // Initial policy deliberately supports exact callsigns only. Pattern and
  // prefix rules can hide unintended stations and require a separate design.
  [[nodiscard]] bool add_ignored(std::string_view callsign);
  [[nodiscard]] bool remove_ignored(std::string_view callsign);
  [[nodiscard]] bool is_ignored(std::string_view callsign) const;
  [[nodiscard]] std::vector<std::string> ignored_callsigns() const;

  [[nodiscard]] static std::optional<std::string> normalize(
      std::string_view callsign);

 private:
  std::unordered_set<std::string> ignored_;
};

}  // namespace cwassistant::core
