#include "airfix/io/DurableFile.hpp"
#include "airfix/texture/TexturePackInstallation.hpp"
#include "airfix/texture/TexturePackLocator.hpp"
#include "airfix/texture/TexturePackLocatorStore.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

constexpr std::string_view firstPackage =
    "pack-11111111-1111-1111-1111-111111111111";
constexpr std::string_view secondPackage =
    "pack-22222222-2222-2222-2222-222222222222";

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class TemporaryDirectory final {
  public:
    TemporaryDirectory() {
        static std::atomic<std::uint64_t> sequence{};
        const auto stamp = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        for (std::uint64_t attempt = 0U; attempt < 100U; ++attempt) {
            path_ = std::filesystem::temp_directory_path() /
                    ("airfix-texture-pack-installation-" +
                     std::to_string(stamp) + "-" +
                     std::to_string(sequence.fetch_add(1U)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
        }
        throw std::runtime_error("cannot create installation test root");
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

void writeFile(const std::filesystem::path& path,
               const std::string_view bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("cannot create installation fixture");
    }
}

[[nodiscard]] std::string digest(const char digit) {
    return std::string(64U, digit);
}

[[nodiscard]] std::string syntheticManifest(const char sourceDigit) {
    std::ostringstream json;
    json << '{' << "\"record_type\":\"hd-reviewed-corpus\","
         << "\"schema_version\":1,"
         << "\"review_run_id\":\"synthetic-review\","
         << "\"source_corpus_id\":\"" << digest('a') << "\","
         << "\"base_manifest_sha256\":\"" << digest('b') << "\","
         << "\"base_pipeline_version\":\"synthetic-v1\","
         << "\"override_sources\":[{\"sha256\":\"" << digest('c')
         << "\",\"pipeline_version\":\"synthetic-v1\","
         << "\"result_count\":1}],"
         << "\"unique_result_count\":1,"
         << "\"logical_texture_count\":1,"
         << "\"status_counts\":{\"accepted\":1,\"rejected\":0,"
         << "\"manual-review\":0},"
         << "\"review_basis\":\"synthetic fixture\","
         << "\"local_only\":true}\n";
    json << '{' << "\"schema_version\":1,"
         << "\"pipeline_version\":\"synthetic-v1\","
         << "\"run_id\":\"synthetic-run\","
         << "\"source_corpus_id\":\"" << digest('a') << "\","
         << "\"source_gti_sha256\":\"" << digest(sourceDigit) << "\","
         << "\"input_png_sha256\":\"" << digest('2') << "\","
         << "\"output_png_sha256\":\"" << digest('3') << "\","
         << "\"logical_paths\":[\"Textures/Synthetic.gti\"],"
         << "\"categories\":[\"ui-fonts\"],"
         << "\"category\":\"ui-fonts\","
         << "\"role\":\"synthetic-role\","
         << "\"method\":\"synthetic-method\","
         << "\"model\":null,\"model_provenance\":null,"
         << "\"parameters\":{\"scale\":4,\"edge_mode\":\"clamp\","
         << "\"rgb_resampler\":\"lanczos\","
         << "\"alpha_resampler\":\"bicubic\","
         << "\"transparent_rgb_extension\":true,"
         << "\"sample_space\":\"encoded-unclassified\"},"
         << "\"source\":{\"width\":1,\"height\":1,"
         << "\"alpha_usage\":\"opaque\",\"mipmaps\":1},"
         << "\"result\":{\"width\":4,\"height\":4,"
         << "\"generated_mipmaps\":3,"
         << "\"path\":\"synthetic/base.png\","
         << "\"mipmap_directory\":\"synthetic/mips\","
         << "\"comparison_board\":\"synthetic/board.png\"},"
         << "\"qa\":{},\"status\":\"accepted\","
         << "\"automated_status\":\"manual-review\","
         << "\"review\":{\"status\":\"accepted\","
         << "\"basis\":\"synthetic explicit review\","
         << "\"review_run_id\":\"synthetic-review\","
         << "\"used_override\":false}}\n";
    return json.str();
}

void makeImport(const std::filesystem::path& path, const char sourceDigit) {
    writeFile(path / "manifests" / "reviewed.jsonl",
              syntheticManifest(sourceDigit));
    writeFile(path / "layers" / "placeholder.bin", "synthetic");
}

void testInstallInspectAndReplace() {
    TemporaryDirectory temporary;
    const auto storage = temporary.path() / "storage";
    const auto firstImport = temporary.path() / "first-import";
    makeImport(firstImport, '1');

    auto first = airfix::texture::installImportedTexturePack(
        storage, firstImport, firstPackage);
    require(first.success() && !std::filesystem::exists(firstImport) &&
                first.locator->packageDirectoryName == firstPackage,
            "valid owner copy was not installed atomically");

    auto restart = airfix::texture::inspectInstalledTexturePack(storage);
    require(restart.success() && restart.locator == first.locator &&
                restart.session->generation() != first.session->generation(),
            "installed texture package did not survive restart inspection");

    first.session.reset();
    restart.session.reset();
    const auto secondImport = temporary.path() / "second-import";
    makeImport(secondImport, '4');
    auto second = airfix::texture::installImportedTexturePack(
        storage, secondImport, secondPackage);
    require(second.success() &&
                second.locator->packageDirectoryName == secondPackage,
            "replacement texture package was not published");
    const auto current = airfix::texture::inspectInstalledTexturePack(storage);
    require(current.success() && current.locator == second.locator,
            "replacement locator was not the active package");
}

void testInvalidImportsFailClosed() {
    TemporaryDirectory temporary;
    const auto storage = temporary.path() / "storage";
    const auto absent = temporary.path() / "absent";
    writeFile(absent / "readme.txt", "synthetic");
    const auto absentResult = airfix::texture::installImportedTexturePack(
        storage, absent, firstPackage);
    require(absentResult.status ==
                airfix::texture::TexturePackInstallStatus::manifestAbsent &&
                !airfix::texture::inspectInstalledTexturePack(storage).success(),
            "manifest-free owner copy became active");

    const auto ambiguous = temporary.path() / "ambiguous";
    writeFile(ambiguous / "one.jsonl", syntheticManifest('1'));
    writeFile(ambiguous / "two.jsonl", syntheticManifest('4'));
    const auto ambiguousResult = airfix::texture::installImportedTexturePack(
        storage, ambiguous, secondPackage);
    require(ambiguousResult.status ==
                airfix::texture::
                    TexturePackInstallStatus::manifestAmbiguous &&
                !airfix::texture::inspectInstalledTexturePack(storage).success(),
            "ambiguous owner copy became active");

    const auto invalidNameSource = temporary.path() / "invalid-name";
    makeImport(invalidNameSource, '5');
    const auto invalidName = airfix::texture::installImportedTexturePack(
        storage, invalidNameSource, "pack-../../escape");
    require(invalidName.status ==
                airfix::texture::
                    TexturePackInstallStatus::invalidConfiguration &&
                std::filesystem::exists(invalidNameSource),
            "invalid destination identity consumed the source copy");
}

void testFutureLocatorBlocksReplacement() {
    TemporaryDirectory temporary;
    const auto storage = temporary.path() / "storage";
    const auto firstImport = temporary.path() / "first";
    makeImport(firstImport, '1');
    auto first = airfix::texture::installImportedTexturePack(
        storage, firstImport, firstPackage);
    require(first.success(), "future-protection setup failed");
    first.session.reset();

    const auto locatorPath = storage / "texture-pack.aftl";
    auto future = airfix::io::readBoundedRegularFile(
        locatorPath, airfix::texture::maximumTexturePackLocatorBytes);
    future[4] = 2U;
    future[5] = 0U;
    std::ofstream output(locatorPath, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(future.data()),
                 static_cast<std::streamsize>(future.size()));
    output.close();

    const auto secondImport = temporary.path() / "second";
    makeImport(secondImport, '4');
    const auto blocked = airfix::texture::installImportedTexturePack(
        storage, secondImport, secondPackage);
    const auto rejectedPackageExists =
        std::filesystem::exists(storage / "packages" / secondPackage);
    require(blocked.status ==
                    airfix::texture::
                        TexturePackInstallStatus::persistenceBlocked &&
                !rejectedPackageExists,
            "future locator replacement failed closed (status=" +
                std::to_string(static_cast<unsigned>(blocked.status)) +
                ", retained=" +
                std::to_string(static_cast<unsigned>(rejectedPackageExists)) +
                ")");
}

} // namespace

int main() {
    try {
        testInstallInspectAndReplace();
        testInvalidImportsFailClosed();
        testFutureLocatorBlocksReplacement();
        std::cout << "Texture pack installation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
