#include "airfix/texture/PrivateTextureFileStoreInternal.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <string_view>

namespace airfix::texture::detail {
namespace {

[[nodiscard]] bool isContinuationByte(const std::uint8_t value) noexcept {
  return (value & 0xC0U) == 0x80U;
}

[[nodiscard]] bool isValidUtf8(const std::string_view text) noexcept {
  std::size_t offset = 0U;
  while (offset < text.size()) {
    const auto first = static_cast<std::uint8_t>(text[offset]);
    if (first <= 0x7FU) {
      ++offset;
      continue;
    }
    if (first >= 0xC2U && first <= 0xDFU) {
      if (offset + 1U >= text.size() ||
          !isContinuationByte(static_cast<std::uint8_t>(text[offset + 1U]))) {
        return false;
      }
      offset += 2U;
      continue;
    }
    if (first >= 0xE0U && first <= 0xEFU) {
      if (offset + 2U >= text.size()) {
        return false;
      }
      const auto second = static_cast<std::uint8_t>(text[offset + 1U]);
      const auto third = static_cast<std::uint8_t>(text[offset + 2U]);
      if (!isContinuationByte(second) || !isContinuationByte(third) ||
          (first == 0xE0U && second < 0xA0U) ||
          (first == 0xEDU && second >= 0xA0U)) {
        return false;
      }
      offset += 3U;
      continue;
    }
    if (first >= 0xF0U && first <= 0xF4U) {
      if (offset + 3U >= text.size()) {
        return false;
      }
      const auto second = static_cast<std::uint8_t>(text[offset + 1U]);
      const auto third = static_cast<std::uint8_t>(text[offset + 2U]);
      const auto fourth = static_cast<std::uint8_t>(text[offset + 3U]);
      if (!isContinuationByte(second) || !isContinuationByte(third) ||
          !isContinuationByte(fourth) || (first == 0xF0U && second < 0x90U) ||
          (first == 0xF4U && second > 0x8FU)) {
        return false;
      }
      offset += 4U;
      continue;
    }
    return false;
  }
  return true;
}

[[nodiscard]] char asciiLower(const char value) noexcept {
  if (value >= 'A' && value <= 'Z') {
    return static_cast<char>(value + ('a' - 'A'));
  }
  return value;
}

[[nodiscard]] bool
isReservedDosDeviceName(const std::string_view component) noexcept {
  const auto dot = component.find('.');
  const auto stem = component.substr(0U, dot);
  if (stem.size() == 3U) {
    std::array<char, 3U> lowered{};
    std::transform(stem.begin(), stem.end(), lowered.begin(), asciiLower);
    const std::string_view name(lowered.data(), lowered.size());
    return name == "con" || name == "prn" || name == "aux" || name == "nul";
  }
  if (stem.size() == 4U) {
    std::array<char, 4U> lowered{};
    std::transform(stem.begin(), stem.end(), lowered.begin(), asciiLower);
    const std::string_view name(lowered.data(), lowered.size());
    return ((name.substr(0U, 3U) == "com" || name.substr(0U, 3U) == "lpt") &&
            name[3U] >= '1' && name[3U] <= '9');
  }
  return false;
}

[[nodiscard]] bool
isSafeComponent(const std::string_view component,
                const PrivateTextureFileStoreLimits &limits) noexcept {
  if (component.empty() || component.size() > limits.maximumComponentBytes ||
      component == "." || component == ".." || component.back() == '.' ||
      component.back() == ' ' || isReservedDosDeviceName(component)) {
    return false;
  }
  for (const auto character : component) {
    const auto byte = static_cast<std::uint8_t>(character);
    if (byte < 0x20U || byte == 0x7FU || character == ':' || character == '<' ||
        character == '>' || character == '"' || character == '|' ||
        character == '?' || character == '*') {
      return false;
    }
  }
  return isValidUtf8(component);
}

} // namespace

bool validPrivateTextureFileStoreLimits(
    const PrivateTextureFileStoreLimits &limits) noexcept {
  return limits.maximumRelativePathBytes > 0U &&
         limits.maximumComponentBytes > 0U && limits.maximumComponents > 0U &&
         limits.maximumComponentBytes <= limits.maximumRelativePathBytes;
}

PrivateTextureRelativePath parsePrivateTextureRelativePath(
    const std::string_view path,
    const PrivateTextureFileStoreLimits &limits) noexcept {
  PrivateTextureRelativePath result;
  if (!validPrivateTextureFileStoreLimits(limits) || path.empty() ||
      path.size() > limits.maximumRelativePathBytes || path.front() == '/' ||
      path.front() == '\\' || path.back() == '/' || path.back() == '\\') {
    return result;
  }

  try {
    result.components.reserve(std::min(path.size(), limits.maximumComponents));
    std::size_t begin = 0U;
    for (std::size_t index = 0U; index <= path.size(); ++index) {
      const bool atEnd = index == path.size();
      if (!atEnd && path[index] != '/' && path[index] != '\\') {
        continue;
      }
      if (result.components.size() >= limits.maximumComponents) {
        return result;
      }
      const auto component = path.substr(begin, index - begin);
      if (!isSafeComponent(component, limits)) {
        return result;
      }
      result.components.emplace_back(component);
      begin = index + 1U;
    }
  } catch (const std::bad_alloc &) {
    result.status = PrivateTextureFileStatus::ioFailure;
    result.components.clear();
    return result;
  } catch (...) {
    result.status = PrivateTextureFileStatus::ioFailure;
    result.components.clear();
    return result;
  }

  if (result.components.empty()) {
    return result;
  }
  result.status = PrivateTextureFileStatus::ready;
  return result;
}

} // namespace airfix::texture::detail
