#include "airfix/content/MissionWorldRoomLoader.hpp"
#include "airfix/content/MissionWorldRoomLoaderDetail.hpp"
#include "support/SyntheticContent.hpp"
#include "support/SyntheticLegacyAssets.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using airfix::assets::MissionStartPosition;
using airfix::content::ContentRevision;
using airfix::content::MissionLoadManifestResult;
using airfix::content::MissionWorldRoomLoadIssueKind;
using airfix::content::MissionWorldRoomLoadLimits;
using airfix::content::MissionWorldRoomLoadPhase;
using airfix::content::MissionWorldRoomLoadRequest;
using airfix::content::MissionWorldRoomLoadResult;
using airfix::content::VerifiedContentSession;
using airfix::render::TextureAssetId;
using airfix::testing::Bytes;
using airfix::testing::SyntheticAfPack;
using airfix::testing::UdspInputEntry;

inline constexpr std::string_view kObjectOnlyTexture = "ObjectOnly";
inline constexpr std::string_view kObjectOnlyGtiLogicalPath =
    "Graphics/Textures/ObjectOnly.gti";

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] ContentRevision
revisionFor(const SyntheticAfPack &pack,
            const std::uint64_t generation = 101U) {
    return {
        .generation = generation,
        .pack =
            {
                .size = pack.size,
                .sha256 = pack.sha256,
            },
    };
}

class TestInputStore final {
  public:
    ~TestInputStore() {
        for (const auto &path : paths_) {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    }

    TestInputStore(const TestInputStore &) = delete;
    TestInputStore &operator=(const TestInputStore &) = delete;

    [[nodiscard]] static TestInputStore &shared() {
        static TestInputStore store;
        return store;
    }

    [[nodiscard]] std::unique_ptr<std::ifstream>
    open(const SyntheticAfPack &pack) {
        static std::atomic<std::uint64_t> sequence{0U};
        const auto tick =
            std::chrono::steady_clock::now().time_since_epoch().count();
        const auto path =
            std::filesystem::temp_directory_path() /
            ("airfix-mission-world-room-" + std::to_string(tick) + "-" +
             std::to_string(sequence.fetch_add(1U, std::memory_order_relaxed)) +
             ".afpack");
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output ||
                (!pack.bytes.empty() &&
                 !output.write(
                     reinterpret_cast<const char *>(pack.bytes.data()),
                     static_cast<std::streamsize>(pack.bytes.size())))) {
                throw std::runtime_error(
                    "failed to create mission loader test input");
            }
        }
        paths_.push_back(path);
        auto input = std::make_unique<std::ifstream>(path, std::ios::binary |
                                                               std::ios::ate);
        if (!*input) {
            throw std::runtime_error(
                "failed to open mission loader test input");
        }
        return input;
    }

  private:
    TestInputStore() = default;

    std::vector<std::filesystem::path> paths_;
};

[[nodiscard]] VerifiedContentSession
openSession(const SyntheticAfPack &pack, const std::uint64_t generation = 101U,
            std::string sourceLabel = "nonexistent/never/reopened.afpack") {
    return VerifiedContentSession::open(TestInputStore::shared().open(pack),
                                        std::move(sourceLabel),
                                        revisionFor(pack, generation));
}

[[nodiscard]] Bytes
literalCompression(const std::span<const std::uint8_t> decoded) {
    Bytes encoded;
    std::size_t offset = 0U;
    while (offset < decoded.size()) {
        const auto count = std::min<std::size_t>(255U, decoded.size() - offset);
        encoded.push_back(0x66U);
        encoded.push_back(static_cast<std::uint8_t>(count));
        encoded.insert(encoded.end(),
                       decoded.begin() + static_cast<std::ptrdiff_t>(offset),
                       decoded.begin() +
                           static_cast<std::ptrdiff_t>(offset + count));
        offset += count;
    }
    return encoded;
}

struct FixtureOptions {
    bool compressedCcfs{};
    bool malformedObjectCcf{};
    bool malformedDetailGti{};
    bool objectUsesMainCcf{};
    bool backdropUsesMainCcf{};
};

[[nodiscard]] std::vector<UdspInputEntry>
missionEntries(const FixtureOptions options = {}) {
    constexpr std::string_view objectPaths[]{
        airfix::testing::kSyntheticObjectLogicalPath,
        airfix::testing::kSyntheticObjectLogicalPath,
    };
    const std::array<std::string_view, 1U> backdrops{
        options.backdropUsesMainCcf
            ? airfix::testing::kSyntheticCcfLogicalPath
            : airfix::testing::kSyntheticBackdropCcfLogicalPath,
    };

    auto mainCcf = airfix::testing::makeSyntheticLegacyCcf({
        .primaryTexture = "Wall",
        .secondaryTexture = std::string{"Detail"},
        .placedTranslation = {1.0F, 2.0F, 3.0F},
        .placedRoomReference = 999U,
    });
    auto backdropCcf = airfix::testing::makeSyntheticLegacyCcf({
        .primaryTexture = "Detail",
        .secondaryTexture = std::string{"Wall"},
        .placedTranslation = {4.0F, 5.0F, 6.0F},
        .placedRoomReference = 20U,
    });
    auto objectCcf =
        options.malformedObjectCcf
            ? Bytes{0x00U}
            : airfix::testing::makeSyntheticLegacyCcf({
                  .primaryTexture = std::string{kObjectOnlyTexture},
                  .secondaryTexture = std::nullopt,
                  .placedTranslation = {7.0F, 8.0F, 9.0F},
                  .placedRoomReference = 20U,
              });

    const auto maybeCompress = [&](Bytes bytes) {
        if (!options.compressedCcfs) {
            return std::pair<Bytes, std::optional<std::uint32_t>>{
                std::move(bytes), std::nullopt};
        }
        const auto unpacked = static_cast<std::uint32_t>(bytes.size());
        return std::pair<Bytes, std::optional<std::uint32_t>>{
            literalCompression(bytes), unpacked};
    };
    auto [mainStored, mainUnpacked] = maybeCompress(std::move(mainCcf));
    auto [backdropStored, backdropUnpacked] =
        maybeCompress(std::move(backdropCcf));
    auto [objectStored, objectUnpacked] = maybeCompress(std::move(objectCcf));

    return {
        {
            .logicalPath =
                std::string(airfix::testing::kSyntheticLevelLogicalPath),
            .bytes = airfix::testing::makeSyntheticLevel(
                airfix::testing::kSyntheticWorldLogicalPath, objectPaths),
            .flags = 0U,
            .unpackedSize = std::nullopt,
        },
        {
            .logicalPath =
                std::string(airfix::testing::kSyntheticWorldLogicalPath),
            .bytes = airfix::testing::makeSyntheticWorldWithBackdrops(
                airfix::testing::kSyntheticCcfLogicalPath,
                airfix::testing::kSyntheticTextureRoot, backdrops),
            .flags = 0U,
            .unpackedSize = std::nullopt,
        },
        {
            .logicalPath =
                std::string(airfix::testing::kSyntheticObjectLogicalPath),
            .bytes = airfix::testing::makeSyntheticObjectDefinition(
                options.objectUsesMainCcf
                    ? airfix::testing::kSyntheticCcfLogicalPath
                    : airfix::testing::kSyntheticAlternateCcfLogicalPath,
                options.objectUsesMainCcf
                    ? std::string_view{"Graphics/ObjectTextures"}
                    : airfix::testing::kSyntheticTextureRoot),
            .flags = 0U,
            .unpackedSize = std::nullopt,
        },
        {
            .logicalPath =
                std::string(airfix::testing::kSyntheticCcfLogicalPath),
            .bytes = std::move(mainStored),
            .flags =
                options.compressedCcfs ? airfix::udsp::kCompressedFlag : 0U,
            .unpackedSize = mainUnpacked,
        },
        {
            .logicalPath =
                std::string(airfix::testing::kSyntheticBackdropCcfLogicalPath),
            .bytes = std::move(backdropStored),
            .flags =
                options.compressedCcfs ? airfix::udsp::kCompressedFlag : 0U,
            .unpackedSize = backdropUnpacked,
        },
        {
            .logicalPath =
                std::string(airfix::testing::kSyntheticAlternateCcfLogicalPath),
            .bytes = std::move(objectStored),
            .flags =
                options.compressedCcfs ? airfix::udsp::kCompressedFlag : 0U,
            .unpackedSize = objectUnpacked,
        },
        {
            .logicalPath =
                std::string(airfix::testing::kSyntheticWallGtiLogicalPath),
            .bytes = airfix::testing::makeSyntheticRgba8Gti(
                airfix::testing::kSyntheticWallRgba),
            .flags = 0U,
            .unpackedSize = std::nullopt,
        },
        {
            .logicalPath =
                std::string(airfix::testing::kSyntheticDetailGtiLogicalPath),
            .bytes =
                options.malformedDetailGti
                    ? Bytes{0x00U}
                    : airfix::testing::makeSyntheticRgba8Gti(
                          airfix::testing::kSyntheticDetailRgba, 0x87654321U),
            .flags = 0U,
            .unpackedSize = std::nullopt,
        },
    };
}

[[nodiscard]] SyntheticAfPack makePack(const FixtureOptions options = {}) {
    auto entries = missionEntries(options);
    return airfix::testing::makeSyntheticAfPack(
        std::span<const UdspInputEntry>(entries));
}

[[nodiscard]] MissionLoadManifestResult
buildManifest(VerifiedContentSession &session) {
    return airfix::content::buildMissionLoadManifest(
        session, {.levelLogicalPath = std::string(
                      airfix::testing::kSyntheticLevelLogicalPath)});
}

[[nodiscard]] bool hasIssue(const MissionWorldRoomLoadResult &result,
                            const MissionWorldRoomLoadIssueKind kind) {
    return std::ranges::any_of(result.issues, [kind](const auto &issue) {
        return issue.kind == kind;
    });
}

void requireAtomicFailure(const MissionWorldRoomLoadResult &result,
                          const MissionWorldRoomLoadIssueKind kind,
                          const std::string_view message) {
    require(!result.success() && !result.room.has_value() &&
                hasIssue(result, kind),
            message);
}

[[nodiscard]] MissionWorldRoomLoadRequest rootRequest() { return {}; }

[[nodiscard]] std::array<MissionStartPosition, 1U> roomStartPositions() {
    return {MissionStartPosition{
        .roomName = "Room",
        .position = {10.0F, 20.0F, 30.0F},
        .axisRotation = {0.1F, 0.2F, 0.3F},
        .sourceOffset = 44U,
    }};
}

void requireOneTriangle(const airfix::content::LoadedMissionWorldRoom &room,
                        const std::size_t expectedSourceIndex,
                        const airfix::render::Vec3 expectedTranslation) {
    require(room.model.meshes.size() == 1U &&
                room.model.instances.size() == 1U &&
                room.meshProvenance.size() == 1U &&
                room.instanceProvenance.size() == 1U,
            "selected mission room did not publish one mesh and instance");
    require(room.model.meshes[0].vertices.size() == 3U &&
                room.model.meshes[0].indices ==
                    std::vector<std::uint32_t>{0U, 1U, 2U} &&
                room.model.instances[0].modelTranslation == expectedTranslation,
            "selected mission room geometry or transform changed");
    require(room.meshProvenance[0].sourceIndex == expectedSourceIndex &&
                room.instanceProvenance[0].sourceIndex == expectedSourceIndex,
            "semantic CCF source provenance was lost");
    require(room.submission.meshUploads.size() == 1U &&
                room.submission.commands.size() == 1U &&
                room.submission.commands[0].indexCount == 3U,
            "selected triangle did not produce one submission command");
}

void testRootFallbackOrderingCachingAndPoisonTexture() {
    const auto pack = makePack();
    auto session = openSession(pack);
    auto manifest = buildManifest(session);
    require(manifest.success(), "root fixture manifest failed");

    std::vector<std::size_t> ccfProgressTotals;
    const auto result = airfix::content::loadMissionWorldRoom(
        session, *manifest.manifest, rootRequest(), {}, {},
        [&](const auto &progress) {
            if (progress.phase ==
                MissionWorldRoomLoadPhase::loadingCcfSources) {
                ccfProgressTotals.push_back(progress.totalItems);
            }
        });

    require(result.success() && result.room.has_value(),
            "valid aggregate root load failed");
    const auto &room = *result.room;
    require(room.revision == revisionFor(pack) &&
                room.startSelection.source ==
                    airfix::assets::MissionWorldStartSelectionSource::
                        rootRoomFallback &&
                room.startSelection.worldRoomIndex == 0U &&
                !room.selectedStart.has_value(),
            "empty start table did not select the root fallback");
    require(
        room.semanticCcfSourceCount == 4U && room.uniqueCcfSourceCount == 3U &&
            room.ccfCacheIndexByLoadSource ==
                std::vector<std::size_t>{0U, 1U, 2U, 2U} &&
            !ccfProgressTotals.empty() &&
            std::ranges::all_of(ccfProgressTotals,
                                [](const auto total) { return total == 3U; }),
        "repeated object CCF was not cached once while retaining loads");

    requireOneTriangle(room, 0U, {1.0F, 2.0F, 3.0F});
    require(room.textures.size() == 2U &&
                room.textures[0].assetId == TextureAssetId{0U} &&
                room.textures[1].assetId == TextureAssetId{1U},
            "root texture imports are not one dense global namespace");
    require(room.submission.commands[0].primary ==
                    std::optional<TextureAssetId>{TextureAssetId{0U}} &&
                room.submission.commands[0].secondary ==
                    std::optional<TextureAssetId>{TextureAssetId{1U}},
            "root Wall then Detail first-use order changed");
    require(std::equal(room.textures[0].uploadLevels[0].pixels.begin(),
                       room.textures[0].uploadLevels[0].pixels.end(),
                       airfix::testing::kSyntheticWallRgba.begin(),
                       airfix::testing::kSyntheticWallRgba.end()) &&
                std::equal(room.textures[1].uploadLevels[0].pixels.begin(),
                           room.textures[1].uploadLevels[0].pixels.end(),
                           airfix::testing::kSyntheticDetailRgba.begin(),
                           airfix::testing::kSyntheticDetailRgba.end()),
            "GTIs were not decoded in canonical root first-use order");

    const auto objectOnlyLookup =
        session.sourceArchive().lookup(kObjectOnlyGtiLogicalPath);
    require(objectOnlyLookup.status == airfix::udsp::LookupStatus::notFound,
            "poison fixture accidentally included the object-only GTI");
}

void testAuthoredStartSelectsBackdropAndOwnsStart() {
    const auto pack = makePack();
    auto session = openSession(pack);
    auto manifest = buildManifest(session);
    require(manifest.success(), "start fixture manifest failed");
    auto starts = roomStartPositions();
    const MissionWorldRoomLoadRequest request{
        .initialRootName = {},
        .startPositions = starts,
        .requestedStartIndex = std::numeric_limits<std::uint32_t>::max(),
        .basis = {},
        .uvPolicy = airfix::render::UvPolicy::preserveRaw,
    };

    const auto result = airfix::content::loadMissionWorldRoom(
        session, *manifest.manifest, request);
    starts[0].roomName = "mutated-after-call";

    require(result.success() && result.room.has_value(),
            "valid authored start load failed");
    const auto &room = *result.room;
    require(room.startSelection.source ==
                    airfix::assets::MissionWorldStartSelectionSource::table &&
                room.startSelection.startPositionIndex ==
                    std::optional<std::size_t>{0U} &&
                room.startSelection.worldRoomIndex == 1U &&
                room.selectedStart.has_value() &&
                room.selectedStart->roomName == "Room" &&
                room.selectedStart->position ==
                    std::array<float, 3U>{10.0F, 20.0F, 30.0F},
            "authored start selection or owned copy changed");
    requireOneTriangle(room, 1U, {4.0F, 5.0F, 6.0F});
    require(room.textures.size() == 2U &&
                room.submission.commands[0].primary ==
                    std::optional<TextureAssetId>{TextureAssetId{0U}} &&
                room.submission.commands[0].secondary ==
                    std::optional<TextureAssetId>{TextureAssetId{1U}} &&
                std::equal(room.textures[0].uploadLevels[0].pixels.begin(),
                           room.textures[0].uploadLevels[0].pixels.end(),
                           airfix::testing::kSyntheticDetailRgba.begin(),
                           airfix::testing::kSyntheticDetailRgba.end()) &&
                std::equal(room.textures[1].uploadLevels[0].pixels.begin(),
                           room.textures[1].uploadLevels[0].pixels.end(),
                           airfix::testing::kSyntheticWallRgba.begin(),
                           airfix::testing::kSyntheticWallRgba.end()),
            "backdrop Detail then Wall first-use namespace changed");
}

void testOnePhysicalCcfRetainsDifferentSemanticLoads() {
    const auto pack = makePack({
        .objectUsesMainCcf = true,
        .backdropUsesMainCcf = true,
    });
    auto session = openSession(pack);
    auto manifest = buildManifest(session);
    require(manifest.success(), "shared physical CCF manifest failed");
    require(manifest.manifest->ccfLoads().size() == 4U &&
                manifest.manifest->ccfLoads()[0].source.archiveFileIndex ==
                    manifest.manifest->ccfLoads()[2].source.archiveFileIndex &&
                manifest.manifest->ccfLoads()[0].textureRoot !=
                    manifest.manifest->ccfLoads()[2].textureRoot &&
                manifest.manifest->ccfLoads()[0].placedSceneEnabled &&
                !manifest.manifest->ccfLoads()[2].placedSceneEnabled,
            "fixture did not create distinct semantics for one physical CCF");

    const auto result = airfix::content::loadMissionWorldRoom(
        session, *manifest.manifest, rootRequest());
    require(result.success() && result.room.has_value(),
            "shared physical CCF aggregate load failed");
    require(result.room->semanticCcfSourceCount == 4U &&
                result.room->uniqueCcfSourceCount == 1U &&
                result.room->ccfCacheIndexByLoadSource ==
                    std::vector<std::size_t>{0U, 0U, 0U, 0U},
            "physical CCF cache erased semantic role/root/flag loads");
    const auto &room = *result.room;
    require(room.model.meshes.size() == 2U &&
                room.model.instances.size() == 2U &&
                room.meshProvenance.size() == 2U &&
                room.instanceProvenance.size() == 2U &&
                room.meshProvenance[0].sourceIndex == 0U &&
                room.meshProvenance[1].sourceIndex == 1U &&
                room.instanceProvenance[0].sourceIndex == 0U &&
                room.instanceProvenance[1].sourceIndex == 1U &&
                room.submission.commands.size() == 2U,
            "shared CCF semantic contributors or provenance were collapsed");
    for (const auto &command : room.submission.commands) {
        require(
            command.primary ==
                    std::optional<TextureAssetId>{TextureAssetId{0U}} &&
                command.secondary ==
                    std::optional<TextureAssetId>{TextureAssetId{1U}},
            "per-source bindings did not converge on the global texture IDs");
    }
}

void testProofRevisionAndSessionIdentityGuards() {
    const auto pack = makePack();
    {
        auto manifestSession = openSession(pack, 201U);
        auto manifest = buildManifest(manifestSession);
        auto loadSession = openSession(pack, 201U);
        require(manifest.success() &&
                    airfix::content::loadMissionWorldRoom(
                        loadSession, *manifest.manifest, rootRequest())
                        .success(),
                "separately authenticated equal revision was rejected");
    }
    {
        auto manifestSession = openSession(pack, 201U);
        auto manifest = buildManifest(manifestSession);
        auto differentGeneration = openSession(pack, 202U);
        requireAtomicFailure(
            airfix::content::loadMissionWorldRoom(
                differentGeneration, *manifest.manifest, rootRequest()),
            MissionWorldRoomLoadIssueKind::manifestRevisionMismatch,
            "different content generation was accepted");
    }
    {
        auto sourceSession = openSession(pack, 201U);
        auto manifest = buildManifest(sourceSession);
        auto exactSession = std::move(sourceSession);
        require(airfix::content::loadMissionWorldRoom(
                    exactSession, *manifest.manifest, rootRequest())
                    .success(),
                "the exact authenticated handle was not usable after move");
        requireAtomicFailure(
            airfix::content::loadMissionWorldRoom(
                sourceSession, *manifest.manifest, rootRequest()),
            MissionWorldRoomLoadIssueKind::sessionIdentityChanged,
            "moved-from input session was accepted");
    }
    {
        auto session = openSession(pack);
        auto manifest = buildManifest(session);
        MissionLoadManifestResult moved = std::move(manifest);
        require(moved.success(), "moved proof destination was invalid");
        requireAtomicFailure(airfix::content::loadMissionWorldRoom(
                                 session, *manifest.manifest, rootRequest()),
                             MissionWorldRoomLoadIssueKind::invalidManifest,
                             "moved-from manifest was accepted");
    }
    {
        auto session = openSession(pack, 203U);
        auto manifest = buildManifest(session);
        auto replacement = openSession(pack, 203U);
        bool replaced = false;
        const auto result = airfix::content::loadMissionWorldRoom(
            session, *manifest.manifest, rootRequest(), {}, {},
            [&](const auto &progress) {
                if (!replaced &&
                    progress.phase ==
                        MissionWorldRoomLoadPhase::loadingCcfSources) {
                    session = std::move(replacement);
                    replaced = true;
                }
            });
        require(replaced, "session replacement callback did not run");
        requireAtomicFailure(
            result, MissionWorldRoomLoadIssueKind::sessionIdentityChanged,
            "same-revision session replacement escaped detection");
    }
    {
        auto session = openSession(pack, 204U);
        auto manifest = buildManifest(session);
        auto replacement = openSession(pack, 204U);
        const auto result = airfix::content::loadMissionWorldRoom(
            session, *manifest.manifest, rootRequest(), {}, {},
            [&](const auto &) {
                session = std::move(replacement);
                throw std::runtime_error(
                    "identity change must outrank callback failure");
            });
        requireAtomicFailure(
            result, MissionWorldRoomLoadIssueKind::sessionIdentityChanged,
            "callback exception outranked a session identity change");
    }
}

void testCallerInputsAreSnapshottedBeforeCallbacks() {
    const auto pack = makePack();
    auto session = openSession(pack);
    auto manifest = buildManifest(session);
    require(manifest.success(), "snapshot fixture manifest failed");
    auto startsArray = roomStartPositions();
    std::vector<MissionStartPosition> starts(startsArray.begin(),
                                             startsArray.end());
    MissionWorldRoomLoadLimits limits;
    const MissionWorldRoomLoadRequest request{
        .initialRootName = {},
        .startPositions = starts,
        .requestedStartIndex = 0U,
        .basis = {},
        .uvPolicy = airfix::render::UvPolicy::preserveRaw,
    };
    bool invalidated = false;
    const auto result = airfix::content::loadMissionWorldRoom(
        session, *manifest.manifest, request, limits, {},
        [&](const auto &progress) {
            if (!invalidated &&
                progress.phase == MissionWorldRoomLoadPhase::validatingInput) {
                manifest.manifest.reset();
                starts.clear();
                starts.shrink_to_fit();
                limits.maximumCcfSourceBytes = 0U;
                invalidated = true;
            }
        });

    require(invalidated, "input invalidation callback did not run");
    require(result.success() && result.room.has_value() &&
                result.room->selectedStart.has_value() &&
                result.room->selectedStart->roomName == "Room",
            "loader dereferenced caller-owned input after its first callback");
    requireOneTriangle(*result.room, 1U, {4.0F, 5.0F, 6.0F});
}

void testCancellationCallbacksAndInvalidStartsAreAtomic() {
    const auto pack = makePack();
    {
        auto session = openSession(pack);
        auto manifest = buildManifest(session);
        std::vector<MissionStartPosition> starts(
            airfix::assets::legacyMissionStartCapacity + 1U);
        bool callbackRan = false;
        const auto result = airfix::content::loadMissionWorldRoom(
            session, *manifest.manifest,
            {
                .initialRootName = {},
                .startPositions = starts,
                .requestedStartIndex = 0U,
                .basis = {},
                .uvPolicy = airfix::render::UvPolicy::preserveRaw,
            },
            {}, {}, [&](const auto &) { callbackRan = true; });
        requireAtomicFailure(
            result, MissionWorldRoomLoadIssueKind::startResolutionFailure,
            "oversized legacy start table was snapshotted");
        require(!callbackRan && result.issues.front().startIssue ==
                                    airfix::assets::MissionWorldStartIssueKind::
                                        startPositionLimitExceeded,
                "oversized start table was not rejected before callbacks");
    }
    {
        auto session = openSession(pack);
        auto manifest = buildManifest(session);
        auto starts = roomStartPositions();
        starts[0].roomName = "123456789";
        MissionWorldRoomLoadLimits limits;
        limits.starts.maximumNameComponentBytes = 8U;
        bool callbackRan = false;
        const auto result = airfix::content::loadMissionWorldRoom(
            session, *manifest.manifest,
            {
                .initialRootName = {},
                .startPositions = starts,
                .requestedStartIndex = 0U,
                .basis = {},
                .uvPolicy = airfix::render::UvPolicy::preserveRaw,
            },
            limits, {}, [&](const auto &) { callbackRan = true; });
        requireAtomicFailure(
            result, MissionWorldRoomLoadIssueKind::startResolutionFailure,
            "oversized start name was deep-copied");
        require(!callbackRan && result.issues.front().startIssue ==
                                    airfix::assets::MissionWorldStartIssueKind::
                                        nameComponentLimitExceeded,
                "oversized start name was not rejected before callbacks");
    }
    {
        auto session = openSession(pack);
        auto manifest = buildManifest(session);
        MissionWorldRoomLoadLimits limits;
        limits.catalog.maximumNameComponentBytes = 8U;
        MissionWorldRoomLoadRequest request = rootRequest();
        request.initialRootName.name = "123456789";
        bool callbackRan = false;
        const auto result = airfix::content::loadMissionWorldRoom(
            session, *manifest.manifest, request, limits, {},
            [&](const auto &) { callbackRan = true; });
        requireAtomicFailure(result,
                             MissionWorldRoomLoadIssueKind::catalogFailure,
                             "oversized initial root name was deep-copied");
        require(!callbackRan &&
                    result.issues.front().catalogIssue ==
                        airfix::assets::MissionWorldRoomBuildIssueKind::
                            nameComponentLimitExceeded,
                "oversized root name was not rejected before callbacks");
    }
    {
        auto session = openSession(pack);
        auto manifest = buildManifest(session);
        std::stop_source source;
        const auto result = airfix::content::loadMissionWorldRoom(
            session, *manifest.manifest, rootRequest(), {}, source.get_token(),
            [&](const auto &) {
                source.request_stop();
                throw std::runtime_error(
                    "callback failure must retain deterministic precedence");
            });
        requireAtomicFailure(
            result, MissionWorldRoomLoadIssueKind::progressCallbackFailure,
            "callback cancellation changed callback-failure precedence");
    }
    {
        auto session = openSession(pack);
        auto manifest = buildManifest(session);
        std::stop_source source;
        source.request_stop();
        requireAtomicFailure(airfix::content::loadMissionWorldRoom(
                                 session, *manifest.manifest, rootRequest(), {},
                                 source.get_token()),
                             MissionWorldRoomLoadIssueKind::cancelled,
                             "pre-cancelled aggregate load succeeded");
    }
    {
        auto session = openSession(pack);
        auto manifest = buildManifest(session);
        std::stop_source source;
        requireAtomicFailure(
            airfix::content::loadMissionWorldRoom(
                session, *manifest.manifest, rootRequest(), {},
                source.get_token(),
                [&](const auto &progress) {
                    if (progress.phase ==
                            MissionWorldRoomLoadPhase::loadingCcfSources &&
                        progress.completedItems == 1U) {
                        source.request_stop();
                    }
                }),
            MissionWorldRoomLoadIssueKind::cancelled,
            "mid-CCF cancellation published a room");
    }
    {
        auto session = openSession(pack);
        auto manifest = buildManifest(session);
        requireAtomicFailure(
            airfix::content::loadMissionWorldRoom(
                session, *manifest.manifest, rootRequest(), {}, {},
                [](const auto &progress) {
                    if (progress.phase == MissionWorldRoomLoadPhase::complete) {
                        throw std::runtime_error("late callback failed");
                    }
                }),
            MissionWorldRoomLoadIssueKind::progressCallbackFailure,
            "complete callback failure published a room");
    }
    {
        auto session = openSession(pack);
        auto manifest = buildManifest(session);
        auto starts = roomStartPositions();
        starts[0].position[1] = std::numeric_limits<float>::infinity();
        requireAtomicFailure(
            airfix::content::loadMissionWorldRoom(
                session, *manifest.manifest,
                {
                    .initialRootName = {},
                    .startPositions = starts,
                    .requestedStartIndex = 0U,
                    .basis = {},
                    .uvPolicy = airfix::render::UvPolicy::preserveRaw,
                }),
            MissionWorldRoomLoadIssueKind::invalidStartPosition,
            "non-finite player start was accepted");
    }
    {
        auto session = openSession(pack);
        auto manifest = buildManifest(session);
        auto starts = roomStartPositions();
        starts[0].roomName = "MissingRoom";
        requireAtomicFailure(
            airfix::content::loadMissionWorldRoom(
                session, *manifest.manifest,
                {
                    .initialRootName = {},
                    .startPositions = starts,
                    .requestedStartIndex = 0U,
                    .basis = {},
                    .uvPolicy = airfix::render::UvPolicy::preserveRaw,
                }),
            MissionWorldRoomLoadIssueKind::startResolutionFailure,
            "missing authored room published a result");
    }
}

void testLateCcfAndGtiFailuresAreAtomic() {
    {
        const auto pack = makePack({
            .malformedObjectCcf = true,
        });
        auto session = openSession(pack);
        auto manifest = buildManifest(session);
        require(manifest.success(),
                "metadata-only manifest read a malformed object CCF");
        const auto result = airfix::content::loadMissionWorldRoom(
            session, *manifest.manifest, rootRequest());
        requireAtomicFailure(
            result, MissionWorldRoomLoadIssueKind::ccfParseFailure,
            "late draw-disabled object CCF failure leaked prior data");
        require(result.issues.front().sourceIndex ==
                    std::optional<std::size_t>{2U},
                "late CCF issue lost its first semantic source index");
    }
    {
        const auto pack = makePack({
            .malformedDetailGti = true,
        });
        auto session = openSession(pack);
        auto manifest = buildManifest(session);
        require(manifest.success(), "malformed GTI manifest failed");
        requireAtomicFailure(
            airfix::content::loadMissionWorldRoom(session, *manifest.manifest,
                                                  rootRequest()),
            MissionWorldRoomLoadIssueKind::texturePreparationFailure,
            "second GTI failure published first texture or model");
    }
    {
        const auto pack = makePack();
        auto session = openSession(pack);
        auto manifest = buildManifest(session);
        MissionWorldRoomLoadLimits limits;
        limits.draw.maximumInstances = 0U;
        requireAtomicFailure(
            airfix::content::loadMissionWorldRoom(session, *manifest.manifest,
                                                  rootRequest(), limits),
            MissionWorldRoomLoadIssueKind::drawAssemblyFailure,
            "late draw limit published decoded textures");
    }
    {
        const auto pack = makePack();
        auto session = openSession(pack);
        auto manifest = buildManifest(session);
        MissionWorldRoomLoadLimits limits;
        limits.submission.maximumCommands = 0U;
        requireAtomicFailure(
            airfix::content::loadMissionWorldRoom(session, *manifest.manifest,
                                                  rootRequest(), limits),
            MissionWorldRoomLoadIssueKind::submissionFailure,
            "late submission limit published a model");
    }
}

[[nodiscard]] std::uint64_t
sourceFootprint(const VerifiedContentSession &session,
                const std::string_view logicalPath) {
    const auto lookup = session.sourceArchive().lookup(logicalPath);
    require(lookup.status == airfix::udsp::LookupStatus::unique &&
                lookup.fileIndex < session.sourceArchive().files().size(),
            "budget fixture lookup failed");
    const auto &entry = session.sourceArchive().files()[lookup.fileIndex];
    return static_cast<std::uint64_t>(entry.storedSize) +
           (entry.isCompressed()
                ? static_cast<std::uint64_t>(entry.unpackedSize)
                : 0U);
}

[[nodiscard]] std::uint64_t independentPublishedCpuBytes(
    const airfix::content::LoadedMissionWorldRoom &room) {
    std::uint64_t total = sizeof(airfix::content::LoadedMissionWorldRoom);
    total += room.textures.size() * sizeof(airfix::content::LoadedTextureAsset);
    total += room.ccfCacheIndexByLoadSource.size() * sizeof(std::size_t);
    if (room.selectedStart.has_value()) {
        total += room.selectedStart->roomName.size();
    }
    for (const auto &texture : room.textures) {
        total += texture.upload.uploadLevels.size() *
                 sizeof(airfix::render::GtiUploadLevel);
        total +=
            texture.uploadLevels.size() * sizeof(airfix::assets::RgbaImage);
        for (const auto &level : texture.uploadLevels) {
            total += level.pixels.size();
        }
    }
    total += room.model.meshes.size() * sizeof(airfix::render::DrawMeshPayload);
    total +=
        room.model.instances.size() * sizeof(airfix::render::DrawMeshInstance);
    for (const auto &mesh : room.model.meshes) {
        total += mesh.vertices.size() * sizeof(airfix::render::DrawVertex);
        total += mesh.indices.size() * sizeof(std::uint32_t);
        total += mesh.materials.size() * sizeof(airfix::render::DrawMaterial);
        total += mesh.ranges.size() * sizeof(airfix::render::DrawRange);
    }
    total += room.meshProvenance.size() *
             sizeof(airfix::render::MissionWorldRoomMeshProvenance);
    total += room.instanceProvenance.size() *
             sizeof(airfix::render::MissionWorldRoomInstanceProvenance);
    total += room.submission.meshUploads.size() *
             sizeof(airfix::render::DrawMeshUploadMetadata);
    total += room.submission.commands.size() *
             sizeof(airfix::render::DrawSubmissionCommand);
    return total;
}

void testAccountingOverflowBoundaries() {
    using airfix::content::detail::checkedMissionWorldRoomByteAdd;
    using airfix::content::detail::checkedMissionWorldRoomByteProduct;

    auto total = std::numeric_limits<std::uint64_t>::max() - 1U;
    require(!checkedMissionWorldRoomByteAdd(total, 2U) &&
                total == std::numeric_limits<std::uint64_t>::max() - 1U,
            "byte-add overflow mutated its accumulator");
    require(checkedMissionWorldRoomByteAdd(total, 1U) &&
                total == std::numeric_limits<std::uint64_t>::max(),
            "byte-add exact boundary was rejected");

    std::uint64_t product = 73U;
    require(!checkedMissionWorldRoomByteProduct(
                std::numeric_limits<std::size_t>::max(), 2U, product) &&
                product == 73U,
            "byte-product overflow mutated its result");
    const auto exactCount = std::numeric_limits<std::size_t>::max() / 2U;
    require(checkedMissionWorldRoomByteProduct(exactCount, 2U, product) &&
                product == static_cast<std::uint64_t>(exactCount) * 2U,
            "byte-product exact platform boundary was rejected");
}

void testExactAndOneUnderBudgets() {
    const auto pack = makePack({
        .compressedCcfs = true,
    });
    MissionWorldRoomLoadLimits exact;
    std::uint64_t maximumCcfFootprint = 0U;
    std::uint64_t maximumTextureFootprint = 0U;
    {
        auto session = openSession(pack);
        auto manifest = buildManifest(session);
        require(manifest.success(), "budget fixture manifest failed");
        const auto baseline = airfix::content::loadMissionWorldRoom(
            session, *manifest.manifest, rootRequest());
        require(baseline.success() && baseline.room.has_value(),
                "budget baseline failed");
        const auto &room = *baseline.room;
        std::unordered_set<std::size_t> uniqueCcfFiles;
        std::uint64_t independentUniqueCcfFootprint = 0U;
        for (const auto &descriptor : manifest.manifest->ccfLoads()) {
            if (uniqueCcfFiles.insert(descriptor.source.archiveFileIndex)
                    .second) {
                independentUniqueCcfFootprint +=
                    descriptor.sourceAllocationFootprintBytes;
            }
        }
        const auto independentTextureFootprint =
            sourceFootprint(session,
                            airfix::testing::kSyntheticWallGtiLogicalPath) +
            sourceFootprint(session,
                            airfix::testing::kSyntheticDetailGtiLogicalPath);
        std::uint64_t independentDecoded = 0U;
        std::uint64_t independentUpload = 0U;
        std::uint64_t independentResident = 0U;
        for (const auto &texture : room.textures) {
            std::uint64_t decodedPixels = 0U;
            for (const auto &level : texture.uploadLevels) {
                decodedPixels += level.pixels.size();
            }
            require(decodedPixels == texture.upload.decodedRgbaBytes,
                    "decoded upload plan does not match owned pixels");
            independentDecoded += decodedPixels;
            independentUpload += texture.upload.uploadRgbaBytes;
            independentResident += texture.upload.residentRgbaBytes;
        }
        require(room.ccfCacheIndexByLoadSource ==
                        std::vector<std::size_t>{0U, 1U, 2U, 2U} &&
                    room.uniqueCcfSourceFootprintBytes ==
                        independentUniqueCcfFootprint &&
                    room.textureSourceFootprintBytes ==
                        independentTextureFootprint &&
                    room.decodedRgbaBytes == independentDecoded &&
                    room.uploadRgbaBytes == independentUpload &&
                    room.residentRgbaBytes == independentResident &&
                    room.publishedCpuBytes ==
                        independentPublishedCpuBytes(room),
                "published audit counters do not match owned payloads");
        exact.maximumCcfSources = room.semanticCcfSourceCount;
        exact.maximumUniqueCcfSources = room.uniqueCcfSourceCount;
        exact.maximumTotalUniqueCcfSourceBytes =
            room.uniqueCcfSourceFootprintBytes;
        exact.maximumRetainedCcfMetadataBytesAfterParse =
            room.retainedCcfMetadataBytes;
        exact.maximumTextureAssets = room.textures.size();
        exact.maximumTotalTextureSourceBytes = room.textureSourceFootprintBytes;
        exact.maximumDecodedRgbaBytes = room.decodedRgbaBytes;
        exact.maximumUploadRgbaBytes = room.uploadRgbaBytes;
        exact.maximumResidentRgbaBytes = room.residentRgbaBytes;
        exact.maximumPublishedCpuBytes = room.publishedCpuBytes;

        for (const auto &descriptor : manifest.manifest->ccfLoads()) {
            maximumCcfFootprint = std::max(
                maximumCcfFootprint, descriptor.sourceAllocationFootprintBytes);
        }
        maximumTextureFootprint = std::max(
            sourceFootprint(session,
                            airfix::testing::kSyntheticWallGtiLogicalPath),
            sourceFootprint(session,
                            airfix::testing::kSyntheticDetailGtiLogicalPath));
        exact.maximumCcfSourceBytes =
            static_cast<std::size_t>(maximumCcfFootprint);
        exact.maximumTextureSourceBytes =
            static_cast<std::size_t>(maximumTextureFootprint);
        exact.gtiPerTexture.maximumSourceBytes =
            exact.maximumTextureSourceBytes;
    }
    {
        auto session = openSession(pack);
        auto manifest = buildManifest(session);
        require(airfix::content::loadMissionWorldRoom(
                    session, *manifest.manifest, rootRequest(), exact)
                    .success(),
                "exact aggregate loader budgets failed");
    }

    const auto requireOneUnder = [&](MissionWorldRoomLoadLimits limits,
                                     const MissionWorldRoomLoadIssueKind issue,
                                     const std::string_view message) {
        auto session = openSession(pack);
        auto manifest = buildManifest(session);
        requireAtomicFailure(
            airfix::content::loadMissionWorldRoom(session, *manifest.manifest,
                                                  rootRequest(), limits),
            issue, message);
    };
    {
        auto limits = exact;
        --limits.maximumCcfSources;
        requireOneUnder(
            limits, MissionWorldRoomLoadIssueKind::ccfSourceCountLimitExceeded,
            "one-under semantic CCF count succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumUniqueCcfSources;
        requireOneUnder(
            limits,
            MissionWorldRoomLoadIssueKind::uniqueCcfSourceCountLimitExceeded,
            "one-under unique CCF count succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumCcfSourceBytes;
        requireOneUnder(limits,
                        MissionWorldRoomLoadIssueKind::ccfSourceLimitExceeded,
                        "one-under compressed CCF peak succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumTotalUniqueCcfSourceBytes;
        requireOneUnder(
            limits,
            MissionWorldRoomLoadIssueKind::aggregateCcfSourceLimitExceeded,
            "one-under unique CCF aggregate succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumRetainedCcfMetadataBytesAfterParse;
        requireOneUnder(
            limits,
            MissionWorldRoomLoadIssueKind::retainedCcfMetadataLimitExceeded,
            "one-under post-parse retained CCF metadata succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumTextureAssets;
        requireOneUnder(
            limits, MissionWorldRoomLoadIssueKind::textureAssetLimitExceeded,
            "one-under texture asset count succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumTextureSourceBytes;
        limits.gtiPerTexture.maximumSourceBytes =
            limits.maximumTextureSourceBytes;
        requireOneUnder(
            limits, MissionWorldRoomLoadIssueKind::textureSourceLimitExceeded,
            "one-under GTI source peak succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumTotalTextureSourceBytes;
        requireOneUnder(
            limits,
            MissionWorldRoomLoadIssueKind::aggregateTextureSourceLimitExceeded,
            "one-under GTI aggregate succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumDecodedRgbaBytes;
        requireOneUnder(limits,
                        MissionWorldRoomLoadIssueKind::decodedRgbaLimitExceeded,
                        "one-under decoded RGBA aggregate succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumUploadRgbaBytes;
        requireOneUnder(limits,
                        MissionWorldRoomLoadIssueKind::uploadRgbaLimitExceeded,
                        "one-under upload RGBA aggregate succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumResidentRgbaBytes;
        requireOneUnder(
            limits, MissionWorldRoomLoadIssueKind::residentRgbaLimitExceeded,
            "one-under resident RGBA aggregate succeeded");
    }
    {
        auto limits = exact;
        --limits.maximumPublishedCpuBytes;
        requireOneUnder(
            limits, MissionWorldRoomLoadIssueKind::publishedCpuLimitExceeded,
            "one-under published CPU aggregate succeeded");
    }
}

} // namespace

int main() {
    try {
        testRootFallbackOrderingCachingAndPoisonTexture();
        testAuthoredStartSelectsBackdropAndOwnsStart();
        testOnePhysicalCcfRetainsDifferentSemanticLoads();
        testProofRevisionAndSessionIdentityGuards();
        testCallerInputsAreSnapshottedBeforeCallbacks();
        testCancellationCallbacksAndInvalidStartsAreAtomic();
        testLateCcfAndGtiFailuresAreAtomic();
        testAccountingOverflowBoundaries();
        testExactAndOneUnderBudgets();
        std::cout << "Mission world room loader tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Mission world room loader tests failed: " << error.what()
                  << '\n';
        return 1;
    }
}
