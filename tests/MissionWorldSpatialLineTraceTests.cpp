#include "airfix/assets/MissionWorldSpatialLineTrace.hpp"

#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

using namespace airfix::assets;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] MissionWorldSpatialPolygon triangle(
    const float marker,
    const std::optional<std::size_t> portalTarget = std::nullopt) {
    return {
        .faceCross = {0.0F, 0.0F, 1.0F},
        .faceNormal = {0.0F, 0.0F, 1.0F},
        .point0 = {-1.0F, -1.0F, marker},
        .edge01 = {2.0F, 0.0F, 0.0F},
        .edge12 = {-1.0F, 2.0F, 0.0F},
        .polygonIndex = static_cast<std::uint32_t>(marker),
        .placedObjectReference = 7U,
        .portalWorldRoomIndex = portalTarget,
        .portalMeshSelectionFlagB = portalTarget.has_value(),
        .portalType = 0U,
        .portalObjectVisible = portalTarget.has_value(),
    };
}

[[nodiscard]] MissionWorldSpatialArena oneTreeArena(
    const CcfBspTreeKind kind,
    std::vector<MissionWorldSpatialPolygon> polygons) {
    MissionWorldSpatialArena arena;
    arena.rooms.resize(2U);
    arena.treeReferences = {0U};
    arena.trees.push_back({
        .kind = kind,
        .worldRoomIndex = 0U,
        .rootNodeIndex = 0U,
        .firstNodeIndex = 0U,
        .nodeCount = 1U,
    });
    arena.nodes.push_back({
        .splitNormal = {0.0F, 0.0F, 1.0F},
        .pointOnPlane = {0.0F, 0.0F, 0.0F},
        .firstPolygonIndex = 0U,
        .polygonCount = polygons.size(),
    });
    arena.polygons = std::move(polygons);
    if (kind == CcfBspTreeKind::staticTree) {
        arena.rooms[0].staticTreeCount = 1U;
    }
    else {
        arena.rooms[0].portalTreeCount = 1U;
    }
    return arena;
}

void testFrontFacingHitAndMiss() {
    auto arena = oneTreeArena(
        CcfBspTreeKind::staticTree, {triangle(0.0F)});
    const auto hit = traceMissionWorldSpatialLine(
        arena,
        0U,
        CcfBspTreeKind::staticTree,
        {0.0F, 0.0F, -2.0F},
        {0.0F, 0.0F, 2.0F});
    require(
        hit.status == MissionWorldSpatialLineTraceStatus::hit &&
            hit.hit.has_value() &&
            hit.hit->fraction == 0.5F &&
            hit.hit->point == CcfVector3{0.0F, 0.0F, 0.0F} &&
            hit.hit->normal == CcfVector3{0.0F, 0.0F, 1.0F} &&
            !hit.hit->reverseFacing,
        "front-facing line did not produce the exact plane hit");

    const auto miss = traceMissionWorldSpatialLine(
        arena,
        0U,
        CcfBspTreeKind::staticTree,
        {3.0F, 0.0F, -2.0F},
        {3.0F, 0.0F, 2.0F});
    require(
        miss.status == MissionWorldSpatialLineTraceStatus::noHit &&
            !miss.hit.has_value(),
        "outside-triangle line produced a hit");
}

void testOneSidedAndReverseFacingBehavior() {
    auto arena = oneTreeArena(
        CcfBspTreeKind::staticTree, {triangle(0.0F)});
    const auto defaultTrace = traceMissionWorldSpatialLine(
        arena,
        0U,
        CcfBspTreeKind::staticTree,
        {0.0F, 0.0F, 2.0F},
        {0.0F, 0.0F, -2.0F});
    require(
        defaultTrace.status ==
            MissionWorldSpatialLineTraceStatus::noHit,
        "default PhLine behavior accepted a reverse face");

    const auto twoSided = traceMissionWorldSpatialLine(
        arena,
        0U,
        CcfBspTreeKind::staticTree,
        {0.0F, 0.0F, 2.0F},
        {0.0F, 0.0F, -2.0F},
        {.allowReverseFacing = true});
    require(
        twoSided.status == MissionWorldSpatialLineTraceStatus::hit &&
            twoSided.hit->fraction == 0.5F &&
            twoSided.hit->reverseFacing,
        "two-sided trace did not report its reverse-facing hit");
}

void testFirstPolygonWinsAnExactTie() {
    auto first = triangle(0.0F);
    first.polygonIndex = 11U;
    auto second = triangle(0.0F);
    second.polygonIndex = 22U;
    auto arena = oneTreeArena(
        CcfBspTreeKind::staticTree, {first, second});

    const auto result = traceMissionWorldSpatialLine(
        arena,
        0U,
        CcfBspTreeKind::staticTree,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 1.0F});
    require(
        result.status == MissionWorldSpatialLineTraceStatus::hit &&
            result.hit->polygonIndex == 0U &&
            arena.polygons[result.hit->polygonIndex].polygonIndex == 11U,
        "strict nearest comparison did not preserve first-hit tie order");
}

void testNearChildRunsBeforeNodeAndFarChild() {
    auto arena = oneTreeArena(
        CcfBspTreeKind::staticTree, {triangle(0.0F)});
    arena.trees[0].nodeCount = 3U;
    arena.trees[0].rootNodeIndex = 0U;
    arena.nodes[0].childAIndex = 1U;
    arena.nodes[0].childBIndex = 2U;
    arena.nodes[0].polygonCount = 0U;
    arena.nodes.push_back({
        .splitNormal = {0.0F, 0.0F, 1.0F},
        .pointOnPlane = {0.0F, 0.0F, -0.5F},
        .firstPolygonIndex = 0U,
        .polygonCount = 1U,
    });
    arena.nodes.push_back({
        .splitNormal = {0.0F, 0.0F, 1.0F},
        .pointOnPlane = {0.0F, 0.0F, 0.5F},
        .firstPolygonIndex = 1U,
        .polygonCount = 1U,
    });
    arena.polygons[0] = triangle(-0.5F);
    arena.polygons.push_back(triangle(0.5F));

    const auto result = traceMissionWorldSpatialLine(
        arena,
        0U,
        CcfBspTreeKind::staticTree,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 1.0F});
    require(
        result.status == MissionWorldSpatialLineTraceStatus::hit &&
            result.hit->nodeIndex == 1U &&
            result.hit->fraction == 0.25F,
        "trace did not retain the near-child hit");
}

void testCoplanarAncestorStillVisitsFarChild() {
    auto childPolygon = triangle(0.0F);
    childPolygon.faceCross = {1.0F, 0.0F, 0.0F};
    childPolygon.faceNormal = {1.0F, 0.0F, 0.0F};
    childPolygon.point0 = {0.0F, -1.0F, -1.0F};
    childPolygon.edge01 = {0.0F, 2.0F, 0.0F};
    childPolygon.edge12 = {0.0F, -1.0F, 2.0F};
    auto arena = oneTreeArena(
        CcfBspTreeKind::staticTree, {childPolygon});
    arena.trees[0].nodeCount = 2U;
    arena.nodes[0].childBIndex = 1U;
    arena.nodes[0].polygonCount = 0U;
    arena.nodes.push_back({
        .splitNormal = {1.0F, 0.0F, 0.0F},
        .pointOnPlane = {},
        .firstPolygonIndex = 0U,
        .polygonCount = 1U,
    });

    const auto result = traceMissionWorldSpatialLine(
        arena,
        0U,
        CcfBspTreeKind::staticTree,
        {-1.0F, 0.0F, 0.0F},
        {1.0F, 0.0F, 0.0F});
    require(
        result.status == MissionWorldSpatialLineTraceStatus::hit &&
            result.hit->nodeIndex == 1U &&
            result.hit->fraction == 0.5F,
        "unordered ancestor crossing skipped the legacy far child");
}

void testBinary32CrossingTieKeepsFirstTree() {
    constexpr float firstPlane = 0.09999258071184158F;
    constexpr float secondPlane = 0.09999258816242218F;
    auto arena = oneTreeArena(
        CcfBspTreeKind::staticTree, {triangle(firstPlane)});
    arena.treeReferences = {1U, 0U};
    arena.rooms[0].staticTreeCount = 2U;
    arena.trees[0].nodeCount = 1U;
    arena.trees.push_back({
        .kind = CcfBspTreeKind::staticTree,
        .worldRoomIndex = 0U,
        .rootNodeIndex = 1U,
        .firstNodeIndex = 1U,
        .nodeCount = 1U,
    });
    arena.nodes[0].pointOnPlane = {0.0F, 0.0F, firstPlane};
    arena.nodes[0].firstPolygonIndex = 0U;
    arena.nodes.push_back({
        .splitNormal = {0.0F, 0.0F, 1.0F},
        .pointOnPlane = {0.0F, 0.0F, secondPlane},
        .firstPolygonIndex = 1U,
        .polygonCount = 1U,
    });
    arena.polygons.push_back(triangle(secondPlane));

    const auto result = traceMissionWorldSpatialLine(
        arena,
        0U,
        CcfBspTreeKind::staticTree,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 1.0F});
    require(
        result.status == MissionWorldSpatialLineTraceStatus::hit &&
            result.hit->treeIndex == 1U,
        "binary32 crossing tie did not keep the first visited tree");
}

void testLegacyPortalHitPointSpillOrder() {
    auto polygon = triangle(0.0F);
    polygon.point0 = {-500.0F, -500.0F, 0.0F};
    polygon.edge01 = {1'000.0F, 0.0F, 0.0F};
    polygon.edge12 = {-500.0F, 1'000.0F, 0.0F};
    auto arena = oneTreeArena(
        CcfBspTreeKind::staticTree, {polygon});

    const auto result = traceMissionWorldSpatialLine(
        arena,
        0U,
        CcfBspTreeKind::staticTree,
        {39.18901824951172F, 0.0F, -1.0F},
        {-249.16871643066406F, 0.0F, 1.0F});
    require(
        result.status == MissionWorldSpatialLineTraceStatus::hit &&
            result.hit.has_value() &&
            std::bit_cast<std::uint32_t>(result.hit->point[0]) ==
                0xC2D1FACDU &&
            result.hit->point[1] == 0.0F &&
            result.hit->point[2] == 0.0F,
        "portal hit point did not preserve the x86 binary32 spill order");
}

void testPolygonEndpointUsesBinary32RelativeSpills() {
    auto polygon = triangle(0.0F);
    polygon.faceNormal = {
        -0.47878551483154297F,
        0.8779318928718567F,
        0.0F,
    };
    polygon.point0 = {
        37312.140625F,
        -250815.84375F,
        0.0F,
    };
    polygon.edge01 = {2'000.0F, 0.0F, 0.0F};
    polygon.edge12 = {-1'000.0F, 2'000.0F, 0.0F};
    auto arena = oneTreeArena(
        CcfBspTreeKind::staticTree, {polygon});
    arena.nodes[0].pointOnPlane = {
        -1023.0088500976562F,
        -271837.03125F,
        0.0F,
    };

    const auto result = traceMissionWorldSpatialLine(
        arena,
        0U,
        CcfBspTreeKind::staticTree,
        {38551.53515625F, -250254.8125F, -1.0F},
        {38072.75F, -249376.875F, 1.0F});
    require(
        result.status == MissionWorldSpatialLineTraceStatus::noHit &&
            !result.hit.has_value(),
        "double cancellation bypassed the binary32 endpoint-plane gate");
}

void testUnknownTreeKindIsInvalidInput() {
    auto arena = oneTreeArena(
        CcfBspTreeKind::staticTree, {triangle(0.0F)});
    const auto result = traceMissionWorldSpatialLine(
        arena,
        0U,
        static_cast<CcfBspTreeKind>(0xFFU),
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 1.0F});
    require(
        result.status == MissionWorldSpatialLineTraceStatus::invalidInput &&
            !result.hit.has_value(),
        "unknown BSP tree kind was not rejected as invalid input");
}

void testPortalTargetAndFailClosedInputs() {
    auto portalArena = oneTreeArena(
        CcfBspTreeKind::portalTree, {triangle(0.0F, 1U)});
    const auto portal = traceMissionWorldSpatialLine(
        portalArena,
        0U,
        CcfBspTreeKind::portalTree,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 1.0F});
    require(
        portal.status == MissionWorldSpatialLineTraceStatus::hit &&
            portal.hit->portalWorldRoomIndex ==
                std::optional<std::size_t>{1U},
        "portal hit did not retain its runtime target");

    const auto transition = traceMissionWorldPortalTransition(
        portalArena,
        0U,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 1.0F});
    require(
        transition.status == MissionWorldPortalTraceStatus::transition &&
            transition.targetWorldRoomIndex ==
                std::optional<std::size_t>{1U} &&
            transition.hit.has_value() &&
            transition.transitionCount == 1U,
        "eligible portal did not transition to its runtime room");

    portalArena.polygons[0].portalMeshSelectionFlagB = false;
    const auto meshBlocked = traceMissionWorldPortalTransition(
        portalArena,
        0U,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 1.0F});
    portalArena.polygons[0].portalMeshSelectionFlagB = true;
    portalArena.polygons[0].portalType = 1U;
    const auto typeBlocked = traceMissionWorldPortalTransition(
        portalArena,
        0U,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 1.0F});
    portalArena.polygons[0].portalType = 0U;
    portalArena.polygons[0].portalObjectVisible = false;
    const auto visibilityBlocked = traceMissionWorldPortalTransition(
        portalArena,
        0U,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 1.0F});
    require(
        meshBlocked.status ==
                MissionWorldPortalTraceStatus::noTransition &&
            typeBlocked.status ==
                MissionWorldPortalTraceStatus::noTransition &&
        visibilityBlocked.status ==
                MissionWorldPortalTraceStatus::noTransition &&
            meshBlocked.hit.has_value() &&
            typeBlocked.hit.has_value() &&
            visibilityBlocked.hit.has_value(),
        "portal follow gates did not block the nearest hit");
    portalArena.polygons[0].portalObjectVisible = true;

    portalArena.polygons[0].portalWorldRoomIndex = std::nullopt;
    const auto malformed = traceMissionWorldSpatialLine(
        portalArena,
        0U,
        CcfBspTreeKind::portalTree,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 1.0F});
    require(
        malformed.status ==
            MissionWorldSpatialLineTraceStatus::invalidArena &&
            !malformed.hit.has_value(),
        "missing portal target did not invalidate the query");

    const auto invalidRoom = traceMissionWorldSpatialLine(
        portalArena,
        2U,
        CcfBspTreeKind::portalTree,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 1.0F});
    require(
        invalidRoom.status ==
            MissionWorldSpatialLineTraceStatus::invalidWorldRoom,
        "invalid runtime room was not rejected");

    const auto invalidInput = traceMissionWorldSpatialLine(
        portalArena,
        0U,
        CcfBspTreeKind::portalTree,
        {std::numeric_limits<float>::infinity(), 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F});
    require(
        invalidInput.status ==
            MissionWorldSpatialLineTraceStatus::invalidInput,
        "non-finite line was not rejected");
}

void testPortalTransitionCycleIsBounded() {
    auto arena = oneTreeArena(
        CcfBspTreeKind::portalTree, {triangle(0.0F, 0U)});
    const auto result = traceMissionWorldPortalTransition(
        arena,
        0U,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 1.0F},
        {.maximumTransitions = 2U});
    require(
        result.status ==
                MissionWorldPortalTraceStatus::
                    transitionLimitExceeded &&
            result.transitionCount == 2U &&
            !result.targetWorldRoomIndex.has_value(),
        "self-portal did not stop at the explicit safety bound");

    const auto unbounded = traceMissionWorldPortalTransition(
        arena,
        0U,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 1.0F},
        {
            .maximumTransitions =
                kMissionWorldSpatialMaximumPortalTransitions + 1U,
        });
    require(
        unbounded.status ==
                MissionWorldPortalTraceStatus::invalidInput &&
            unbounded.transitionCount == 0U,
        "caller bypassed the hard portal transition cap");
}

void testStrictPortalTransitionRejectsEpsilonHit() {
    auto arena = oneTreeArena(
        CcfBspTreeKind::portalTree, {triangle(1.0F, 1U)});
    arena.nodes[0].pointOnPlane = {0.0F, 0.0F, 1.0F};
    const auto raw = traceMissionWorldPortalTransition(
        arena,
        0U,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 0.9999995F});
    require(
        raw.status == MissionWorldPortalTraceStatus::transition &&
            raw.targetWorldRoomIndex ==
                std::optional<std::size_t>{1U},
        "raw portal trace no longer preserves the legacy epsilon hit");

    const auto strict = traceMissionWorldPortalTransition(
        arena,
        0U,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 0.9999995F},
        {.requireHitsWithinSegment = true});
    require(
        strict.status ==
                MissionWorldPortalTraceStatus::outOfSegmentHit &&
            strict.hit.has_value() &&
            strict.hit->fraction > 1.0F &&
            !strict.targetWorldRoomIndex.has_value() &&
            strict.transitionCount == 0U,
        "strict portal trace accepted an epsilon hit outside the segment");
}

void testStrictPortalTransitionChecksEveryHop() {
    auto arena = oneTreeArena(
        CcfBspTreeKind::portalTree, {triangle(0.0F, 1U)});
    arena.rooms.resize(3U);
    arena.rooms[1].firstPortalTreeReference = 1U;
    arena.rooms[1].portalTreeCount = 1U;
    arena.treeReferences.push_back(1U);
    arena.trees.push_back({
        .kind = CcfBspTreeKind::portalTree,
        .worldRoomIndex = 1U,
        .rootNodeIndex = 1U,
        .firstNodeIndex = 1U,
        .nodeCount = 1U,
    });
    arena.nodes.push_back({
        .splitNormal = {0.0F, 0.0F, 1.0F},
        .pointOnPlane = {0.0F, 0.0F, 1.0F},
        .firstPolygonIndex = 1U,
        .polygonCount = 1U,
    });
    arena.polygons.push_back(triangle(1.0F, 2U));

    const auto strict = traceMissionWorldPortalTransition(
        arena,
        0U,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 0.9999995F},
        {.requireHitsWithinSegment = true});
    require(
        strict.status ==
                MissionWorldPortalTraceStatus::outOfSegmentHit &&
            strict.hit.has_value() &&
            strict.hit->ownerWorldRoomIndex == 1U &&
            strict.hit->fraction > 1.0F &&
            strict.transitionCount == 1U &&
            !strict.targetWorldRoomIndex.has_value(),
        "strict portal trace checked only the last published result");
}

void testLegacyFaceNormalSentinelNeverHits() {
    auto sentinel = triangle(0.0F);
    const auto nan = std::bit_cast<float>(0xFFC00000U);
    sentinel.faceNormal = {nan, nan, nan};
    auto arena = oneTreeArena(
        CcfBspTreeKind::staticTree, {sentinel});
    const auto result = traceMissionWorldSpatialLine(
        arena,
        0U,
        CcfBspTreeKind::staticTree,
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 1.0F},
        {.allowReverseFacing = true});
    require(
        result.status == MissionWorldSpatialLineTraceStatus::noHit,
        "legacy NaN face-normal sentinel produced a collision");
}

} // namespace

int main() {
    try {
        testFrontFacingHitAndMiss();
        testOneSidedAndReverseFacingBehavior();
        testFirstPolygonWinsAnExactTie();
        testNearChildRunsBeforeNodeAndFarChild();
        testCoplanarAncestorStillVisitsFarChild();
        testBinary32CrossingTieKeepsFirstTree();
        testLegacyPortalHitPointSpillOrder();
        testPolygonEndpointUsesBinary32RelativeSpills();
        testUnknownTreeKindIsInvalidInput();
        testPortalTargetAndFailClosedInputs();
        testPortalTransitionCycleIsBounded();
        testStrictPortalTransitionRejectsEpsilonHit();
        testStrictPortalTransitionChecksEveryHop();
        testLegacyFaceNormalSentinelNeverHits();
        std::cout << "Mission world spatial line trace tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "Mission world spatial line trace tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
