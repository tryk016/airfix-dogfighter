#include "airfix/crypto/Sha256.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace airfix::crypto {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
    0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
    0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
    0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
    0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
    0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
    0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
    0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
    0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
    0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
    0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U,
    0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
    0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U,
    0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
    0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
    0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U,
};

[[nodiscard]] constexpr std::uint32_t rotateRight(
    const std::uint32_t value,
    const unsigned count) noexcept {
    return (value >> count) | (value << (32U - count));
}

} // namespace

Sha256::Sha256() noexcept
    : state_{
        0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
        0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U,
    } {}

void Sha256::update(const std::span<const std::uint8_t> bytes) {
    if (finished_) {
        throw std::logic_error("SHA-256 update after finish");
    }
    if (bytes.size() > std::numeric_limits<std::uint64_t>::max() - totalBytes_) {
        throw std::length_error("SHA-256 input length overflows");
    }
    totalBytes_ += bytes.size();

    std::size_t inputOffset = 0U;
    if (bufferSize_ != 0U) {
        const auto copied = std::min(buffer_.size() - bufferSize_, bytes.size());
        std::copy_n(bytes.begin(), copied, buffer_.begin() +
            static_cast<std::ptrdiff_t>(bufferSize_));
        bufferSize_ += copied;
        inputOffset += copied;
        if (bufferSize_ == buffer_.size()) {
            transform(buffer_.data());
            bufferSize_ = 0U;
        }
    }

    while (bytes.size() - inputOffset >= buffer_.size()) {
        transform(bytes.data() + inputOffset);
        inputOffset += buffer_.size();
    }

    const auto remaining = bytes.size() - inputOffset;
    if (remaining != 0U) {
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(inputOffset),
            remaining, buffer_.begin());
        bufferSize_ = remaining;
    }
}

Sha256Digest Sha256::finish() {
    if (finished_) {
        throw std::logic_error("SHA-256 finish called twice");
    }
    if (totalBytes_ > std::numeric_limits<std::uint64_t>::max() / 8U) {
        throw std::length_error("SHA-256 bit length overflows");
    }

    const auto bitLength = totalBytes_ * 8U;
    buffer_[bufferSize_++] = 0x80U;
    if (bufferSize_ > 56U) {
        std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(bufferSize_),
            buffer_.end(), 0U);
        transform(buffer_.data());
        bufferSize_ = 0U;
    }
    std::fill(
        buffer_.begin() + static_cast<std::ptrdiff_t>(bufferSize_),
        buffer_.begin() + 56,
        0U);
    for (std::size_t byte = 0U; byte < 8U; ++byte) {
        buffer_[56U + byte] = static_cast<std::uint8_t>(
            bitLength >> ((7U - byte) * 8U));
    }
    transform(buffer_.data());

    Sha256Digest digest{};
    for (std::size_t word = 0U; word < state_.size(); ++word) {
        for (std::size_t byte = 0U; byte < 4U; ++byte) {
            digest[word * 4U + byte] = static_cast<std::uint8_t>(
                state_[word] >> ((3U - byte) * 8U));
        }
    }
    finished_ = true;
    return digest;
}

void Sha256::transform(const std::uint8_t* const block) noexcept {
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t index = 0U; index < 16U; ++index) {
        const auto offset = index * 4U;
        schedule[index] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
            (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
            (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) |
            static_cast<std::uint32_t>(block[offset + 3U]);
    }
    for (std::size_t index = 16U; index < schedule.size(); ++index) {
        const auto s0 = rotateRight(schedule[index - 15U], 7U) ^
            rotateRight(schedule[index - 15U], 18U) ^
            (schedule[index - 15U] >> 3U);
        const auto s1 = rotateRight(schedule[index - 2U], 17U) ^
            rotateRight(schedule[index - 2U], 19U) ^
            (schedule[index - 2U] >> 10U);
        schedule[index] = schedule[index - 16U] + s0 +
            schedule[index - 7U] + s1;
    }

    auto a = state_[0];
    auto b = state_[1];
    auto c = state_[2];
    auto d = state_[3];
    auto e = state_[4];
    auto f = state_[5];
    auto g = state_[6];
    auto h = state_[7];

    for (std::size_t index = 0U; index < schedule.size(); ++index) {
        const auto sum1 = rotateRight(e, 6U) ^ rotateRight(e, 11U) ^ rotateRight(e, 25U);
        const auto choose = (e & f) ^ (~e & g);
        const auto temp1 = h + sum1 + choose + kRoundConstants[index] + schedule[index];
        const auto sum0 = rotateRight(a, 2U) ^ rotateRight(a, 13U) ^ rotateRight(a, 22U);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto temp2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

Sha256Digest sha256(const std::span<const std::uint8_t> bytes) {
    Sha256 hash;
    hash.update(bytes);
    return hash.finish();
}

Sha256Digest sha256FileRegion(
    const std::filesystem::path& path,
    const std::uint64_t offset,
    const std::uint64_t size) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("cannot open file for SHA-256: " + path.string());
    }
    const auto end = input.tellg();
    if (end < 0) {
        throw std::runtime_error("cannot determine file size for SHA-256: " + path.string());
    }
    const auto physicalSize = static_cast<std::uint64_t>(end);
    if (offset > physicalSize || size > physicalSize - offset ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        throw std::runtime_error("SHA-256 region is outside file: " + path.string());
    }
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) {
        throw std::runtime_error("cannot seek file for SHA-256: " + path.string());
    }

    constexpr std::size_t kBufferSize = 1024U * 1024U;
    std::array<std::uint8_t, kBufferSize> buffer{};
    Sha256 hash;
    auto remaining = size;
    while (remaining != 0U) {
        const auto chunkSize = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, buffer.size()));
        if (!input.read(reinterpret_cast<char*>(buffer.data()),
                static_cast<std::streamsize>(chunkSize))) {
            throw std::runtime_error("cannot read file for SHA-256: " + path.string());
        }
        hash.update(std::span<const std::uint8_t>(buffer).first(chunkSize));
        remaining -= chunkSize;
    }
    return hash.finish();
}

std::string toHex(const Sha256Digest& digest) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(digest.size() * 2U, '0');
    for (std::size_t index = 0U; index < digest.size(); ++index) {
        result[index * 2U] = digits[digest[index] >> 4U];
        result[index * 2U + 1U] = digits[digest[index] & 0x0FU];
    }
    return result;
}

} // namespace airfix::crypto
