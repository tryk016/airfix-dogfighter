#include "airfix/content/WorldRoomLoader.hpp"
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
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using airfix::content::ContentRevision;
using airfix::content::LoadedWorldRoom;
using airfix::content::VerifiedContentSession;
using airfix::content::WorldRoomLoadIssueKind;
using airfix::content::WorldRoomLoadLimits;
using airfix::content::WorldRoomLoadPhase;
using airfix::content::WorldRoomLoadRequest;
using airfix::content::WorldRoomLoadResult;
using airfix::render::TextureAssetId;
using airfix::testing::Bytes;
using airfix::testing::SyntheticAfPack;
using airfix::testing::SyntheticLegacyAssets;
using airfix::testing::UdspInputEntry;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] ContentRevision revisionFor(
    const SyntheticAfPack& pack,
    const std::uint64_t generation = 71U) {
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
            ("airfix-world-room-input-" + std::to_string(tick) + "-" +
                std::to_string(sequence.fetch_add(
                    1U, std::memory_order_relaxed)) + ".afpack");
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output || (!pack.bytes.empty() && !output.write(
                    reinterpret_cast<const char*>(pack.bytes.data()),
                    static_cast<std::streamsize>(pack.bytes.size())))) {
                throw std::runtime_error(
                    "failed to create world room test input");
            }
        }
        paths_.push_back(path);
        auto input = std::make_unique<std::ifstream>(
            path, std::ios::binary | std::ios::ate);
        if (!*input) {
            throw std::runtime_error(
                "failed to open world room test input");
        }
        return input;
    }

private:
    TestInputStore() = default;

    std::vector<std::filesystem::path> paths_;
};

[[nodiscard]] VerifiedContentSession openSession(
    const SyntheticAfPack& pack,
    const std::uint64_t generation = 71U) {
    return VerifiedContentSession::open(
        TestInputStore::shared().open(pack),
        "synthetic-world-room.afpack",
        revisionFor(pack, generation));
}

[[nodiscard]] std::vector<UdspInputEntry> sourceEntries(
    const SyntheticLegacyAssets& assets) {
    std::vector<UdspInputEntry> entries{
        {
            .logicalPath =
                std::string(airfix::testing::kSyntheticWorldLogicalPath),
            .bytes = assets.world,
            .flags = 0U,
            .unpackedSize = std::nullopt,
        },
        {
            .logicalPath =
                std::string(airfix::testing::kSyntheticCcfLogicalPath),
            .bytes = assets.ccf,
            .flags = 0U,
            .unpackedSize = std::nullopt,
        },
        {
            .logicalPath =
                std::string(airfix::testing::kSyntheticWallGtiLogicalPath),
            .bytes = assets.wallGti,
            .flags = 0U,
            .unpackedSize = std::nullopt,
        },
    };
    if (assets.detailGti.has_value()) {
        entries.push_back({
            .logicalPath =
                std::string(airfix::testing::kSyntheticDetailGtiLogicalPath),
            .bytes = *assets.detailGti,
            .flags = 0U,
            .unpackedSize = std::nullopt,
        });
    }
    return entries;
}

[[nodiscard]] SyntheticAfPack makePack(
    std::vector<UdspInputEntry> entries) {
    return airfix::testing::makeSyntheticAfPack(
        std::span<const UdspInputEntry>(entries));
}

[[nodiscard]] SyntheticAfPack makePack(
    const SyntheticLegacyAssets& assets) {
    return makePack(sourceEntries(assets));
}

[[nodiscard]] Bytes paddedLiteralCompression(
    const std::span<const std::uint8_t> decoded,
    const std::size_t zeroLengthBlocks) {
    Bytes encoded;
    encoded.reserve(zeroLengthBlocks * 2U + decoded.size() + 2U);
    for (std::size_t index = 0U; index < zeroLengthBlocks; ++index) {
        encoded.push_back(0x66U);
        encoded.push_back(0U);
    }
    std::size_t offset = 0U;
    while (offset < decoded.size()) {
        const auto count =
            std::min<std::size_t>(255U, decoded.size() - offset);
        encoded.push_back(0x66U);
        encoded.push_back(static_cast<std::uint8_t>(count));
        encoded.insert(
            encoded.end(),
            decoded.begin() + static_cast<std::ptrdiff_t>(offset),
            decoded.begin() + static_cast<std::ptrdiff_t>(offset + count));
        offset += count;
    }
    return encoded;
}

[[nodiscard]] WorldRoomLoadRequest request(
    const std::size_t roomIndex = 1U,
    std::string worldPath =
        std::string(airfix::testing::kSyntheticWorldLogicalPath)) {
    return {
        .worldLogicalPath = std::move(worldPath),
        .ccfRoomIndex = roomIndex,
    };
}

[[nodiscard]] bool hasIssue(
    const WorldRoomLoadResult& result,
    const WorldRoomLoadIssueKind kind) {
    return std::ranges::any_of(
        result.issues,
        [kind](const auto& issue) { return issue.kind == kind; });
}

void requireAtomicFailure(
    const WorldRoomLoadResult& result,
    const WorldRoomLoadIssueKind kind,
    const std::string_view message) {
    require(
        !result.success() &&
            !result.room.has_value() &&
            hasIssue(result, kind),
        message);
}

void requireOneTriangleRoom(
    const LoadedWorldRoom& room,
    const ContentRevision& revision) {
    require(room.revision == revision, "content revision was not retained");
    require(
        room.model.meshes.size() == 1U &&
            room.model.instances.size() == 1U,
        "room did not publish one shared mesh and one instance");
    require(
        room.model.meshes[0].vertices.size() == 3U &&
            room.model.meshes[0].indices ==
                std::vector<std::uint32_t>{0U, 1U, 2U} &&
            room.model.meshes[0].ranges.size() == 1U,
        "synthetic triangle geometry changed");
    require(
        room.model.instances[0].sourceNodeReference == 100U &&
            room.model.instances[0].modelTranslation ==
                airfix::render::Vec3{4.0F, 5.0F, 6.0F},
        "placed instance identity or transform changed");
    require(
        room.submission.meshUploads.size() == 1U &&
            room.submission.commands.size() == 1U &&
            room.submission.commands[0].indexCount == 3U &&
            room.submission.commands[0].materialState ==
                airfix::render::DrawMaterialState{
                    .lightingMode = 2U,
                    .gouraudShading = true,
                    .blendMode = 3U,
                    .flag2151 = true,
                    .scalar2140 = 0.625F,
                    .firstVector2140 = {1.25F, 2.5F, 3.75F},
                    .secondVector2140 = {4.5F, 5.25F, 6.75F},
                },
        "one triangle lost its non-default material state");
}

void testHappyOrdinaryRoomAndExactRgba() {
    const auto assets = airfix::testing::makeSyntheticLegacyAssets();
    const auto pack = makePack(assets);
    const auto revision = revisionFor(pack, 91U);
    auto session = openSession(pack, revision.generation);
    const auto result =
        airfix::content::loadWorldRoom(session, request());

    require(result.success() && result.room.has_value(),
        "valid ordinary room was rejected");
    requireOneTriangleRoom(*result.room, revision);
    require(result.room->textures.size() == 1U,
        "one texture dependency did not produce one texture asset");

    const auto& texture = result.room->textures[0];
    require(
        texture.assetId == TextureAssetId{0U} &&
            texture.upload.request.assetId == TextureAssetId{0U} &&
            texture.uploadLevels.size() == 1U,
        "dense texture asset ID zero or its upload was not retained");
    require(
        texture.uploadLevels[0].width == 2U &&
            texture.uploadLevels[0].height == 2U &&
            std::equal(
                texture.uploadLevels[0].pixels.begin(),
                texture.uploadLevels[0].pixels.end(),
                airfix::testing::kSyntheticWallRgba.begin(),
                airfix::testing::kSyntheticWallRgba.end()),
        "GTI pixels were not decoded to the exact authored RGBA values");
    require(
        result.room->submission.commands[0].primary ==
            std::optional<TextureAssetId>{TextureAssetId{0U}},
        "draw submission lost texture asset ID zero");
}

void testPrimaryRoomIsValidAndEmpty() {
    const auto assets = airfix::testing::makeSyntheticLegacyAssets();
    const auto pack = makePack(assets);
    const auto revision = revisionFor(pack);
    auto session = openSession(pack);
    const auto result =
        airfix::content::loadWorldRoom(session, request(0U));

    require(result.success() && result.room.has_value(),
        "valid empty receiver room was rejected");
    require(
        result.room->revision == revision &&
            result.room->model.meshes.empty() &&
            result.room->model.instances.empty() &&
            result.room->submission.meshUploads.empty() &&
            result.room->submission.commands.empty() &&
            result.room->textures.empty(),
        "empty receiver room published unrelated geometry or textures");
}

void testWorldCcffSelectsExactCcfAmongTwo() {
    const auto assets = airfix::testing::makeSyntheticLegacyAssets();
    auto entries = sourceEntries(assets);
    entries.push_back({
        .logicalPath =
            std::string(airfix::testing::kSyntheticAlternateCcfLogicalPath),
        .bytes = airfix::testing::makeSyntheticLegacyCcf({
            .primaryTexture = "Wall",
            .secondaryTexture = std::nullopt,
            .placedTranslation = {99.0F, 98.0F, 97.0F},
        }),
        .flags = 0U,
        .unpackedSize = std::nullopt,
    });
    const auto pack = makePack(std::move(entries));
    auto session = openSession(pack);
    const auto result =
        airfix::content::loadWorldRoom(session, request());

    require(result.success() && result.room.has_value(),
        "two CCF entries made the exact CCFF selection ambiguous");
    require(
        result.room->model.instances.size() == 1U &&
            result.room->model.instances[0].modelTranslation ==
                airfix::render::Vec3{4.0F, 5.0F, 6.0F},
        "loader selected a CCF other than the world's exact CCFF path");
}

void testLookupParseAndRoomFailuresAreAtomic() {
    const auto assets = airfix::testing::makeSyntheticLegacyAssets();
    {
        const auto pack = makePack(assets);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::loadWorldRoom(
                session, request(1U, "Game/Worlds/Missing.world")),
            WorldRoomLoadIssueKind::worldNotFound,
            "missing World did not fail atomically");
    }
    {
        auto entries = sourceEntries(assets);
        entries[0].bytes = Bytes{'N', 'O', 'T', '-', 'W', 'O', 'R', 'L', 'D'};
        const auto pack = makePack(std::move(entries));
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::loadWorldRoom(session, request()),
            WorldRoomLoadIssueKind::worldParseFailure,
            "malformed World did not fail atomically");
    }
    {
        auto missingCcfAssets = assets;
        missingCcfAssets.world = airfix::testing::makeSyntheticWorld(
            "Graphics/Missing.ccf");
        const auto pack = makePack(missingCcfAssets);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::loadWorldRoom(session, request()),
            WorldRoomLoadIssueKind::ccfNotFound,
            "missing CCF did not fail atomically");
    }
    {
        auto malformedCcfAssets = assets;
        malformedCcfAssets.ccf = Bytes{'N', 'O', 'T', '-', 'C', 'C', 'F'};
        const auto pack = makePack(malformedCcfAssets);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::loadWorldRoom(session, request()),
            WorldRoomLoadIssueKind::ccfParseFailure,
            "malformed CCF did not fail atomically");
    }
    {
        const auto pack = makePack(assets);
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::loadWorldRoom(session, request(2U)),
            WorldRoomLoadIssueKind::textureResolutionFailure,
            "out-of-range physical room did not fail atomically");
    }
}

void testCancellationIsTypedAndAtomic() {
    const auto assets = airfix::testing::makeSyntheticLegacyAssets();
    const auto pack = makePack(assets);
    auto session = openSession(pack);
    std::stop_source cancellation;
    std::size_t callbackCount = 0U;

    const auto result = airfix::content::loadWorldRoom(
        session,
        request(),
        {},
        cancellation.get_token(),
        [&](const airfix::content::WorldRoomLoadProgress& progress) {
            if (progress.phase == WorldRoomLoadPhase::loadingTextures) {
                ++callbackCount;
                cancellation.request_stop();
            }
        });
    require(callbackCount == 1U, "texture-stage cancellation hook was not reached");
    requireAtomicFailure(
        result,
        WorldRoomLoadIssueKind::cancelled,
        "cancellation published a partial room");
}

void testCompressedSourcePeakBudgetsRejectBeforeRead() {
    const auto assets = airfix::testing::makeSyntheticLegacyAssets();
    const auto compressed = [](UdspInputEntry& entry) {
        entry.bytes.assign(64U, 0xA5U);
        entry.flags = airfix::udsp::kCompressedFlag;
        entry.unpackedSize = 64U;
    };
    {
        auto entries = sourceEntries(assets);
        compressed(entries[0]);
        const auto pack = makePack(std::move(entries));
        auto session = openSession(pack);
        WorldRoomLoadLimits limits;
        limits.maximumWorldSourceBytes = 64U;
        requireAtomicFailure(
            airfix::content::loadWorldRoom(session, request(), limits),
            WorldRoomLoadIssueKind::worldSourceLimitExceeded,
            "compressed World peak allocation escaped its source budget");
    }
    {
        auto entries = sourceEntries(assets);
        compressed(entries[1]);
        const auto pack = makePack(std::move(entries));
        auto session = openSession(pack);
        WorldRoomLoadLimits limits;
        limits.maximumCcfSourceBytes = 64U;
        requireAtomicFailure(
            airfix::content::loadWorldRoom(session, request(), limits),
            WorldRoomLoadIssueKind::ccfSourceLimitExceeded,
            "compressed CCF peak allocation escaped its source budget");
    }
    {
        auto entries = sourceEntries(assets);
        compressed(entries[2]);
        const auto pack = makePack(std::move(entries));
        auto session = openSession(pack);
        WorldRoomLoadLimits limits;
        limits.maximumTextureSourceBytes = 64U;
        limits.maximumTotalTextureSourceBytes = 64U;
        limits.gtiPerTexture.maximumSourceBytes = 64U;
        requireAtomicFailure(
            airfix::content::loadWorldRoom(session, request(), limits),
            WorldRoomLoadIssueKind::textureSourceLimitExceeded,
            "compressed GTI peak allocation escaped its source budget");
    }
}

[[nodiscard]] std::uint64_t publishedCpuFootprint(
    const LoadedWorldRoom& room) {
    std::uint64_t total = sizeof(LoadedWorldRoom);
    total += room.textures.size() * sizeof(airfix::content::LoadedTextureAsset);
    for (const auto& texture : room.textures) {
        total += texture.upload.uploadLevels.size() *
            sizeof(airfix::render::GtiUploadLevel);
        total += texture.uploadLevels.size() *
            sizeof(airfix::assets::RgbaImage);
        for (const auto& image : texture.uploadLevels) {
            total += image.pixels.size();
        }
    }
    total += room.model.meshes.size() *
        sizeof(airfix::render::DrawMeshPayload);
    total += room.model.instances.size() *
        sizeof(airfix::render::DrawMeshInstance);
    for (const auto& mesh : room.model.meshes) {
        total += mesh.vertices.size() * sizeof(airfix::render::DrawVertex);
        total += mesh.indices.size() * sizeof(std::uint32_t);
        total += mesh.materials.size() * sizeof(airfix::render::DrawMaterial);
        total += mesh.ranges.size() * sizeof(airfix::render::DrawRange);
    }
    total += room.submission.meshUploads.size() *
        sizeof(airfix::render::DrawMeshUploadMetadata);
    total += room.submission.commands.size() *
        sizeof(airfix::render::DrawSubmissionCommand);
    return total;
}

void testPublishedCpuBudgetExactAndOneBelow() {
    const auto assets = airfix::testing::makeSyntheticLegacyAssets();
    const auto pack = makePack(assets);
    std::uint64_t exactFootprint = 0U;
    {
        auto session = openSession(pack);
        const auto result =
            airfix::content::loadWorldRoom(session, request());
        require(result.success() && result.room.has_value(),
            "baseline room failed before published CPU budget test");
        exactFootprint = publishedCpuFootprint(*result.room);
    }
    {
        WorldRoomLoadLimits limits;
        limits.maximumPublishedCpuBytes = exactFootprint;
        auto session = openSession(pack);
        const auto result =
            airfix::content::loadWorldRoom(session, request(), limits);
        require(result.success() && result.room.has_value(),
            "exact published CPU budget was rejected");
    }
    {
        WorldRoomLoadLimits limits;
        limits.maximumPublishedCpuBytes = exactFootprint - 1U;
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::loadWorldRoom(session, request(), limits),
            WorldRoomLoadIssueKind::publishedCpuLimitExceeded,
            "published CPU budget N-1 was accepted");
    }
}

void testAggregateTextureBudgetsExactAndOneBelow() {
    const auto assets = airfix::testing::makeSyntheticLegacyAssets({
        .includeSecondaryTexture = true,
    });
    require(assets.detailGti.has_value(), "two-texture fixture is incomplete");
    const auto pack = makePack(assets);
    const auto totalSource = static_cast<std::uint64_t>(
        assets.wallGti.size() + assets.detailGti->size());
    constexpr std::uint64_t totalRgba = 32U;

    WorldRoomLoadLimits exact;
    exact.maximumTotalTextureSourceBytes = totalSource;
    exact.maximumDecodedRgbaBytes = totalRgba;
    exact.maximumUploadRgbaBytes = totalRgba;
    exact.maximumResidentRgbaBytes = totalRgba;
    {
        auto session = openSession(pack);
        const auto result =
            airfix::content::loadWorldRoom(session, request(), exact);
        require(
            result.success() && result.room.has_value() &&
                result.room->textures.size() == 2U,
            "exact aggregate texture budgets were rejected");
        require(
            result.room->textures[0].assetId == TextureAssetId{0U} &&
                result.room->textures[1].assetId == TextureAssetId{1U},
            "two texture assets were not published in dense first-use order");
    }
    {
        auto limits = exact;
        --limits.maximumTotalTextureSourceBytes;
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::loadWorldRoom(session, request(), limits),
            WorldRoomLoadIssueKind::textureSourceLimitExceeded,
            "aggregate GTI source budget N-1 was accepted");
    }
    {
        auto limits = exact;
        --limits.maximumDecodedRgbaBytes;
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::loadWorldRoom(session, request(), limits),
            WorldRoomLoadIssueKind::decodedRgbaLimitExceeded,
            "aggregate decoded RGBA budget N-1 was accepted");
    }
    {
        auto limits = exact;
        --limits.maximumUploadRgbaBytes;
        auto session = openSession(pack);
        const auto result =
            airfix::content::loadWorldRoom(session, request(), limits);
        requireAtomicFailure(
            result,
            WorldRoomLoadIssueKind::uploadRgbaLimitExceeded,
            "aggregate upload RGBA budget N-1 was accepted");
    }
    {
        auto limits = exact;
        --limits.maximumResidentRgbaBytes;
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::loadWorldRoom(session, request(), limits),
            WorldRoomLoadIssueKind::residentRgbaLimitExceeded,
            "aggregate resident RGBA budget N-1 was accepted");
    }
}

void testLaterTextureFailurePublishesNothing() {
    auto assets = airfix::testing::makeSyntheticLegacyAssets({
        .includeSecondaryTexture = true,
    });
    require(assets.detailGti.has_value(), "two-texture fixture is incomplete");
    assets.detailGti->front() ^= 0xFFU;
    const auto pack = makePack(assets);
    auto session = openSession(pack);
    const auto result =
        airfix::content::loadWorldRoom(session, request());

    requireAtomicFailure(
        result,
        WorldRoomLoadIssueKind::texturePreparationFailure,
        "later GTI failure leaked the first prepared texture or room");
    require(
        std::ranges::any_of(
            result.issues,
            [](const auto& issue) {
                return issue.kind ==
                        WorldRoomLoadIssueKind::texturePreparationFailure &&
                    issue.textureAssetId ==
                        std::optional<TextureAssetId>{TextureAssetId{1U}};
            }),
        "later GTI failure did not identify dense texture asset ID one");
}

void testRepeatedTextureDependencyIsDeduplicated() {
    SyntheticLegacyAssets assets{
        .world = airfix::testing::makeSyntheticWorld(),
        .ccf = airfix::testing::makeSyntheticLegacyCcf({
            .primaryTexture = "Wall",
            .secondaryTexture = "Wall",
        }),
        .wallGti = airfix::testing::makeSyntheticRgba8Gti(),
        .detailGti = std::nullopt,
    };
    const auto pack = makePack(assets);
    auto session = openSession(pack);
    const auto result =
        airfix::content::loadWorldRoom(session, request());

    require(result.success() && result.room.has_value(),
        "repeated texture dependency was rejected");
    require(
        result.room->textures.size() == 1U &&
            result.room->submission.commands.size() == 1U &&
            result.room->submission.commands[0].primary ==
                std::optional<TextureAssetId>{TextureAssetId{0U}} &&
            result.room->submission.commands[0].secondary ==
                std::optional<TextureAssetId>{TextureAssetId{0U}},
        "one source GTI was duplicated instead of sharing asset ID zero");
}

void testCompressedStoredFootprintIsPreflighted() {
    const auto assets = airfix::testing::makeSyntheticLegacyAssets();
    auto entries = sourceEntries(assets);
    const auto unpackedWorldSize =
        static_cast<std::uint32_t>(entries[0].bytes.size());
    auto encodedWorld =
        paddedLiteralCompression(entries[0].bytes, 1'024U);
    require(
        encodedWorld.size() > entries[0].bytes.size(),
        "compressed regression fixture lacks a larger stored footprint");
    entries[0].bytes = std::move(encodedWorld);
    entries[0].flags = airfix::udsp::kCompressedFlag;
    entries[0].unpackedSize = unpackedWorldSize;
    const auto storedWorldSize = entries[0].bytes.size();
    const auto peakWorldBytes =
        storedWorldSize + static_cast<std::size_t>(unpackedWorldSize);
    const auto pack = makePack(std::move(entries));

    {
        WorldRoomLoadLimits limits;
        limits.maximumWorldSourceBytes = peakWorldBytes;
        auto session = openSession(pack);
        const auto result =
            airfix::content::loadWorldRoom(session, request(), limits);
        require(
            result.success(),
            "exact compressed World peak-allocation limit was rejected");
    }
    {
        WorldRoomLoadLimits limits;
        limits.maximumWorldSourceBytes = peakWorldBytes - 1U;
        auto session = openSession(pack);
        requireAtomicFailure(
            airfix::content::loadWorldRoom(session, request(), limits),
            WorldRoomLoadIssueKind::worldSourceLimitExceeded,
            "compressed World peak allocation bypassed its source limit");
    }
}

} // namespace

int main() {
    try {
        testHappyOrdinaryRoomAndExactRgba();
        testPrimaryRoomIsValidAndEmpty();
        testWorldCcffSelectsExactCcfAmongTwo();
        testLookupParseAndRoomFailuresAreAtomic();
        testCancellationIsTypedAndAtomic();
        testCompressedSourcePeakBudgetsRejectBeforeRead();
        testPublishedCpuBudgetExactAndOneBelow();
        testAggregateTextureBudgetsExactAndOneBelow();
        testLaterTextureFailurePublishesNothing();
        testRepeatedTextureDependencyIsDeduplicated();
        testCompressedStoredFootprintIsPreflighted();
    }
    catch (const std::exception& error) {
        std::cerr << "World room loader test failure: "
                  << error.what() << '\n';
        return 1;
    }
    std::cout << "World room loader tests passed\n";
    return 0;
}
