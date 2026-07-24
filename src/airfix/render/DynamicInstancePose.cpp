#include "airfix/render/DynamicInstancePose.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace airfix::render::detail {

namespace {

[[nodiscard]] std::size_t acceptedSlotCapacity(
    const DynamicInstancePoseLimits& limits) noexcept {
    return std::min(
        limits.maximumOverrides,
        limits.maximumFrameBytes /
            sizeof(DynamicInstancePoseOverride));
}

} // namespace

struct ScenePoseExchangeSlot {
    std::vector<DynamicInstancePoseOverride> overrides;
    // The high bit is an exclusive writer claim. Remaining values count
    // leases. Claiming the non-front slot closes the race with a reader that
    // sampled an older front generation but has not registered its lease yet.
    std::atomic<std::uint32_t> accessState{0U};
    std::size_t overrideCount{};
    std::uint64_t sceneGeneration{};
    std::uint64_t publicationGeneration{};
    std::uint64_t simulationStep{};
};

struct ScenePoseExchangeStorage {
    explicit ScenePoseExchangeStorage(
        const DynamicInstancePoseLimits configuredLimits)
        : limits(configuredLimits) {
        const auto capacity = acceptedSlotCapacity(limits);
        slots[0].overrides.resize(capacity);
        slots[1].overrides.resize(capacity);
    }

    DynamicInstancePoseLimits limits;
    std::array<ScenePoseExchangeSlot, 2U> slots;
    // Zero means no frame. A nonzero value is both the publication generation
    // and an ABA-resistant front-slot tag; slot = (generation - 1) % 2.
    std::atomic<std::uint64_t> publishedGeneration{0U};
    std::atomic<std::uint64_t> activeSceneGeneration{0U};
    std::atomic<bool> closed{false};
    std::uint64_t nextSceneGeneration{1U};
    std::uint32_t instanceCount{};
    bool hasPublishedStep{false};
    std::uint64_t lastPublishedStep{};
    std::uint64_t publicationGeneration{};
};

} // namespace airfix::render::detail

namespace airfix::render {

namespace {

constexpr std::uint32_t kWriterClaim =
    std::uint32_t{1U} << 31U;
constexpr std::uint32_t kMaximumReaderCount = kWriterClaim - 1U;

[[nodiscard]] bool finite(const Vec3& vector) noexcept {
    return std::isfinite(vector.x) &&
           std::isfinite(vector.y) &&
           std::isfinite(vector.z);
}

[[nodiscard]] bool finite(const Mat3& matrix) noexcept {
    return finite(matrix.columns[0]) &&
           finite(matrix.columns[1]) &&
           finite(matrix.columns[2]);
}

} // namespace

ScenePoseHandle::ScenePoseHandle(
    std::shared_ptr<detail::ScenePoseExchangeStorage> storage,
    const std::uint64_t sceneGeneration) noexcept
    : storage_(std::move(storage)),
      sceneGeneration_(sceneGeneration) {}

ScenePoseHandle::ScenePoseHandle(ScenePoseHandle&& other) noexcept
    : storage_(std::move(other.storage_)),
      sceneGeneration_(std::exchange(other.sceneGeneration_, 0U)) {}

ScenePoseHandle& ScenePoseHandle::operator=(
    ScenePoseHandle&& other) noexcept {
    if (this != &other) {
        storage_ = std::move(other.storage_);
        sceneGeneration_ =
            std::exchange(other.sceneGeneration_, 0U);
    }
    return *this;
}

bool ScenePoseHandle::valid() const noexcept {
    return storage_ != nullptr &&
           sceneGeneration_ != 0U &&
           !storage_->closed.load(std::memory_order_acquire) &&
           sceneGeneration_ ==
               storage_->activeSceneGeneration.load(
                   std::memory_order_acquire);
}

DynamicInstancePoseLease::DynamicInstancePoseLease(
    std::shared_ptr<detail::ScenePoseExchangeStorage> storage,
    const std::uint8_t slotIndex) noexcept
    : storage_(std::move(storage)),
      slotIndex_(slotIndex) {}

DynamicInstancePoseLease::DynamicInstancePoseLease(
    DynamicInstancePoseLease&& other) noexcept
    : storage_(std::move(other.storage_)),
      slotIndex_(std::exchange(other.slotIndex_, 0U)) {}

DynamicInstancePoseLease& DynamicInstancePoseLease::operator=(
    DynamicInstancePoseLease&& other) noexcept {
    if (this != &other) {
        reset();
        storage_ = std::move(other.storage_);
        slotIndex_ = std::exchange(other.slotIndex_, 0U);
    }
    return *this;
}

DynamicInstancePoseLease::~DynamicInstancePoseLease() {
    reset();
}

bool DynamicInstancePoseLease::valid() const noexcept {
    return storage_ != nullptr;
}

std::uint64_t DynamicInstancePoseLease::sceneGeneration() const noexcept {
    return storage_ != nullptr
        ? storage_->slots[slotIndex_].sceneGeneration
        : 0U;
}

std::uint64_t
DynamicInstancePoseLease::publicationGeneration() const noexcept {
    return storage_ != nullptr
        ? storage_->slots[slotIndex_].publicationGeneration
        : 0U;
}

std::uint64_t DynamicInstancePoseLease::simulationStep() const noexcept {
    return storage_ != nullptr
        ? storage_->slots[slotIndex_].simulationStep
        : 0U;
}

std::span<const DynamicInstancePoseOverride>
DynamicInstancePoseLease::overrides() const noexcept {
    if (storage_ == nullptr) {
        return {};
    }
    const auto& slot = storage_->slots[slotIndex_];
    return {
        slot.overrides.data(),
        slot.overrideCount,
    };
}

ResolvedInstancePose DynamicInstancePoseLease::resolve(
    const std::uint32_t instanceIndex,
    const Mat3& authoredLinear,
    const Vec3& authoredTranslation) const noexcept {
    const auto values = overrides();
    const auto found = std::lower_bound(
        values.begin(),
        values.end(),
        instanceIndex,
        [](const DynamicInstancePoseOverride& candidate,
           const std::uint32_t expectedIndex) {
            return candidate.instanceIndex < expectedIndex;
        });
    if (found != values.end() &&
        found->instanceIndex == instanceIndex) {
        return {
            .modelLinear = found->modelLinear,
            .modelTranslation = found->modelTranslation,
        };
    }
    return {
        .modelLinear = authoredLinear,
        .modelTranslation = authoredTranslation,
    };
}

void DynamicInstancePoseLease::reset() noexcept {
    if (storage_ != nullptr) {
        storage_->slots[slotIndex_].accessState.fetch_sub(
            1U, std::memory_order_release);
        storage_.reset();
        slotIndex_ = 0U;
    }
}

ScenePoseExchange::ScenePoseExchange(
    const DynamicInstancePoseLimits limits)
    : storage_(
          std::make_shared<detail::ScenePoseExchangeStorage>(
              limits)) {}

ScenePoseExchange::~ScenePoseExchange() {
    storage_->closed.store(true, std::memory_order_release);
    storage_->activeSceneGeneration.store(
        0U, std::memory_order_release);
    storage_->publishedGeneration.store(
        0U, std::memory_order_release);
}

std::optional<ScenePoseHandle> ScenePoseExchange::tryBeginScene(
    const std::uint32_t instanceCount) noexcept {
    if (storage_->closed.load(std::memory_order_acquire) ||
        instanceCount > storage_->limits.maximumInstances ||
        storage_->nextSceneGeneration == 0U ||
        storage_->slots[0].accessState.load(
            std::memory_order_acquire) != 0U ||
        storage_->slots[1].accessState.load(
            std::memory_order_acquire) != 0U) {
        return std::nullopt;
    }

    storage_->publishedGeneration.store(
        0U, std::memory_order_release);
    const auto generation = storage_->nextSceneGeneration;
    ++storage_->nextSceneGeneration;
    storage_->instanceCount = instanceCount;
    storage_->hasPublishedStep = false;
    storage_->lastPublishedStep = 0U;
    storage_->publicationGeneration = 0U;
    storage_->activeSceneGeneration.store(
        generation, std::memory_order_release);
    return ScenePoseHandle(storage_, generation);
}

DynamicInstancePosePublishResult ScenePoseExchange::tryPublish(
    const ScenePoseHandle& scene,
    const DynamicInstancePoseFrameView frame) noexcept {
    const auto activeGeneration =
        storage_->activeSceneGeneration.load(
            std::memory_order_acquire);
    if (storage_->closed.load(std::memory_order_acquire) ||
        scene.storage_ != storage_ ||
        scene.sceneGeneration_ == 0U ||
        scene.sceneGeneration_ != activeGeneration) {
        return DynamicInstancePosePublishResult::wrongOrStaleScene;
    }
    if (storage_->hasPublishedStep &&
        frame.simulationStep <= storage_->lastPublishedStep) {
        return DynamicInstancePosePublishResult::
            simulationStepNotIncreasing;
    }
    if (frame.overrides.size() >
        storage_->limits.maximumOverrides) {
        return DynamicInstancePosePublishResult::
            overrideLimitExceeded;
    }
    if (frame.overrides.size() >
        storage_->limits.maximumFrameBytes /
            sizeof(DynamicInstancePoseOverride)) {
        return DynamicInstancePosePublishResult::
            frameByteLimitExceeded;
    }

    bool hasPreviousIndex = false;
    std::uint32_t previousIndex = 0U;
    for (const auto& pose : frame.overrides) {
        if (pose.instanceIndex >= storage_->instanceCount) {
            return DynamicInstancePosePublishResult::
                instanceIndexOutOfRange;
        }
        if (hasPreviousIndex &&
            pose.instanceIndex <= previousIndex) {
            return DynamicInstancePosePublishResult::
                overridesNotStrictlyIncreasing;
        }
        if (!finite(pose.modelLinear) ||
            !finite(pose.modelTranslation)) {
            return DynamicInstancePosePublishResult::
                nonFiniteTransform;
        }
        previousIndex = pose.instanceIndex;
        hasPreviousIndex = true;
    }
    if (storage_->publicationGeneration ==
        std::numeric_limits<std::uint64_t>::max()) {
        return DynamicInstancePosePublishResult::
            generationExhausted;
    }

    const auto nextPublicationGeneration =
        storage_->publicationGeneration + 1U;
    const auto target = static_cast<std::uint8_t>(
        (nextPublicationGeneration - 1U) % 2U);
    auto& slot = storage_->slots[target];
    std::uint32_t expectedAccessState = 0U;
    if (!slot.accessState.compare_exchange_strong(
            expectedAccessState,
            kWriterClaim,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return DynamicInstancePosePublishResult::busy;
    }

    std::copy(
        frame.overrides.begin(),
        frame.overrides.end(),
        slot.overrides.begin());
    slot.overrideCount = frame.overrides.size();
    slot.sceneGeneration = activeGeneration;
    slot.publicationGeneration = nextPublicationGeneration;
    slot.simulationStep = frame.simulationStep;

    storage_->hasPublishedStep = true;
    storage_->lastPublishedStep = frame.simulationStep;
    storage_->publicationGeneration = nextPublicationGeneration;
    storage_->publishedGeneration.store(
        nextPublicationGeneration, std::memory_order_release);
    slot.accessState.store(0U, std::memory_order_release);
    return DynamicInstancePosePublishResult::published;
}

std::optional<DynamicInstancePoseLease>
ScenePoseExchange::tryAcquire(
    const ScenePoseHandle& scene) noexcept {
    const auto activeGeneration =
        storage_->activeSceneGeneration.load(
            std::memory_order_acquire);
    if (storage_->closed.load(std::memory_order_acquire) ||
        scene.storage_ != storage_ ||
        scene.sceneGeneration_ == 0U ||
        scene.sceneGeneration_ != activeGeneration) {
        return std::nullopt;
    }

    const auto observed = storage_->publishedGeneration.load(
        std::memory_order_acquire);
    if (observed == 0U) {
        return std::nullopt;
    }

    const auto slotIndex = static_cast<std::uint8_t>(
        (observed - 1U) % 2U);
    auto& slot = storage_->slots[slotIndex];
    auto accessState =
        slot.accessState.load(std::memory_order_acquire);
    if ((accessState & kWriterClaim) != 0U ||
        accessState == kMaximumReaderCount ||
        !slot.accessState.compare_exchange_strong(
            accessState,
            accessState + 1U,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return std::nullopt;
    }
    const auto confirmed = storage_->publishedGeneration.load(
        std::memory_order_acquire);
    if (confirmed == observed &&
        slot.sceneGeneration == scene.sceneGeneration_) {
        return DynamicInstancePoseLease(storage_, slotIndex);
    }
    slot.accessState.fetch_sub(1U, std::memory_order_release);
    return std::nullopt;
}

DynamicInstancePoseLimits ScenePoseExchange::limits() const noexcept {
    return storage_->limits;
}

} // namespace airfix::render
