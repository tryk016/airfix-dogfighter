#include "airfix/texture/TexturePackSession.hpp"

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

using airfix::texture::TexturePackSessionStatus;

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
                    ("airfix-texture-pack-session-tests-" +
                     std::to_string(stamp) + "-" +
                     std::to_string(sequence.fetch_add(1U)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
        }
        throw std::runtime_error("cannot create temporary test directory");
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

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
        throw std::runtime_error("cannot create synthetic file fixture");
    }
}

[[nodiscard]] std::string digest(const char digit) {
    return std::string(64U, digit);
}

[[nodiscard]] std::string syntheticManifest() {
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
         << "\"source_gti_sha256\":\"" << digest('1') << "\","
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

void testValidSessionAndFixedFailures() {
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "root";
    writeFile(root / "manifests" / "reviewed.jsonl", syntheticManifest());

    auto opened = airfix::texture::openTexturePackSession(
        root, "manifests/reviewed.jsonl");
    require(opened.success(), "valid synthetic texture package was rejected");
    require(opened.session->generation() != 0U &&
                opened.session->generation() ==
                    opened.session->files().generation() &&
                opened.session->generation() ==
                    opened.session->resolver().generation(),
            "texture package generation was not shared by its capabilities");

    const auto second = airfix::texture::openTexturePackSession(
        root, "manifests/reviewed.jsonl");
    require(second.success() &&
                second.session->generation() != opened.session->generation(),
            "separate texture package sessions reused a generation");

    const auto missing =
        airfix::texture::openTexturePackSession(root, "missing.jsonl");
    require(!missing.success() &&
                missing.status == TexturePackSessionStatus::manifestUnavailable,
            "missing manifest did not fail with a fixed category");

    writeFile(root / "invalid.jsonl", "not-json\n");
    const auto invalid =
        airfix::texture::openTexturePackSession(root, "invalid.jsonl");
    require(!invalid.success() &&
                invalid.status == TexturePackSessionStatus::manifestInvalid,
            "malformed manifest did not fail with a fixed category");

    const auto unsafe = airfix::texture::openTexturePackSession(
        root, "../reviewed.jsonl");
    require(!unsafe.success() &&
                unsafe.status ==
                    TexturePackSessionStatus::invalidConfiguration,
            "escaping manifest path did not fail closed");

    const auto relative = airfix::texture::openTexturePackSession(
        "relative-root", "reviewed.jsonl");
    require(!relative.success() &&
                relative.status ==
                    TexturePackSessionStatus::invalidConfiguration,
            "relative private root did not fail closed");
}

} // namespace

int main() {
    try {
        testValidSessionAndFixedFailures();
        std::cout << "Texture pack session tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
