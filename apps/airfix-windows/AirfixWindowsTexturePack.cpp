#include "AirfixWindowsTexturePack.hpp"

#include "airfix/texture/PrivateTextureFileStorePlatform.hpp"

#include <atomic>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace airfix::windows {
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
  // Saturate permanently instead of wrapping and reusing an old cache/session
  // identity, even though exhaustion is not reachable in a real process.
  return 0U;
}

} // namespace

AirfixWindowsTexturePackOpenResult openAirfixWindowsTexturePack(
    const std::filesystem::path &configuredRoot,
    const std::string_view manifestRelativePath) noexcept {
  if (configuredRoot.empty() || !configuredRoot.is_absolute() ||
      manifestRelativePath.empty() || manifestRelativePath.size() > 4096U) {
    return {
        .status = AirfixWindowsTexturePackStatus::invalidConfiguration,
        .session = nullptr,
    };
  }

  const auto generation = nextGeneration();
  if (generation == 0U) {
    return {
        .status = AirfixWindowsTexturePackStatus::internalFailure,
        .session = nullptr,
    };
  }

  try {
    auto opened = texture::openPrivateTextureFileStoreLocalRoot(configuredRoot,
                                                                generation);
    if (!opened.success()) {
      return {
          .status = opened.status ==
                            texture::PrivateTextureFileStatus::invalidArgument
                        ? AirfixWindowsTexturePackStatus::invalidConfiguration
                        : AirfixWindowsTexturePackStatus::rootUnavailable,
          .session = nullptr,
      };
    }

    auto manifestRead = opened.store->readFile(
        manifestRelativePath, maximumManifestBytes, generation);
    if (!manifestRead.success()) {
      const bool invalidRelativePath =
          manifestRead.status ==
              texture::PrivateTextureFileStatus::invalidArgument ||
          manifestRead.status ==
              texture::PrivateTextureFileStatus::invalidRelativePath;
      return {
          .status = invalidRelativePath
                        ? AirfixWindowsTexturePackStatus::invalidConfiguration
                        : AirfixWindowsTexturePackStatus::manifestUnavailable,
          .session = nullptr,
      };
    }

    auto parsed = texture::parseTextureHdManifest(manifestRead.bytes);
    if (!parsed.success()) {
      return {
          .status = AirfixWindowsTexturePackStatus::manifestInvalid,
          .session = nullptr,
      };
    }

    auto index = std::make_unique<texture::TextureHdManifestIndex>(
        std::move(*parsed.index));
    auto resolver = std::make_unique<texture::TextureReplacementResolver>(
        *index, *opened.store, generation);
    auto session = std::unique_ptr<AirfixWindowsTexturePackSession>(
        new AirfixWindowsTexturePackSession(
            std::move(opened.store), std::move(index), std::move(resolver)));
    return {
        .status = AirfixWindowsTexturePackStatus::ready,
        .session = std::move(session),
    };
  } catch (const std::bad_alloc &) {
    return {
        .status = AirfixWindowsTexturePackStatus::allocationFailure,
        .session = nullptr,
    };
  } catch (...) {
    return {
        .status = AirfixWindowsTexturePackStatus::internalFailure,
        .session = nullptr,
    };
  }
}

} // namespace airfix::windows
