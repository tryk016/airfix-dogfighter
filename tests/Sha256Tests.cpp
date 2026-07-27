#include "airfix/crypto/Sha256.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::span<const std::uint8_t> bytes(const std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testKnownVectors() {
    require(
        airfix::crypto::toHex(airfix::crypto::sha256(bytes(""))) ==
            "e3b0c44298fc1c149afbf4c8996fb924"
            "27ae41e4649b934ca495991b7852b855",
        "empty SHA-256 vector mismatch");
    require(
        airfix::crypto::toHex(airfix::crypto::sha256(bytes("abc"))) ==
            "ba7816bf8f01cfea414140de5dae2223"
            "b00361a396177a9cb410ff61f20015ad",
        "abc SHA-256 vector mismatch");
    require(
        airfix::crypto::toHex(airfix::crypto::sha256(bytes(
            "abcdbcdecdefdefgefghfghighijhijk"
            "ijkljklmklmnlmnomnopnopq"))) ==
            "248d6a61d20638b8e5c026930c3e6039"
            "a33ce45964ff2167f6ecedd419db06c1",
        "multi-block SHA-256 vector mismatch");
}

void testIncrementalUpdates() {
    std::vector<std::uint8_t> data(1'000'000U, static_cast<std::uint8_t>('a'));
    airfix::crypto::Sha256 hash;
    std::size_t offset = 0U;
    while (offset < data.size()) {
        const auto size = std::min<std::size_t>(7919U, data.size() - offset);
        hash.update(std::span<const std::uint8_t>(data).subspan(offset, size));
        offset += size;
    }
    require(
        airfix::crypto::toHex(hash.finish()) ==
            "cdc76e5c9914fb9281a1c7e284d73e67"
            "f1809a48a497200e046d39ccc7112cd0",
        "incremental million-a SHA-256 vector mismatch");
}

void testLifecycleErrors() {
    airfix::crypto::Sha256 hash;
    (void)hash.finish();
    try {
        hash.update(bytes("x"));
    }
    catch (const std::logic_error&) {
        return;
    }
    throw std::runtime_error("expected update-after-finish error");
}

} // namespace

int main() {
    try {
        testKnownVectors();
        testIncrementalUpdates();
        testLifecycleErrors();
        std::cout << "all SHA-256 tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "SHA-256 test failure: " << error.what() << '\n';
        return 1;
    }
}
