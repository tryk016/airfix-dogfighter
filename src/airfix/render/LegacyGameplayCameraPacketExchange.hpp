#pragma once

#include "airfix/render/LegacyGameplayCameraClipPacket.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace airfix::render {

enum class LegacyGameplayCameraPacketPublishResult : std::uint8_t {
    published,
    simulationStepNotIncreasing,
    cameraGenerationNotIncreasing,
    exchangeGenerationExhausted,
    busy,
};

namespace detail {
struct LegacyGameplayCameraPacketExchangeStorage;
}

class LegacyGameplayCameraPacketExchange;

// A stable read-only claim on one complete packet slot. The lease is
// move-only, releases its reader claim on destruction, and keeps the private
// storage alive if it outlives the exchange facade. packet() is valid only
// while this exact lease remains active and has not been moved.
class LegacyGameplayCameraPacketLease final {
  public:
    LegacyGameplayCameraPacketLease(
        LegacyGameplayCameraPacketLease&& other) noexcept;
    LegacyGameplayCameraPacketLease&
    operator=(LegacyGameplayCameraPacketLease&& other) noexcept;
    ~LegacyGameplayCameraPacketLease();

    LegacyGameplayCameraPacketLease(const LegacyGameplayCameraPacketLease&) =
        delete;
    LegacyGameplayCameraPacketLease&
    operator=(const LegacyGameplayCameraPacketLease&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint64_t exchangePublicationGeneration() const noexcept;
    [[nodiscard]] std::uint64_t simulationStep() const noexcept;
    [[nodiscard]] std::uint64_t cameraPublicationGeneration() const noexcept;
    [[nodiscard]] const LegacyGameplayCameraClipPacket* packet() const noexcept;

    void reset() noexcept;

  private:
    friend class LegacyGameplayCameraPacketExchange;

    LegacyGameplayCameraPacketLease(
        std::shared_ptr<detail::LegacyGameplayCameraPacketExchangeStorage>
            storage,
        std::uint8_t slotIndex) noexcept;

    std::shared_ptr<detail::LegacyGameplayCameraPacketExchangeStorage> storage_;
    std::uint8_t slotIndex_{};
};

// A fixed two-slot single-producer/single-consumer exchange for complete,
// owning gameplay-camera clip packets. Construction publishes the initial
// packet and performs the only allocation. Publish/acquire are bounded,
// allocation-free, noexcept, and never wait or overwrite a leased slot.
//
// Camera generations and simulation steps must increase, but need not be
// contiguous: a producer may drop a packet after busy and later publish a
// newer complete state. The exchange has its own contiguous generation solely
// for ABA-resistant slot selection.
//
// Destruction requires producer/consumer quiescence. An already acquired lease
// remains readable and releasable afterwards through the private shared
// storage.
class LegacyGameplayCameraPacketExchange final {
  public:
    explicit LegacyGameplayCameraPacketExchange(
        const LegacyGameplayCameraClipPacket& initialPacket);
    ~LegacyGameplayCameraPacketExchange();

    LegacyGameplayCameraPacketExchange(
        const LegacyGameplayCameraPacketExchange&) = delete;
    LegacyGameplayCameraPacketExchange&
    operator=(const LegacyGameplayCameraPacketExchange&) = delete;
    LegacyGameplayCameraPacketExchange(LegacyGameplayCameraPacketExchange&&) =
        delete;
    LegacyGameplayCameraPacketExchange&
    operator=(LegacyGameplayCameraPacketExchange&&) = delete;

    [[nodiscard]] LegacyGameplayCameraPacketPublishResult
    tryPublish(const LegacyGameplayCameraClipPacket& packet) noexcept;

    // Empty means the reader lost a bounded publication race. The next render
    // iteration may retry without reconstructing any camera state.
    [[nodiscard]] std::optional<LegacyGameplayCameraPacketLease>
    tryAcquire() noexcept;

    // Logical retained storage, excluding allocator/control-block overhead.
    // This is stable for platform admission checks within one build.
    [[nodiscard]] static std::size_t retainedBytes() noexcept;

  private:
    std::shared_ptr<detail::LegacyGameplayCameraPacketExchangeStorage> storage_;
};

} // namespace airfix::render
