#include "airfix/render/PlayerActorVisualDrawAssembly.hpp"

#include <array>
#include <limits>
#include <numbers>
#include <utility>

namespace airfix::render {
namespace {

void addObjectVisualIssue(
    PlayerActorVisualDrawAssembly& result,
    const ObjectVisualDrawIssue& upstream) {
    result.issues.push_back({
        .kind = PlayerActorVisualDrawIssueKind::objectVisualFailure,
        .objectVisualIssue = upstream.kind,
        .blueprintIndex = upstream.blueprintIndex,
        .physicalMeshIndex = upstream.physicalMeshIndex,
        .sourceReference = upstream.sourceReference,
        .dependencyIssue = upstream.dependencyIssue,
        .graphIssue = upstream.graphIssue,
        .geometryError = upstream.geometryError,
        .drawMeshError = upstream.drawMeshError,
    });
}

void addIssue(
    PlayerActorVisualDrawAssembly& result,
    const PlayerActorVisualDrawIssueKind kind,
    const std::optional<std::size_t> blueprintIndex = std::nullopt,
    const std::optional<std::size_t> physicalMeshIndex = std::nullopt,
    const std::optional<std::uint32_t> sourceReference = std::nullopt,
    const std::optional<GeometryErrorCode> geometryError = std::nullopt) {
    result.issues.push_back({
        .kind = kind,
        .objectVisualIssue = std::nullopt,
        .blueprintIndex = blueprintIndex,
        .physicalMeshIndex = physicalMeshIndex,
        .sourceReference = sourceReference,
        .dependencyIssue = std::nullopt,
        .graphIssue = std::nullopt,
        .geometryError = geometryError,
        .drawMeshError = std::nullopt,
    });
}

[[nodiscard]] bool accountElements(
    std::size_t& total,
    const std::size_t count,
    const std::size_t elementSize,
    const std::size_t limit,
    PlayerActorVisualDrawAssembly& result) {
    if (elementSize != 0U &&
        count > std::numeric_limits<std::size_t>::max() / elementSize) {
        addIssue(
            result,
            PlayerActorVisualDrawIssueKind::integerOverflow);
        return false;
    }
    const auto bytes = count * elementSize;
    if (bytes > std::numeric_limits<std::size_t>::max() - total) {
        addIssue(
            result,
            PlayerActorVisualDrawIssueKind::integerOverflow);
        return false;
    }
    total += bytes;
    if (total > limit) {
        addIssue(
            result,
            PlayerActorVisualDrawIssueKind::limitExceeded);
        return false;
    }
    return true;
}

[[nodiscard]] bool accountFinalLogicalPayload(
    const DrawModelPayload& model,
    const std::size_t meshProvenanceCount,
    const std::size_t instanceProvenanceCount,
    const std::size_t limit,
    PlayerActorVisualDrawAssembly& result) {
    std::size_t total = 0U;
    if (!accountElements(
            total,
            model.meshes.size(),
            sizeof(DrawMeshPayload),
            limit,
            result) ||
        !accountElements(
            total,
            model.instances.size(),
            sizeof(DrawMeshInstance),
            limit,
            result) ||
        !accountElements(
            total,
            meshProvenanceCount,
            sizeof(PlayerActorVisualProvenance),
            limit,
            result) ||
        !accountElements(
            total,
            instanceProvenanceCount,
            sizeof(PlayerActorVisualProvenance),
            limit,
            result)) {
        return false;
    }

    for (const auto& mesh : model.meshes) {
        if (!accountElements(
                total,
                mesh.vertices.size(),
                sizeof(DrawVertex),
                limit,
                result) ||
            !accountElements(
                total,
                mesh.indices.size(),
                sizeof(std::uint32_t),
                limit,
                result) ||
            !accountElements(
                total,
                mesh.materials.size(),
                sizeof(DrawMaterial),
                limit,
                result) ||
            !accountElements(
                total,
                mesh.ranges.size(),
                sizeof(DrawRange),
                limit,
                result)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool validProvenance(
    const ObjectVisualProvenance& provenance,
    const assets::CcfMetadata& ccf) noexcept {
    return provenance.blueprintIndex < ccf.blueprints.size() &&
        provenance.physicalMeshIndex < ccf.meshes.size();
}

[[nodiscard]] PlayerActorVisualProvenance makeProvenance(
    const ObjectVisualProvenance& provenance,
    const assets::CcfMetadata& ccf) noexcept {
    return {
        .legacySkinSlot = 0U,
        .blueprintIndex = provenance.blueprintIndex,
        .blueprintReference =
            ccf.blueprints[provenance.blueprintIndex].reference,
        .physicalMeshIndex = provenance.physicalMeshIndex,
    };
}

} // namespace

PlayerActorVisualDrawAssembly buildPlayerActorVisualDrawAssembly(
    const assets::ObjectDefinition& object,
    const assets::CcfMetadata& ccf,
    const std::span<const DrawMaterial> materialBindings,
    const BasisTransform& basis,
    const UvPolicy uvPolicy,
    const ObjectVisualDrawLimits& limits) {
    PlayerActorVisualDrawAssembly result;
    auto objectVisual = buildObjectVisualDrawAssembly(
        object, ccf, materialBindings, basis, uvPolicy, limits);
    result.resolution = std::move(objectVisual.resolution);

    if (!objectVisual.issues.empty()) {
        result.issues.reserve(objectVisual.issues.size());
        for (const auto& issue : objectVisual.issues) {
            addObjectVisualIssue(result, issue);
        }
        return result;
    }

    if (!result.resolution.rootBlueprintIndex.has_value() ||
        *result.resolution.rootBlueprintIndex >= ccf.blueprints.size() ||
        objectVisual.model.instances.size() !=
            objectVisual.instanceProvenance.size() ||
        objectVisual.model.meshes.size() !=
            objectVisual.meshProvenance.size()) {
        addIssue(
            result,
            PlayerActorVisualDrawIssueKind::invalidObjectVisualAssembly,
            result.resolution.rootBlueprintIndex);
        return result;
    }
    for (const auto& provenance : objectVisual.meshProvenance) {
        if (!validProvenance(provenance, ccf)) {
            addIssue(
                result,
                PlayerActorVisualDrawIssueKind::invalidObjectVisualAssembly,
                provenance.blueprintIndex,
                provenance.physicalMeshIndex);
            return result;
        }
    }
    for (const auto& provenance : objectVisual.instanceProvenance) {
        if (!validProvenance(provenance, ccf)) {
            addIssue(
                result,
                PlayerActorVisualDrawIssueKind::invalidObjectVisualAssembly,
                provenance.blueprintIndex,
                provenance.physicalMeshIndex);
            return result;
        }
    }

    const auto rootIndex = *result.resolution.rootBlueprintIndex;
    const auto& rootBlueprint = ccf.blueprints[rootIndex];
    ConvertedNodeTransform authoredRoot;
    try {
        authoredRoot = convertLegacyTransform(
            rootBlueprint.authoredTransform, basis);
    }
    catch (const GeometryError& error) {
        addIssue(
            result,
            PlayerActorVisualDrawIssueKind::invalidSelectedRootTransform,
            rootIndex,
            rootBlueprint.meshIndex,
            rootBlueprint.reference,
            error.code());
        return result;
    }

    ConvertedNodeTransform recoveredRootLocal;
    try {
        // The original passes a float pi through the legacy Y-axis rotation
        // path. Using the existing converter keeps the recovered operation
        // order and basis conversion independent of any host math-library
        // matrix convention.
        recoveredRootLocal = convertLegacyAxisRotationWorldPose(
            Vec3{},
            std::array<float, 3>{
                0.0F,
                std::numbers::pi_v<float>,
                0.0F,
            },
            basis);
    }
    catch (const GeometryError& error) {
        addIssue(
            result,
            PlayerActorVisualDrawIssueKind::invalidActorLocalTransform,
            rootIndex,
            rootBlueprint.meshIndex,
            rootBlueprint.reference,
            error.code());
        return result;
    }

    DrawModelPayload candidate = std::move(objectVisual.model);
    for (std::size_t instanceIndex = 0U;
         instanceIndex < candidate.instances.size();
         ++instanceIndex) {
        const auto& provenance =
            objectVisual.instanceProvenance[instanceIndex];
        const auto& blueprint =
            ccf.blueprints[provenance.blueprintIndex];
        try {
            // Generic instances are authored-world. Re-convert the source
            // transform to retain its validated metadata, derive it relative
            // to the selected authored root, and only then establish the
            // recovered actor-local root pose.
            const auto authoredNode = convertLegacyTransform(
                blueprint.authoredTransform, basis);
            const auto relative =
                deriveLocalTransform(authoredRoot, authoredNode);
            const auto actorLocal =
                composeNodeTransforms(recoveredRootLocal, relative);
            candidate.instances[instanceIndex].modelLinear =
                actorLocal.linear;
            candidate.instances[instanceIndex].modelTranslation =
                actorLocal.translation;
        }
        catch (const GeometryError& error) {
            addIssue(
                result,
                PlayerActorVisualDrawIssueKind::invalidActorLocalTransform,
                provenance.blueprintIndex,
                provenance.physicalMeshIndex,
                blueprint.reference,
                error.code());
            return result;
        }
    }

    std::vector<PlayerActorVisualProvenance> meshProvenance;
    meshProvenance.reserve(objectVisual.meshProvenance.size());
    for (const auto& provenance : objectVisual.meshProvenance) {
        meshProvenance.push_back(makeProvenance(provenance, ccf));
    }
    std::vector<PlayerActorVisualProvenance> instanceProvenance;
    instanceProvenance.reserve(objectVisual.instanceProvenance.size());
    for (const auto& provenance : objectVisual.instanceProvenance) {
        instanceProvenance.push_back(makeProvenance(provenance, ccf));
    }

    // Recount the complete final logical payload from zero. The generic
    // builder accounted its smaller ObjectVisualProvenance elements; this
    // publication contains the larger actor provenance and therefore cannot
    // safely inherit or increment the upstream byte total.
    if (!accountFinalLogicalPayload(
            candidate,
            meshProvenance.size(),
            instanceProvenance.size(),
            limits.maximumTotalBytes,
            result)) {
        return result;
    }

    result.model = std::move(candidate);
    result.meshProvenance = std::move(meshProvenance);
    result.instanceProvenance = std::move(instanceProvenance);
    return result;
}

} // namespace airfix::render
