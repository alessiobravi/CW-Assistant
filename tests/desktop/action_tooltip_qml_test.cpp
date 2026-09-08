#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

std::string readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

std::size_t lineAt(const std::string_view text, const std::size_t offset) {
  std::size_t line = 1;
  for (std::size_t index = 0; index < offset && index < text.size(); ++index)
    if (text[index] == '\n') ++line;
  return line;
}

bool audit(const std::filesystem::path& path) {
  const std::string source = readFile(path);
  if (source.empty()) {
    std::cerr << "cannot read " << path << '\n';
    return false;
  }
  bool passed = true;
  for (const std::string_view type : {"Button", "ToolButton"}) {
    const std::string needle = std::string(type) + " {";
    std::size_t position = 0;
    while ((position = source.find(needle, position)) != std::string::npos) {
      // Do not mistake the Button suffix inside TabButton for a Button.
      if (position > 0U &&
          (std::isalnum(static_cast<unsigned char>(source[position - 1U])) != 0 ||
           source[position - 1U] == '_')) {
        position += needle.size();
        continue;
      }
      const std::size_t opening = source.find('{', position);
      std::size_t depth = 0;
      std::size_t closing = opening;
      char quote = 0;
      bool escaped = false;
      for (; closing < source.size(); ++closing) {
        const char character = source[closing];
        if (quote != 0) {
          if (escaped) escaped = false;
          else if (character == '\\') escaped = true;
          else if (character == quote) quote = 0;
          continue;
        }
        if (character == '\'' || character == '"') quote = character;
        else if (character == '{') ++depth;
        else if (character == '}' && --depth == 0U) break;
      }
      const std::string_view block(source.data() + opening,
                                   closing - opening + 1U);
      if (block.find("ToolTip.") == std::string_view::npos) {
        std::cerr << path.filename().string() << ':'
                  << lineAt(source, position) << ' ' << type
                  << " has no hover explanation\n";
        passed = false;
      }
      position = closing + 1U;
    }
  }
  return passed;
}

}  // namespace

int main() {
  const std::array paths{
      std::filesystem::path(CWA_MAIN_QML_PATH),
      std::filesystem::path(CWA_SETTINGS_QML_PATH),
      std::filesystem::path(CWA_SETUP_QML_PATH),
      std::filesystem::path(CWA_PROFILE_QML_PATH),
  };
  bool passed = true;
  for (const auto& path : paths) passed = audit(path) && passed;
  return passed ? 0 : 1;
}
