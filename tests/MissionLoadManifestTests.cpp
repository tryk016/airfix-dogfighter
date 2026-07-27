#include "airfix/content/MissionLoadManifest.hpp"
#include "support/SyntheticContent.hpp"
#include "support/SyntheticLegacyAssets.hpp"

#include <algorithm>
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
using airfix::testing::Bytes;
using airfix::testing::SyntheticAfPack;
using airfix::testing::UdspInputEntry;

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
    };
}

[[nodiscard]] SyntheticAfPack makePack(
    const std::vector<UdspInputEntry>& entries) {
    return airfix::testing::makeSyntheticAfPack(
        std::span<const UdspInputEntry>(entries));
}

[[nodiscard]] MissionLoadManifestRequest request(
    std::string levelPath =
        std::string(airfix::testing::kSyntheticLevelLogicalPath)) {
    return {.levelLogicalPath = std::move(levelPath)};
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
        manifest.publishedCpuBytes() > 0U &&
            manifest.definitionSourceFootprintBytes() > 0U &&
            manifest.plannedCcfSourceFootprintBytes() ==
                loads[0].sourceAllocationFootprintBytes +
                    loads[1].sourceAllocationFootprintBytes +
                    loads[2].sourceAllocationFootprintBytes +
                    loads[3].sourceAllocationFootprintBytes,
        "manifest accounting mismatch");
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
                airfix::testing::kSyntheticObjectLogicalPath) {
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
            moved.manifest->belongsTo(session),
        "move construction poisoned the destination proof");

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
            assignmentDestination.manifest->valid(),
        "move assignment did not poison only the source proof");
}

} // namespace

int main() {
    try {
        testHappyPathOrderingCachingAndOwnership();
        testLookupFailuresAreTypedAndAtomic();
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
