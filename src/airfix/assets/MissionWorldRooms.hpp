#pragma once

#include "airfix/assets/LegacyFormats.hpp"
#include "airfix/assets/MissionSetup.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace airfix::assets {

// CcName distinguishes a null component from a present empty string.
struct CcNameState {
    std::optional<std::string> name;
    std::optional<std::string> prefix;

    friend bool operator==(const CcNameState&, const CcNameState&) = default;
};

struct MissionCcfRoomLoadSource {
    // Metadata must outlive buildMissionWorldRoomCatalog.
    const CcfMetadata* ccf{};
    // CcRoom::LoadSceneCcf skips the complete room section for flag 0x20.
    bool roomSectionEnabled{true};
    // Load flag 0x4000 copies each section's primary CcName to the receiver;
    // repeated sections overwrite it in load order.
    bool copyPrimaryNameToRoot{false};
    // Load flag 0x2000 suppresses the independent placed-node scene while
    // leaving room-section publication enabled.
    bool placedSceneEnabled{true};
};

struct MissionWorldRoomBuildInput {
    CcNameState initialRootName;
    std::span<const MissionCcfRoomLoadSource> sources;
};

struct MissionWorldRoomContributor {
    std::size_t sourceIndex{};
    std::size_t physicalRoomIndex{};

    friend bool operator==(
        const MissionWorldRoomContributor&,
        const MissionWorldRoomContributor&) = default;
};

struct MissionRuntimeRoom {
    CcNameState ccName;
    std::vector<MissionWorldRoomContributor> contributors;

    friend bool operator==(
        const MissionRuntimeRoom&,
        const MissionRuntimeRoom&) = default;
};

enum class MissionWorldRoomBuildIssueKind : std::uint8_t {
    sourceLimitExceeded,
    roomSectionLimitExceeded,
    contributorLimitExceeded,
    runtimeRoomLimitExceeded,
    nameComponentLimitExceeded,
    retainedNameLimitExceeded,
    integerOverflow,
    invalidSourceMetadata,
    invalidPrimaryBinding,
    embeddedNul,
    unsupportedNonAscii,
};

struct MissionWorldRoomBuildIssue {
    MissionWorldRoomBuildIssueKind kind{
        MissionWorldRoomBuildIssueKind::sourceLimitExceeded};
    std::optional<std::size_t> sourceIndex;
    std::optional<std::size_t> physicalRoomIndex;
};

struct MissionWorldRoomBuildLimits {
    std::size_t maximumSources{65'536U};
    std::size_t maximumRoomSections{65'536U};
    std::size_t maximumContributors{262'144U};
    // Includes the root room at index zero.
    std::size_t maximumRuntimeRooms{100'000U};
    std::size_t maximumNameComponentBytes{4'096U};
    // Bounds retained CcName strings plus folded lookup-index keys.
    std::size_t maximumRetainedNameBytes{16U * 1024U * 1024U};
};

struct MissionWorldRoomCatalog {
    std::size_t sourceCount{};
    std::vector<std::size_t> sourcePhysicalRoomCounts;
    // Retained so downstream consumers can replay and authenticate the exact
    // catalog from the same ordered source list.
    CcNameState initialRootName;
    // Root is index zero. Non-root rooms are newest-created-first, matching
    // the legacy linked-list lookup order.
    std::vector<MissionRuntimeRoom> rooms;
    std::vector<MissionWorldRoomBuildIssue> issues;

    [[nodiscard]] bool complete() const noexcept {
        return issues.empty() && !rooms.empty();
    }
};

// Reconstructs CcRoom::LoadSceneCcf room publication for an ordered series of
// loads into one receiving CcWorld. The input order is the runtime load order.
// Comparisons are exact ASCII case-insensitive parity. Inputs containing bytes
// whose MSVCRT locale behavior is unknown fail closed.
[[nodiscard]] MissionWorldRoomCatalog buildMissionWorldRoomCatalog(
    const MissionWorldRoomBuildInput& input,
    const MissionWorldRoomBuildLimits& limits = {});

// CcWorld assigns root ID zero, then monotonically assigns IDs from one as
// rooms are created. The catalog stores non-root rooms in the legacy linked
// list's newest-first order, so their portable index is the reverse of that
// creation ID. These helpers translate a complete mission-load snapshot only;
// they do not model later room deletion or creation.
[[nodiscard]] std::optional<std::int32_t>
legacyCcRoomIdForWorldRoomIndex(
    const MissionWorldRoomCatalog& catalog,
    std::size_t worldRoomIndex) noexcept;

[[nodiscard]] std::optional<std::size_t>
worldRoomIndexForLegacyCcRoomId(
    const MissionWorldRoomCatalog& catalog,
    std::int32_t roomId) noexcept;

enum class MissionWorldStartIssueKind : std::uint8_t {
    catalogIncomplete,
    startPositionLimitExceeded,
    nameComponentLimitExceeded,
    embeddedNul,
    unsupportedNonAscii,
    missingStartRoom,
    ambiguousStartRoom,
};

struct MissionWorldStartIssue {
    MissionWorldStartIssueKind kind{
        MissionWorldStartIssueKind::catalogIncomplete};
    std::optional<std::size_t> startPositionIndex;
};

struct ResolvedMissionWorldStart {
    std::size_t startPositionIndex{};
    // Index into MissionWorldRoomCatalog::rooms. A table entry is never root.
    std::size_t worldRoomIndex{};

    friend bool operator==(
        const ResolvedMissionWorldStart&,
        const ResolvedMissionWorldStart&) = default;
};

struct MissionWorldStartResolutionLimits {
    std::size_t maximumRuntimeRooms{100'000U};
    std::size_t maximumContributors{262'144U};
    std::size_t maximumNameComponentBytes{4'096U};
    // Bounds catalog CcName strings plus folded resolver keys.
    std::size_t maximumRetainedNameBytes{16U * 1024U * 1024U};
};

struct MissionWorldStartResolution {
    std::size_t worldRoomCount{};
    std::vector<ResolvedMissionWorldStart> starts;
    std::vector<MissionWorldStartIssue> issues;

    [[nodiscard]] bool complete() const noexcept {
        return issues.empty() && worldRoomCount != 0U;
    }
};

// Mirrors AddStartPos lookup: root is excluded because its CcName prefix cannot
// match the name-only query. Missing or ambiguous names clear all results.
[[nodiscard]] MissionWorldStartResolution resolveMissionStartsInWorld(
    std::span<const MissionStartPosition> starts,
    const MissionWorldRoomCatalog& catalog,
    const MissionWorldStartResolutionLimits& limits = {});

enum class MissionWorldStartSelectionSource : std::uint8_t {
    table,
    rootRoomFallback,
};

struct MissionWorldStartSelection {
    MissionWorldStartSelectionSource source{
        MissionWorldStartSelectionSource::rootRoomFallback};
    std::optional<std::size_t> startPositionIndex;
    std::size_t worldRoomIndex{};

    friend bool operator==(
        const MissionWorldStartSelection&,
        const MissionWorldStartSelection&) = default;
};

// Uses unsigned modulo for a non-empty table. An empty table selects root room
// zero. Forged indices and non-canonical table entries fail closed.
[[nodiscard]] std::optional<MissionWorldStartSelection>
selectMissionWorldStart(
    const MissionWorldStartResolution& resolution,
    std::uint32_t requestedIndex) noexcept;

} // namespace airfix::assets
