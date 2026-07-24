#pragma once

#include <cstddef>
#include <memory>
#include <optional>

namespace airfix::render {

inline constexpr std::size_t
    kMaximumAggregateSnapshotGpuHeapAdmissionBytes =
    384U * 1024U * 1024U;

namespace detail {
struct SnapshotGpuBudgetLedgerState;
}

// A single-consumption debit in one ledger. Moving a reservation transfers
// ownership; reset and destruction are idempotent on a moved-from or already
// consumed token.
class SnapshotGpuBudgetReservation final {
public:
    SnapshotGpuBudgetReservation() noexcept = default;
    SnapshotGpuBudgetReservation(
        SnapshotGpuBudgetReservation&& other) noexcept;
    SnapshotGpuBudgetReservation& operator=(
        SnapshotGpuBudgetReservation&& other) noexcept;
    ~SnapshotGpuBudgetReservation();

    SnapshotGpuBudgetReservation(
        const SnapshotGpuBudgetReservation&) = delete;
    SnapshotGpuBudgetReservation& operator=(
        const SnapshotGpuBudgetReservation&) = delete;

    // Reduces a conservative prospective debit to the measured current heap
    // allocation. Growth through this operation is an accounting error and
    // permanently closes the ledger; post-creation page-rounding growth must
    // first obtain a separate reservation and absorb it.
    [[nodiscard]] bool reconcile(std::size_t exactBytes) noexcept;

    // Consolidates another debit from the same ledger without changing the
    // aggregate. On success, other becomes inactive.
    [[nodiscard]] bool absorb(
        SnapshotGpuBudgetReservation&& other) noexcept;

    // Transfers part of this already-reserved debit without changing the
    // ledger aggregate. Used when one prospective bootstrap reservation is
    // finalized into snapshot and renderer-persistent resource owners.
    [[nodiscard]] std::optional<SnapshotGpuBudgetReservation> split(
        std::size_t bytes) noexcept;

    void reset() noexcept;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::size_t bytes() const noexcept;

private:
    friend class SnapshotGpuBudgetLedger;

    SnapshotGpuBudgetReservation(
        std::shared_ptr<detail::SnapshotGpuBudgetLedgerState> state,
        std::size_t bytes) noexcept;

    std::shared_ptr<detail::SnapshotGpuBudgetLedgerState> state_;
    std::size_t bytes_{0U};
    bool active_{false};
};

// The high bit and reserved-byte count share one atomic CAS word, making close
// and capacity decisions one linearizable synchronization domain.
class SnapshotGpuBudgetLedger final {
public:
    explicit SnapshotGpuBudgetLedger(
        std::size_t maximumBytes =
            kMaximumAggregateSnapshotGpuHeapAdmissionBytes);

    SnapshotGpuBudgetLedger(const SnapshotGpuBudgetLedger&) = delete;
    SnapshotGpuBudgetLedger& operator=(
        const SnapshotGpuBudgetLedger&) = delete;

    [[nodiscard]] std::optional<SnapshotGpuBudgetReservation> tryReserve(
        std::size_t bytes) noexcept;

    [[nodiscard]] std::size_t reservedBytes() const noexcept;
    [[nodiscard]] std::size_t maximumBytes() const noexcept;
    [[nodiscard]] bool closed() const noexcept;

private:
    std::shared_ptr<detail::SnapshotGpuBudgetLedgerState> state_;
};

} // namespace airfix::render
