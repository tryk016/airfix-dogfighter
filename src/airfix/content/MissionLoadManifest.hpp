#pragma once

#include "airfix/assets/AfChunkContainer.hpp"
#include "airfix/assets/MissionEntryResolver.hpp"
#include "airfix/content/VerifiedContentSession.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace airfix::content {

struct MissionLoadManifestRequest {
    std::string levelLogicalPath;
};

struct MissionLoadManifestLimits {
    assets::DefinitionParseLimits level{};
    assets::DefinitionParseLimits world{};
    assets::MissionEntryResolutionLimits entries{};

    std::size_t maximumLevelSourceBytes{64U * 1024U * 1024U};
    std::size_t maximumWorldSourceBytes{64U * 1024U * 1024U};
    std::size_t maximumObjectDefinitionSourceBytes{64U * 1024U * 1024U};
    std::uint64_t maximumTotalDefinitionSourceBytes{
        512U * 1024U * 1024U};
    std::size_t maximumCcfSourceBytes{256U * 1024U * 1024U};
    std::uint64_t maximumTotalCcfSourceBytes{1024U * 1024U * 1024U};
    std::size_t maximumCcfSources{65'536U};
    std::uint64_t maximumPublishedCpuBytes{512U * 1024U * 1024U};
};

enum class MissionLoadManifestPhase : std::uint8_t {
    validatingRequest,
    lookingUpLevel,
    readingLevel,
    parsingLevel,
    resolvingMissionEntries,
    readingWorld,
    parsingWorld,
    readingObjectDefinitions,
    resolvingCcfSources,
    complete,
};

struct MissionLoadManifestProgress {
    MissionLoadManifestPhase phase{};
    std::size_t completedItems{};
    std::size_t totalItems{};
};

using MissionLoadManifestProgressCallback =
    std::function<void(const MissionLoadManifestProgress&)>;

enum class MissionLoadDependencyKind : std::uint8_t {
    level,
    world,
    objectDefinition,
    modelDefinition,
    mainWorldCcf,
    backdropCcf,
    objectCcf,
};

enum class MissionLoadManifestIssueKind : std::uint8_t {
    cancelled,
    invalidLimits,
    missingLogicalPath,
    invalidLogicalPath,
    notFound,
    ambiguous,
    sourceLimitExceeded,
    placementLimitExceeded,
    aggregateDefinitionSourceLimitExceeded,
    readFailure,
    parseFailure,
    objectDefinitionKindMismatch,
    ccfSourceCountLimitExceeded,
    aggregateCcfSourceLimitExceeded,
    publishedCpuLimitExceeded,
    sessionIdentityChanged,
    integerOverflow,
    allocationFailure,
    progressCallbackFailure,
    internalFailure,
};

struct MissionLoadManifestIssue {
    MissionLoadManifestIssueKind kind{
        MissionLoadManifestIssueKind::internalFailure};
    MissionLoadDependencyKind dependency{MissionLoadDependencyKind::level};
    std::optional<std::size_t> dependencyIndex;
    std::optional<std::size_t> sourceFileIndex;
};

struct MissionArchiveEntryIdentity {
    // Canonical spelling retained from the authenticated UDSP directory.
    std::string logicalPath;
    std::size_t archiveFileIndex{};

    friend bool operator==(
        const MissionArchiveEntryIdentity&,
        const MissionArchiveEntryIdentity&) = default;
};

struct MissionUniqueObjectDefinition {
    MissionArchiveEntryIdentity source;
    assets::ObjectDefinition definition;
};

enum class MissionCcfLoadRole : std::uint8_t {
    mainWorld,
    backdrop,
    objectPlacement,
};

struct MissionCcfLoadDescriptor {
    MissionCcfLoadRole role{MissionCcfLoadRole::mainWorld};
    // Dense legacy load order; always equal to this descriptor's vector index.
    std::size_t sourceIndex{};
    MissionArchiveEntryIdentity source;
    std::optional<std::string> textureRoot;
    std::uint32_t legacyLoadFlags{};
    bool roomSectionEnabled{true};
    bool copyPrimaryNameToRoot{};
    bool placedSceneEnabled{true};
    std::optional<std::size_t> objectPlacementIndex;
    std::optional<std::size_t> uniqueObjectDefinitionIndex;
    // storedSize + unpackedSize for compressed entries, otherwise storedSize.
    std::uint64_t sourceAllocationFootprintBytes{};

    friend bool operator==(
        const MissionCcfLoadDescriptor&,
        const MissionCcfLoadDescriptor&) = default;
};

class MissionLoadManifest;
struct MissionLoadManifestResult;

class MissionLoadManifest final {
public:
    MissionLoadManifest(const MissionLoadManifest&) = delete;
    MissionLoadManifest& operator=(const MissionLoadManifest&) = delete;
    MissionLoadManifest(MissionLoadManifest&& other) noexcept;
    MissionLoadManifest& operator=(MissionLoadManifest&& other) noexcept;
    ~MissionLoadManifest() = default;

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] const ContentRevision& revision() const noexcept {
        return revision_;
    }
    [[nodiscard]] bool belongsTo(
        const VerifiedContentSession& session) const noexcept {
        return valid_ && revision_ == session.revision();
    }
    [[nodiscard]] const MissionArchiveEntryIdentity& levelEntry()
        const noexcept {
        return levelEntry_;
    }
    [[nodiscard]] const MissionArchiveEntryIdentity& worldEntry()
        const noexcept {
        return worldEntry_;
    }
    [[nodiscard]] const assets::LevelDefinition& level() const noexcept {
        return level_;
    }
    [[nodiscard]] const assets::WorldDefinition& world() const noexcept {
        return world_;
    }
    [[nodiscard]] const std::vector<MissionArchiveEntryIdentity>&
    objectEntries() const noexcept {
        return objectEntries_;
    }
    [[nodiscard]] const std::vector<MissionArchiveEntryIdentity>&
    modelEntries() const noexcept {
        return modelEntries_;
    }
    [[nodiscard]] const std::vector<std::size_t>&
    objectDefinitionIndices() const noexcept {
        return objectDefinitionIndices_;
    }
    [[nodiscard]] const std::vector<MissionUniqueObjectDefinition>&
    uniqueObjectDefinitions() const noexcept {
        return uniqueObjectDefinitions_;
    }
    [[nodiscard]] const std::vector<MissionCcfLoadDescriptor>&
    ccfLoads() const noexcept {
        return ccfLoads_;
    }
    [[nodiscard]] std::uint64_t definitionSourceFootprintBytes()
        const noexcept {
        return definitionSourceFootprintBytes_;
    }
    [[nodiscard]] std::uint64_t plannedCcfSourceFootprintBytes()
        const noexcept {
        return plannedCcfSourceFootprintBytes_;
    }
    [[nodiscard]] std::uint64_t publishedCpuBytes() const noexcept {
        return publishedCpuBytes_;
    }

private:
    MissionLoadManifest() = default;

    [[nodiscard]] std::optional<std::uint64_t>
    calculatePublishedCpuBytes() const noexcept;

    friend MissionLoadManifestResult buildMissionLoadManifest(
        VerifiedContentSession&,
        const MissionLoadManifestRequest&,
        const MissionLoadManifestLimits&,
        std::stop_token,
        MissionLoadManifestProgressCallback);

    ContentRevision revision_;
    MissionArchiveEntryIdentity levelEntry_;
    MissionArchiveEntryIdentity worldEntry_;
    assets::LevelDefinition level_;
    assets::WorldDefinition world_;
    std::vector<MissionArchiveEntryIdentity> objectEntries_;
    std::vector<MissionArchiveEntryIdentity> modelEntries_;
    std::vector<std::size_t> objectDefinitionIndices_;
    std::vector<MissionUniqueObjectDefinition> uniqueObjectDefinitions_;
    std::vector<MissionCcfLoadDescriptor> ccfLoads_;
    std::uint64_t definitionSourceFootprintBytes_{};
    std::uint64_t plannedCcfSourceFootprintBytes_{};
    std::uint64_t publishedCpuBytes_{};
    bool valid_{};
};

struct MissionLoadManifestResult {
    // Published only after the complete authenticated metadata transaction.
    std::optional<MissionLoadManifest> manifest;
    std::vector<MissionLoadManifestIssue> issues;

    [[nodiscard]] bool success() const noexcept;
};

// Authenticates only the mission dependency graph and planned CCF source
// identities. Level, World, and each unique Level OBJE definition are read
// through session's already-authenticated handle. CCF/GTI payloads are never
// read here. Level MODL references are resolved as metadata but are neither
// parsed nor promoted to mission-root CCF loads.
[[nodiscard]] MissionLoadManifestResult buildMissionLoadManifest(
    VerifiedContentSession& session,
    const MissionLoadManifestRequest& request,
    const MissionLoadManifestLimits& limits = {},
    std::stop_token stopToken = {},
    MissionLoadManifestProgressCallback progress = {});

inline bool MissionLoadManifestResult::success() const noexcept {
    return manifest.has_value() && manifest->valid() && issues.empty();
}

} // namespace airfix::content
