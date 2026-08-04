#include "airfix/texture/TexturePackSession.hpp"

#include "airfix/texture/PrivateTextureFileStorePlatform.hpp"

#include <atomic>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace airfix::texture {
namespace {

constexpr std::size_t maximumManifestBytes = 64U * 1024U * 1024U;

[[nodiscard]] std::uint64_t nextGeneration() noexcept {
    static std::atomic<std::uint64_t> generation{1U};
    auto selected = generation.load(std::memory_order_relaxed);
    while (selected != 0U &&
           selected != std::numeric_limits<std::uint64_t>::max()) {
        if (generation.compare_exchange_weak(selected, selected + 1U,
                                             std::memory_order_relaxed)) {
            return selected;
        }
    }
    // Saturate rather than wrap and reuse an old cache/session identity.
    return 0U;
}

} // namespace

TexturePackSessionOpenResult openTexturePackSession(
    const std::filesystem::path& configuredRoot,
    const std::string_view manifestRelativePath) noexcept {
    if (configuredRoot.empty() || !configuredRoot.is_absolute() ||
        manifestRelativePath.empty() || manifestRelativePath.size() > 4096U) {
        return {
            .status = TexturePackSessionStatus::invalidConfiguration,
            .session = nullptr,
        };
    }

    const auto generation = nextGeneration();
    if (generation == 0U) {
        return {
            .status = TexturePackSessionStatus::internalFailure,
            .session = nullptr,
        };
    }

    try {
        auto opened =
            openPrivateTextureFileStoreLocalRoot(configuredRoot, generation);
        if (!opened.success()) {
            return {
                .status = opened.status ==
                                  PrivateTextureFileStatus::invalidArgument
                              ? TexturePackSessionStatus::invalidConfiguration
                              : TexturePackSessionStatus::rootUnavailable,
                .session = nullptr,
            };
        }

        auto manifestRead = opened.store->readFile(
            manifestRelativePath, maximumManifestBytes, generation);
        if (!manifestRead.success()) {
            const bool invalidRelativePath =
                manifestRead.status ==
                    PrivateTextureFileStatus::invalidArgument ||
                manifestRead.status ==
                    PrivateTextureFileStatus::invalidRelativePath;
            return {
                .status = invalidRelativePath
                              ? TexturePackSessionStatus::invalidConfiguration
                              : TexturePackSessionStatus::manifestUnavailable,
                .session = nullptr,
            };
        }

        auto parsed = parseTextureHdManifest(manifestRead.bytes);
        if (!parsed.success()) {
            return {
                .status = TexturePackSessionStatus::manifestInvalid,
                .session = nullptr,
            };
        }

        auto index =
            std::make_unique<TextureHdManifestIndex>(std::move(*parsed.index));
        auto resolver = std::make_unique<TextureReplacementResolver>(
            *index, *opened.store, generation);
        auto session = std::unique_ptr<TexturePackSession>(
            new TexturePackSession(std::move(opened.store), std::move(index),
                                   std::move(resolver)));
        return {
            .status = TexturePackSessionStatus::ready,
            .session = std::move(session),
        };
    } catch (const std::bad_alloc&) {
        return {
            .status = TexturePackSessionStatus::allocationFailure,
            .session = nullptr,
        };
    } catch (...) {
        return {
            .status = TexturePackSessionStatus::internalFailure,
            .session = nullptr,
        };
    }
}

} // namespace airfix::texture
