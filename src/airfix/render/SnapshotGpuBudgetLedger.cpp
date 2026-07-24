#include "airfix/render/SnapshotGpuBudgetLedger.hpp"

#include <atomic>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace airfix::render::detail {

namespace {

constexpr std::uint64_t kClosedBit = std::uint64_t{1U} << 63U;
constexpr std::uint64_t kReservedMask = kClosedBit - 1U;

} // namespace

struct SnapshotGpuBudgetLedgerState {
    explicit SnapshotGpuBudgetLedgerState(
        const std::size_t configuredMaximumBytes)
        : maximumBytes(configuredMaximumBytes) {
        if (configuredMaximumBytes > kReservedMask) {
            throw std::invalid_argument(
                "snapshot GPU budget exceeds the CAS representation");
        }
    }

    [[nodiscard]] bool tryReserve(const std::size_t bytes) noexcept {
        std::uint64_t observed =
            encoded.load(std::memory_order_acquire);
        for (;;) {
            if ((observed & kClosedBit) != 0U) {
                return false;
            }
            const auto reserved = observed & kReservedMask;
            if (reserved > maximumBytes ||
                bytes > maximumBytes - reserved) {
                return false;
            }
            const auto desired =
                reserved + static_cast<std::uint64_t>(bytes);
            if (encoded.compare_exchange_weak(
                    observed,
                    desired,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                return true;
            }
        }
    }

    [[nodiscard]] bool release(const std::size_t bytes) noexcept {
        std::uint64_t observed =
            encoded.load(std::memory_order_acquire);
        for (;;) {
            const auto reserved = observed & kReservedMask;
            if (bytes > reserved) {
                close();
                return false;
            }
            const auto desired =
                (observed & kClosedBit) |
                (reserved - static_cast<std::uint64_t>(bytes));
            if (encoded.compare_exchange_weak(
                    observed,
                    desired,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                return true;
            }
        }
    }

    void close() noexcept {
        encoded.fetch_or(kClosedBit, std::memory_order_acq_rel);
    }

    [[nodiscard]] std::size_t reservedBytes() const noexcept {
        return static_cast<std::size_t>(
            encoded.load(std::memory_order_acquire) &
            kReservedMask);
    }

    [[nodiscard]] bool closed() const noexcept {
        return (encoded.load(std::memory_order_acquire) &
                kClosedBit) != 0U;
    }

    const std::uint64_t maximumBytes;
    std::atomic<std::uint64_t> encoded{0U};
};

} // namespace airfix::render::detail

namespace airfix::render {

SnapshotGpuBudgetReservation::SnapshotGpuBudgetReservation(
    std::shared_ptr<detail::SnapshotGpuBudgetLedgerState> state,
    const std::size_t bytes) noexcept
    : state_(std::move(state)),
      bytes_(bytes),
      active_(true) {}

SnapshotGpuBudgetReservation::SnapshotGpuBudgetReservation(
    SnapshotGpuBudgetReservation&& other) noexcept
    : state_(std::move(other.state_)),
      bytes_(std::exchange(other.bytes_, 0U)),
      active_(std::exchange(other.active_, false)) {}

SnapshotGpuBudgetReservation&
SnapshotGpuBudgetReservation::operator=(
    SnapshotGpuBudgetReservation&& other) noexcept {
    if (this != &other) {
        reset();
        state_ = std::move(other.state_);
        bytes_ = std::exchange(other.bytes_, 0U);
        active_ = std::exchange(other.active_, false);
    }
    return *this;
}

SnapshotGpuBudgetReservation::~SnapshotGpuBudgetReservation() {
    reset();
}

bool SnapshotGpuBudgetReservation::reconcile(
    const std::size_t exactBytes) noexcept {
    if (!active_ || state_ == nullptr) {
        return false;
    }
    if (exactBytes > bytes_) {
        state_->close();
        return false;
    }
    const auto excess = bytes_ - exactBytes;
    if (!state_->release(excess)) {
        return false;
    }
    bytes_ = exactBytes;
    return true;
}

bool SnapshotGpuBudgetReservation::absorb(
    SnapshotGpuBudgetReservation&& other) noexcept {
    if (this == &other ||
        !active_ ||
        !other.active_ ||
        state_ == nullptr ||
        state_ != other.state_ ||
        other.bytes_ >
            std::numeric_limits<std::size_t>::max() - bytes_) {
        return false;
    }
    bytes_ += other.bytes_;
    other.active_ = false;
    other.bytes_ = 0U;
    other.state_.reset();
    return true;
}

std::optional<SnapshotGpuBudgetReservation>
SnapshotGpuBudgetReservation::split(
    const std::size_t bytes) noexcept {
    if (!active_ || state_ == nullptr || bytes > bytes_) {
        return std::nullopt;
    }
    bytes_ -= bytes;
    return SnapshotGpuBudgetReservation(state_, bytes);
}

void SnapshotGpuBudgetReservation::reset() noexcept {
    if (!active_) {
        return;
    }
    active_ = false;
    if (state_ != nullptr) {
        (void)state_->release(bytes_);
    }
    bytes_ = 0U;
    state_.reset();
}

bool SnapshotGpuBudgetReservation::active() const noexcept {
    return active_;
}

std::size_t SnapshotGpuBudgetReservation::bytes() const noexcept {
    return bytes_;
}

SnapshotGpuBudgetLedger::SnapshotGpuBudgetLedger(
    const std::size_t maximumBytes)
    : state_(
          std::make_shared<detail::SnapshotGpuBudgetLedgerState>(
              maximumBytes)) {}

std::optional<SnapshotGpuBudgetReservation>
SnapshotGpuBudgetLedger::tryReserve(
    const std::size_t bytes) noexcept {
    if (!state_->tryReserve(bytes)) {
        return std::nullopt;
    }
    return SnapshotGpuBudgetReservation(state_, bytes);
}

std::size_t SnapshotGpuBudgetLedger::reservedBytes() const noexcept {
    return state_->reservedBytes();
}

std::size_t SnapshotGpuBudgetLedger::maximumBytes() const noexcept {
    return static_cast<std::size_t>(state_->maximumBytes);
}

bool SnapshotGpuBudgetLedger::closed() const noexcept {
    return state_->closed();
}

} // namespace airfix::render
