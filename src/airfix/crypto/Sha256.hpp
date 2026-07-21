#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace airfix::crypto {

using Sha256Digest = std::array<std::uint8_t, 32>;

class Sha256 final {
public:
    Sha256() noexcept;

    void update(std::span<const std::uint8_t> bytes);
    [[nodiscard]] Sha256Digest finish();

private:
    void transform(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t bufferSize_{};
    std::uint64_t totalBytes_{};
    bool finished_{};
};

[[nodiscard]] Sha256Digest sha256(std::span<const std::uint8_t> bytes);
[[nodiscard]] Sha256Digest sha256FileRegion(
    const std::filesystem::path& path,
    std::uint64_t offset,
    std::uint64_t size);
[[nodiscard]] std::string toHex(const Sha256Digest& digest);

} // namespace airfix::crypto
