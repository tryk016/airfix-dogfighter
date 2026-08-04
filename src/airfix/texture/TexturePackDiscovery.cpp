#include "airfix/texture/TexturePackDiscovery.hpp"

#include "airfix/texture/PrivateTextureFileStoreInternal.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace airfix::texture {
namespace {

[[nodiscard]] bool validLimits(
    const TexturePackDiscoveryLimits& limits) noexcept {
    return limits.maximumEntries > 0U &&
           limits.maximumManifestCandidates > 0U &&
           limits.maximumManifestCandidates <= limits.maximumEntries &&
           limits.maximumAggregateFileBytes > 0U;
}

[[nodiscard]] bool isJsonLinesFile(
    const std::filesystem::path& path) noexcept {
    const auto extension = path.extension().string();
    if (extension.size() != 6U) {
        return false;
    }
    constexpr std::string_view expected = ".jsonl";
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        char value = extension[index];
        if (value >= 'A' && value <= 'Z') {
            value = static_cast<char>(value + ('a' - 'A'));
        }
        if (value != expected[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::string> relativeCandidate(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) {
    const auto relative = candidate.lexically_relative(root).generic_string();
    if (relative.empty()) {
        return std::nullopt;
    }
    PrivateTextureFileStoreLimits limits;
    if (!detail::parsePrivateTextureRelativePath(relative, limits).valid()) {
        return std::nullopt;
    }
    return relative;
}

void setFailure(TexturePackDiscoveryResult& result,
                const TexturePackDiscoveryStatus status) noexcept {
    result.session.reset();
    result.manifestRelativePath.clear();
    result.status = status;
}

} // namespace

TexturePackDiscoveryResult discoverTexturePackSession(
    const std::filesystem::path& importedRoot,
    const TexturePackDiscoveryLimits& limits) noexcept {
    TexturePackDiscoveryResult result;
    if (importedRoot.empty() || !importedRoot.is_absolute() ||
        !validLimits(limits)) {
        setFailure(result,
                   TexturePackDiscoveryStatus::invalidConfiguration);
        return result;
    }

    try {
        std::error_code error;
        const auto rootStatus = std::filesystem::symlink_status(importedRoot,
                                                                 error);
        if (error) {
            setFailure(result, TexturePackDiscoveryStatus::rootUnavailable);
            return result;
        }
        if (!std::filesystem::is_directory(rootStatus) ||
            std::filesystem::is_symlink(rootStatus)) {
            setFailure(result, TexturePackDiscoveryStatus::unsafeEntry);
            return result;
        }

        std::filesystem::recursive_directory_iterator iterator(
            importedRoot, std::filesystem::directory_options::none, error);
        const std::filesystem::recursive_directory_iterator end;
        if (error) {
            setFailure(result, TexturePackDiscoveryStatus::rootUnavailable);
            return result;
        }

        std::size_t entries = 0U;
        std::size_t candidates = 0U;
        std::uint64_t aggregateBytes = 0U;
        while (iterator != end) {
            if (++entries > limits.maximumEntries) {
                setFailure(
                    result,
                    TexturePackDiscoveryStatus::scanLimitExceeded);
                return result;
            }
            const auto path = iterator->path();
            const auto status = std::filesystem::symlink_status(path, error);
            if (error || std::filesystem::is_symlink(status)) {
                setFailure(result, TexturePackDiscoveryStatus::unsafeEntry);
                return result;
            }
            if (std::filesystem::is_directory(status)) {
                iterator.increment(error);
                if (error) {
                    setFailure(result,
                               TexturePackDiscoveryStatus::unsafeEntry);
                    return result;
                }
                continue;
            }
            if (!std::filesystem::is_regular_file(status)) {
                setFailure(result, TexturePackDiscoveryStatus::unsafeEntry);
                return result;
            }
            const auto links = std::filesystem::hard_link_count(path, error);
            if (error || links != 1U) {
                setFailure(result, TexturePackDiscoveryStatus::unsafeEntry);
                return result;
            }
            const auto size = std::filesystem::file_size(path, error);
            if (error || size > limits.maximumAggregateFileBytes ||
                aggregateBytes > limits.maximumAggregateFileBytes - size) {
                setFailure(
                    result,
                    TexturePackDiscoveryStatus::scanLimitExceeded);
                return result;
            }
            aggregateBytes += size;

            if (isJsonLinesFile(path)) {
                if (++candidates > limits.maximumManifestCandidates) {
                    setFailure(
                        result,
                        TexturePackDiscoveryStatus::scanLimitExceeded);
                    return result;
                }
                const auto relative = relativeCandidate(importedRoot, path);
                if (!relative.has_value()) {
                    setFailure(result,
                               TexturePackDiscoveryStatus::unsafeEntry);
                    return result;
                }
                auto opened =
                    openTexturePackSession(importedRoot, *relative);
                if (opened.success()) {
                    if (result.session != nullptr) {
                        setFailure(
                            result,
                            TexturePackDiscoveryStatus::manifestAmbiguous);
                        return result;
                    }
                    result.session = std::move(opened.session);
                    result.manifestRelativePath = *relative;
                } else if (opened.status ==
                               TexturePackSessionStatus::allocationFailure) {
                    setFailure(
                        result,
                        TexturePackDiscoveryStatus::allocationFailure);
                    return result;
                } else if (opened.status ==
                               TexturePackSessionStatus::internalFailure) {
                    setFailure(result,
                               TexturePackDiscoveryStatus::internalFailure);
                    return result;
                }
            }

            iterator.increment(error);
            if (error) {
                setFailure(result, TexturePackDiscoveryStatus::unsafeEntry);
                return result;
            }
        }

        if (result.session == nullptr) {
            setFailure(result, TexturePackDiscoveryStatus::manifestAbsent);
            return result;
        }
        result.status = TexturePackDiscoveryStatus::ready;
        return result;
    } catch (const std::bad_alloc&) {
        setFailure(result, TexturePackDiscoveryStatus::allocationFailure);
        return result;
    } catch (...) {
        setFailure(result, TexturePackDiscoveryStatus::internalFailure);
        return result;
    }
}

} // namespace airfix::texture
