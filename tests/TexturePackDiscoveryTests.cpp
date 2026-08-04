#include "airfix/texture/TexturePackDiscovery.hpp"

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

namespace {

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
                    ("airfix-texture-pack-discovery-" +
                     std::to_string(stamp) + "-" +
                     std::to_string(sequence.fetch_add(1U)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
        }
        throw std::runtime_error("cannot create discovery test directory");
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
        throw std::runtime_error("cannot create discovery fixture");
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

void testUniqueReviewedManifest() {
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "imported";
    writeFile(root / "manifests" / "inventory.jsonl", "not-reviewed\n");
    writeFile(root / "private" / "selected.JSONL", syntheticManifest('1'));
    writeFile(root / "layers" / "placeholder.bin", "synthetic");

    auto discovered = airfix::texture::discoverTexturePackSession(root);
    require(discovered.success() && discovered.session->generation() != 0U &&
                discovered.manifestRelativePath == "private/selected.JSONL",
            "unique reviewed manifest was not discovered");
}

void testAbsentAndAmbiguousManifests() {
    TemporaryDirectory empty;
    const auto emptyRoot = empty.path() / "empty";
    writeFile(emptyRoot / "readme.txt", "synthetic");
    const auto absent = airfix::texture::discoverTexturePackSession(emptyRoot);
    require(absent.status ==
                airfix::texture::TexturePackDiscoveryStatus::manifestAbsent &&
                !absent.success(),
            "manifest-free package was accepted");

    TemporaryDirectory multiple;
    const auto multipleRoot = multiple.path() / "multiple";
    writeFile(multipleRoot / "one.jsonl", syntheticManifest('1'));
    writeFile(multipleRoot / "two.jsonl", syntheticManifest('4'));
    const auto ambiguous =
        airfix::texture::discoverTexturePackSession(multipleRoot);
    require(ambiguous.status ==
                airfix::texture::
                    TexturePackDiscoveryStatus::manifestAmbiguous &&
                !ambiguous.success() && ambiguous.session == nullptr &&
                ambiguous.manifestRelativePath.empty(),
            "ambiguous reviewed manifests did not fail closed");
}

void testFailureAfterCandidateRedactsState() {
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "late-failure";
    writeFile(root / "000-reviewed.jsonl", syntheticManifest('1'));
    writeFile(root / "zzz-extra.bin", "synthetic");
    const auto limited = airfix::texture::discoverTexturePackSession(
        root,
        {
            .maximumEntries = 1U,
            .maximumManifestCandidates = 1U,
            .maximumAggregateFileBytes = 1024U * 1024U,
        });
    require(limited.status ==
                    airfix::texture::
                        TexturePackDiscoveryStatus::scanLimitExceeded &&
                limited.session == nullptr &&
                limited.manifestRelativePath.empty(),
            "late discovery failure retained private candidate state");
}

void testConfigurationAndLimits() {
    const auto relative =
        airfix::texture::discoverTexturePackSession("relative-root");
    require(relative.status ==
                airfix::texture::
                    TexturePackDiscoveryStatus::invalidConfiguration,
            "relative discovery root was accepted");

    TemporaryDirectory temporary;
    const auto root = temporary.path() / "limited";
    writeFile(root / "one.txt", "1");
    writeFile(root / "two.txt", "2");
    const auto limited = airfix::texture::discoverTexturePackSession(
        root,
        {
            .maximumEntries = 1U,
            .maximumManifestCandidates = 1U,
            .maximumAggregateFileBytes = 1024U,
        });
    require(limited.status ==
                airfix::texture::
                    TexturePackDiscoveryStatus::scanLimitExceeded,
            "entry limit did not stop package discovery");
}

void testHardLinksFailClosed() {
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "linked";
    const auto first = root / "first.bin";
    const auto second = root / "second.bin";
    writeFile(first, "linked");
    std::error_code error;
    std::filesystem::create_hard_link(first, second, error);
    if (error) {
        return;
    }
    const auto discovered =
        airfix::texture::discoverTexturePackSession(root);
    require(discovered.status ==
                airfix::texture::TexturePackDiscoveryStatus::unsafeEntry,
            "multiply linked imported file was accepted");
}

} // namespace

int main() {
    try {
        testUniqueReviewedManifest();
        testAbsentAndAmbiguousManifests();
        testConfigurationAndLimits();
        testFailureAfterCandidateRedactsState();
        testHardLinksFailClosed();
        std::cout << "Texture pack discovery tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
