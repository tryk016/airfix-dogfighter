#include "airfix/assets/WorldCcfTextureResolver.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
using airfix::assets::CcfMetadata;
using airfix::assets::CcfPlacedNodeKind;
using airfix::assets::CcfPlacedNodeMetadata;

struct TestDirectory {
    std::string path;
    std::vector<std::string> files;
};

void appendU32(Bytes& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void writeU32(
    Bytes& bytes,
    const std::size_t offset,
    const std::uint32_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value);
    bytes.at(offset + 1U) = static_cast<std::uint8_t>(value >> 8U);
    bytes.at(offset + 2U) = static_cast<std::uint8_t>(value >> 16U);
    bytes.at(offset + 3U) = static_cast<std::uint8_t>(value >> 24U);
}

void appendRecord(
    Bytes& bytes,
    const std::uint32_t hash,
    const std::uint32_t nameOffset,
    const std::uint32_t field08,
    const std::uint32_t field0C,
    const std::uint32_t field10,
    const std::uint32_t field14) {
    appendU32(bytes, hash);
    appendU32(bytes, nameOffset);
    appendU32(bytes, field08);
    appendU32(bytes, field0C);
    appendU32(bytes, field10);
    appendU32(bytes, field14);
}

[[nodiscard]] Bytes makeArchive(std::vector<TestDirectory> directories) {
    std::stable_sort(
        directories.begin(), directories.end(),
        [](const auto& left, const auto& right) {
            return airfix::udsp::nameHash(left.path) <
                airfix::udsp::nameHash(right.path);
        });
    for (auto& directory : directories) {
        std::stable_sort(
            directory.files.begin(), directory.files.end(),
            [](const auto& left, const auto& right) {
                return airfix::udsp::nameHash(left) <
                    airfix::udsp::nameHash(right);
            });
    }

    Bytes strings;
    struct EncodedDirectory {
        std::uint32_t pathOffset{};
        std::uint32_t firstFileIndex{};
        std::vector<std::uint32_t> fileNameOffsets;
    };
    std::vector<EncodedDirectory> encodedDirectories;
    encodedDirectories.reserve(directories.size());
    std::uint32_t fileCount = 0U;
    for (const auto& directory : directories) {
        EncodedDirectory encoded{
            .pathOffset = static_cast<std::uint32_t>(strings.size()),
            .firstFileIndex = fileCount,
            .fileNameOffsets = {},
        };
        strings.insert(
            strings.end(), directory.path.begin(), directory.path.end());
        strings.push_back(0U);
        encoded.fileNameOffsets.reserve(directory.files.size());
        for (const auto& file : directory.files) {
            encoded.fileNameOffsets.push_back(
                static_cast<std::uint32_t>(strings.size()));
            strings.insert(strings.end(), file.begin(), file.end());
            strings.push_back(0U);
            ++fileCount;
        }
        encodedDirectories.push_back(std::move(encoded));
    }

    Bytes archive(airfix::udsp::kHeaderSize, 0U);
    const auto directoryOffset = static_cast<std::uint32_t>(archive.size());
    for (std::size_t index = 0U; index < directories.size(); ++index) {
        const auto& directory = directories[index];
        const auto& encoded = encodedDirectories[index];
        appendRecord(
            archive,
            airfix::udsp::nameHash(directory.path),
            encoded.pathOffset,
            0U,
            0U,
            static_cast<std::uint32_t>(directory.files.size()),
            encoded.firstFileIndex *
                static_cast<std::uint32_t>(airfix::udsp::kRecordSize));
    }

    const auto fileOffset = static_cast<std::uint32_t>(archive.size());
    for (std::size_t directoryIndex = 0U;
         directoryIndex < directories.size();
         ++directoryIndex) {
        const auto& directory = directories[directoryIndex];
        const auto& encoded = encodedDirectories[directoryIndex];
        for (std::size_t fileIndex = 0U;
             fileIndex < directory.files.size();
             ++fileIndex) {
            appendRecord(
                archive,
                airfix::udsp::nameHash(directory.files[fileIndex]),
                encoded.fileNameOffsets[fileIndex],
                0U,
                0U,
                0U,
                static_cast<std::uint32_t>(airfix::udsp::kHeaderSize));
        }
    }

    const auto stringOffset = static_cast<std::uint32_t>(archive.size());
    archive.insert(archive.end(), strings.begin(), strings.end());
    archive[0] = 'U';
    archive[1] = 'D';
    archive[2] = 'S';
    archive[3] = 'P';
    writeU32(archive, 4U, airfix::udsp::kVersion);
    writeU32(
        archive,
        8U,
        static_cast<std::uint32_t>(
            directories.size() * airfix::udsp::kRecordSize));
    writeU32(archive, 12U, directoryOffset);
    writeU32(
        archive, 16U, static_cast<std::uint32_t>(strings.size()));
    writeU32(archive, 20U, stringOffset);
    writeU32(
        archive,
        24U,
        fileCount * static_cast<std::uint32_t>(airfix::udsp::kRecordSize));
    writeU32(archive, 28U, fileOffset);
    return archive;
}

[[nodiscard]] airfix::udsp::Archive standardArchive() {
    return airfix::udsp::Archive::parse(makeArchive({
        {
            .path = "Graphics\\Textures",
            .files = {"Clouds.gti", "Wing.gti"},
        },
        {
            .path = "Misc",
            .files = {"Other.ccf"},
        },
        {
            .path = "Worlds",
            .files = {"Airfield.ccf"},
        },
    }));
}

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] std::size_t fileIndex(
    const airfix::udsp::Archive& archive,
    const std::string_view name) {
    const auto found = std::find_if(
        archive.files().begin(), archive.files().end(),
        [name](const auto& entry) { return entry.name == name; });
    require(found != archive.files().end(), "synthetic archive file missing");
    return static_cast<std::size_t>(found - archive.files().begin());
}

[[nodiscard]] bool hasIssue(
    const airfix::assets::WorldCcfTextureResolution& result,
    const airfix::assets::WorldCcfTextureIssueKind kind) {
    return std::any_of(
        result.issues.begin(), result.issues.end(),
        [kind](const auto& issue) { return issue.kind == kind; });
}

[[nodiscard]] bool hasTextureIssue(
    const airfix::assets::WorldCcfTextureResolution& result,
    const airfix::assets::TextureEntryIssueKind kind,
    const std::optional<std::size_t> dependencyIndex) {
    return std::any_of(
        result.textures.issues.begin(), result.textures.issues.end(),
        [kind, dependencyIndex](const auto& issue) {
            return issue.kind == kind &&
                issue.dependencyIndex == dependencyIndex;
        });
}

[[nodiscard]] airfix::assets::WorldDefinition standardWorld() {
    airfix::assets::WorldDefinition world;
    world.ccfPath = "worlds/AIRFIELD.CCF";
    world.textureRoot = "graphics/Textures/";
    return world;
}

[[nodiscard]] airfix::assets::CcfRoomMetadata room(
    const std::uint32_t reference) {
    return {
        .name = "room",
        .prefix = "",
        .reference = reference,
        .primaryBinding = true,
    };
}

[[nodiscard]] airfix::assets::CcfMaterialMetadata material(
    const std::uint32_t reference,
    const std::optional<std::string>& primary,
    const std::optional<std::string>& environment = std::nullopt) {
    return {
        .name = "material",
        .prefix = "",
        .reference = reference,
        .primaryTexture = primary,
        .environmentTexture = environment,
    };
}

[[nodiscard]] airfix::assets::CcfMeshMetadata mesh(
    const std::uint32_t reference,
    const std::vector<std::uint32_t>& materialReferences) {
    airfix::assets::CcfMeshMetadata result{
        .name = "mesh",
        .prefix = "",
        .reference = reference,
    };
    for (const auto materialReference : materialReferences) {
        result.triangles.push_back({
            .materialReference = materialReference,
        });
    }
    return result;
}

[[nodiscard]] CcfPlacedNodeMetadata object(
    const std::uint32_t currentReference,
    const std::uint32_t roomReference,
    const std::uint32_t meshReference) {
    return {
        .kind = CcfPlacedNodeKind::object,
        .name = "object",
        .prefix = "",
        .currentReference = currentReference,
        .roomReference = roomReference,
        .data = airfix::assets::CcfPlacedObjectMetadata{
            .meshReference = meshReference,
        },
    };
}

[[nodiscard]] CcfMetadata standardCcf() {
    CcfMetadata ccf;
    ccf.rooms = {room(10U)};
    ccf.meshes = {mesh(100U, {7U, 9U})};
    ccf.materials = {
        material(7U, "wing"),
        material(9U, std::nullopt, "CLOUDS"),
    };
    ccf.placedNodes = {object(1U, 10U, 100U)};
    return ccf;
}

void requireNoPublishedTextures(
    const airfix::assets::WorldCcfTextureResolution& result,
    const std::string& context) {
    require(
        result.textures.entries.empty() && result.textures.issues.empty(),
        context + " published texture entries or diagnostics");
}

void testHappyPathCaseSeparatorsAndCanonicalPlan() {
    const auto archive = standardArchive();
    const auto ccf = standardCcf();
    const auto ccfIndex = fileIndex(archive, "Airfield.ccf");
    const auto result =
        airfix::assets::resolveWorldFirstRoomTextures(
            standardWorld(), ccf, ccfIndex, archive);

    require(
        result.ccf.status ==
                airfix::assets::WorldCcfBindingStatus::unique &&
            result.ccf.logicalPath == "worlds\\AIRFIELD.CCF" &&
            result.ccf.archiveDirectoryIndex.has_value() &&
            result.ccf.archiveFileIndex == ccfIndex &&
            result.ccf.archiveLogicalPath ==
                std::optional<std::string>{"Worlds\\Airfield.ccf"} &&
            result.ccf.suppliedArchiveFileIndex == ccfIndex,
        "CCF case/separator lookup metadata mismatch");
    require(
        result.issues.empty() && result.plan.issues.empty() &&
            result.plan.roomIndex == 0U &&
            result.plan.textures.size() == 2U,
        "canonical first-room plan was not retained");
    require(
        result.textures.entries.size() == 2U &&
            result.textures.issues.empty(),
        "valid first-room TEXU edges did not resolve");
    require(
        result.textures.entries[0].status ==
                airfix::assets::TextureEntryStatus::unique &&
            result.textures.entries[0].archiveFileIndex ==
                fileIndex(archive, "Wing.gti") &&
            result.textures.entries[0].logicalPath ==
                "graphics\\Textures\\wing.gti" &&
            result.textures.entries[0].role ==
                airfix::assets::TextureDependencyRole::primary &&
            result.textures.entries[0].materialReference == 7U &&
            result.textures.entries[0].materialIndex == 0U &&
            result.textures.entries[0].sourceText == "wing",
        "primary first-room TEXU metadata mismatch");
    require(
        result.textures.entries[1].status ==
                airfix::assets::TextureEntryStatus::unique &&
            result.textures.entries[1].archiveFileIndex ==
                fileIndex(archive, "Clouds.gti") &&
            result.textures.entries[1].role ==
                airfix::assets::TextureDependencyRole::environment &&
            result.textures.entries[1].materialReference == 9U,
        "environment first-room TEXU metadata mismatch");
}

void testCcfBindingFailuresAreTypedAndFailClosed() {
    const auto archive = standardArchive();
    const auto ccf = standardCcf();
    const auto ccfIndex = fileIndex(archive, "Airfield.ccf");

    auto world = standardWorld();
    world.ccfPath = std::nullopt;
    const auto missing =
        airfix::assets::resolveWorldFirstRoomTextures(
            world, ccf, ccfIndex, archive);
    require(
        missing.ccf.status ==
                airfix::assets::WorldCcfBindingStatus::missing &&
            hasIssue(
                missing,
                airfix::assets::WorldCcfTextureIssueKind::missingCcfPath),
        "missing CCFF path issue mismatch");
    require(
        missing.plan.issues.empty() &&
            !missing.plan.roomIndex.has_value(),
        "missing CCF path incorrectly built a draw plan");
    requireNoPublishedTextures(missing, "missing CCF");

    world = standardWorld();
    world.ccfPath = "";
    const auto empty =
        airfix::assets::resolveWorldFirstRoomTextures(
            world, ccf, ccfIndex, archive);
    require(
        empty.ccf.status ==
                airfix::assets::WorldCcfBindingStatus::invalid &&
            hasIssue(
                empty,
                airfix::assets::WorldCcfTextureIssueKind::invalidCcfPath),
        "present empty CCFF path was not invalid");
    requireNoPublishedTextures(empty, "empty CCF path");

    world = standardWorld();
    world.ccfPath = "../Worlds/Airfield.ccf";
    const auto invalid =
        airfix::assets::resolveWorldFirstRoomTextures(
            world, ccf, ccfIndex, archive);
    require(
        invalid.ccf.status ==
                airfix::assets::WorldCcfBindingStatus::invalid &&
            hasIssue(
                invalid,
                airfix::assets::WorldCcfTextureIssueKind::invalidCcfPath),
        "invalid CCFF path issue mismatch");
    requireNoPublishedTextures(invalid, "invalid CCF");

    world = standardWorld();
    world.ccfPath = "Worlds/Missing.ccf";
    const auto notFound =
        airfix::assets::resolveWorldFirstRoomTextures(
            world, ccf, ccfIndex, archive);
    require(
        notFound.ccf.status ==
                airfix::assets::WorldCcfBindingStatus::notFound &&
            hasIssue(
                notFound,
                airfix::assets::WorldCcfTextureIssueKind::ccfNotFound),
        "not-found CCFF issue mismatch");
    requireNoPublishedTextures(notFound, "not-found CCF");

    const auto duplicateArchive =
        airfix::udsp::Archive::parse(makeArchive({
            {
                .path = "Graphics\\Textures",
                .files = {"Wing.gti"},
            },
            {
                .path = "Worlds",
                .files = {"Airfield.ccf", "Airfield.ccf"},
            },
        }));
    world = standardWorld();
    const auto ambiguous =
        airfix::assets::resolveWorldFirstRoomTextures(
            world, ccf, 0U, duplicateArchive);
    require(
        ambiguous.ccf.status ==
                airfix::assets::WorldCcfBindingStatus::ambiguous &&
            hasIssue(
                ambiguous,
                airfix::assets::WorldCcfTextureIssueKind::ccfAmbiguous) &&
            !ambiguous.ccf.archiveFileIndex.has_value(),
        "ambiguous CCFF issue mismatch");
    requireNoPublishedTextures(ambiguous, "ambiguous CCF");
}

void testSuppliedCcfEntryMismatchFailsClosed() {
    const auto archive = standardArchive();
    const auto ccf = standardCcf();
    const auto expectedIndex = fileIndex(archive, "Airfield.ccf");
    const auto otherIndex = fileIndex(archive, "Other.ccf");

    const auto mismatch =
        airfix::assets::resolveWorldFirstRoomTextures(
            standardWorld(), ccf, otherIndex, archive);
    require(
        mismatch.ccf.status ==
                airfix::assets::WorldCcfBindingStatus::mismatch &&
            mismatch.ccf.archiveFileIndex == expectedIndex &&
            mismatch.ccf.suppliedArchiveFileIndex == otherIndex &&
            hasIssue(
                mismatch,
                airfix::assets::WorldCcfTextureIssueKind::ccfEntryMismatch),
        "supplied CCF entry mismatch issue or metadata mismatch");
    require(
        !mismatch.plan.roomIndex.has_value(),
        "entry mismatch incorrectly built a draw plan");
    requireNoPublishedTextures(mismatch, "mismatched CCF entry");

    const auto outOfRange =
        airfix::assets::resolveWorldFirstRoomTextures(
            standardWorld(),
            ccf,
            archive.files().size(),
            archive);
    require(
        outOfRange.ccf.status ==
                airfix::assets::WorldCcfBindingStatus::mismatch &&
            outOfRange.ccf.archiveFileIndex == expectedIndex &&
            outOfRange.ccf.suppliedArchiveFileIndex ==
                archive.files().size() &&
            hasIssue(
                outOfRange,
                airfix::assets::WorldCcfTextureIssueKind::ccfEntryMismatch),
        "out-of-range supplied CCF index was not a typed mismatch");
    requireNoPublishedTextures(outOfRange, "out-of-range CCF entry");
}

void testPlanDependencyIsRetainedAndFailClosed() {
    const auto archive = standardArchive();
    const auto ccfIndex = fileIndex(archive, "Airfield.ccf");

    CcfMetadata malformed;
    const auto noRoom =
        airfix::assets::resolveWorldFirstRoomTextures(
            standardWorld(), malformed, ccfIndex, archive);
    require(
        noRoom.ccf.status ==
                airfix::assets::WorldCcfBindingStatus::unique &&
            hasIssue(
                noRoom,
                airfix::assets::WorldCcfTextureIssueKind::
                    firstRoomPlanDependency) &&
            noRoom.plan.issues.size() == 1U &&
            noRoom.plan.issues[0].kind ==
                airfix::assets::CcfRoomDrawPlanIssueKind::noRoom,
        "malformed CCF plan dependency was not retained");
    requireNoPublishedTextures(noRoom, "malformed CCF plan");

    auto foreign = standardCcf();
    foreign.materials.clear();
    const auto missingMaterial =
        airfix::assets::resolveWorldFirstRoomTextures(
            standardWorld(), foreign, ccfIndex, archive);
    require(
        hasIssue(
            missingMaterial,
            airfix::assets::WorldCcfTextureIssueKind::
                firstRoomPlanDependency) &&
            missingMaterial.plan.issues.size() == 2U &&
            std::all_of(
                missingMaterial.plan.issues.begin(),
                missingMaterial.plan.issues.end(),
                [](const auto& issue) {
                    return issue.kind ==
                        airfix::assets::CcfRoomDrawPlanIssueKind::
                            materialNotFound;
                }) &&
            missingMaterial.plan.textures.empty(),
        "foreign CCF metadata did not fail through its canonical plan");
    requireNoPublishedTextures(
        missingMaterial, "foreign CCF plan dependency");
}

void testTextureDiagnosticsSurviveValidPlan() {
    const auto archive = standardArchive();
    const auto ccfIndex = fileIndex(archive, "Airfield.ccf");
    auto ccf = standardCcf();
    ccf.materials[0].primaryTexture = "Missing";
    ccf.materials[1].environmentTexture = std::nullopt;

    const auto missingTexture =
        airfix::assets::resolveWorldFirstRoomTextures(
            standardWorld(), ccf, ccfIndex, archive);
    require(
        missingTexture.ccf.status ==
                airfix::assets::WorldCcfBindingStatus::unique &&
            missingTexture.issues.empty() &&
            missingTexture.plan.issues.empty(),
        "valid CCF plan failed before missing TEXU diagnostics");
    require(
        missingTexture.textures.entries.size() == 1U &&
            missingTexture.textures.entries[0].status ==
                airfix::assets::TextureEntryStatus::notFound &&
            hasTextureIssue(
                missingTexture,
                airfix::assets::TextureEntryIssueKind::notFound,
                0U),
        "missing TEXU diagnostic was discarded");

    auto noRoot = standardWorld();
    noRoot.textureRoot = std::nullopt;
    const auto missingRoot =
        airfix::assets::resolveWorldFirstRoomTextures(
            noRoot, ccf, ccfIndex, archive);
    require(
        missingRoot.textures.entries.size() == 1U &&
            missingRoot.textures.entries[0].status ==
                airfix::assets::TextureEntryStatus::missingTextureRoot &&
            hasTextureIssue(
                missingRoot,
                airfix::assets::TextureEntryIssueKind::missingTextureRoot,
                0U),
        "missing texture-root diagnostic was discarded");
}

void testIndependentPlanAndTextureLimits() {
    const auto archive = standardArchive();
    const auto ccf = standardCcf();
    const auto ccfIndex = fileIndex(archive, "Airfield.ccf");

    auto ccfPathLimits =
        airfix::assets::WorldCcfTextureResolutionLimits{};
    ccfPathLimits.maximumCcfLogicalPathBytes =
        std::string_view{"worlds/AIRFIELD.CCF"}.size();
    const auto exactCcfPath =
        airfix::assets::resolveWorldFirstRoomTextures(
            standardWorld(), ccf, ccfIndex, archive, ccfPathLimits);
    require(
        exactCcfPath.ccf.status ==
            airfix::assets::WorldCcfBindingStatus::unique,
        "exact CCF logical-path limit was rejected");

    --ccfPathLimits.maximumCcfLogicalPathBytes;
    const auto oversizedCcfPath =
        airfix::assets::resolveWorldFirstRoomTextures(
            standardWorld(), ccf, ccfIndex, archive, ccfPathLimits);
    require(
        oversizedCcfPath.ccf.status ==
                airfix::assets::WorldCcfBindingStatus::invalid &&
            hasIssue(
                oversizedCcfPath,
                airfix::assets::WorldCcfTextureIssueKind::invalidCcfPath),
        "CCF logical-path limit was not enforced");
    requireNoPublishedTextures(oversizedCcfPath, "limited CCF path");

    auto planLimits =
        airfix::assets::WorldCcfTextureResolutionLimits{};
    planLimits.plan.maximumTextureEdges = 1U;
    const auto planLimited =
        airfix::assets::resolveWorldFirstRoomTextures(
            standardWorld(), ccf, ccfIndex, archive, planLimits);
    require(
        hasIssue(
            planLimited,
            airfix::assets::WorldCcfTextureIssueKind::
                firstRoomPlanDependency) &&
            planLimited.plan.issues.size() == 1U &&
            planLimited.plan.issues[0].kind ==
                airfix::assets::CcfRoomDrawPlanIssueKind::limitExceeded,
        "first-room plan limit did not surface as a dependency failure");
    requireNoPublishedTextures(planLimited, "limited first-room plan");

    auto dependencyLimits =
        airfix::assets::WorldCcfTextureResolutionLimits{};
    dependencyLimits.textureEntries.maximumDependencies = 1U;
    const auto dependencyLimited =
        airfix::assets::resolveWorldFirstRoomTextures(
            standardWorld(), ccf, ccfIndex, archive, dependencyLimits);
    require(
        dependencyLimited.ccf.status ==
                airfix::assets::WorldCcfBindingStatus::unique &&
            dependencyLimited.plan.issues.empty() &&
            dependencyLimited.plan.textures.size() == 2U &&
            dependencyLimited.textures.entries.empty() &&
            hasTextureIssue(
                dependencyLimited,
                airfix::assets::TextureEntryIssueKind::limitExceeded,
                std::nullopt),
        "texture dependency limit did not fail closed after valid plan");

    auto texturePathLimits =
        airfix::assets::WorldCcfTextureResolutionLimits{};
    texturePathLimits.textureEntries.maximumLogicalPathBytes = 8U;
    const auto texturePathLimited =
        airfix::assets::resolveWorldFirstRoomTextures(
            standardWorld(),
            ccf,
            ccfIndex,
            archive,
            texturePathLimits);
    require(
        texturePathLimited.plan.issues.empty() &&
            texturePathLimited.textures.entries.size() == 2U &&
            std::all_of(
                texturePathLimited.textures.entries.begin(),
                texturePathLimited.textures.entries.end(),
                [](const auto& entry) {
                    return entry.status ==
                        airfix::assets::TextureEntryStatus::
                            invalidLogicalPath;
                }) &&
            hasTextureIssue(
                texturePathLimited,
                airfix::assets::TextureEntryIssueKind::invalidLogicalPath,
                0U) &&
            hasTextureIssue(
                texturePathLimited,
                airfix::assets::TextureEntryIssueKind::invalidLogicalPath,
                1U),
        "texture path-limit diagnostics were not preserved");
}

void testEmptyPlanTexturesStillValidateCcf() {
    const auto archive = standardArchive();
    const auto ccfIndex = fileIndex(archive, "Airfield.ccf");
    auto ccf = standardCcf();
    ccf.materials[0].primaryTexture = std::nullopt;
    ccf.materials[1].environmentTexture = std::nullopt;

    const auto result =
        airfix::assets::resolveWorldFirstRoomTextures(
            standardWorld(), ccf, ccfIndex, archive);
    require(
        result.ccf.status ==
                airfix::assets::WorldCcfBindingStatus::unique &&
            result.issues.empty() && result.plan.issues.empty() &&
            result.plan.roomIndex == 0U &&
            result.plan.textures.empty() &&
            result.textures.entries.empty() &&
            result.textures.issues.empty(),
        "empty canonical texture plan did not retain a valid CCF binding");
}

} // namespace

int main() {
    try {
        testHappyPathCaseSeparatorsAndCanonicalPlan();
        testCcfBindingFailuresAreTypedAndFailClosed();
        testSuppliedCcfEntryMismatchFailsClosed();
        testPlanDependencyIsRetainedAndFailClosed();
        testTextureDiagnosticsSurviveValidPlan();
        testIndependentPlanAndTextureLimits();
        testEmptyPlanTexturesStillValidateCcf();
        std::cout << "all world first-room texture resolver tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "world first-room texture resolver test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
