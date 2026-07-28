#include "airfix/render/LegacyGameplayCameraPacketExchange.hpp"

#include <array>
#include <atomic>
#include <limits>
#include <type_traits>
#include <utility>

namespace airfix::render::detail {

struct LegacyGameplayCameraPacketExchangeSlot final {
    std::optional<LegacyGameplayCameraClipPacket> packet;
    // The high bit is an exclusive writer claim. Remaining values count
    // active reader leases.
    std::atomic<std::uint32_t> accessState{0U};
    std::uint64_t exchangePublicationGeneration{};
};

struct LegacyGameplayCameraPacketExchangeStorage final {
    explicit LegacyGameplayCameraPacketExchangeStorage(
        const LegacyGameplayCameraClipPacket& initialPacket) {
        slots[0U].packet.emplace(initialPacket);
        slots[0U].exchangePublicationGeneration = 1U;
        const auto& initialFrame = initialPacket.pose().frame();
        lastPublishedSimulationStep = initialFrame.simulationStep;
        lastPublishedCameraGeneration = initialFrame.publicationGeneration;
        exchangePublicationGeneration = 1U;
        publishedExchangeGeneration.store(1U, std::memory_order_release);
    }

    std::array<LegacyGameplayCameraPacketExchangeSlot, 2U> slots;
    // Zero means closed/no packet. A nonzero value is both the exchange
    // publication generation and the front-slot tag:
    // slot = (generation - 1) % 2.
    std::atomic<std::uint64_t> publishedExchangeGeneration{0U};
    std::atomic<bool> closed{false};

    // Producer-only after construction.
    std::uint64_t lastPublishedSimulationStep{};
    std::uint64_t lastPublishedCameraGeneration{};
    std::uint64_t exchangePublicationGeneration{};
};

} // namespace airfix::render::detail

namespace airfix::render {
namespace {

constexpr std::uint32_t kWriterClaim = std::uint32_t{1U} << 31U;
constexpr std::uint32_t kMaximumReaderCount = kWriterClaim - 1U;

static_assert(
    std::is_nothrow_copy_constructible_v<LegacyGameplayCameraClipPacket>);

} // namespace

LegacyGameplayCameraPacketLease::LegacyGameplayCameraPacketLease(
    std::shared_ptr<detail::LegacyGameplayCameraPacketExchangeStorage> storage,
    const std::uint8_t slotIndex) noexcept
    : storage_(std::move(storage)), slotIndex_(slotIndex) {}

LegacyGameplayCameraPacketLease::LegacyGameplayCameraPacketLease(
    LegacyGameplayCameraPacketLease&& other) noexcept
    : storage_(std::move(other.storage_)),
      slotIndex_(std::exchange(other.slotIndex_, 0U)) {}

LegacyGameplayCameraPacketLease& LegacyGameplayCameraPacketLease::operator=(
    LegacyGameplayCameraPacketLease&& other) noexcept {
    if (this != &other) {
        reset();
        storage_ = std::move(other.storage_);
        slotIndex_ = std::exchange(other.slotIndex_, 0U);
    }
    return *this;
}

LegacyGameplayCameraPacketLease::~LegacyGameplayCameraPacketLease() {
    reset();
}

bool LegacyGameplayCameraPacketLease::valid() const noexcept {
    return packet() != nullptr;
}

std::uint64_t LegacyGameplayCameraPacketLease::exchangePublicationGeneration()
    const noexcept {
    return storage_ != nullptr
               ? storage_->slots[slotIndex_].exchangePublicationGeneration
               : 0U;
}

std::uint64_t LegacyGameplayCameraPacketLease::simulationStep() const noexcept {
    const auto* value = packet();
    return value != nullptr ? value->pose().frame().simulationStep : 0U;
}

std::uint64_t
LegacyGameplayCameraPacketLease::cameraPublicationGeneration() const noexcept {
    const auto* value = packet();
    return value != nullptr ? value->pose().frame().publicationGeneration : 0U;
}

const LegacyGameplayCameraClipPacket*
LegacyGameplayCameraPacketLease::packet() const noexcept {
    if (storage_ == nullptr) {
        return nullptr;
    }
    const auto& packet = storage_->slots[slotIndex_].packet;
    return packet.has_value() ? &*packet : nullptr;
}

void LegacyGameplayCameraPacketLease::reset() noexcept {
    if (storage_ != nullptr) {
        storage_->slots[slotIndex_].accessState.fetch_sub(
            1U, std::memory_order_release);
        storage_.reset();
        slotIndex_ = 0U;
    }
}

LegacyGameplayCameraPacketExchange::LegacyGameplayCameraPacketExchange(
    const LegacyGameplayCameraClipPacket& initialPacket)
    : storage_(
          std::make_shared<detail::LegacyGameplayCameraPacketExchangeStorage>(
              initialPacket)) {}

LegacyGameplayCameraPacketExchange::~LegacyGameplayCameraPacketExchange() {
    storage_->closed.store(true, std::memory_order_release);
    storage_->publishedExchangeGeneration.store(0U, std::memory_order_release);
}

LegacyGameplayCameraPacketPublishResult
LegacyGameplayCameraPacketExchange::tryPublish(
    const LegacyGameplayCameraClipPacket& packet) noexcept {
    const auto& frame = packet.pose().frame();
    if (frame.simulationStep <= storage_->lastPublishedSimulationStep) {
        return LegacyGameplayCameraPacketPublishResult::
            simulationStepNotIncreasing;
    }
    if (frame.publicationGeneration <=
        storage_->lastPublishedCameraGeneration) {
        return LegacyGameplayCameraPacketPublishResult::
            cameraGenerationNotIncreasing;
    }
    if (storage_->exchangePublicationGeneration ==
        std::numeric_limits<std::uint64_t>::max()) {
        return LegacyGameplayCameraPacketPublishResult::
            exchangeGenerationExhausted;
    }

    const auto nextExchangeGeneration =
        storage_->exchangePublicationGeneration + 1U;
    const auto target = static_cast<std::uint8_t>(
        (nextExchangeGeneration - 1U) % storage_->slots.size());
    auto& slot = storage_->slots[target];
    std::uint32_t expectedAccessState = 0U;
    if (!slot.accessState.compare_exchange_strong(expectedAccessState,
                                                  kWriterClaim,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire)) {
        return LegacyGameplayCameraPacketPublishResult::busy;
    }

    slot.packet.emplace(packet);
    slot.exchangePublicationGeneration = nextExchangeGeneration;

    storage_->lastPublishedSimulationStep = frame.simulationStep;
    storage_->lastPublishedCameraGeneration = frame.publicationGeneration;
    storage_->exchangePublicationGeneration = nextExchangeGeneration;
    storage_->publishedExchangeGeneration.store(nextExchangeGeneration,
                                                std::memory_order_release);
    slot.accessState.store(0U, std::memory_order_release);
    return LegacyGameplayCameraPacketPublishResult::published;
}

std::optional<LegacyGameplayCameraPacketLease>
LegacyGameplayCameraPacketExchange::tryAcquire() noexcept {
    if (storage_->closed.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    const auto observed =
        storage_->publishedExchangeGeneration.load(std::memory_order_acquire);
    if (observed == 0U) {
        return std::nullopt;
    }

    const auto slotIndex =
        static_cast<std::uint8_t>((observed - 1U) % storage_->slots.size());
    auto& slot = storage_->slots[slotIndex];
    auto accessState = slot.accessState.load(std::memory_order_acquire);
    if ((accessState & kWriterClaim) != 0U ||
        accessState == kMaximumReaderCount ||
        !slot.accessState.compare_exchange_strong(accessState,
                                                  accessState + 1U,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire)) {
        return std::nullopt;
    }

    const auto confirmed =
        storage_->publishedExchangeGeneration.load(std::memory_order_acquire);
    if (confirmed == observed &&
        slot.exchangePublicationGeneration == observed &&
        slot.packet.has_value()) {
        return LegacyGameplayCameraPacketLease(storage_, slotIndex);
    }
    slot.accessState.fetch_sub(1U, std::memory_order_release);
    return std::nullopt;
}

std::size_t LegacyGameplayCameraPacketExchange::retainedBytes() noexcept {
    return sizeof(detail::LegacyGameplayCameraPacketExchangeStorage);
}

} // namespace airfix::render
