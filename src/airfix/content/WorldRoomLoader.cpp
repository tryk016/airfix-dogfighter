#include "airfix/content/WorldRoomLoader.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <span>
#include <unordered_set>
#include <utility>

namespace airfix::content {
namespace {

[[nodiscard]] WorldRoomLoadIssue issue(
    const WorldRoomLoadIssueKind kind) noexcept {
    return {
        .kind = kind,
        .sourceFileIndex = std::nullopt,
        .textureAssetId = std::nullopt,
        .textureResolutionIssue = std::nullopt,
        .textureBindingIssue = std::nullopt,
        .texturePreparationIssue = std::nullopt,
        .drawAssemblyIssue = std::nullopt,
        .submissionIssue = std::nullopt,
    };
}

void addIssue(
    WorldRoomLoadResult& result,
    const WorldRoomLoadIssueKind kind) {
    result.room.reset();
    result.issues.push_back(issue(kind));
}

[[nodiscard]] bool report(
    WorldRoomLoadResult& result,
    const std::stop_token stopToken,
    const WorldRoomLoadProgressCallback& callback,
    const WorldRoomLoadPhase phase,
    const std::size_t completedItems,
    const std::size_t totalItems) {
    if (stopToken.stop_requested()) {
        addIssue(result, WorldRoomLoadIssueKind::cancelled);
        return false;
    }
    if (completedItems > totalItems) {
        addIssue(result, WorldRoomLoadIssueKind::internalFailure);
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
        catch (const std::bad_alloc&) {
            throw;
        }
        catch (...) {
            addIssue(result, WorldRoomLoadIssueKind::progressCallbackFailure);
            return false;
        }
    }
    if (stopToken.stop_requested()) {
        addIssue(result, WorldRoomLoadIssueKind::cancelled);
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

[[nodiscard]] std::uint64_t sourceAllocationFootprint(
    const udsp::FileEntry& entry) noexcept {
    // A compressed read retains the stored payload while allocating the
    // decoded output. An uncompressed read returns its stored buffer directly.
    return static_cast<std::uint64_t>(entry.storedSize) +
        (entry.isCompressed()
            ? static_cast<std::uint64_t>(entry.unpackedSize)
            : 0U);
}

[[nodiscard]] bool checkedProduct(
    const std::size_t count,
    const std::size_t elementSize,
    std::uint64_t& product) noexcept {
    if (elementSize != 0U &&
        count > std::numeric_limits<std::size_t>::max() / elementSize) {
        return false;
    }
    const auto sizeProduct = count * elementSize;
    if constexpr (
        std::numeric_limits<std::size_t>::max() >
        std::numeric_limits<std::uint64_t>::max()) {
        if (sizeProduct > std::numeric_limits<std::uint64_t>::max()) {
            return false;
        }
    }
    product = static_cast<std::uint64_t>(sizeProduct);
    return true;
}

[[nodiscard]] bool accountPublished(
    std::uint64_t& total,
    const std::size_t count,
    const std::size_t elementSize,
    const std::uint64_t limit) noexcept {
    std::uint64_t bytes = 0U;
    return checkedProduct(count, elementSize, bytes) &&
        checkedAdd(total, bytes) &&
        total <= limit;
}

[[nodiscard]] std::uint64_t remaining(
    const std::uint64_t limit,
    const std::uint64_t used) noexcept {
    return used <= limit ? limit - used : 0U;
}

[[nodiscard]] std::size_t clampToSize(
    const std::uint64_t value) noexcept {
    if constexpr (
        std::numeric_limits<std::size_t>::max() <
        std::numeric_limits<std::uint64_t>::max()) {
        return value > std::numeric_limits<std::size_t>::max()
            ? std::numeric_limits<std::size_t>::max()
            : static_cast<std::size_t>(value);
    }
    return static_cast<std::size_t>(value);
}

[[nodiscard]] bool validateLimits(
    const WorldRoomLoadLimits& limits) noexcept {
    return limits.maximumWorldSourceBytes != 0U &&
        limits.maximumCcfSourceBytes != 0U &&
        limits.maximumTextureSourceBytes != 0U &&
        limits.maximumPublishedCpuBytes != 0U &&
        limits.textureResolution.maximumCcfLogicalPathBytes != 0U &&
        limits.gtiPerTexture.maximumSourceBytes != 0U;
}

[[nodiscard]] bool addModelPublishedBytes(
    std::uint64_t& total,
    const render::DrawModelPayload& model,
    const std::uint64_t limit) noexcept {
    if (!accountPublished(
            total,
            model.meshes.size(),
            sizeof(render::DrawMeshPayload),
            limit) ||
        !accountPublished(
            total,
            model.instances.size(),
            sizeof(render::DrawMeshInstance),
            limit)) {
        return false;
    }
    for (const auto& mesh : model.meshes) {
        if (!accountPublished(
                total,
                mesh.vertices.size(),
                sizeof(render::DrawVertex),
                limit) ||
            !accountPublished(
                total,
                mesh.indices.size(),
                sizeof(std::uint32_t),
                limit) ||
            !accountPublished(
                total,
                mesh.materials.size(),
                sizeof(render::DrawMaterial),
                limit) ||
            !accountPublished(
                total,
                mesh.ranges.size(),
                sizeof(render::DrawRange),
                limit)) {
            return false;
        }
    }
    return true;
}

void addResolutionIssues(
    WorldRoomLoadResult& result,
    const assets::WorldCcfTextureResolution& resolution) {
    for (const auto& upstream : resolution.issues) {
        auto mapped = issue(
            WorldRoomLoadIssueKind::textureResolutionFailure);
        mapped.textureResolutionIssue = upstream.kind;
        result.issues.push_back(std::move(mapped));
    }
    if (resolution.issues.empty()) {
        addIssue(result, WorldRoomLoadIssueKind::textureResolutionFailure);
    }
}

void addBindingIssues(
    WorldRoomLoadResult& result,
    const render::TextureBindingPlan& binding) {
    for (const auto& upstream : binding.issues) {
        auto mapped = issue(WorldRoomLoadIssueKind::textureBindingFailure);
        mapped.textureBindingIssue = upstream.kind;
        result.issues.push_back(std::move(mapped));
    }
    if (binding.issues.empty()) {
        addIssue(result, WorldRoomLoadIssueKind::textureBindingFailure);
    }
}

void addPreparationIssues(
    WorldRoomLoadResult& result,
    const render::TextureImportRequest& request,
    const render::GtiUploadPreparation& preparation,
    const WorldRoomLoadIssueKind kind) {
    if (preparation.issues.empty()) {
        auto mapped = issue(kind);
        mapped.sourceFileIndex = request.archiveFileIndex;
        mapped.textureAssetId = request.assetId;
        result.issues.push_back(std::move(mapped));
        return;
    }
    for (const auto& upstream : preparation.issues) {
        auto mapped = issue(kind);
        mapped.sourceFileIndex = request.archiveFileIndex;
        mapped.textureAssetId = request.assetId;
        mapped.texturePreparationIssue = upstream.kind;
        result.issues.push_back(std::move(mapped));
    }
}

void addPreparationIssue(
    WorldRoomLoadResult& result,
    const render::TextureImportRequest& request,
    const render::GtiUploadDataIssueKind upstreamKind) {
    auto mapped = issue(
        WorldRoomLoadIssueKind::texturePreparationFailure);
    mapped.sourceFileIndex = request.archiveFileIndex;
    mapped.textureAssetId = request.assetId;
    mapped.texturePreparationIssue = upstreamKind;
    result.issues.push_back(std::move(mapped));
}

void addTextureIssue(
    WorldRoomLoadResult& result,
    const render::TextureImportRequest& request,
    const WorldRoomLoadIssueKind kind) {
    auto mapped = issue(kind);
    mapped.sourceFileIndex = request.archiveFileIndex;
    mapped.textureAssetId = request.assetId;
    result.issues.push_back(std::move(mapped));
}

[[nodiscard]] render::GtiUploadDataIssueKind mapPlanIssue(
    const render::GtiUploadIssueKind kind) noexcept {
    switch (kind) {
    case render::GtiUploadIssueKind::limitExceeded:
        return render::GtiUploadDataIssueKind::limitExceeded;
    case render::GtiUploadIssueKind::integerOverflow:
        return render::GtiUploadDataIssueKind::integerOverflow;
    default:
        return render::GtiUploadDataIssueKind::planFailure;
    }
}

[[nodiscard]] bool sameUploadPlan(
    const render::GtiUploadPlan& left,
    const render::GtiUploadPlan& right) noexcept {
    return left.request == right.request &&
        left.variantIndex == right.variantIndex &&
        left.format == right.format &&
        left.checksum == right.checksum &&
        left.sampleSpace == right.sampleSpace &&
        left.mipPolicy == right.mipPolicy &&
        left.uploadLevels == right.uploadLevels &&
        left.allocatedMipCount == right.allocatedMipCount &&
        left.uploadedMipCount == right.uploadedMipCount &&
        left.decodedRgbaBytes == right.decodedRgbaBytes &&
        left.uploadRgbaBytes == right.uploadRgbaBytes &&
        left.residentRgbaBytes == right.residentRgbaBytes;
}

void addDrawIssues(
    WorldRoomLoadResult& result,
    const render::CcfRoomDrawAssembly& assembly) {
    for (const auto& upstream : assembly.issues) {
        auto mapped = issue(WorldRoomLoadIssueKind::drawAssemblyFailure);
        mapped.drawAssemblyIssue = upstream.kind;
        result.issues.push_back(std::move(mapped));
    }
    if (assembly.issues.empty()) {
        addIssue(result, WorldRoomLoadIssueKind::drawAssemblyFailure);
    }
}

void addSubmissionIssues(
    WorldRoomLoadResult& result,
    const render::DrawSubmissionDescription& submission) {
    for (const auto& upstream : submission.issues) {
        auto mapped = issue(WorldRoomLoadIssueKind::submissionFailure);
        mapped.submissionIssue = upstream.kind;
        result.issues.push_back(std::move(mapped));
    }
    if (submission.issues.empty()) {
        addIssue(result, WorldRoomLoadIssueKind::submissionFailure);
    }
}

} // namespace

WorldRoomLoadResult loadWorldRoom(
    VerifiedContentSession& session,
    const WorldRoomLoadRequest& request,
    const WorldRoomLoadLimits& limits,
    const std::stop_token stopToken,
    WorldRoomLoadProgressCallback progress) {
    WorldRoomLoadResult result;
    try {
        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::validatingRequest,
                0U,
                1U)) {
            return result;
        }
        if (!validateLimits(limits)) {
            addIssue(result, WorldRoomLoadIssueKind::invalidLimits);
            return result;
        }
        if (!udsp::isLogicalPathValid(
                request.worldLogicalPath,
                limits.textureResolution.maximumCcfLogicalPathBytes)) {
            addIssue(
                result,
                WorldRoomLoadIssueKind::invalidWorldLogicalPath);
            return result;
        }
        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::validatingRequest,
                1U,
                1U)) {
            return result;
        }

        const auto& archive = session.sourceArchive();
        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::lookingUpWorld,
                0U,
                1U)) {
            return result;
        }
        udsp::FileLookupResult worldLookup;
        try {
            worldLookup = archive.lookup(
                request.worldLogicalPath,
                limits.textureResolution.maximumCcfLogicalPathBytes);
        }
        catch (const std::bad_alloc&) {
            throw;
        }
        catch (...) {
            addIssue(
                result,
                WorldRoomLoadIssueKind::invalidWorldLogicalPath);
            return result;
        }
        switch (worldLookup.status) {
        case udsp::LookupStatus::notFound:
            addIssue(result, WorldRoomLoadIssueKind::worldNotFound);
            return result;
        case udsp::LookupStatus::ambiguous:
            addIssue(result, WorldRoomLoadIssueKind::worldAmbiguous);
            return result;
        case udsp::LookupStatus::unique:
            break;
        }
        if (worldLookup.fileIndex >= archive.files().size()) {
            addIssue(result, WorldRoomLoadIssueKind::internalFailure);
            return result;
        }
        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::lookingUpWorld,
                1U,
                1U)) {
            return result;
        }

        const auto& worldEntry = archive.files()[worldLookup.fileIndex];
        const auto worldSourceFootprint =
            sourceAllocationFootprint(worldEntry);
        if (worldSourceFootprint > limits.maximumWorldSourceBytes) {
            addIssue(
                result,
                WorldRoomLoadIssueKind::worldSourceLimitExceeded);
            return result;
        }
        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::readingWorld,
                0U,
                1U)) {
            return result;
        }
        std::vector<std::uint8_t> worldBytes;
        try {
            worldBytes = session.readSourceFile(
                worldLookup.fileIndex,
                limits.maximumWorldSourceBytes);
        }
        catch (const std::bad_alloc&) {
            throw;
        }
        catch (...) {
            addIssue(result, WorldRoomLoadIssueKind::worldReadFailure);
            return result;
        }
        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::readingWorld,
                1U,
                1U)) {
            return result;
        }

        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::parsingWorld,
                0U,
                1U)) {
            return result;
        }
        assets::WorldDefinition world;
        try {
            world = assets::parseWorldDefinition(worldBytes, limits.world);
        }
        catch (const std::bad_alloc&) {
            throw;
        }
        catch (...) {
            addIssue(result, WorldRoomLoadIssueKind::worldParseFailure);
            return result;
        }
        std::vector<std::uint8_t>().swap(worldBytes);
        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::parsingWorld,
                1U,
                1U)) {
            return result;
        }

        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::lookingUpCcf,
                0U,
                1U)) {
            return result;
        }
        if (!world.ccfPath.has_value()) {
            addIssue(result, WorldRoomLoadIssueKind::missingCcfPath);
            return result;
        }
        if (!udsp::isLogicalPathValid(
                *world.ccfPath,
                limits.textureResolution.maximumCcfLogicalPathBytes)) {
            addIssue(result, WorldRoomLoadIssueKind::invalidCcfPath);
            return result;
        }
        udsp::FileLookupResult ccfLookup;
        try {
            ccfLookup = archive.lookup(
                *world.ccfPath,
                limits.textureResolution.maximumCcfLogicalPathBytes);
        }
        catch (const std::bad_alloc&) {
            throw;
        }
        catch (...) {
            addIssue(result, WorldRoomLoadIssueKind::invalidCcfPath);
            return result;
        }
        switch (ccfLookup.status) {
        case udsp::LookupStatus::notFound:
            addIssue(result, WorldRoomLoadIssueKind::ccfNotFound);
            return result;
        case udsp::LookupStatus::ambiguous:
            addIssue(result, WorldRoomLoadIssueKind::ccfAmbiguous);
            return result;
        case udsp::LookupStatus::unique:
            break;
        }
        if (ccfLookup.fileIndex >= archive.files().size()) {
            addIssue(result, WorldRoomLoadIssueKind::internalFailure);
            return result;
        }
        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::lookingUpCcf,
                1U,
                1U)) {
            return result;
        }

        const auto& ccfEntry = archive.files()[ccfLookup.fileIndex];
        const auto ccfSourceFootprint =
            sourceAllocationFootprint(ccfEntry);
        if (ccfSourceFootprint > limits.maximumCcfSourceBytes) {
            addIssue(
                result,
                WorldRoomLoadIssueKind::ccfSourceLimitExceeded);
            return result;
        }
        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::readingCcf,
                0U,
                1U)) {
            return result;
        }
        std::vector<std::uint8_t> ccfBytes;
        try {
            ccfBytes = session.readSourceFile(
                ccfLookup.fileIndex,
                limits.maximumCcfSourceBytes);
        }
        catch (const std::bad_alloc&) {
            throw;
        }
        catch (...) {
            addIssue(result, WorldRoomLoadIssueKind::ccfReadFailure);
            return result;
        }
        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::readingCcf,
                1U,
                1U)) {
            return result;
        }

        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::parsingCcf,
                0U,
                1U)) {
            return result;
        }
        assets::CcfMetadata ccf;
        try {
            ccf = assets::parseCcf(ccfBytes);
        }
        catch (const std::bad_alloc&) {
            throw;
        }
        catch (...) {
            addIssue(result, WorldRoomLoadIssueKind::ccfParseFailure);
            return result;
        }
        std::vector<std::uint8_t>().swap(ccfBytes);
        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::parsingCcf,
                1U,
                1U)) {
            return result;
        }

        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::resolvingTextures,
                0U,
                1U)) {
            return result;
        }
        assets::WorldCcfTextureResolution resolution;
        try {
            resolution = assets::resolveWorldRoomTextures(
                world,
                ccf,
                ccfLookup.fileIndex,
                archive,
                request.ccfRoomIndex,
                limits.textureResolution);
        }
        catch (const std::bad_alloc&) {
            throw;
        }
        catch (...) {
            addIssue(
                result,
                WorldRoomLoadIssueKind::textureResolutionFailure);
            return result;
        }
        if (!resolution.issues.empty() ||
            resolution.ccf.status != assets::WorldCcfBindingStatus::unique ||
            !resolution.plan.issues.empty()) {
            addResolutionIssues(result, resolution);
            return result;
        }
        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::resolvingTextures,
                1U,
                1U)) {
            return result;
        }

        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::planningTextureBindings,
                0U,
                1U)) {
            return result;
        }
        std::vector<std::uint32_t> materialReferences;
        materialReferences.reserve(resolution.plan.materialIndices.size());
        std::vector<render::DrawMaterialState> materialStates;
        materialStates.reserve(resolution.plan.materialIndices.size());
        for (const auto materialIndex : resolution.plan.materialIndices) {
            if (materialIndex >= ccf.materials.size()) {
                addIssue(
                    result,
                    WorldRoomLoadIssueKind::textureResolutionFailure);
                return result;
            }
            const auto& material = ccf.materials[materialIndex];
            materialReferences.push_back(material.reference);
            materialStates.push_back(
                render::makeDrawMaterialState(material));
        }
        render::TextureBindingPlan binding;
        try {
            binding = render::buildTextureBindingPlan(
                materialReferences,
                materialStates,
                resolution.plan.textures,
                resolution.textures,
                limits.textureBindings);
        }
        catch (const std::bad_alloc&) {
            throw;
        }
        catch (...) {
            addIssue(
                result,
                WorldRoomLoadIssueKind::textureBindingFailure);
            return result;
        }
        if (!binding.issues.empty()) {
            addBindingIssues(result, binding);
            return result;
        }
        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::planningTextureBindings,
                1U,
                1U)) {
            return result;
        }

        if (binding.imports.size() > limits.maximumTextureAssets) {
            addIssue(
                result,
                WorldRoomLoadIssueKind::textureAssetLimitExceeded);
            return result;
        }
        std::uint64_t totalTextureSourceBytes = 0U;
        std::unordered_set<std::size_t> uniqueImports;
        uniqueImports.reserve(binding.imports.size());
        for (std::size_t index = 0U;
             index < binding.imports.size();
             ++index) {
            const auto& import = binding.imports[index];
            if (static_cast<std::size_t>(import.assetId.value) != index ||
                import.archiveFileIndex >= archive.files().size() ||
                !uniqueImports.insert(import.archiveFileIndex).second) {
                auto mapped = issue(
                    WorldRoomLoadIssueKind::invalidTextureImport);
                mapped.sourceFileIndex = import.archiveFileIndex;
                mapped.textureAssetId = import.assetId;
                result.issues.push_back(std::move(mapped));
                return result;
            }
            const auto& sourceEntry =
                archive.files()[import.archiveFileIndex];
            const auto sourceSize =
                sourceAllocationFootprint(sourceEntry);
            if (sourceSize > limits.maximumTextureSourceBytes ||
                sourceSize > limits.gtiPerTexture.maximumSourceBytes) {
                auto mapped = issue(
                    WorldRoomLoadIssueKind::textureSourceLimitExceeded);
                mapped.sourceFileIndex = import.archiveFileIndex;
                mapped.textureAssetId = import.assetId;
                result.issues.push_back(std::move(mapped));
                return result;
            }
            if (!checkedAdd(totalTextureSourceBytes, sourceSize)) {
                addIssue(result, WorldRoomLoadIssueKind::integerOverflow);
                return result;
            }
            if (totalTextureSourceBytes >
                limits.maximumTotalTextureSourceBytes) {
                addIssue(
                    result,
                    WorldRoomLoadIssueKind::textureSourceLimitExceeded);
                return result;
            }
        }

        std::uint64_t publishedCpuBytes =
            static_cast<std::uint64_t>(sizeof(LoadedWorldRoom));
        if (!accountPublished(
                publishedCpuBytes,
                binding.imports.size(),
                sizeof(LoadedTextureAsset),
                limits.maximumPublishedCpuBytes)) {
            addIssue(
                result,
                publishedCpuBytes >
                        limits.maximumPublishedCpuBytes
                    ? WorldRoomLoadIssueKind::publishedCpuLimitExceeded
                    : WorldRoomLoadIssueKind::integerOverflow);
            return result;
        }

        LoadedWorldRoom candidate{
            .revision = session.revision(),
            .model = {},
            .submission = {},
            .textures = {},
        };
        candidate.textures.reserve(binding.imports.size());
        std::uint64_t decodedRgbaBytes = 0U;
        std::uint64_t uploadRgbaBytes = 0U;
        std::uint64_t residentRgbaBytes = 0U;

        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::loadingTextures,
                0U,
                binding.imports.size())) {
            return result;
        }
        for (std::size_t index = 0U;
             index < binding.imports.size();
             ++index) {
            const auto& import = binding.imports[index];

            std::vector<std::uint8_t> gtiBytes;
            try {
                gtiBytes = session.readSourceFile(
                    import.archiveFileIndex,
                    std::min(
                        limits.maximumTextureSourceBytes,
                        limits.gtiPerTexture.maximumSourceBytes));
            }
            catch (const std::bad_alloc&) {
                throw;
            }
            catch (...) {
                auto mapped = issue(
                    WorldRoomLoadIssueKind::textureReadFailure);
                mapped.sourceFileIndex = import.archiveFileIndex;
                mapped.textureAssetId = import.assetId;
                result.issues.push_back(std::move(mapped));
                return result;
            }

            auto perAssetLimits = limits.gtiPerTexture;
            perAssetLimits.maximumSourceBytes = std::min(
                perAssetLimits.maximumSourceBytes,
                limits.maximumTextureSourceBytes);

            assets::GtiMetadata metadata;
            try {
                metadata = assets::parseGti(
                    gtiBytes,
                    assets::GtiParseLimits{
                        .maximumVariants =
                            perAssetLimits.upload.maximumVariants,
                        .maximumMetadataBytes =
                            perAssetLimits.upload.maximumMetadataBytes,
                    });
            }
            catch (const std::bad_alloc&) {
                throw;
            }
            catch (const assets::GtiParseLimitError&) {
                addPreparationIssue(
                    result,
                    import,
                    render::GtiUploadDataIssueKind::limitExceeded);
                return result;
            }
            catch (const assets::ParseError&) {
                addPreparationIssue(
                    result,
                    import,
                    render::GtiUploadDataIssueKind::parseFailure);
                return result;
            }
            catch (...) {
                addPreparationIssue(
                    result,
                    import,
                    render::GtiUploadDataIssueKind::parseFailure);
                return result;
            }

            render::GtiUploadDescription description;
            try {
                description = render::describeGtiUpload(
                    import, metadata, perAssetLimits.upload);
            }
            catch (const std::bad_alloc&) {
                throw;
            }
            catch (...) {
                addPreparationIssue(
                    result,
                    import,
                    render::GtiUploadDataIssueKind::planFailure);
                return result;
            }
            if (!description.issues.empty()) {
                for (const auto& upstream : description.issues) {
                    addPreparationIssue(
                        result, import, mapPlanIssue(upstream.kind));
                }
                return result;
            }
            if (!description.plan.has_value()) {
                addPreparationIssue(
                    result,
                    import,
                    render::GtiUploadDataIssueKind::planFailure);
                return result;
            }

            const auto& plannedUpload = *description.plan;
            if (plannedUpload.decodedRgbaBytes >
                remaining(
                    limits.maximumDecodedRgbaBytes,
                    decodedRgbaBytes)) {
                addTextureIssue(
                    result,
                    import,
                    WorldRoomLoadIssueKind::decodedRgbaLimitExceeded);
                return result;
            }
            if (plannedUpload.uploadRgbaBytes >
                remaining(
                    limits.maximumUploadRgbaBytes,
                    uploadRgbaBytes)) {
                addTextureIssue(
                    result,
                    import,
                    WorldRoomLoadIssueKind::uploadRgbaLimitExceeded);
                return result;
            }
            if (plannedUpload.residentRgbaBytes >
                remaining(
                    limits.maximumResidentRgbaBytes,
                    residentRgbaBytes)) {
                addTextureIssue(
                    result,
                    import,
                    WorldRoomLoadIssueKind::residentRgbaLimitExceeded);
                return result;
            }

            auto prospectivePublished = publishedCpuBytes;
            if (!accountPublished(
                    prospectivePublished,
                    plannedUpload.uploadLevels.size(),
                    sizeof(render::GtiUploadLevel),
                    limits.maximumPublishedCpuBytes) ||
                !accountPublished(
                    prospectivePublished,
                    plannedUpload.uploadLevels.size(),
                    sizeof(assets::RgbaImage),
                    limits.maximumPublishedCpuBytes)) {
                addTextureIssue(
                    result,
                    import,
                    prospectivePublished >
                            limits.maximumPublishedCpuBytes
                        ? WorldRoomLoadIssueKind::
                              publishedCpuLimitExceeded
                        : WorldRoomLoadIssueKind::integerOverflow);
                return result;
            }
            if (!checkedAdd(
                    prospectivePublished,
                    plannedUpload.decodedRgbaBytes)) {
                addTextureIssue(
                    result,
                    import,
                    WorldRoomLoadIssueKind::integerOverflow);
                return result;
            }
            if (prospectivePublished >
                limits.maximumPublishedCpuBytes) {
                addTextureIssue(
                    result,
                    import,
                    WorldRoomLoadIssueKind::publishedCpuLimitExceeded);
                return result;
            }
            if (stopToken.stop_requested()) {
                addIssue(result, WorldRoomLoadIssueKind::cancelled);
                return result;
            }

            render::GtiUploadPreparation preparation;
            try {
                preparation = render::prepareGtiUpload(
                    import, gtiBytes, perAssetLimits);
            }
            catch (const std::bad_alloc&) {
                throw;
            }
            catch (...) {
                addPreparationIssue(
                    result,
                    import,
                    render::GtiUploadDataIssueKind::decodeFailure);
                return result;
            }
            std::vector<std::uint8_t>().swap(gtiBytes);
            if (!preparation.success()) {
                addPreparationIssues(
                    result,
                    import,
                    preparation,
                    WorldRoomLoadIssueKind::texturePreparationFailure);
                return result;
            }
            if (!sameUploadPlan(
                    plannedUpload, *preparation.plan)) {
                addPreparationIssue(
                    result,
                    import,
                    render::GtiUploadDataIssueKind::planMismatch);
                return result;
            }

            const auto& upload = *preparation.plan;
            if (!checkedAdd(decodedRgbaBytes, upload.decodedRgbaBytes) ||
                !checkedAdd(uploadRgbaBytes, upload.uploadRgbaBytes) ||
                !checkedAdd(residentRgbaBytes, upload.residentRgbaBytes)) {
                addIssue(result, WorldRoomLoadIssueKind::integerOverflow);
                return result;
            }
            publishedCpuBytes = prospectivePublished;

            candidate.textures.push_back({
                .assetId = import.assetId,
                .sourceFileIndex = import.archiveFileIndex,
                .upload = std::move(*preparation.plan),
                .uploadLevels = std::move(preparation.uploadLevels),
            });
            if (!report(
                    result,
                    stopToken,
                    progress,
                    WorldRoomLoadPhase::loadingTextures,
                    index + 1U,
                    binding.imports.size())) {
                return result;
            }
        }

        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::assemblingRoom,
                0U,
                1U)) {
            return result;
        }
        auto roomDrawLimits = limits.roomDraw;
        roomDrawLimits.maximumTotalBytes = std::min(
            roomDrawLimits.maximumTotalBytes,
            clampToSize(remaining(
                limits.maximumPublishedCpuBytes, publishedCpuBytes)));
        render::CcfRoomDrawAssembly assembly;
        try {
            assembly = render::buildRoomDrawAssembly(
                ccf,
                request.ccfRoomIndex,
                binding.materials,
                {},
                render::UvPolicy::preserveRaw,
                roomDrawLimits);
        }
        catch (const std::bad_alloc&) {
            throw;
        }
        catch (...) {
            addIssue(
                result,
                WorldRoomLoadIssueKind::drawAssemblyFailure);
            return result;
        }
        if (!assembly.issues.empty()) {
            addDrawIssues(result, assembly);
            return result;
        }
        if (!addModelPublishedBytes(
                publishedCpuBytes,
                assembly.model,
                limits.maximumPublishedCpuBytes)) {
            addIssue(
                result,
                publishedCpuBytes >
                        limits.maximumPublishedCpuBytes
                    ? WorldRoomLoadIssueKind::publishedCpuLimitExceeded
                    : WorldRoomLoadIssueKind::integerOverflow);
            return result;
        }
        candidate.model = std::move(assembly.model);
        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::assemblingRoom,
                1U,
                1U)) {
            return result;
        }

        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::planningSubmission,
                0U,
                1U)) {
            return result;
        }
        std::size_t commandCount = 0U;
        for (const auto& instance : candidate.model.instances) {
            if (instance.meshSlot >= candidate.model.meshes.size()) {
                break;
            }
            const auto rangeCount =
                candidate.model.meshes[instance.meshSlot].ranges.size();
            if (rangeCount >
                std::numeric_limits<std::size_t>::max() - commandCount) {
                addIssue(result, WorldRoomLoadIssueKind::integerOverflow);
                return result;
            }
            commandCount += rangeCount;
        }
        auto prospectivePublished = publishedCpuBytes;
        if (!accountPublished(
                prospectivePublished,
                candidate.model.meshes.size(),
                sizeof(render::DrawMeshUploadMetadata),
                limits.maximumPublishedCpuBytes) ||
            !accountPublished(
                prospectivePublished,
                commandCount,
                sizeof(render::DrawSubmissionCommand),
                limits.maximumPublishedCpuBytes)) {
            addIssue(
                result,
                prospectivePublished >
                        limits.maximumPublishedCpuBytes
                    ? WorldRoomLoadIssueKind::publishedCpuLimitExceeded
                    : WorldRoomLoadIssueKind::integerOverflow);
            return result;
        }
        render::DrawSubmissionDescription submission;
        try {
            submission = render::buildDrawSubmissionPlan(
                candidate.model,
                candidate.textures.size(),
                limits.submission);
        }
        catch (const std::bad_alloc&) {
            throw;
        }
        catch (...) {
            addIssue(result, WorldRoomLoadIssueKind::submissionFailure);
            return result;
        }
        if (!submission.issues.empty() || !submission.plan.has_value()) {
            addSubmissionIssues(result, submission);
            return result;
        }
        publishedCpuBytes = prospectivePublished;
        candidate.submission = std::move(*submission.plan);
        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::planningSubmission,
                1U,
                1U)) {
            return result;
        }

        if (!report(
                result,
                stopToken,
                progress,
                WorldRoomLoadPhase::complete,
                1U,
                1U)) {
            return result;
        }
        result.room = std::move(candidate);
        return result;
    }
    catch (const std::bad_alloc&) {
        result.room.reset();
        result.issues.clear();
        addIssue(result, WorldRoomLoadIssueKind::allocationFailure);
        return result;
    }
    catch (...) {
        result.room.reset();
        result.issues.clear();
        addIssue(result, WorldRoomLoadIssueKind::internalFailure);
        return result;
    }
}

} // namespace airfix::content
