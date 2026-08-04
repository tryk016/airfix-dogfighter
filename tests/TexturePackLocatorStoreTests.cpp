#include "airfix/io/DurableFile.hpp"
#include "airfix/texture/TexturePackLocator.hpp"
#include "airfix/texture/TexturePackLocatorStore.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

constexpr std::string_view currentName = "texture-pack.aftl";
constexpr std::string_view backupName = "texture-pack.aftl.backup";

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class TemporaryDirectory final {
  public:
    TemporaryDirectory() {
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0U; attempt < 100U; ++attempt) {
            root_ = std::filesystem::temp_directory_path() /
                    ("airfix-texture-locator-store-" +
                     std::to_string(stamp) + "-" +
                     std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(root_, error)) {
                directory_ = root_ / "private-store";
                return;
            }
        }
        throw std::runtime_error("cannot create locator store test root");
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& directory() const noexcept {
        return directory_;
    }

    [[nodiscard]] std::filesystem::path current() const {
        return directory_ / currentName;
    }

    [[nodiscard]] std::filesystem::path backup() const {
        return directory_ / backupName;
    }

  private:
    std::filesystem::path root_;
    std::filesystem::path directory_;
};

[[nodiscard]] airfix::texture::TexturePackLocatorRecord locator(
    const char first) {
    std::string uuid = "12345678-90ab-cdef-1234-567890abcdef";
    uuid.front() = first;
    return {
        .packageDirectoryName = "pack-" + uuid,
        .manifestRelativePath = "manifests/reviewed.jsonl",
    };
}

[[nodiscard]] std::vector<std::uint8_t> read(
    const std::filesystem::path& path) {
    return airfix::io::readBoundedRegularFile(
        path, airfix::texture::maximumTexturePackLocatorBytes);
}

void writeRaw(const std::filesystem::path& path,
              const std::span<const std::uint8_t> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output ||
        (!bytes.empty() &&
         !output.write(reinterpret_cast<const char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size())))) {
        throw std::runtime_error("cannot write locator store fixture");
    }
}

void writeU16(std::vector<std::uint8_t>& bytes,
              const std::size_t offset,
              const std::uint16_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value);
    bytes.at(offset + 1U) = static_cast<std::uint8_t>(value >> 8U);
}

void requireStoreError(
    const std::function<void()>& action,
    const airfix::texture::TexturePackLocatorStoreErrorKind expected,
    const std::filesystem::path& privatePath) {
    try {
        action();
    } catch (const airfix::texture::TexturePackLocatorStoreError& error) {
        require(error.kind() == expected,
                "locator store failed with an unexpected category");
        require(std::string(error.what()).find(privatePath.string()) ==
                    std::string::npos,
                "locator store error leaked its host path");
        return;
    }
    throw std::runtime_error("expected locator store failure");
}

void testMissingSaveRotationAndRecovery() {
    TemporaryDirectory temporary;
    const auto missing = airfix::texture::loadTexturePackLocator(
        temporary.directory());
    require(!missing.ready() && !missing.persistenceBlocked &&
                missing.current.status ==
                    airfix::texture::TexturePackLocatorFileStatus::missing,
            "missing locator store was not optional");

    const auto first = locator('1');
    const auto firstSave = airfix::texture::saveTexturePackLocator(
        temporary.directory(), first);
    require(firstSave.status ==
                airfix::texture::TexturePackLocatorSaveStatus::committed &&
                !firstSave.backupRotated,
            "first locator save had the wrong result");
    const auto firstLoad = airfix::texture::loadTexturePackLocator(
        temporary.directory());
    require(firstLoad.ready() && firstLoad.locator == first &&
                firstLoad.source ==
                    airfix::texture::TexturePackLocatorLoadSource::current,
            "saved locator did not survive restart");

    const auto unchanged = airfix::texture::saveTexturePackLocator(
        temporary.directory(), first);
    require(unchanged.status ==
                airfix::texture::TexturePackLocatorSaveStatus::unchanged &&
                !unchanged.backupRotated,
            "unchanged locator rewrote the document pair");

    const auto second = locator('2');
    const auto secondSave = airfix::texture::saveTexturePackLocator(
        temporary.directory(), second);
    require(secondSave.backupRotated,
            "changed locator did not retain its prior current record");

    auto corrupt = read(temporary.current());
    corrupt.back() ^= 0x01U;
    writeRaw(temporary.current(), corrupt);
    const auto recovered = airfix::texture::loadTexturePackLocator(
        temporary.directory());
    require(recovered.ready() && recovered.locator == first &&
                recovered.source ==
                    airfix::texture::TexturePackLocatorLoadSource::backup,
            "invalid current locator did not recover its valid backup");
}

void testMalformedReplacementAndFutureProtection() {
    TemporaryDirectory temporary;
    const auto first = locator('3');
    (void)airfix::texture::saveTexturePackLocator(temporary.directory(), first);

    const std::vector<std::uint8_t> malformed{'B', 'A', 'D', '!'};
    writeRaw(temporary.current(), malformed);
    const auto replacement = locator('4');
    const auto replaced = airfix::texture::saveTexturePackLocator(
        temporary.directory(), replacement);
    require(!replaced.backupRotated &&
                airfix::texture::loadTexturePackLocator(temporary.directory())
                        .locator == replacement,
            "malformed locator was promoted or not replaced");

    const auto previous = locator('5');
    (void)airfix::texture::saveTexturePackLocator(temporary.directory(),
                                                  previous);
    auto future = read(temporary.current());
    writeU16(future, 4U, 2U);
    writeRaw(temporary.current(), future);
    const auto loaded = airfix::texture::loadTexturePackLocator(
        temporary.directory());
    require(!loaded.ready() && loaded.persistenceBlocked &&
                loaded.current.status ==
                    airfix::texture::TexturePackLocatorFileStatus::futureSchema,
            "future locator was interpreted or downgraded to its backup");
    const auto exactFuture = read(temporary.current());
    requireStoreError(
        [&] {
            static_cast<void>(airfix::texture::saveTexturePackLocator(
                temporary.directory(), locator('6')));
        },
        airfix::texture::TexturePackLocatorStoreErrorKind::persistenceBlocked,
        temporary.directory());
    require(read(temporary.current()) == exactFuture,
            "downgrade attempt changed the future locator bytes");
}

void testInvalidAndUnsafeInputs() {
    TemporaryDirectory temporary;
    auto invalid = locator('7');
    invalid.manifestRelativePath = "../escape.jsonl";
    requireStoreError(
        [&] {
            static_cast<void>(airfix::texture::saveTexturePackLocator(
                temporary.directory(), invalid));
        },
        airfix::texture::TexturePackLocatorStoreErrorKind::invalidLocator,
        temporary.directory());

    std::filesystem::create_directory(temporary.directory());
    std::filesystem::create_directory(temporary.current());
    const auto unsafe = airfix::texture::loadTexturePackLocator(
        temporary.directory());
    require(!unsafe.ready() && unsafe.persistenceBlocked &&
                unsafe.current.status ==
                    airfix::texture::
                        TexturePackLocatorFileStatus::wrongTypeOrLinked,
            "wrong-type current locator did not block persistence");
}

} // namespace

int main() {
    try {
        testMissingSaveRotationAndRecovery();
        testMalformedReplacementAndFutureProtection();
        testInvalidAndUnsafeInputs();
        std::cout << "Texture pack locator store tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
