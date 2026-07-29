#include "airfix/simulation/LegacyProjectileCollisionLoop.hpp"

#include <cmath>

namespace airfix::simulation {
namespace {

[[nodiscard]] bool finite(
    const LegacyMachineGunVector3& value) noexcept {
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

[[nodiscard]] LegacyProjectileCollisionLoopResult failure(
    const LegacyProjectileCollisionLoopStatus status,
    const std::size_t queryCount,
    const std::size_t portalTransitionCount) noexcept {
    return {
        .status = status,
        .decision = std::nullopt,
        .queryCount = queryCount,
        .portalTransitionCount = portalTransitionCount,
    };
}

} // namespace

LegacyProjectileCollisionLoopResult
resolveLegacyProjectileCollisionLoop(
    const LegacyProjectileCollisionQueryInput& input,
    const LegacyProjectileCollisionQuery query,
    void* const queryContext,
    const LegacyProjectileCollisionLoopOptions& options) noexcept {
    if (query == nullptr ||
        !finite(input.segmentStart) ||
        !finite(input.segmentEnd) ||
        options.maximumPortalTransitions >
            legacyProjectileHardMaximumPortalTransitions) {
        return failure(
            LegacyProjectileCollisionLoopStatus::invalidInput,
            0U,
            0U);
    }

    auto current = input;
    std::size_t queryCount = 0U;
    std::size_t portalTransitionCount = 0U;
    while (true) {
        const auto queried = query(queryContext, current);
        ++queryCount;

        std::optional<LegacyProjectileCollisionHit> hit;
        if (queried.status ==
            LegacyProjectileCollisionQueryStatus::noHit) {
            if (queried.hit.has_value()) {
                return failure(
                    LegacyProjectileCollisionLoopStatus::queryRejected,
                    queryCount,
                    portalTransitionCount);
            }
        } else if (queried.status ==
            LegacyProjectileCollisionQueryStatus::hit) {
            if (!queried.hit.has_value()) {
                return failure(
                    LegacyProjectileCollisionLoopStatus::queryRejected,
                    queryCount,
                    portalTransitionCount);
            }
            hit = queried.hit;
        } else {
            return failure(
                LegacyProjectileCollisionLoopStatus::queryRejected,
                queryCount,
                portalTransitionCount);
        }

        const auto decision = legacyProjectileCollisionDecision({
            .segmentStart = current.segmentStart,
            .segmentEnd = current.segmentEnd,
            .roomId = current.roomId,
            .hit = hit,
        });
        if (!decision.has_value()) {
            return failure(
                LegacyProjectileCollisionLoopStatus::decisionRejected,
                queryCount,
                portalTransitionCount);
        }
        if (decision->outcome !=
            LegacyProjectileCollisionOutcome::followPortal) {
            return {
                .status =
                    LegacyProjectileCollisionLoopStatus::completed,
                .decision = decision,
                .queryCount = queryCount,
                .portalTransitionCount = portalTransitionCount,
            };
        }
        if (portalTransitionCount >=
            options.maximumPortalTransitions) {
            return failure(
                LegacyProjectileCollisionLoopStatus::
                    portalTransitionLimitExceeded,
                queryCount,
                portalTransitionCount);
        }

        current.segmentStart = decision->position;
        current.segmentEnd = decision->previousPosition;
        current.roomId = decision->roomId;
        ++portalTransitionCount;
    }
}

} // namespace airfix::simulation
