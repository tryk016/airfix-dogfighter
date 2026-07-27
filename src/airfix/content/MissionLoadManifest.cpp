#include "airfix/content/MissionLoadManifest.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <new>
#include <span>
#include <utility>

namespace airfix::content {
namespace {

class SessionIdentityChanged final {};

[[nodiscard]] MissionLoadManifestIssue makeIssue(
    const MissionLoadManifestIssueKind kind,
    const MissionLoadDependencyKind dependency,
    const std::optional<std::size_t> dependencyIndex = std::nullopt,
    const std::optional<std::size_t> sourceFileIndex = std::nullopt,
    const std::optional<assets::MissionSetupParseErrorCode> setupParseError =
        std::nullopt,
    const std::optional<std::uint64_t> sourceOffset = std::nullopt) noexcept {
    return {
        .kind = kind,
        .dependency = dependency,
        .dependencyIndex = dependencyIndex,
        .sourceFileIndex = sourceFileIndex,
        .setupParseError = setupParseError,
        .sourceOffset = sourceOffset,
    };
}

void fail(
    MissionLoadManifestResult& result,
    const MissionLoadManifestIssueKind kind,
    const MissionLoadDependencyKind dependency,
    const std::optional<std::size_t> dependencyIndex = std::nullopt,
    const std::optional<std::size_t> sourceFileIndex = std::nullopt,
    const std::optional<assets::MissionSetupParseErrorCode> setupParseError =
        std::nullopt,
    const std::optional<std::uint64_t> sourceOffset = std::nullopt) {
    result.manifest.reset();
    result.issues.clear();
    result.issues.push_back(makeIssue(
        kind,
        dependency,
        dependencyIndex,
        sourceFileIndex,
        setupParseError,
        sourceOffset));
}

[[nodiscard]] bool report(
    MissionLoadManifestResult& result,
    const std::stop_token stopToken,
    const MissionLoadManifestProgressCallback& callback,
    const MissionLoadManifestPhase phase,
    const std::size_t completedItems,
    const std::size_t totalItems) {
    if (stopToken.stop_requested()) {
        fail(
            result,
            MissionLoadManifestIssueKind::cancelled,
            MissionLoadDependencyKind::level);
        return false;
    }
    if (completedItems > totalItems) {
        fail(
            result,
            MissionLoadManifestIssueKind::internalFailure,
            MissionLoadDependencyKind::level);
        return false;
    }
    if (callback) {
        try {
            callback({
                .phase = phase,
                .completedItems = completedItems,
                .totalItems = totalItems,
            });
        }
        catch (const SessionIdentityChanged&) {
            fail(
                result,
                MissionLoadManifestIssueKind::sessionIdentityChanged,
                MissionLoadDependencyKind::level);
            return false;
        }
        catch (const std::bad_alloc&) {
            throw;
        }
        catch (...) {
            fail(
                result,
                MissionLoadManifestIssueKind::progressCallbackFailure,
                MissionLoadDependencyKind::level);
            return false;
        }
    }
    if (stopToken.stop_requested()) {
        fail(
            result,
            MissionLoadManifestIssueKind::cancelled,
            MissionLoadDependencyKind::level);
        return false;
    }
    return true;
}

[[nodiscard]] bool checkedAdd(
    std::uint64_t& total,
    const std::uint64_t addition) noexcept {
    if (addition > std::numeric_limits<std::uint64_t>::max() - total) {
        return false;
    }
    total += addition;
    return true;
}

[[nodiscard]] bool checkedProduct(
    const std::size_t count,
    const std::size_t elementSize,
    std::uint64_t& product) noexcept {
    if (elementSize != 0U &&
        count > std::numeric_limits<std::size_t>::max() / elementSize) {
        return false;
    }
    const auto nativeProduct = count * elementSize;
    if constexpr (
        std::numeric_limits<std::size_t>::max() >
        std::numeric_limits<std::uint64_t>::max()) {
        if (nativeProduct > std::numeric_limits<std::uint64_t>::max()) {
            return false;
        }
    }
    product = static_cast<std::uint64_t>(nativeProduct);
    return true;
}

[[nodiscard]] std::uint64_t sourceAllocationFootprint(
    const udsp::FileEntry& entry) noexcept {
    return static_cast<std::uint64_t>(entry.storedSize) +
        (entry.isCompressed()
            ? static_cast<std::uint64_t>(entry.unpackedSize)
            : 0U);
}

[[nodiscard]] bool validLimits(
    const MissionLoadManifestLimits& limits) noexcept {
    return limits.entries.maximumLogicalPathBytes != 0U &&
        limits.maximumSetupSourceBytes != 0U &&
        limits.setup.maximumSourceBytes != 0U &&
        limits.maximumLevelSourceBytes != 0U &&
        limits.maximumWorldSourceBytes != 0U &&
        limits.maximumObjectDefinitionSourceBytes != 0U &&
        limits.maximumTotalDefinitionSourceBytes != 0U &&
        limits.maximumCcfSourceBytes != 0U &&
        limits.maximumTotalCcfSourceBytes != 0U &&
        limits.maximumCcfSources != 0U &&
        limits.maximumPublishedCpuBytes != 0U;
}

[[nodiscard]] std::string archiveLogicalPath(
    const udsp::Archive& archive,
    const std::size_t directoryIndex,
    const std::size_t fileIndex) {
    if (directoryIndex >= archive.directories().size() ||
        fileIndex >= archive.files().size()) {
        throw std::logic_error("UDSP lookup returned an invalid entry index");
    }
    std::string result = archive.directories()[directoryIndex].path;
    if (!result.empty()) {
        result.push_back('\\');
    }
    result += archive.files()[fileIndex].name;
    return result;
}

[[nodiscard]] bool resolveExact(
    MissionLoadManifestResult& result,
    const udsp::Archive& archive,
    const std::string_view logicalPath,
    const std::size_t logicalPathLimit,
    const MissionLoadDependencyKind dependency,
    const std::optional<std::size_t> dependencyIndex,
    MissionArchiveEntryIdentity& destination) {
    if (logicalPath.empty()) {
        fail(
            result,
            MissionLoadManifestIssueKind::missingLogicalPath,
            dependency,
            dependencyIndex);
        return false;
    }
    if (!udsp::isLogicalPathValid(logicalPath, logicalPathLimit)) {
        fail(
            result,
            MissionLoadManifestIssueKind::invalidLogicalPath,
            dependency,
            dependencyIndex);
        return false;
    }

    udsp::FileLookupResult lookup;
    try {
        lookup = archive.lookup(logicalPath, logicalPathLimit);
    }
    catch (const std::bad_alloc&) {
        throw;
    }
    catch (...) {
        fail(
            result,
            MissionLoadManifestIssueKind::invalidLogicalPath,
            dependency,
            dependencyIndex);
        return false;
    }
    switch (lookup.status) {
    case udsp::LookupStatus::notFound:
        fail(
            result,
            MissionLoadManifestIssueKind::notFound,
            dependency,
            dependencyIndex);
        return false;
    case udsp::LookupStatus::ambiguous:
        fail(
            result,
            MissionLoadManifestIssueKind::ambiguous,
            dependency,
            dependencyIndex);
        return false;
    case udsp::LookupStatus::unique:
        break;
    }
    if (lookup.fileIndex >= archive.files().size() ||
        lookup.directoryIndex >= archive.directories().size()) {
        fail(
            result,
            MissionLoadManifestIssueKind::internalFailure,
            dependency,
            dependencyIndex);
        return false;
    }
    destination = {
        .logicalPath = archiveLogicalPath(
            archive, lookup.directoryIndex, lookup.fileIndex),
        .archiveFileIndex = lookup.fileIndex,
    };
    return true;
}

[[nodiscard]] bool preflightDefinitionSource(
    MissionLoadManifestResult& result,
    const udsp::Archive& archive,
    const MissionArchiveEntryIdentity& identity,
    const std::uint64_t perSourceLimit,
    const std::uint64_t aggregateLimit,
    std::uint64_t& aggregate,
    const MissionLoadDependencyKind dependency,
    const std::optional<std::size_t> dependencyIndex) {
    if (identity.archiveFileIndex >= archive.files().size()) {
        fail(
            result,
            MissionLoadManifestIssueKind::internalFailure,
            dependency,
            dependencyIndex);
        return false;
    }
    const auto footprint =
        sourceAllocationFootprint(archive.files()[identity.archiveFileIndex]);
    if (footprint > perSourceLimit) {
        fail(
            result,
            MissionLoadManifestIssueKind::sourceLimitExceeded,
            dependency,
            dependencyIndex,
            identity.archiveFileIndex);
        return false;
    }
    if (!checkedAdd(aggregate, footprint)) {
        fail(
            result,
            MissionLoadManifestIssueKind::integerOverflow,
            dependency,
            dependencyIndex,
            identity.archiveFileIndex);
        return false;
    }
    if (aggregate > aggregateLimit) {
        fail(
            result,
            MissionLoadManifestIssueKind::
                aggregateDefinitionSourceLimitExceeded,
            dependency,
            dependencyIndex,
            identity.archiveFileIndex);
        return false;
    }
    return true;
}

[[nodiscard]] bool preflightSetupSource(
    MissionLoadManifestResult& result,
    const udsp::Archive& archive,
    const MissionArchiveEntryIdentity& identity,
    const std::uint64_t allocationFootprintLimit,
    const std::uint64_t decodedSourceLimit,
    const std::uint64_t aggregateLimit,
    std::uint64_t& aggregate,
    std::uint64_t& footprint) {
    if (identity.archiveFileIndex >= archive.files().size()) {
        fail(
            result,
            MissionLoadManifestIssueKind::internalFailure,
            MissionLoadDependencyKind::missionSetup);
        return false;
    }
    const auto& entry = archive.files()[identity.archiveFileIndex];
    footprint = sourceAllocationFootprint(entry);
    if (footprint > allocationFootprintLimit) {
        fail(
            result,
            MissionLoadManifestIssueKind::sourceLimitExceeded,
            MissionLoadDependencyKind::missionSetup,
            std::nullopt,
            identity.archiveFileIndex);
        return false;
    }
    const auto decodedSourceBytes = static_cast<std::uint64_t>(
        entry.isCompressed() ? entry.unpackedSize : entry.storedSize);
    if (decodedSourceBytes > decodedSourceLimit) {
        fail(
            result,
            MissionLoadManifestIssueKind::sourceLimitExceeded,
            MissionLoadDependencyKind::missionSetup,
            std::nullopt,
            identity.archiveFileIndex);
        return false;
    }
    if (!checkedAdd(aggregate, footprint)) {
        fail(
            result,
            MissionLoadManifestIssueKind::integerOverflow,
            MissionLoadDependencyKind::missionSetup,
            std::nullopt,
            identity.archiveFileIndex);
        return false;
    }
    if (aggregate > aggregateLimit) {
        fail(
            result,
            MissionLoadManifestIssueKind::
                aggregateDefinitionSourceLimitExceeded,
            MissionLoadDependencyKind::missionSetup,
            std::nullopt,
            identity.archiveFileIndex);
        return false;
    }
    return true;
}

[[nodiscard]] bool addBytes(
    std::uint64_t& total,
    const std::size_t count,
    const std::size_t elementSize) noexcept {
    std::uint64_t product = 0U;
    return checkedProduct(count, elementSize, product) &&
        checkedAdd(total, product);
}

[[nodiscard]] bool addString(
    std::uint64_t& total,
    const std::string& value) noexcept {
    return checkedAdd(total, static_cast<std::uint64_t>(value.size()));
}

[[nodiscard]] bool addOptionalString(
    std::uint64_t& total,
    const std::optional<std::string>& value) noexcept {
    return !value.has_value() || addString(total, *value);
}

[[nodiscard]] bool addArchiveIdentity(
    std::uint64_t& total,
    const MissionArchiveEntryIdentity& identity) noexcept {
    return addString(total, identity.logicalPath);
}

[[nodiscard]] bool addObjectDefinition(
    std::uint64_t& total,
    const assets::ObjectDefinition& definition) noexcept {
    return addOptionalString(total, definition.type) &&
        addOptionalString(total, definition.category) &&
        addOptionalString(total, definition.name) &&
        addOptionalString(total, definition.nationality) &&
        addOptionalString(total, definition.textureRoot) &&
        addOptionalString(total, definition.ccfPath) &&
        addOptionalString(total, definition.meshName) &&
        addBytes(
            total,
            definition.unknownChunks.size(),
            sizeof(assets::AfChunk));
}

[[nodiscard]] bool addLevelDefinition(
    std::uint64_t& total,
    const assets::LevelDefinition& level) noexcept {
    if (!addOptionalString(total, level.worldPath) ||
        !addBytes(
            total,
            level.objects.size(),
            sizeof(assets::LevelObjectPlacement)) ||
        !addBytes(
            total,
            level.models.size(),
            sizeof(assets::LevelModelPlacement)) ||
        !addBytes(
            total,
            level.instanceStates.size(),
            sizeof(assets::LevelInstanceState)) ||
        !addBytes(
            total,
            level.unknownChunks.size(),
            sizeof(assets::AfChunk))) {
        return false;
    }
    for (const auto& placement : level.objects) {
        if (!addString(total, placement.room) ||
            !addString(total, placement.objectPath)) {
            return false;
        }
    }
    for (const auto& model : level.models) {
        if (!addString(total, model.placement.room) ||
            !addString(total, model.placement.objectPath)) {
            return false;
        }
        for (const auto& state : model.stateStrings) {
            if (!addString(total, state)) {
                return false;
            }
        }
    }
    for (const auto& state : level.instanceStates) {
        if (!addString(total, state.typeIdentity) ||
            !addString(total, state.instanceIdentity) ||
            !addBytes(
                total,
                state.stateWords.size(),
                sizeof(std::uint32_t))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool addWorldDefinition(
    std::uint64_t& total,
    const assets::WorldDefinition& world) noexcept {
    if (!addOptionalString(total, world.textureRoot) ||
        !addOptionalString(total, world.ccfPath) ||
        !addOptionalString(total, world.backdrop) ||
        !addBytes(total, world.rooms.size(), sizeof(assets::WorldRoom)) ||
        !addBytes(
            total,
            world.lineLists.size(),
            sizeof(assets::WorldLineList)) ||
        !addBytes(
            total,
            world.ignoredDuplicateChunks.size(),
            sizeof(assets::AfChunk)) ||
        !addBytes(
            total,
            world.unknownChunks.size(),
            sizeof(assets::AfChunk))) {
        return false;
    }
    for (const auto& room : world.rooms) {
        if (!addString(total, room.internalName) ||
            !addString(total, room.localizedName)) {
            return false;
        }
    }
    for (const auto& lineList : world.lineLists) {
        if (!addString(total, lineList.name) ||
            !addBytes(
                total,
                lineList.lines.size(),
                sizeof(std::array<float, 4U>))) {
            return false;
        }
    }
    return true;
}

} // namespace

MissionLoadManifest::MissionLoadManifest(
    MissionLoadManifest&& other) noexcept {
    *this = std::move(other);
}

MissionLoadManifest& MissionLoadManifest::operator=(
    MissionLoadManifest&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    revision_ = std::move(other.revision_);
    setupEntry_ = std::move(other.setupEntry_);
    levelEntry_ = std::move(other.levelEntry_);
    worldEntry_ = std::move(other.worldEntry_);
    level_ = std::move(other.level_);
    world_ = std::move(other.world_);
    objectEntries_ = std::move(other.objectEntries_);
    modelEntries_ = std::move(other.modelEntries_);
    objectDefinitionIndices_ =
        std::move(other.objectDefinitionIndices_);
    uniqueObjectDefinitions_ =
        std::move(other.uniqueObjectDefinitions_);
    ccfLoads_ = std::move(other.ccfLoads_);
    startPositions_ = std::move(other.startPositions_);
    setupSourceFootprintBytes_ = other.setupSourceFootprintBytes_;
    definitionSourceFootprintBytes_ =
        other.definitionSourceFootprintBytes_;
    plannedCcfSourceFootprintBytes_ =
        other.plannedCcfSourceFootprintBytes_;
    publishedCpuBytes_ = other.publishedCpuBytes_;
    valid_ = std::exchange(other.valid_, false);
    return *this;
}

std::optional<std::uint64_t>
MissionLoadManifest::calculatePublishedCpuBytes() const noexcept {
    std::uint64_t total = sizeof(MissionLoadManifest);
    if (!addArchiveIdentity(total, setupEntry_) ||
        !addArchiveIdentity(total, levelEntry_) ||
        !addArchiveIdentity(total, worldEntry_) ||
        !addLevelDefinition(total, level_) ||
        !addWorldDefinition(total, world_) ||
        !addBytes(
            total,
            objectEntries_.size(),
            sizeof(MissionArchiveEntryIdentity)) ||
        !addBytes(
            total,
            modelEntries_.size(),
            sizeof(MissionArchiveEntryIdentity)) ||
        !addBytes(
            total,
            objectDefinitionIndices_.size(),
            sizeof(std::size_t)) ||
        !addBytes(
            total,
            uniqueObjectDefinitions_.size(),
            sizeof(MissionUniqueObjectDefinition)) ||
        !addBytes(
            total,
            ccfLoads_.size(),
            sizeof(MissionCcfLoadDescriptor)) ||
        !addBytes(
            total,
            startPositions_.size(),
            sizeof(assets::MissionStartPosition))) {
        return std::nullopt;
    }
    for (const auto& entry : objectEntries_) {
        if (!addArchiveIdentity(total, entry)) {
            return std::nullopt;
        }
    }
    for (const auto& entry : modelEntries_) {
        if (!addArchiveIdentity(total, entry)) {
            return std::nullopt;
        }
    }
    for (const auto& object : uniqueObjectDefinitions_) {
        if (!addArchiveIdentity(total, object.source) ||
            !addObjectDefinition(total, object.definition)) {
            return std::nullopt;
        }
    }
    for (const auto& load : ccfLoads_) {
        if (!addArchiveIdentity(total, load.source) ||
            !addOptionalString(total, load.textureRoot)) {
            return std::nullopt;
        }
    }
    for (const auto& start : startPositions_) {
        if (!addString(total, start.roomName)) {
            return std::nullopt;
        }
    }
    return total;
}

MissionLoadManifestResult buildMissionLoadManifest(
    VerifiedContentSession& session,
    const MissionLoadManifestRequest& externalRequest,
    const MissionLoadManifestLimits& externalLimits,
    const std::stop_token stopToken,
    MissionLoadManifestProgressCallback progress) {
    MissionLoadManifestResult result;
    try {
        const auto expectedTransactionIdentity =
            session.transactionIdentity();
        const ContentRevision expectedRevision = session.revision();
        if (!expectedTransactionIdentity.valid()) {
            fail(
                result,
                MissionLoadManifestIssueKind::sessionIdentityChanged,
                MissionLoadDependencyKind::level);
            return result;
        }

        // Snapshot every caller-owned input before the first callback. Logical
        // paths are validated against the copied limit before allocation, so a
        // callback may destroy or mutate request/limits without invalidating
        // anything used by the transaction.
        const MissionLoadManifestLimits limits = externalLimits;
        if (!validLimits(limits)) {
            fail(
                result,
                MissionLoadManifestIssueKind::invalidLimits,
                MissionLoadDependencyKind::level);
            return result;
        }
        MissionLoadManifestRequest request;
        const auto snapshotPath = [&](
            const std::string& externalPath,
            const MissionLoadDependencyKind dependency,
            std::string& destination) {
            if (externalPath.empty()) {
                fail(
                    result,
                    MissionLoadManifestIssueKind::missingLogicalPath,
                    dependency);
                return false;
            }
            if (externalPath.size() >
                    limits.entries.maximumLogicalPathBytes ||
                !udsp::isLogicalPathValid(
                    externalPath,
                    limits.entries.maximumLogicalPathBytes)) {
                fail(
                    result,
                    MissionLoadManifestIssueKind::invalidLogicalPath,
                    dependency);
                return false;
            }
            destination = externalPath;
            return true;
        };
        if (!snapshotPath(
                externalRequest.levelLogicalPath,
                MissionLoadDependencyKind::level,
                request.levelLogicalPath) ||
            !snapshotPath(
                externalRequest.setupLogicalPath,
                MissionLoadDependencyKind::missionSetup,
                request.setupLogicalPath)) {
            return result;
        }

        MissionLoadManifestProgressCallback guardedProgress =
            [&](const MissionLoadManifestProgress& update) {
                const auto requireExpectedSession = [&] {
                    if (session.transactionIdentity() !=
                            expectedTransactionIdentity ||
                        session.revision() != expectedRevision) {
                        throw SessionIdentityChanged{};
                    }
                };
                requireExpectedSession();
                if (progress) {
                    try {
                        progress(update);
                    }
                    catch (...) {
                        requireExpectedSession();
                        throw;
                    }
                }
                requireExpectedSession();
            };
        if (!report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::validatingRequest,
                0U,
                1U)) {
            return result;
        }

        MissionLoadManifest candidate;
        candidate.revision_ = expectedRevision;
        const auto& archive = session.sourceArchive();
        if (!report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::validatingRequest,
                1U,
                1U) ||
            !report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::lookingUpMissionSetup,
                0U,
                1U)) {
            return result;
        }
        if (!resolveExact(
                result,
                archive,
                request.setupLogicalPath,
                limits.entries.maximumLogicalPathBytes,
                MissionLoadDependencyKind::missionSetup,
                std::nullopt,
                candidate.setupEntry_)) {
            return result;
        }
        if (!report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::lookingUpMissionSetup,
                1U,
                1U)) {
            return result;
        }

        assets::MissionSetupParseLimits setupLimits = limits.setup;
        setupLimits.maximumStartPositions = std::min(
            setupLimits.maximumStartPositions,
            assets::legacyMissionStartCapacity);
        if (!preflightSetupSource(
                result,
                archive,
                candidate.setupEntry_,
                limits.maximumSetupSourceBytes,
                setupLimits.maximumSourceBytes,
                limits.maximumTotalDefinitionSourceBytes,
                candidate.definitionSourceFootprintBytes_,
                candidate.setupSourceFootprintBytes_)) {
            return result;
        }
        if (!report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::readingMissionSetup,
                0U,
                1U)) {
            return result;
        }
        std::vector<std::uint8_t> setupBytes;
        try {
            setupBytes = session.readSourceFile(
                candidate.setupEntry_.archiveFileIndex,
                setupLimits.maximumSourceBytes);
        }
        catch (const std::bad_alloc&) {
            throw;
        }
        catch (...) {
            fail(
                result,
                MissionLoadManifestIssueKind::readFailure,
                MissionLoadDependencyKind::missionSetup,
                std::nullopt,
                candidate.setupEntry_.archiveFileIndex);
            return result;
        }
        if (!report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::readingMissionSetup,
                1U,
                1U) ||
            !report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::parsingMissionSetup,
                0U,
                1U)) {
            return result;
        }
        try {
            candidate.startPositions_ =
                assets::parseMissionStartPositions(setupBytes, setupLimits);
        }
        catch (const assets::MissionSetupParseError& error) {
            fail(
                result,
                MissionLoadManifestIssueKind::parseFailure,
                MissionLoadDependencyKind::missionSetup,
                std::nullopt,
                candidate.setupEntry_.archiveFileIndex,
                error.code(),
                error.offset());
            return result;
        }
        catch (const std::bad_alloc&) {
            throw;
        }
        catch (...) {
            fail(
                result,
                MissionLoadManifestIssueKind::parseFailure,
                MissionLoadDependencyKind::missionSetup,
                std::nullopt,
                candidate.setupEntry_.archiveFileIndex);
            return result;
        }
        std::vector<std::uint8_t>().swap(setupBytes);
        if (!report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::parsingMissionSetup,
                1U,
                1U) ||
            !report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::lookingUpLevel,
                0U,
                1U)) {
            return result;
        }
        if (!resolveExact(
                result,
                archive,
                request.levelLogicalPath,
                limits.entries.maximumLogicalPathBytes,
                MissionLoadDependencyKind::level,
                std::nullopt,
                candidate.levelEntry_)) {
            return result;
        }
        if (!report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::lookingUpLevel,
                1U,
                1U)) {
            return result;
        }

        if (!preflightDefinitionSource(
                result,
                archive,
                candidate.levelEntry_,
                limits.maximumLevelSourceBytes,
                limits.maximumTotalDefinitionSourceBytes,
                candidate.definitionSourceFootprintBytes_,
                MissionLoadDependencyKind::level,
                std::nullopt)) {
            return result;
        }
        if (!report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::readingLevel,
                0U,
                1U)) {
            return result;
        }
        std::vector<std::uint8_t> levelBytes;
        try {
            levelBytes = session.readSourceFile(
                candidate.levelEntry_.archiveFileIndex,
                limits.maximumLevelSourceBytes);
        }
        catch (const std::bad_alloc&) {
            throw;
        }
        catch (...) {
            fail(
                result,
                MissionLoadManifestIssueKind::readFailure,
                MissionLoadDependencyKind::level,
                std::nullopt,
                candidate.levelEntry_.archiveFileIndex);
            return result;
        }
        if (!report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::readingLevel,
                1U,
                1U) ||
            !report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::parsingLevel,
                0U,
                1U)) {
            return result;
        }
        try {
            candidate.level_ =
                assets::parseLevelDefinition(levelBytes, limits.level);
        }
        catch (const std::bad_alloc&) {
            throw;
        }
        catch (...) {
            fail(
                result,
                MissionLoadManifestIssueKind::parseFailure,
                MissionLoadDependencyKind::level,
                std::nullopt,
                candidate.levelEntry_.archiveFileIndex);
            return result;
        }
        std::vector<std::uint8_t>().swap(levelBytes);
        if (candidate.level_.objects.size() >
                limits.entries.maximumPlacements ||
            candidate.level_.models.size() >
                limits.entries.maximumPlacements -
                    candidate.level_.objects.size()) {
            fail(
                result,
                MissionLoadManifestIssueKind::placementLimitExceeded,
                MissionLoadDependencyKind::objectDefinition);
            return result;
        }
        if (candidate.level_.objects.size() >
                std::numeric_limits<std::size_t>::max() - 1U ||
            candidate.level_.models.size() >
                std::numeric_limits<std::size_t>::max() - 1U -
                    candidate.level_.objects.size()) {
            fail(
                result,
                MissionLoadManifestIssueKind::integerOverflow,
                MissionLoadDependencyKind::objectDefinition);
            return result;
        }
        const auto resolutionItemCount =
            1U + candidate.level_.objects.size() +
            candidate.level_.models.size();
        if (!report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::parsingLevel,
                1U,
                1U) ||
            !report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::resolvingMissionEntries,
                0U,
                resolutionItemCount)) {
            return result;
        }

        if (!candidate.level_.worldPath.has_value()) {
            fail(
                result,
                MissionLoadManifestIssueKind::missingLogicalPath,
                MissionLoadDependencyKind::world);
            return result;
        }
        if (!resolveExact(
                result,
                archive,
                *candidate.level_.worldPath,
                limits.entries.maximumLogicalPathBytes,
                MissionLoadDependencyKind::world,
                std::nullopt,
                candidate.worldEntry_)) {
            return result;
        }
        std::size_t resolvedItems = 1U;
        if (!report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::resolvingMissionEntries,
                resolvedItems,
                resolutionItemCount)) {
            return result;
        }
        candidate.objectEntries_.reserve(
            candidate.level_.objects.size());
        for (std::size_t index = 0U;
             index < candidate.level_.objects.size();
             ++index) {
            MissionArchiveEntryIdentity identity;
            if (!resolveExact(
                    result,
                    archive,
                    candidate.level_.objects[index].objectPath,
                    limits.entries.maximumLogicalPathBytes,
                    MissionLoadDependencyKind::objectDefinition,
                    index,
                    identity)) {
                return result;
            }
            candidate.objectEntries_.push_back(std::move(identity));
            ++resolvedItems;
            if (!report(
                    result,
                    stopToken,
                    guardedProgress,
                    MissionLoadManifestPhase::resolvingMissionEntries,
                    resolvedItems,
                    resolutionItemCount)) {
                return result;
            }
        }
        candidate.modelEntries_.reserve(
            candidate.level_.models.size());
        for (std::size_t index = 0U;
             index < candidate.level_.models.size();
             ++index) {
            MissionArchiveEntryIdentity identity;
            if (!resolveExact(
                    result,
                    archive,
                    candidate.level_.models[index].placement.objectPath,
                    limits.entries.maximumLogicalPathBytes,
                    MissionLoadDependencyKind::modelDefinition,
                    index,
                    identity)) {
                return result;
            }
            candidate.modelEntries_.push_back(std::move(identity));
            ++resolvedItems;
            if (!report(
                    result,
                    stopToken,
                    guardedProgress,
                    MissionLoadManifestPhase::resolvingMissionEntries,
                    resolvedItems,
                    resolutionItemCount)) {
                return result;
            }
        }

        if (!preflightDefinitionSource(
                result,
                archive,
                candidate.worldEntry_,
                limits.maximumWorldSourceBytes,
                limits.maximumTotalDefinitionSourceBytes,
                candidate.definitionSourceFootprintBytes_,
                MissionLoadDependencyKind::world,
                std::nullopt)) {
            return result;
        }
        if (!report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::readingWorld,
                0U,
                1U)) {
            return result;
        }
        std::vector<std::uint8_t> worldBytes;
        try {
            worldBytes = session.readSourceFile(
                candidate.worldEntry_.archiveFileIndex,
                limits.maximumWorldSourceBytes);
        }
        catch (const std::bad_alloc&) {
            throw;
        }
        catch (...) {
            fail(
                result,
                MissionLoadManifestIssueKind::readFailure,
                MissionLoadDependencyKind::world,
                std::nullopt,
                candidate.worldEntry_.archiveFileIndex);
            return result;
        }
        if (!report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::readingWorld,
                1U,
                1U) ||
            !report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::parsingWorld,
                0U,
                1U)) {
            return result;
        }
        try {
            candidate.world_ =
                assets::parseWorldDefinition(worldBytes, limits.world);
        }
        catch (const std::bad_alloc&) {
            throw;
        }
        catch (...) {
            fail(
                result,
                MissionLoadManifestIssueKind::parseFailure,
                MissionLoadDependencyKind::world,
                std::nullopt,
                candidate.worldEntry_.archiveFileIndex);
            return result;
        }
        std::vector<std::uint8_t>().swap(worldBytes);
        if (!report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::parsingWorld,
                1U,
                1U) ||
            !report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::readingObjectDefinitions,
                0U,
                candidate.objectEntries_.size())) {
            return result;
        }

        std::map<std::size_t, std::size_t> uniqueByFileIndex;
        candidate.objectDefinitionIndices_.reserve(
            candidate.objectEntries_.size());
        candidate.uniqueObjectDefinitions_.reserve(
            candidate.objectEntries_.size());
        for (std::size_t placementIndex = 0U;
             placementIndex < candidate.objectEntries_.size();
             ++placementIndex) {
            if (stopToken.stop_requested()) {
                fail(
                    result,
                    MissionLoadManifestIssueKind::cancelled,
                    MissionLoadDependencyKind::objectDefinition,
                    placementIndex);
                return result;
            }
            const auto& entry = candidate.objectEntries_[placementIndex];
            const auto cached =
                uniqueByFileIndex.find(entry.archiveFileIndex);
            if (cached != uniqueByFileIndex.end()) {
                candidate.objectDefinitionIndices_.push_back(cached->second);
            }
            else {
                if (!preflightDefinitionSource(
                        result,
                        archive,
                        entry,
                        limits.maximumObjectDefinitionSourceBytes,
                        limits.maximumTotalDefinitionSourceBytes,
                        candidate.definitionSourceFootprintBytes_,
                        MissionLoadDependencyKind::objectDefinition,
                        placementIndex)) {
                    return result;
                }
                std::vector<std::uint8_t> objectBytes;
                try {
                    objectBytes = session.readSourceFile(
                        entry.archiveFileIndex,
                        limits.maximumObjectDefinitionSourceBytes);
                }
                catch (const std::bad_alloc&) {
                    throw;
                }
                catch (...) {
                    fail(
                        result,
                        MissionLoadManifestIssueKind::readFailure,
                        MissionLoadDependencyKind::objectDefinition,
                        placementIndex,
                        entry.archiveFileIndex);
                    return result;
                }
                if (stopToken.stop_requested()) {
                    fail(
                        result,
                        MissionLoadManifestIssueKind::cancelled,
                        MissionLoadDependencyKind::objectDefinition,
                        placementIndex,
                        entry.archiveFileIndex);
                    return result;
                }
                assets::ObjectDefinition definition;
                try {
                    definition = assets::parseObjectDefinition(objectBytes);
                }
                catch (const std::bad_alloc&) {
                    throw;
                }
                catch (...) {
                    fail(
                        result,
                        MissionLoadManifestIssueKind::parseFailure,
                        MissionLoadDependencyKind::objectDefinition,
                        placementIndex,
                        entry.archiveFileIndex);
                    return result;
                }
                std::vector<std::uint8_t>().swap(objectBytes);
                if (definition.kind !=
                    assets::ObjectDefinitionKind::object) {
                    fail(
                        result,
                        MissionLoadManifestIssueKind::
                            objectDefinitionKindMismatch,
                        MissionLoadDependencyKind::objectDefinition,
                        placementIndex,
                        entry.archiveFileIndex);
                    return result;
                }
                const auto uniqueIndex =
                    candidate.uniqueObjectDefinitions_.size();
                uniqueByFileIndex.emplace(
                    entry.archiveFileIndex, uniqueIndex);
                candidate.uniqueObjectDefinitions_.push_back({
                    .source = entry,
                    .definition = std::move(definition),
                });
                candidate.objectDefinitionIndices_.push_back(uniqueIndex);
            }
            if (!report(
                    result,
                    stopToken,
                    guardedProgress,
                    MissionLoadManifestPhase::readingObjectDefinitions,
                    placementIndex + 1U,
                    candidate.objectEntries_.size())) {
                return result;
            }
        }

        const auto appendCcfLoad = [&](
            const std::string_view logicalPath,
            const std::optional<std::string>& textureRoot,
            const MissionCcfLoadRole role,
            const std::uint32_t legacyFlags,
            const MissionLoadDependencyKind dependency,
            const std::optional<std::size_t> dependencyIndex,
            const std::optional<std::size_t> objectPlacementIndex,
            const std::optional<std::size_t> uniqueObjectDefinitionIndex) {
            if (candidate.ccfLoads_.size() >=
                limits.maximumCcfSources) {
                fail(
                    result,
                    MissionLoadManifestIssueKind::
                        ccfSourceCountLimitExceeded,
                    dependency,
                    dependencyIndex);
                return false;
            }

            MissionArchiveEntryIdentity identity;
            if (!resolveExact(
                    result,
                    archive,
                    logicalPath,
                    limits.entries.maximumLogicalPathBytes,
                    dependency,
                    dependencyIndex,
                    identity)) {
                return false;
            }
            const auto footprint = sourceAllocationFootprint(
                archive.files()[identity.archiveFileIndex]);
            if (footprint > limits.maximumCcfSourceBytes) {
                fail(
                    result,
                    MissionLoadManifestIssueKind::sourceLimitExceeded,
                    dependency,
                    dependencyIndex,
                    identity.archiveFileIndex);
                return false;
            }
            auto aggregate =
                candidate.plannedCcfSourceFootprintBytes_;
            if (!checkedAdd(aggregate, footprint)) {
                fail(
                    result,
                    MissionLoadManifestIssueKind::integerOverflow,
                    dependency,
                    dependencyIndex,
                    identity.archiveFileIndex);
                return false;
            }
            if (aggregate >
                limits.maximumTotalCcfSourceBytes) {
                fail(
                    result,
                    MissionLoadManifestIssueKind::
                        aggregateCcfSourceLimitExceeded,
                    dependency,
                    dependencyIndex,
                    identity.archiveFileIndex);
                return false;
            }

            const auto sourceIndex = candidate.ccfLoads_.size();
            candidate.ccfLoads_.push_back({
                .role = role,
                .sourceIndex = sourceIndex,
                .source = std::move(identity),
                .textureRoot = textureRoot,
                .legacyLoadFlags = legacyFlags,
                .roomSectionEnabled = true,
                .copyPrimaryNameToRoot = false,
                .placedSceneEnabled = legacyFlags != 0x2000U,
                .objectPlacementIndex = objectPlacementIndex,
                .uniqueObjectDefinitionIndex =
                    uniqueObjectDefinitionIndex,
                .sourceAllocationFootprintBytes = footprint,
            });
            candidate.plannedCcfSourceFootprintBytes_ = aggregate;
            return true;
        };

        std::size_t expectedCcfSources = 1U;
        if (candidate.world_.backdrop.has_value()) {
            if (expectedCcfSources == std::numeric_limits<std::size_t>::max()) {
                fail(
                    result,
                    MissionLoadManifestIssueKind::integerOverflow,
                    MissionLoadDependencyKind::backdropCcf);
                return result;
            }
            ++expectedCcfSources;
        }
        if (candidate.objectEntries_.size() >
            std::numeric_limits<std::size_t>::max() - expectedCcfSources) {
            fail(
                result,
                MissionLoadManifestIssueKind::integerOverflow,
                MissionLoadDependencyKind::objectCcf);
            return result;
        }
        expectedCcfSources += candidate.objectEntries_.size();
        if (expectedCcfSources > limits.maximumCcfSources) {
            fail(
                result,
                MissionLoadManifestIssueKind::ccfSourceCountLimitExceeded,
                MissionLoadDependencyKind::mainWorldCcf);
            return result;
        }
        candidate.ccfLoads_.reserve(expectedCcfSources);
        if (!report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::resolvingCcfSources,
                0U,
                expectedCcfSources)) {
            return result;
        }
        if (!candidate.world_.ccfPath.has_value()) {
            fail(
                result,
                MissionLoadManifestIssueKind::missingLogicalPath,
                MissionLoadDependencyKind::mainWorldCcf);
            return result;
        }
        if (!appendCcfLoad(
                *candidate.world_.ccfPath,
                candidate.world_.textureRoot,
                MissionCcfLoadRole::mainWorld,
                0U,
                MissionLoadDependencyKind::mainWorldCcf,
                std::nullopt,
                std::nullopt,
                std::nullopt)) {
            return result;
        }
        if (!report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::resolvingCcfSources,
                candidate.ccfLoads_.size(),
                expectedCcfSources)) {
            return result;
        }
        if (candidate.world_.backdrop.has_value()) {
            if (!appendCcfLoad(
                    *candidate.world_.backdrop,
                    candidate.world_.textureRoot,
                    MissionCcfLoadRole::backdrop,
                    0U,
                    MissionLoadDependencyKind::backdropCcf,
                    0U,
                    std::nullopt,
                    std::nullopt)) {
                return result;
            }
            if (!report(
                    result,
                    stopToken,
                    guardedProgress,
                    MissionLoadManifestPhase::resolvingCcfSources,
                    candidate.ccfLoads_.size(),
                    expectedCcfSources)) {
                return result;
            }
        }
        for (std::size_t placementIndex = 0U;
             placementIndex < candidate.objectDefinitionIndices_.size();
             ++placementIndex) {
            const auto uniqueIndex =
                candidate.objectDefinitionIndices_[placementIndex];
            if (uniqueIndex >=
                candidate.uniqueObjectDefinitions_.size()) {
                fail(
                    result,
                    MissionLoadManifestIssueKind::internalFailure,
                    MissionLoadDependencyKind::objectCcf,
                    placementIndex);
                return result;
            }
            const auto& definition =
                candidate.uniqueObjectDefinitions_[uniqueIndex].definition;
            if (!definition.ccfPath.has_value()) {
                fail(
                    result,
                    MissionLoadManifestIssueKind::missingLogicalPath,
                    MissionLoadDependencyKind::objectCcf,
                    placementIndex);
                return result;
            }
            if (!appendCcfLoad(
                    *definition.ccfPath,
                    definition.textureRoot,
                    MissionCcfLoadRole::objectPlacement,
                    0x2000U,
                    MissionLoadDependencyKind::objectCcf,
                    placementIndex,
                    placementIndex,
                    uniqueIndex)) {
                return result;
            }
            if (!report(
                    result,
                    stopToken,
                    guardedProgress,
                    MissionLoadManifestPhase::resolvingCcfSources,
                    candidate.ccfLoads_.size(),
                    expectedCcfSources)) {
                return result;
            }
        }

        const auto publishedBytes =
            candidate.calculatePublishedCpuBytes();
        if (!publishedBytes.has_value()) {
            fail(
                result,
                MissionLoadManifestIssueKind::integerOverflow,
                MissionLoadDependencyKind::level);
            return result;
        }
        if (*publishedBytes > limits.maximumPublishedCpuBytes) {
            fail(
                result,
                MissionLoadManifestIssueKind::publishedCpuLimitExceeded,
                MissionLoadDependencyKind::level);
            return result;
        }
        candidate.publishedCpuBytes_ = *publishedBytes;
        if (!report(
                result,
                stopToken,
                guardedProgress,
                MissionLoadManifestPhase::complete,
                1U,
                1U)) {
            return result;
        }
        if (session.transactionIdentity() !=
                expectedTransactionIdentity ||
            session.revision() != expectedRevision) {
            fail(
                result,
                MissionLoadManifestIssueKind::sessionIdentityChanged,
                MissionLoadDependencyKind::level);
            return result;
        }
        candidate.valid_ = true;
        result.manifest = std::move(candidate);
        return result;
    }
    catch (const std::bad_alloc&) {
        result.manifest.reset();
        result.issues.clear();
        result.issues.push_back(makeIssue(
            MissionLoadManifestIssueKind::allocationFailure,
            MissionLoadDependencyKind::level));
        return result;
    }
    catch (...) {
        result.manifest.reset();
        result.issues.clear();
        result.issues.push_back(makeIssue(
            MissionLoadManifestIssueKind::internalFailure,
            MissionLoadDependencyKind::level));
        return result;
    }
}

} // namespace airfix::content
