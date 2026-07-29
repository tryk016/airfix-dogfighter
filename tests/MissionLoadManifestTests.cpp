#include "airfix/content/MissionLoadManifest.hpp"
#include "support/SyntheticContent.hpp"
#include "support/SyntheticLegacyAssets.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using airfix::content::ContentRevision;
using airfix::content::MissionCcfLoadRole;
using airfix::content::MissionLoadDependencyKind;
using airfix::content::MissionLoadManifestIssueKind;
using airfix::content::MissionLoadManifestLimits;
using airfix::content::MissionLoadManifestPhase;
using airfix::content::MissionLoadManifestRequest;
using airfix::content::MissionLoadManifestResult;
using airfix::content::VerifiedContentSession;
using airfix::assets::MissionSetupParseErrorCode;
using airfix::testing::Bytes;
using airfix::testing::SyntheticAfPack;
using airfix::testing::UdspInputEntry;

constexpr std::string_view kPlayerObjectLogicalPath =
    "Game/Objects/Player.object";
constexpr std::string_view kPlayerCcfLogicalPath =
    "Graphics/Player.ccf";
constexpr std::string_view kPlayerBlueprintSelector = "PrimaryRoot";

static_assert(!std::is_default_constructible_v<
    airfix::content::MissionLoadManifest>);
static_assert(!std::is_copy_constructible_v<
    airfix::content::MissionLoadManifest>);
static_assert(!std::is_copy_assignable_v<
    airfix::content::MissionLoadManifest>);
static_assert(std::is_nothrow_move_constructible_v<
    airfix::content::MissionLoadManifest>);
static_assert(!std::is_default_constructible_v<
    airfix::content::VerifiedContentTransactionIdentity>);
static_assert(std::is_copy_constructible_v<
    airfix::content::VerifiedContentTransactionIdentity>);

void require(
    const bool condition,
    const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] ContentRevision revisionFor(
    const SyntheticAfPack& pack,
    const std::uint64_t generation = 81U) {
    return {
        .generation = generation,
        .pack = {
            .size = pack.size,
            .sha256 = pack.sha256,
        },
    };
}

class TestInputStore final {
public:
    ~TestInputStore() {
        for (const auto& path : paths_) {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    }

    TestInputStore(const TestInputStore&) = delete;
    TestInputStore& operator=(const TestInputStore&) = delete;

    [[nodiscard]] static TestInputStore& shared() {
        static TestInputStore store;
        return store;
    }

    [[nodiscard]] std::unique_ptr<std::ifstream> open(
        const SyntheticAfPack& pack) {
        static std::atomic<std::uint64_t> sequence{0U};
        const auto tick = std::chrono::steady_clock::now()
            .time_since_epoch()
            .count();
        const auto path = std::filesystem::temp_directory_path() /
            ("airfix-mission-manifest-" + std::to_string(tick) + "-" +
             std::to_string(sequence.fetch_add(
                 1U, std::memory_order_relaxed)) + ".afpack");
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output ||
                (!pack.bytes.empty() &&
                 !output.write(
                     reinterpret_cast<const char*>(pack.bytes.data()),
                     static_cast<std::streamsize>(pack.bytes.size())))) {
                throw std::runtime_error(
                    "failed to create mission manifest test input");
            }
        }
        paths_.push_back(path);
        auto input = std::make_unique<std::ifstream>(
            path, std::ios::binary | std::ios::ate);
        if (!*input) {
            throw std::runtime_error(
                "failed to open mission manifest test input");
        }
        return input;
    }

private:
    TestInputStore() = default;

    std::vector<std::filesystem::path> paths_;
};

[[nodiscard]] VerifiedContentSession openSession(
    const SyntheticAfPack& pack,
    const std::uint64_t generation = 81U,
    std::string sourceLabel = "synthetic-mission-manifest.afpack") {
    return VerifiedContentSession::open(
        TestInputStore::shared().open(pack),
        std::move(sourceLabel),
        revisionFor(pack, generation));
}

[[nodiscard]] Bytes literalCompression(
    const std::span<const std::uint8_t> decoded) {
    Bytes encoded;
    std::size_t offset = 0U;
    while (offset < decoded.size()) {
        const auto count =
            std::min<std::size_t>(255U, decoded.size() - offset);
        encoded.push_back(0x66U);
        encoded.push_back(static_cast<std::uint8_t>(count));
        encoded.insert(
            encoded.end(),
            decoded.begin() + static_cast<std::ptrdiff_t>(offset),
            decoded.begin() +
                static_cast<std::ptrdiff_t>(offset + count));
        offset += count;
    }
    return encoded;
}

[[nodiscard]] Bytes sourceBytes(const std::string_view source) {
    return {source.begin(), source.end()};
}

[[nodiscard]] Bytes makePlayerObjectDefinition(
    const std::optional<std::string_view> ccfPath =
        kPlayerCcfLogicalPath,
    const std::optional<std::string_view> selector =
        kPlayerBlueprintSelector,
    const std::optional<std::string_view> textureRoot =
        airfix::testing::kSyntheticTextureRoot,
    const airfix::assets::ObjectDefinitionKind kind =
        airfix::assets::ObjectDefinitionKind::model) {
    Bytes chunks;
    if (textureRoot.has_value()) {
        airfix::testing::legacy_detail::appendAfChunk(
            chunks,
            airfix::assets::fourCC('T', 'E', 'X', 'U'),
            *textureRoot);
    }
    if (ccfPath.has_value()) {
        airfix::testing::legacy_detail::appendAfChunk(
            chunks,
            airfix::assets::fourCC('C', 'C', 'F', 'F'),
            *ccfPath);
    }
    if (selector.has_value()) {
        airfix::testing::legacy_detail::appendAfChunk(
            chunks,
            airfix::assets::fourCC('M', 'E', 'S', 'H'),
            *selector);
    }

    Bytes bytes;
    airfix::testing::legacy_detail::appendU32(
        bytes,
        kind == airfix::assets::ObjectDefinitionKind::object
            ? airfix::assets::kAfObjectRoot
            : airfix::assets::kAfModelRoot);
    airfix::testing::legacy_detail::appendU32(
        bytes, static_cast<std::uint32_t>(chunks.size()));
    airfix::testing::legacy_detail::appendBytes(bytes, chunks);
    return bytes;
}

[[nodiscard]] std::vector<UdspInputEntry> happyEntries() {
    constexpr std::string_view objectPaths[]{
        airfix::testing::kSyntheticObjectLogicalPath,
        airfix::testing::kSyntheticObjectLogicalPath,
    };
    constexpr std::string_view modelPaths[]{
        airfix::testing::kSyntheticModelLogicalPath,
    };
    constexpr std::string_view backdrops[]{
        airfix::testing::kSyntheticBackdropCcfLogicalPath,
        "Graphics/IgnoredBackdrop.ccf",
    };
    return {
        {
            .logicalPath =
                std::string(airfix::testing::kSyntheticLevelLogicalPath),
            .bytes = airfix::testing::makeSyntheticLevel(
                airfix::testing::kSyntheticWorldLogicalPath,
                objectPaths,
                modelPaths),
        },
        {
            .logicalPath =
                std::string(airfix::testing::kSyntheticWorldLogicalPath),
            .bytes =
                airfix::testing::makeSyntheticWorldWithBackdrops(
                    airfix::testing::kSyntheticCcfLogicalPath,
                    airfix::testing::kSyntheticTextureRoot,
                    backdrops),
        },
        {
            .logicalPath =
                std::string(airfix::testing::kSyntheticObjectLogicalPath),
            .bytes = airfix::testing::makeSyntheticObjectDefinition(),
        },
        {
            .logicalPath =
                std::string(airfix::testing::kSyntheticModelLogicalPath),
            // Unknown compression opcode: any payload read must fail.
            .bytes = {0xFFU},
            .flags = airfix::udsp::kCompressedFlag,
            .unpackedSize = 48U,
        },
        {
            .logicalPath =
                std::string(airfix::testing::kSyntheticCcfLogicalPath),
            .bytes = {0xFFU},
            .flags = airfix::udsp::kCompressedFlag,
            .unpackedSize = 31U,
        },
        {
            .logicalPath =
                std::string(
                    airfix::testing::kSyntheticBackdropCcfLogicalPath),
            .bytes = {0xFFU},
            .flags = airfix::udsp::kCompressedFlag,
            .unpackedSize = 32U,
        },
        {
            .logicalPath = "Graphics/IgnoredBackdrop.ccf",
            .bytes = {0xFFU},
            .flags = airfix::udsp::kCompressedFlag,
            .unpackedSize = 33U,
        },
        {
            .logicalPath =
                std::string(
                    airfix::testing::kSyntheticAlternateCcfLogicalPath),
            .bytes = {0xFFU},
            .flags = airfix::udsp::kCompressedFlag,
            .unpackedSize = 64U,
        },
        {
            .logicalPath = std::string(
                airfix::testing::kSyntheticMissionSetupLogicalPath),
            .bytes = airfix::testing::makeSyntheticMissionSetup(),
        },
    };
}

[[nodiscard]] std::vector<UdspInputEntry> playerHappyEntries() {
    auto entries = happyEntries();
    entries.push_back({
        .logicalPath = std::string(kPlayerObjectLogicalPath),
        .bytes = makePlayerObjectDefinition(),
    });
    entries.push_back({
        .logicalPath = std::string(kPlayerCcfLogicalPath),
        // Unknown compression opcode: a CCF payload read must fail.
        .bytes = {0xFFU},
        .flags = airfix::udsp::kCompressedFlag,
        .unpackedSize = 47U,
    });
    return entries;
}

[[nodiscard]] SyntheticAfPack makePack(
    const std::vector<UdspInputEntry>& entries) {
    return airfix::testing::makeSyntheticAfPack(
        std::span<const UdspInputEntry>(entries));
}

[[nodiscard]] MissionLoadManifestRequest request(
    std::string levelPath =
        std::string(airfix::testing::kSyntheticLevelLogicalPath),
    std::string setupPath =
        std::string(airfix::testing::kSyntheticMissionSetupLogicalPath)) {
    return {
        .levelLogicalPath = std::move(levelPath),
        .setupLogicalPath = std::move(setupPath),
    };
}

[[nodiscard]] MissionLoadManifestRequest playerRequest() {
    auto result = request();
    result.playerObjectLogicalPath =
        std::string(kPlayerObjectLogicalPath);
    return result;
}

[[nodiscard]] bool hasIssue(
    const MissionLoadManifestResult& result,
    const MissionLoadManifestIssueKind kind,
    const MissionLoadDependencyKind dependency) {
    return std::ranges::any_of(
        result.issues,
        [=](const auto& issue) {
            return issue.kind == kind &&
                issue.dependency == dependency;
        });
}

void requireAtomicFailure(
    const MissionLoadManifestResult& result,
    const MissionLoadManifestIssueKind kind,
    const MissionLoadDependencyKind dependency,
    const std::string_view message) {
    require(
        !result.success() &&
            !result.manifest.has_value() &&
            hasIssue(result, kind, dependency),
        message);
}

[[nodiscard]] std::uint64_t footprint(
    const VerifiedContentSession& session,
    const std::string_view logicalPath) {
    const auto lookup = session.sourceArchive().lookup(logicalPath);
    require(
        lookup.status == airfix::udsp::LookupStatus::unique,
        "fixture lookup was not unique");
    const auto& entry =
        session.sourceArchive().files().at(lookup.fileIndex);
    return static_cast<std::uint64_t>(entry.storedSize) +
        (entry.isCompressed()
            ? static_cast<std::uint64_t>(entry.unpackedSize)
            : 0U);
}

[[nodiscard]] std::uint64_t decodedSize(
    const VerifiedContentSession& session,
    const std::string_view logicalPath) {
    const auto lookup = session.sourceArchive().lookup(logicalPath);
    require(
        lookup.status == airfix::udsp::LookupStatus::unique,
        "decoded-size fixture lookup was not unique");
    const auto& entry =
        session.sourceArchive().files().at(lookup.fileIndex);
    return entry.isCompressed()
        ? static_cast<std::uint64_t>(entry.unpackedSize)
        : static_cast<std::uint64_t>(entry.storedSize);
}

void requirePoisonPayloadsAreUnreadable(
    const SyntheticAfPack& pack) {
    auto probe = openSession(pack);
    for (const auto path : {
             airfix::testing::kSyntheticModelLogicalPath,
             airfix::testing::kSyntheticCcfLogicalPath,
             airfix::testing::kSyntheticBackdropCcfLogicalPath,
             airfix::testing::kSyntheticAlternateCcfLogicalPath}) {
        const auto lookup = probe.sourceArchive().lookup(path);
        require(
            lookup.status == airfix::udsp::LookupStatus::unique,
            "unread poison fixture was not unique");
        bool threw = false;
        try {
            (void)probe.readSourceFile(lookup.fileIndex, 1024U);
        }
        catch (...) {
            threw = true;
        }
        require(
            threw,
            "unread MODL/CCF poison payload unexpectedly decoded");
    }
}

void testHappyPathOrderingCachingAndOwnership() {
    const auto pack = makePack(happyEntries());
    // This independent probe proves the metadata-only inputs would fail if
    // MissionLoadManifest accidentally read any MODL or CCF payload.
    requirePoisonPayloadsAreUnreadable(pack);
    auto session = openSession(
        pack,
        81U,
        "Z:\\this\\diagnostic\\path\\must-not-be-opened.afpack");
    const auto result =
        airfix::content::buildMissionLoadManifest(session, request());
    require(result.success(), "mission manifest happy path failed");

    const auto& manifest = *result.manifest;
    require(
        manifest.belongsTo(session) &&
            manifest.revision() == revisionFor(pack),
        "manifest did not retain the authenticated revision");
    require(
        manifest.setupEntry().logicalPath ==
                "Game\\Missions\\Setup\\DetachedFlight.afs" &&
            manifest.setupEntry().archiveFileIndex <
                session.sourceArchive().files().size() &&
            manifest.setupSourceFootprintBytes() ==
                footprint(
                    session,
                    airfix::testing::kSyntheticMissionSetupLogicalPath),
        "manifest did not retain exact setup identity and footprint");
    require(
        manifest.startPositions().size() == 1U &&
            manifest.startPositions()[0].roomName == "Room" &&
            manifest.startPositions()[0].position ==
                std::array<float, 3U>{10.0F, 20.0F, 30.0F} &&
            manifest.startPositions()[0].axisRotation ==
                std::array<float, 3U>{0.1F, 0.2F, 0.3F} &&
            manifest.startPositions()[0].sourceOffset > 0U,
        "manifest did not own the parsed authenticated start table");
    require(
        manifest.level().objects.size() == 2U &&
            manifest.level().models.size() == 1U &&
            manifest.objectEntries().size() == 2U &&
            manifest.modelEntries().size() == 1U,
        "manifest did not retain parallel placement metadata");
    require(
        manifest.uniqueObjectDefinitions().size() == 1U &&
            manifest.objectDefinitionIndices() ==
                std::vector<std::size_t>{0U, 0U},
        "repeated OBJE definitions were not cached by exact file index");

    const auto& loads = manifest.ccfLoads();
    require(loads.size() == 4U, "CCF descriptor count mismatch");
    for (std::size_t index = 0U; index < loads.size(); ++index) {
        require(
            loads[index].sourceIndex == index,
            "CCF source indices are not dense");
    }
    require(
        loads[0].role == MissionCcfLoadRole::mainWorld &&
            loads[0].legacyLoadFlags == 0U &&
            loads[0].placedSceneEnabled &&
            loads[0].textureRoot ==
                std::optional<std::string>{
                    airfix::testing::kSyntheticTextureRoot},
        "main World CCF descriptor mismatch");
    require(
        loads[1].role == MissionCcfLoadRole::backdrop &&
            loads[1].source.logicalPath ==
                "Graphics\\Backdrop.ccf" &&
            loads[1].legacyLoadFlags == 0U &&
            loads[1].placedSceneEnabled,
        "only the first BCKD should become the backdrop descriptor");
    require(
        loads[2].role == MissionCcfLoadRole::objectPlacement &&
            loads[3].role == MissionCcfLoadRole::objectPlacement &&
            loads[2].source.archiveFileIndex ==
                loads[3].source.archiveFileIndex &&
            loads[2].legacyLoadFlags == 0x2000U &&
            loads[3].legacyLoadFlags == 0x2000U &&
            !loads[2].placedSceneEnabled &&
            !loads[3].placedSceneEnabled &&
            loads[2].objectPlacementIndex ==
                std::optional<std::size_t>{0U} &&
            loads[3].objectPlacementIndex ==
                std::optional<std::size_t>{1U},
        "physical OBJE descriptors were reordered or deduplicated");
    require(
        !manifest.playerVisual().has_value() &&
            manifest.plannedPlayerVisualCcfSourceFootprintBytes() == 0U &&
            manifest.plannedTotalCcfSourceFootprintBytes() ==
                manifest.plannedCcfSourceFootprintBytes() &&
            manifest.publishedCpuBytes() > 0U &&
            manifest.definitionSourceFootprintBytes() > 0U &&
            manifest.plannedCcfSourceFootprintBytes() ==
                loads[0].sourceAllocationFootprintBytes +
                    loads[1].sourceAllocationFootprintBytes +
                    loads[2].sourceAllocationFootprintBytes +
                    loads[3].sourceAllocationFootprintBytes,
        "manifest accounting mismatch");
}

void testAuthenticatedPlayerVisualDescriptor() {
    const auto entries = playerHappyEntries();
    const auto pack = makePack(entries);
    auto session = openSession(pack);
    const auto playerCcfLookup =
        session.sourceArchive().lookup(kPlayerCcfLogicalPath);
    require(
        playerCcfLookup.status == airfix::udsp::LookupStatus::unique,
        "player CCF fixture lookup failed");
    bool poisonRejected = false;
    try {
        (void)session.readSourceFile(playerCcfLookup.fileIndex, 1024U);
    }
    catch (...) {
        poisonRejected = true;
    }
    require(
        poisonRejected,
        "player CCF poison payload unexpectedly decoded");

    auto authenticatedRequest = playerRequest();
    authenticatedRequest.playerObjectLogicalPath =
        "game\\objects\\PLAYER.object";
    const auto result = airfix::content::buildMissionLoadManifest(
        session, authenticatedRequest);
    require(result.success(), "authenticated player descriptor failed");
    const auto& manifest = *result.manifest;
    require(
        manifest.playerVisual().has_value() &&
            manifest.playerVisual()->valid(),
        "player descriptor was not published validly");
    const auto& player = *manifest.playerVisual();
    require(
        player.objectDefinitionSource.logicalPath ==
                "Game\\Objects\\Player.object" &&
            player.modelCcfSource.logicalPath ==
                "Graphics\\Player.ccf" &&
            player.modelCcfSource.archiveFileIndex ==
                playerCcfLookup.fileIndex &&
            player.blueprintSelector == kPlayerBlueprintSelector &&
            player.textureRoot ==
                std::optional<std::string>{
                    airfix::testing::kSyntheticTextureRoot} &&
            player.legacySkinSlot == 0U,
        "player descriptor lost canonical identity or derived MODL fields");
    require(
        player.objectDefinitionSourceAllocationFootprintBytes ==
                footprint(session, kPlayerObjectLogicalPath) &&
            player.modelCcfSourceAllocationFootprintBytes ==
                footprint(session, kPlayerCcfLogicalPath) &&
            manifest.plannedPlayerVisualCcfSourceFootprintBytes() ==
                player.modelCcfSourceAllocationFootprintBytes &&
            manifest.plannedTotalCcfSourceFootprintBytes() ==
                manifest.plannedCcfSourceFootprintBytes() +
                    manifest
                        .plannedPlayerVisualCcfSourceFootprintBytes(),
        "player source footprint accounting mismatch");
    require(
        manifest.ccfLoads().size() == 4U &&
            std::ranges::none_of(
                manifest.ccfLoads(),
                [&](const auto& load) {
                    return load.source.archiveFileIndex ==
                        player.modelCcfSource.archiveFileIndex;
                }),
        "player CCF leaked into the room CCF catalogue");

    auto absentSession = openSession(pack);
    const auto absent =
        airfix::content::buildMissionLoadManifest(
            absentSession, request());
    const auto expectedPublishedDelta =
        static_cast<std::uint64_t>(
            player.objectDefinitionSource.logicalPath.size() +
            player.modelCcfSource.logicalPath.size() +
            player.blueprintSelector.size() +
            player.textureRoot->size());
    require(
        absent.success() &&
            !absent.manifest->playerVisual().has_value() &&
            absent.manifest->ccfLoads() == manifest.ccfLoads() &&
            absent.manifest->plannedCcfSourceFootprintBytes() ==
                manifest.plannedCcfSourceFootprintBytes() &&
            absent.manifest->publishedCpuBytes() +
                    expectedPublishedDelta ==
                manifest.publishedCpuBytes() &&
            absent.manifest->definitionSourceFootprintBytes() +
                    player
                        .objectDefinitionSourceAllocationFootprintBytes ==
                manifest.definitionSourceFootprintBytes(),
        "absent player request changed room flow or player ownership accounting");

    auto withoutRoot = playerHappyEntries();
    withoutRoot[withoutRoot.size() - 2U].bytes =
        makePlayerObjectDefinition(
            kPlayerCcfLogicalPath,
            kPlayerBlueprintSelector,
            std::nullopt);
    const auto withoutRootPack = makePack(withoutRoot);
    auto withoutRootSession = openSession(withoutRootPack);
    const auto withoutRootResult =
        airfix::content::buildMissionLoadManifest(
            withoutRootSession, playerRequest());
    require(
        withoutRootResult.success() &&
            withoutRootResult.manifest->playerVisual().has_value() &&
            !withoutRootResult.manifest->playerVisual()
                 ->textureRoot.has_value(),
        "optional player texture root was not preserved");
}

void testPlayerVisualFailuresAreTypedAndAtomic() {
    const auto requireRequestFailure = [](
        std::optional<std::string> playerPath,
        const MissionLoadManifestIssueKind kind,
        const std::string_view message) {
        const auto pack = makePack(playerHappyEntries());
        auto session = openSession(pack);
        auto player = request();
        player.playerObjectLogicalPath = std::move(playerPath);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(session, player),
            kind,
            MissionLoadDependencyKind::playerObjectDefinition,
            message);
    };
    requireRequestFailure(
        std::string{},
        MissionLoadManifestIssueKind::missingLogicalPath,
        "present empty player visual path was accepted");
    requireRequestFailure(
        "../Player.object",
        MissionLoadManifestIssueKind::invalidLogicalPath,
        "invalid player visual path was accepted");
    requireRequestFailure(
        "Game/Objects/MissingPlayer.object",
        MissionLoadManifestIssueKind::notFound,
        "missing player visual path was accepted");

    const auto requireObjectFailure = [](
        Bytes objectBytes,
        const MissionLoadManifestIssueKind kind,
        const MissionLoadDependencyKind dependency,
        const std::string_view message) {
        auto entries = playerHappyEntries();
        entries[entries.size() - 2U].bytes = std::move(objectBytes);
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, playerRequest()),
            kind,
            dependency,
            message);
    };
    requireObjectFailure(
        {0x00U, 0x01U, 0x02U},
        MissionLoadManifestIssueKind::parseFailure,
        MissionLoadDependencyKind::playerObjectDefinition,
        "malformed player MODL-root definition was accepted");
    {
        auto entries = playerHappyEntries();
        auto& object = entries[entries.size() - 2U];
        object.bytes = {0xFFU};
        object.flags = airfix::udsp::kCompressedFlag;
        object.unpackedSize = 64U;
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, playerRequest()),
            MissionLoadManifestIssueKind::readFailure,
            MissionLoadDependencyKind::playerObjectDefinition,
            "unreadable compressed player MODL-root definition was accepted");
    }
    requireObjectFailure(
        makePlayerObjectDefinition(
            kPlayerCcfLogicalPath,
            kPlayerBlueprintSelector,
            airfix::testing::kSyntheticTextureRoot,
            airfix::assets::ObjectDefinitionKind::object),
        MissionLoadManifestIssueKind::objectDefinitionKindMismatch,
        MissionLoadDependencyKind::playerObjectDefinition,
        "player OBJE-root definition was accepted");
    requireObjectFailure(
        makePlayerObjectDefinition(
            kPlayerCcfLogicalPath, std::nullopt),
        MissionLoadManifestIssueKind::missingBlueprintSelector,
        MissionLoadDependencyKind::playerObjectDefinition,
        "missing player blueprint selector was accepted");
    requireObjectFailure(
        makePlayerObjectDefinition(kPlayerCcfLogicalPath, ""),
        MissionLoadManifestIssueKind::missingBlueprintSelector,
        MissionLoadDependencyKind::playerObjectDefinition,
        "empty player blueprint selector was accepted");
    requireObjectFailure(
        makePlayerObjectDefinition(std::nullopt),
        MissionLoadManifestIssueKind::missingLogicalPath,
        MissionLoadDependencyKind::playerModelCcf,
        "missing derived player CCF path was accepted");
    requireObjectFailure(
        makePlayerObjectDefinition(""),
        MissionLoadManifestIssueKind::missingLogicalPath,
        MissionLoadDependencyKind::playerModelCcf,
        "empty derived player CCF path was accepted");
    requireObjectFailure(
        makePlayerObjectDefinition("../Player.ccf"),
        MissionLoadManifestIssueKind::invalidLogicalPath,
        MissionLoadDependencyKind::playerModelCcf,
        "invalid derived player CCF path was accepted");
    requireObjectFailure(
        makePlayerObjectDefinition("Graphics/MissingPlayer.ccf"),
        MissionLoadManifestIssueKind::notFound,
        MissionLoadDependencyKind::playerModelCcf,
        "missing derived player CCF was accepted");

    {
        auto entries = playerHappyEntries();
        entries.push_back(entries[entries.size() - 2U]);
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, playerRequest()),
            MissionLoadManifestIssueKind::ambiguous,
            MissionLoadDependencyKind::playerObjectDefinition,
            "ambiguous player visual definition was accepted");
    }
    {
        auto entries = playerHappyEntries();
        entries.push_back(entries.back());
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, playerRequest()),
            MissionLoadManifestIssueKind::ambiguous,
            MissionLoadDependencyKind::playerModelCcf,
            "ambiguous derived player CCF was accepted");
    }
    {
        const auto pack = makePack(playerHappyEntries());
        auto session = openSession(pack);
        MissionLoadManifestLimits limits;
        limits.maximumPlayerBlueprintSelectorBytes =
            kPlayerBlueprintSelector.size() - 1U;
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, playerRequest(), limits),
            MissionLoadManifestIssueKind::invalidBlueprintSelector,
            MissionLoadDependencyKind::playerObjectDefinition,
            "over-limit player blueprint selector was accepted");
    }
}

void testPlayerVisualExactBudgetsAndLateAtomicity() {
    auto entries = playerHappyEntries();
    auto& playerObject = entries[entries.size() - 2U];
    playerObject.unpackedSize =
        static_cast<std::uint32_t>(playerObject.bytes.size());
    playerObject.bytes = literalCompression(playerObject.bytes);
    playerObject.flags = airfix::udsp::kCompressedFlag;
    const auto pack = makePack(entries);

    MissionLoadManifestResult baseline;
    {
        auto session = openSession(pack);
        baseline = airfix::content::buildMissionLoadManifest(
            session, playerRequest());
    }
    require(
        baseline.success() &&
            baseline.manifest->playerVisual().has_value(),
        "player budget baseline failed");
    const auto& descriptor = *baseline.manifest->playerVisual();
    MissionLoadManifestLimits exact;
    exact.maximumPlayerObjectDefinitionSourceBytes =
        static_cast<std::size_t>(
            descriptor
                .objectDefinitionSourceAllocationFootprintBytes);
    exact.maximumPlayerCcfSourceBytes = static_cast<std::size_t>(
        descriptor.modelCcfSourceAllocationFootprintBytes);
    exact.maximumTotalDefinitionSourceBytes =
        baseline.manifest->definitionSourceFootprintBytes();
    exact.maximumTotalCcfSourceBytes =
        baseline.manifest->plannedTotalCcfSourceFootprintBytes();
    exact.maximumCcfSources =
        baseline.manifest->ccfLoads().size() + 1U;
    exact.maximumPublishedCpuBytes =
        baseline.manifest->publishedCpuBytes();
    {
        auto session = openSession(pack);
        require(
            airfix::content::buildMissionLoadManifest(
                session, playerRequest(), exact).success(),
            "exact player source/accounting limits failed");
    }

    const auto requireOneUnder = [&](
        MissionLoadManifestLimits limits,
        const MissionLoadManifestIssueKind kind,
        const MissionLoadDependencyKind dependency,
        const std::string_view message) {
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, playerRequest(), limits),
            kind,
            dependency,
            message);
    };
    {
        auto limits = exact;
        --limits.maximumPlayerObjectDefinitionSourceBytes;
        requireOneUnder(
            limits,
            MissionLoadManifestIssueKind::sourceLimitExceeded,
            MissionLoadDependencyKind::playerObjectDefinition,
            "one-under player MODL source limit succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumTotalDefinitionSourceBytes;
        requireOneUnder(
            limits,
            MissionLoadManifestIssueKind::
                aggregateDefinitionSourceLimitExceeded,
            MissionLoadDependencyKind::playerObjectDefinition,
            "one-under player definition aggregate succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumPlayerCcfSourceBytes;
        requireOneUnder(
            limits,
            MissionLoadManifestIssueKind::sourceLimitExceeded,
            MissionLoadDependencyKind::playerModelCcf,
            "one-under player CCF source limit succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumTotalCcfSourceBytes;
        requireOneUnder(
            limits,
            MissionLoadManifestIssueKind::
                aggregateCcfSourceLimitExceeded,
            MissionLoadDependencyKind::objectCcf,
            "one-under total CCF aggregate succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumCcfSources;
        requireOneUnder(
            limits,
            MissionLoadManifestIssueKind::
                ccfSourceCountLimitExceeded,
            MissionLoadDependencyKind::playerModelCcf,
            "one-under total CCF source count succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumPublishedCpuBytes;
        requireOneUnder(
            limits,
            MissionLoadManifestIssueKind::
                publishedCpuLimitExceeded,
            MissionLoadDependencyKind::level,
            "one-under player published CPU limit succeeded");
    }

    {
        auto session = openSession(pack);
        std::optional<MissionLoadManifestRequest> externalRequest{
            playerRequest()};
        std::optional<MissionLoadManifestLimits> externalLimits{
            MissionLoadManifestLimits{}};
        bool destroyed = false;
        const auto result =
            airfix::content::buildMissionLoadManifest(
                session,
                *externalRequest,
                *externalLimits,
                {},
                [&](const auto& progress) {
                    if (!destroyed &&
                        progress.phase ==
                            MissionLoadManifestPhase::
                                validatingRequest &&
                        progress.completedItems == 0U) {
                        externalRequest.reset();
                        externalLimits.reset();
                        destroyed = true;
                    }
                });
        require(
            destroyed && result.success() &&
                result.manifest->playerVisual().has_value(),
            "player request was dereferenced after the first callback");
    }
    {
        constexpr std::array playerPhases{
            MissionLoadManifestPhase::
                lookingUpPlayerObjectDefinition,
            MissionLoadManifestPhase::
                readingPlayerObjectDefinition,
            MissionLoadManifestPhase::
                parsingPlayerObjectDefinition,
            MissionLoadManifestPhase::resolvingPlayerModelCcf,
        };
        for (const auto phase : playerPhases) {
            {
                auto session = openSession(pack);
                std::stop_source source;
                bool requested = false;
                const auto result =
                    airfix::content::buildMissionLoadManifest(
                        session,
                        playerRequest(),
                        {},
                        source.get_token(),
                        [&](const auto& progress) {
                            if (!requested &&
                                progress.phase == phase) {
                                source.request_stop();
                                requested = true;
                            }
                        });
                require(
                    requested,
                    "player cancellation phase was not reached");
                requireAtomicFailure(
                    result,
                    MissionLoadManifestIssueKind::cancelled,
                    MissionLoadDependencyKind::level,
                    "player phase cancellation published a manifest");
            }
            {
                auto session = openSession(pack);
                bool threw = false;
                const auto result =
                    airfix::content::buildMissionLoadManifest(
                        session,
                        playerRequest(),
                        {},
                        {},
                        [&](const auto& progress) {
                            if (!threw && progress.phase == phase) {
                                threw = true;
                                throw std::runtime_error(
                                    "player phase callback failed");
                            }
                        });
                require(
                    threw,
                    "player callback failure phase was not reached");
                requireAtomicFailure(
                    result,
                    MissionLoadManifestIssueKind::
                        progressCallbackFailure,
                    MissionLoadDependencyKind::level,
                    "player phase callback failure published a manifest");
            }
            {
                auto session = openSession(pack, 81U);
                auto replacement = openSession(pack, 81U);
                bool replaced = false;
                const auto result =
                    airfix::content::buildMissionLoadManifest(
                        session,
                        playerRequest(),
                        {},
                        {},
                        [&](const auto& progress) {
                            if (!replaced &&
                                progress.phase == phase) {
                                session = std::move(replacement);
                                replaced = true;
                            }
                        });
                require(
                    replaced,
                    "player session replacement phase was not reached");
                requireAtomicFailure(
                    result,
                    MissionLoadManifestIssueKind::
                        sessionIdentityChanged,
                    MissionLoadDependencyKind::level,
                    "player phase session replacement published a manifest");
            }
        }
    }

    auto moveSession = openSession(pack);
    auto moveSource = airfix::content::buildMissionLoadManifest(
        moveSession, playerRequest());
    MissionLoadManifestResult moved = std::move(moveSource);
    require(
        moveSource.manifest.has_value() &&
            !moveSource.manifest->valid() &&
            moved.success() &&
            moved.manifest->playerVisual().has_value() &&
            moved.manifest->playerVisual()
                    ->objectDefinitionSource.logicalPath ==
                "Game\\Objects\\Player.object",
        "player descriptor identity did not survive a poisoned move");
}

void testLookupFailuresAreTypedAndAtomic() {
    {
        const auto pack = makePack(happyEntries());
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, request("../escape.level")),
            MissionLoadManifestIssueKind::invalidLogicalPath,
            MissionLoadDependencyKind::level,
            "invalid Level path was not rejected");
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, request("Game/Levels/Missing.level")),
            MissionLoadManifestIssueKind::notFound,
            MissionLoadDependencyKind::level,
            "missing Level was not rejected");
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session,
                request(
                    std::string(
                        airfix::testing::kSyntheticLevelLogicalPath),
                    "")),
            MissionLoadManifestIssueKind::missingLogicalPath,
            MissionLoadDependencyKind::missionSetup,
            "missing setup path was not rejected");
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session,
                request(
                    std::string(
                        airfix::testing::kSyntheticLevelLogicalPath),
                    "../escape.afs")),
            MissionLoadManifestIssueKind::invalidLogicalPath,
            MissionLoadDependencyKind::missionSetup,
            "invalid setup path was not rejected");
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session,
                request(
                    std::string(
                        airfix::testing::kSyntheticLevelLogicalPath),
                    "Game/Missions/Missing.afs")),
            MissionLoadManifestIssueKind::notFound,
            MissionLoadDependencyKind::missionSetup,
            "missing setup entry was not rejected");
    }
    {
        auto entries = happyEntries();
        entries.push_back(entries.back());
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, request()),
            MissionLoadManifestIssueKind::ambiguous,
            MissionLoadDependencyKind::missionSetup,
            "ambiguous setup entry was not rejected");
    }
    {
        auto entries = happyEntries();
        entries[0].bytes = airfix::testing::makeSyntheticLevel(
            "Game/Worlds/Missing.world");
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, request()),
            MissionLoadManifestIssueKind::notFound,
            MissionLoadDependencyKind::world,
            "missing World was not rejected");
    }
    {
        auto entries = happyEntries();
        entries[0].bytes =
            airfix::testing::makeSyntheticLevel("../Bad.world");
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, request()),
            MissionLoadManifestIssueKind::invalidLogicalPath,
            MissionLoadDependencyKind::world,
            "invalid World path was not rejected");
    }
    {
        auto entries = happyEntries();
        entries.push_back(entries[1]);
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, request()),
            MissionLoadManifestIssueKind::ambiguous,
            MissionLoadDependencyKind::world,
            "ambiguous World was not rejected");
    }
    {
        auto entries = happyEntries();
        constexpr std::string_view objects[]{
            "Game/Objects/Missing.object",
        };
        constexpr std::string_view models[]{
            airfix::testing::kSyntheticModelLogicalPath,
        };
        entries[0].bytes = airfix::testing::makeSyntheticLevel(
            airfix::testing::kSyntheticWorldLogicalPath,
            objects,
            models);
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, request()),
            MissionLoadManifestIssueKind::notFound,
            MissionLoadDependencyKind::objectDefinition,
            "missing OBJE definition was not rejected");
    }
    {
        auto entries = happyEntries();
        constexpr std::string_view objects[]{"../Bad.object"};
        constexpr std::string_view models[]{
            airfix::testing::kSyntheticModelLogicalPath,
        };
        entries[0].bytes = airfix::testing::makeSyntheticLevel(
            airfix::testing::kSyntheticWorldLogicalPath,
            objects,
            models);
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, request()),
            MissionLoadManifestIssueKind::invalidLogicalPath,
            MissionLoadDependencyKind::objectDefinition,
            "invalid OBJE path was not rejected");
    }
    {
        auto entries = happyEntries();
        entries.push_back(entries[2]);
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, request()),
            MissionLoadManifestIssueKind::ambiguous,
            MissionLoadDependencyKind::objectDefinition,
            "ambiguous OBJE definition was not rejected");
    }
    {
        auto entries = happyEntries();
        entries.erase(entries.begin() + 3);
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, request()),
            MissionLoadManifestIssueKind::notFound,
            MissionLoadDependencyKind::modelDefinition,
            "missing MODL metadata was not rejected");
    }
    {
        auto entries = happyEntries();
        constexpr std::string_view objects[]{
            airfix::testing::kSyntheticObjectLogicalPath,
        };
        constexpr std::string_view models[]{"../Bad.model"};
        entries[0].bytes = airfix::testing::makeSyntheticLevel(
            airfix::testing::kSyntheticWorldLogicalPath,
            objects,
            models);
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, request()),
            MissionLoadManifestIssueKind::invalidLogicalPath,
            MissionLoadDependencyKind::modelDefinition,
            "invalid MODL path was not rejected");
    }
    {
        auto entries = happyEntries();
        entries.push_back(entries[4]);
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, request()),
            MissionLoadManifestIssueKind::ambiguous,
            MissionLoadDependencyKind::mainWorldCcf,
            "ambiguous main CCF was not rejected");
    }
    {
        auto entries = happyEntries();
        entries[1].bytes = airfix::testing::makeSyntheticWorld(
            "../Bad.ccf",
            airfix::testing::kSyntheticTextureRoot);
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, request()),
            MissionLoadManifestIssueKind::invalidLogicalPath,
            MissionLoadDependencyKind::mainWorldCcf,
            "invalid main CCF path was not rejected");
    }
}

void testSetupParsingIsTypedBoundedAndOwned() {
    {
        auto entries = happyEntries();
        entries.back().bytes =
            sourceBytes("object EmptySetup { event Setup {} }");
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        const auto result =
            airfix::content::buildMissionLoadManifest(
                session, request());
        require(
            result.success() && result.manifest->startPositions().empty() &&
                result.manifest->setupEntry().logicalPath ==
                    "Game\\Missions\\Setup\\DetachedFlight.afs",
            "authenticated setup without starts did not publish an empty table");
    }

    const auto requireSetupParseFailure = [](
        Bytes source,
        const MissionSetupParseErrorCode expected,
        const std::string_view message) {
        auto entries = happyEntries();
        entries.back().bytes = std::move(source);
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        const auto result =
            airfix::content::buildMissionLoadManifest(
                session, request());
        requireAtomicFailure(
            result,
            MissionLoadManifestIssueKind::parseFailure,
            MissionLoadDependencyKind::missionSetup,
            message);
        require(
            result.issues.size() == 1U &&
                result.issues.front().setupParseError ==
                    std::optional<MissionSetupParseErrorCode>{expected} &&
                result.issues.front().sourceOffset.has_value() &&
                result.issues.front().sourceFileIndex.has_value(),
            "setup parse failure lost its typed code, offset, or source index");
    };

    requireSetupParseFailure(
        sourceBytes(
            "AddStartPos(\"Room\",coord3d(1,2),coord3d(4,5,6));"),
        MissionSetupParseErrorCode::malformedText,
        "malformed setup call was not typed");

    auto embeddedNul = sourceBytes("/* setup ");
    embeddedNul.push_back(0U);
    const auto nulOffset = embeddedNul.size() - 1U;
    embeddedNul.insert(
        embeddedNul.end(),
        {' ', '*', '/'});
    {
        auto entries = happyEntries();
        entries.back().bytes = std::move(embeddedNul);
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        const auto result =
            airfix::content::buildMissionLoadManifest(
                session, request());
        requireAtomicFailure(
            result,
            MissionLoadManifestIssueKind::parseFailure,
            MissionLoadDependencyKind::missionSetup,
            "embedded setup NUL was not rejected");
        require(
            result.issues.front().setupParseError ==
                    std::optional<MissionSetupParseErrorCode>{
                        MissionSetupParseErrorCode::malformedText} &&
                result.issues.front().sourceOffset ==
                    std::optional<std::uint64_t>{nulOffset},
            "embedded setup NUL lost its exact parser offset");
    }

    requireSetupParseFailure(
        sourceBytes(
            "AddStartPos(\"Room\",coord3d(1e999,2,3),coord3d(4,5,6));"),
        MissionSetupParseErrorCode::invalidNumber,
        "non-finite setup number was not typed");

    std::string tooManyStarts;
    for (std::size_t index = 0U;
         index <= airfix::assets::legacyMissionStartCapacity;
         ++index) {
        tooManyStarts +=
            "AddStartPos(\"Room\",coord3d(1,2,3),coord3d(4,5,6));";
    }
    requireSetupParseFailure(
        sourceBytes(tooManyStarts),
        MissionSetupParseErrorCode::startPositionLimitExceeded,
        "seventeenth setup start was not rejected");

    {
        const auto pack = makePack(happyEntries());
        auto session = openSession(pack);
        MissionLoadManifestLimits limits;
        limits.setup.maximumRoomNameBytes = 3U;
        const auto result =
            airfix::content::buildMissionLoadManifest(
                session, request(), limits);
        requireAtomicFailure(
            result,
            MissionLoadManifestIssueKind::parseFailure,
            MissionLoadDependencyKind::missionSetup,
            "setup room-name limit was not enforced");
        require(
            result.issues.front().setupParseError ==
                std::optional<MissionSetupParseErrorCode>{
                    MissionSetupParseErrorCode::roomNameLimitExceeded},
            "setup room-name failure lost its typed parser code");
    }
}

void testObjectRootAndLateFailuresAreAtomic() {
    {
        auto entries = happyEntries();
        entries[2].bytes =
            airfix::testing::makeSyntheticObjectDefinition(
                airfix::testing::kSyntheticAlternateCcfLogicalPath,
                airfix::testing::kSyntheticTextureRoot,
                airfix::assets::ObjectDefinitionKind::model);
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, request()),
            MissionLoadManifestIssueKind::
                objectDefinitionKindMismatch,
            MissionLoadDependencyKind::objectDefinition,
            "OBJE reference to a MODL-root definition was accepted");
    }
    {
        auto entries = happyEntries();
        constexpr std::string_view objects[]{
            airfix::testing::kSyntheticObjectLogicalPath,
            "Game/Objects/Late.object",
        };
        constexpr std::string_view models[]{
            airfix::testing::kSyntheticModelLogicalPath,
        };
        entries[0].bytes = airfix::testing::makeSyntheticLevel(
            airfix::testing::kSyntheticWorldLogicalPath,
            objects,
            models);
        entries.push_back({
            .logicalPath = "Game/Objects/Late.object",
            .bytes = {0x00U, 0x01U, 0x02U},
        });
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        const auto result =
            airfix::content::buildMissionLoadManifest(
                session, request());
        requireAtomicFailure(
            result,
            MissionLoadManifestIssueKind::parseFailure,
            MissionLoadDependencyKind::objectDefinition,
            "late malformed OBJE published a partial manifest");
        require(
            result.issues.front().dependencyIndex ==
                std::optional<std::size_t>{1U},
            "late OBJE failure lost its placement index");
    }
    {
        auto entries = happyEntries();
        entries.erase(entries.begin() + 7);
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, request()),
            MissionLoadManifestIssueKind::notFound,
            MissionLoadDependencyKind::objectCcf,
            "late object CCF failure published a partial manifest");
    }
}

void testReadParseAndStructuralLimitFailures() {
    const auto requireEntryFailure = [](
        const std::size_t entryIndex,
        const bool corruptRead,
        const MissionLoadDependencyKind dependency,
        const MissionLoadManifestIssueKind expected,
        const std::string_view message) {
        auto entries = happyEntries();
        if (corruptRead) {
            const auto decodedSize = static_cast<std::uint32_t>(
                entries[entryIndex].bytes.size());
            entries[entryIndex].bytes = {0xFFU};
            entries[entryIndex].flags =
                airfix::udsp::kCompressedFlag;
            entries[entryIndex].unpackedSize = decodedSize;
        }
        else {
            entries[entryIndex].bytes = {0x00U};
        }
        const auto pack = makePack(entries);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, request()),
            expected,
            dependency,
            message);
    };

    requireEntryFailure(
        0U,
        true,
        MissionLoadDependencyKind::level,
        MissionLoadManifestIssueKind::readFailure,
        "Level read failure was not typed");
    requireEntryFailure(
        1U,
        true,
        MissionLoadDependencyKind::world,
        MissionLoadManifestIssueKind::readFailure,
        "World read failure was not typed");
    requireEntryFailure(
        2U,
        true,
        MissionLoadDependencyKind::objectDefinition,
        MissionLoadManifestIssueKind::readFailure,
        "OBJE read failure was not typed");
    requireEntryFailure(
        happyEntries().size() - 1U,
        true,
        MissionLoadDependencyKind::missionSetup,
        MissionLoadManifestIssueKind::readFailure,
        "setup read failure was not typed");
    requireEntryFailure(
        0U,
        false,
        MissionLoadDependencyKind::level,
        MissionLoadManifestIssueKind::parseFailure,
        "Level parse failure was not typed");
    requireEntryFailure(
        1U,
        false,
        MissionLoadDependencyKind::world,
        MissionLoadManifestIssueKind::parseFailure,
        "World parse failure was not typed");

    const auto pack = makePack(happyEntries());
    {
        auto session = openSession(pack);
        MissionLoadManifestLimits limits;
        limits.maximumPublishedCpuBytes = 0U;
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, request(), limits),
            MissionLoadManifestIssueKind::invalidLimits,
            MissionLoadDependencyKind::level,
            "zero required limit was not rejected");
    }
    {
        auto session = openSession(pack);
        MissionLoadManifestLimits limits;
        limits.entries.maximumPlacements = 2U;
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, request(), limits),
            MissionLoadManifestIssueKind::placementLimitExceeded,
            MissionLoadDependencyKind::objectDefinition,
            "combined OBJE/MODL placement limit was not enforced");
    }
}

void testExactAndOneUnderBudgets() {
    auto entries = happyEntries();
    for (auto& entry : entries) {
        if (entry.logicalPath ==
                airfix::testing::kSyntheticLevelLogicalPath ||
            entry.logicalPath ==
                airfix::testing::kSyntheticWorldLogicalPath ||
            entry.logicalPath ==
                airfix::testing::kSyntheticObjectLogicalPath ||
            entry.logicalPath ==
                airfix::testing::kSyntheticMissionSetupLogicalPath) {
            const auto unpackedSize =
                static_cast<std::uint32_t>(entry.bytes.size());
            entry.bytes = literalCompression(entry.bytes);
            entry.flags = airfix::udsp::kCompressedFlag;
            entry.unpackedSize = unpackedSize;
        }
    }
    const auto pack = makePack(entries);

    MissionLoadManifestLimits exact;
    {
        auto session = openSession(pack);
        exact.maximumSetupSourceBytes = static_cast<std::size_t>(
            footprint(
                session,
                airfix::testing::kSyntheticMissionSetupLogicalPath));
        exact.setup.maximumSourceBytes = static_cast<std::size_t>(
            decodedSize(
                session,
                airfix::testing::kSyntheticMissionSetupLogicalPath));
        exact.maximumLevelSourceBytes = static_cast<std::size_t>(
            footprint(
                session,
                airfix::testing::kSyntheticLevelLogicalPath));
        exact.maximumWorldSourceBytes = static_cast<std::size_t>(
            footprint(
                session,
                airfix::testing::kSyntheticWorldLogicalPath));
        exact.maximumObjectDefinitionSourceBytes =
            static_cast<std::size_t>(footprint(
                session,
                airfix::testing::kSyntheticObjectLogicalPath));
    }
    MissionLoadManifestResult baseline;
    {
        auto session = openSession(pack);
        baseline = airfix::content::buildMissionLoadManifest(
            session, request(), exact);
    }
    require(baseline.success(), "exact compressed source limits failed");
    {
        auto session = openSession(pack);
        const auto expectedDefinitionFootprint =
            footprint(
                session,
                airfix::testing::kSyntheticMissionSetupLogicalPath) +
            footprint(
                session,
                airfix::testing::kSyntheticLevelLogicalPath) +
            footprint(
                session,
                airfix::testing::kSyntheticWorldLogicalPath) +
            footprint(
                session,
                airfix::testing::kSyntheticObjectLogicalPath);
        require(
            baseline.manifest->definitionSourceFootprintBytes() ==
                    expectedDefinitionFootprint &&
                baseline.manifest->setupSourceFootprintBytes() ==
                    footprint(
                        session,
                        airfix::testing::kSyntheticMissionSetupLogicalPath),
            "definition aggregate did not include setup allocation footprint");
    }

    exact.maximumTotalDefinitionSourceBytes =
        baseline.manifest->definitionSourceFootprintBytes();
    exact.maximumTotalCcfSourceBytes =
        baseline.manifest->plannedCcfSourceFootprintBytes();
    exact.maximumPublishedCpuBytes =
        baseline.manifest->publishedCpuBytes();
    exact.maximumCcfSources = baseline.manifest->ccfLoads().size();
    exact.maximumCcfSourceBytes = static_cast<std::size_t>(
        std::ranges::max(
            baseline.manifest->ccfLoads(),
            {},
            &airfix::content::MissionCcfLoadDescriptor::
                sourceAllocationFootprintBytes)
            .sourceAllocationFootprintBytes);
    {
        auto session = openSession(pack);
        require(
            airfix::content::buildMissionLoadManifest(
                session, request(), exact).success(),
            "exact aggregate/published limits failed");
    }

    const auto requireOneUnder = [&](
        MissionLoadManifestLimits limits,
        const MissionLoadManifestIssueKind kind,
        const MissionLoadDependencyKind dependency,
        const std::string_view message) {
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, request(), limits),
            kind,
            dependency,
            message);
    };
    {
        auto limits = exact;
        --limits.maximumSetupSourceBytes;
        requireOneUnder(
            limits,
            MissionLoadManifestIssueKind::sourceLimitExceeded,
            MissionLoadDependencyKind::missionSetup,
            "one-under compressed setup allocation limit succeeded");
    }
    {
        auto limits = exact;
        --limits.setup.maximumSourceBytes;
        requireOneUnder(
            limits,
            MissionLoadManifestIssueKind::sourceLimitExceeded,
            MissionLoadDependencyKind::missionSetup,
            "one-under clamped setup parser source limit succeeded");
    }
    {
        auto limits = exact;
        limits.maximumTotalDefinitionSourceBytes =
            baseline.manifest->setupSourceFootprintBytes() - 1U;
        requireOneUnder(
            limits,
            MissionLoadManifestIssueKind::
                aggregateDefinitionSourceLimitExceeded,
            MissionLoadDependencyKind::missionSetup,
            "setup was not charged to the definition aggregate");
    }
    {
        auto limits = exact;
        --limits.maximumLevelSourceBytes;
        requireOneUnder(
            limits,
            MissionLoadManifestIssueKind::sourceLimitExceeded,
            MissionLoadDependencyKind::level,
            "one-under compressed Level limit succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumWorldSourceBytes;
        requireOneUnder(
            limits,
            MissionLoadManifestIssueKind::sourceLimitExceeded,
            MissionLoadDependencyKind::world,
            "one-under compressed World limit succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumObjectDefinitionSourceBytes;
        requireOneUnder(
            limits,
            MissionLoadManifestIssueKind::sourceLimitExceeded,
            MissionLoadDependencyKind::objectDefinition,
            "one-under compressed OBJE limit succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumTotalDefinitionSourceBytes;
        requireOneUnder(
            limits,
            MissionLoadManifestIssueKind::
                aggregateDefinitionSourceLimitExceeded,
            MissionLoadDependencyKind::objectDefinition,
            "one-under definition aggregate succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumCcfSourceBytes;
        requireOneUnder(
            limits,
            MissionLoadManifestIssueKind::sourceLimitExceeded,
            MissionLoadDependencyKind::objectCcf,
            "one-under CCF per-source limit succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumTotalCcfSourceBytes;
        requireOneUnder(
            limits,
            MissionLoadManifestIssueKind::
                aggregateCcfSourceLimitExceeded,
            MissionLoadDependencyKind::objectCcf,
            "one-under CCF aggregate succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumCcfSources;
        requireOneUnder(
            limits,
            MissionLoadManifestIssueKind::
                ccfSourceCountLimitExceeded,
            MissionLoadDependencyKind::mainWorldCcf,
            "one-under CCF source count succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumPublishedCpuBytes;
        requireOneUnder(
            limits,
            MissionLoadManifestIssueKind::
                publishedCpuLimitExceeded,
            MissionLoadDependencyKind::level,
            "one-under published CPU limit succeeded");
    }
}

void testCancellationCallbacksAndRevisionBinding() {
    const auto pack = makePack(happyEntries());
    {
        auto session = openSession(pack);
        std::stop_source source;
        source.request_stop();
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, request(), {}, source.get_token()),
            MissionLoadManifestIssueKind::cancelled,
            MissionLoadDependencyKind::level,
            "pre-cancelled manifest build succeeded");
    }
    {
        auto session = openSession(pack);
        std::optional<MissionLoadManifestRequest> externalRequest{
            request()};
        std::optional<MissionLoadManifestLimits> externalLimits{
            MissionLoadManifestLimits{}};
        bool destroyed = false;
        const auto result =
            airfix::content::buildMissionLoadManifest(
                session,
                *externalRequest,
                *externalLimits,
                {},
                [&](const auto& progress) {
                    if (!destroyed &&
                        progress.phase ==
                            MissionLoadManifestPhase::validatingRequest &&
                        progress.completedItems == 0U) {
                        externalRequest.reset();
                        externalLimits.reset();
                        destroyed = true;
                    }
                });
        require(
            destroyed && result.success() &&
                result.manifest->startPositions().size() == 1U,
            "manifest dereferenced request or limits after its first callback");
    }
    {
        auto session = openSession(pack);
        std::stop_source source;
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session,
                request(),
                {},
                source.get_token(),
                [&](const auto& progress) {
                    if (progress.phase ==
                            MissionLoadManifestPhase::parsingMissionSetup &&
                        progress.completedItems == 1U) {
                        source.request_stop();
                    }
                }),
            MissionLoadManifestIssueKind::cancelled,
            MissionLoadDependencyKind::level,
            "post-setup cancellation published a manifest");
    }
    {
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session,
                request(),
                {},
                {},
                [](const auto&) {
                    throw std::runtime_error("callback failed");
                }),
            MissionLoadManifestIssueKind::progressCallbackFailure,
            MissionLoadDependencyKind::level,
            "progress callback exception escaped");
    }
    {
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session,
                request(),
                {},
                {},
                [](const auto& progress) {
                    if (progress.phase ==
                        MissionLoadManifestPhase::complete) {
                        throw std::runtime_error(
                            "late callback failed");
                    }
                }),
            MissionLoadManifestIssueKind::progressCallbackFailure,
            MissionLoadDependencyKind::level,
            "late callback exception published a manifest");
    }
    {
        auto session = openSession(pack);
        std::stop_source source;
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session,
                request(),
                {},
                source.get_token(),
                [&](const auto& progress) {
                    if (progress.phase ==
                            MissionLoadManifestPhase::
                                readingObjectDefinitions &&
                        progress.completedItems == 0U) {
                        source.request_stop();
                    }
                }),
            MissionLoadManifestIssueKind::cancelled,
            MissionLoadDependencyKind::level,
            "mid-transaction cancellation published a manifest");
    }
    {
        auto session = openSession(pack);
        std::stop_source source;
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session,
                request(),
                {},
                source.get_token(),
                [&](const auto& progress) {
                    if (progress.phase ==
                            MissionLoadManifestPhase::
                                resolvingMissionEntries &&
                        progress.completedItems == 2U) {
                        source.request_stop();
                    }
                }),
            MissionLoadManifestIssueKind::cancelled,
            MissionLoadDependencyKind::level,
            "resolution-loop cancellation published a manifest");
    }
    {
        auto session = openSession(pack, 81U);
        // Same revision, different authenticated stream handle: only the
        // opaque transaction identity can detect this replacement.
        auto replacement = openSession(pack, 81U);
        bool replaced = false;
        const auto result =
            airfix::content::buildMissionLoadManifest(
                session,
                request(),
                {},
                {},
                [&](const auto&) {
                    if (!replaced) {
                        session = std::move(replacement);
                        replaced = true;
                    }
                });
        require(
            replaced,
            "session replacement callback did not run");
        requireAtomicFailure(
            result,
            MissionLoadManifestIssueKind::sessionIdentityChanged,
            MissionLoadDependencyKind::level,
            "session replacement was not detected atomically");
    }
    {
        auto session = openSession(pack, 81U);
        std::optional<VerifiedContentSession> stolen;
        bool moved = false;
        const auto result =
            airfix::content::buildMissionLoadManifest(
                session,
                request(),
                {},
                {},
                [&](const auto& progress) {
                    if (!moved &&
                        progress.phase ==
                            MissionLoadManifestPhase::readingLevel &&
                        progress.completedItems == 0U) {
                        stolen.emplace(std::move(session));
                        moved = true;
                    }
                });
        require(
            moved && stolen.has_value() &&
                stolen->transactionIdentity().valid() &&
                !session.transactionIdentity().valid(),
            "callback did not transfer the authenticated handle");
        requireAtomicFailure(
            result,
            MissionLoadManifestIssueKind::sessionIdentityChanged,
            MissionLoadDependencyKind::level,
            "moving the session handle escaped identity detection");
    }
    {
        auto session = openSession(pack, 81U);
        const auto lookup = session.sourceArchive().lookup(
            airfix::testing::kSyntheticLevelLogicalPath);
        require(
            lookup.status == airfix::udsp::LookupStatus::unique,
            "moved-from read fixture lookup failed");
        auto stolen = std::move(session);
        require(
            stolen.transactionIdentity().valid() &&
                !session.transactionIdentity().valid(),
            "session move did not update opaque handle identity");
        bool prefixRejected = false;
        try {
            (void)session.readSourceFilePrefix(
                lookup.fileIndex, 1U);
        }
        catch (const airfix::content::VerifiedContentError&) {
            prefixRejected = true;
        }
        bool fullReadRejected = false;
        try {
            (void)session.readSourceFile(
                lookup.fileIndex, 1024U);
        }
        catch (const airfix::content::VerifiedContentError&) {
            fullReadRejected = true;
        }
        require(
            prefixRejected && fullReadRejected,
            "moved-from session read did not fail closed");
        requireAtomicFailure(
            airfix::content::buildMissionLoadManifest(
                session, request()),
            MissionLoadManifestIssueKind::sessionIdentityChanged,
            MissionLoadDependencyKind::level,
            "moved-from input session was not rejected");
    }
    {
        auto first = openSession(pack, 81U);
        auto second = openSession(pack, 82U);
        const auto result =
            airfix::content::buildMissionLoadManifest(
                first, request());
        require(
            result.success() &&
                result.manifest->belongsTo(first) &&
                !result.manifest->belongsTo(second),
            "manifest revision binding did not reject a different session");
    }
}

void testMovedFromProofsArePoisoned() {
    const auto pack = makePack(happyEntries());
    auto session = openSession(pack);
    const auto expectedSetupFootprint = footprint(
        session,
        airfix::testing::kSyntheticMissionSetupLogicalPath);
    const auto retainsSetupProof = [&](const auto& manifest) {
        return manifest.setupEntry().logicalPath ==
                "Game\\Missions\\Setup\\DetachedFlight.afs" &&
            manifest.startPositions().size() == 1U &&
            manifest.startPositions()[0].roomName == "Room" &&
            manifest.setupSourceFootprintBytes() ==
                expectedSetupFootprint;
    };
    auto source =
        airfix::content::buildMissionLoadManifest(
            session, request());
    require(
        source.success() && source.manifest->valid(),
        "move proof fixture failed");

    MissionLoadManifestResult moved = std::move(source);
    require(
        source.manifest.has_value() &&
            !source.success() &&
            !source.manifest->valid() &&
            !source.manifest->belongsTo(session),
        "move construction left the source proof usable");
    require(
        moved.success() &&
            moved.manifest->valid() &&
            moved.manifest->belongsTo(session) &&
            retainsSetupProof(*moved.manifest),
        "move construction lost or poisoned setup provenance");

    auto assignmentSource =
        airfix::content::buildMissionLoadManifest(
            session, request());
    MissionLoadManifestResult assignmentDestination;
    assignmentDestination = std::move(assignmentSource);
    require(
        assignmentSource.manifest.has_value() &&
            !assignmentSource.success() &&
            !assignmentSource.manifest->valid() &&
            assignmentDestination.success() &&
            assignmentDestination.manifest->valid() &&
            retainsSetupProof(*assignmentDestination.manifest),
        "move assignment did not preserve setup proof while poisoning source");
}

} // namespace

int main() {
    try {
        testHappyPathOrderingCachingAndOwnership();
        testAuthenticatedPlayerVisualDescriptor();
        testPlayerVisualFailuresAreTypedAndAtomic();
        testPlayerVisualExactBudgetsAndLateAtomicity();
        testLookupFailuresAreTypedAndAtomic();
        testSetupParsingIsTypedBoundedAndOwned();
        testObjectRootAndLateFailuresAreAtomic();
        testReadParseAndStructuralLimitFailures();
        testExactAndOneUnderBudgets();
        testCancellationCallbacksAndRevisionBinding();
        testMovedFromProofsArePoisoned();
        std::cout << "Mission load manifest tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "Mission load manifest tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
