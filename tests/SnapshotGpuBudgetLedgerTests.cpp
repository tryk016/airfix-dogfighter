#include "airfix/render/SnapshotGpuBudgetLedger.hpp"

#include <atomic>
#include <cstddef>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using airfix::render::SnapshotGpuBudgetLedger;
using airfix::render::SnapshotGpuBudgetReservation;
using airfix::render::
    kMaximumAggregateSnapshotGpuHeapAdmissionBytes;

static_assert(!std::is_copy_constructible_v<
              SnapshotGpuBudgetReservation>);
static_assert(!std::is_copy_assignable_v<
              SnapshotGpuBudgetReservation>);
static_assert(std::is_nothrow_move_constructible_v<
              SnapshotGpuBudgetReservation>);
static_assert(std::is_nothrow_move_assignable_v<
              SnapshotGpuBudgetReservation>);

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

SnapshotGpuBudgetReservation take(
    std::optional<SnapshotGpuBudgetReservation>& reservation,
    const std::string& message) {
    require(reservation.has_value(), message);
    return std::move(*reservation);
}

void testDefaultAndExactBoundaries() {
    SnapshotGpuBudgetLedger production;
    require(
        production.maximumBytes() ==
            384U * 1024U * 1024U,
        "production aggregate heap admission ceiling changed");
    require(
        production.maximumBytes() ==
            kMaximumAggregateSnapshotGpuHeapAdmissionBytes,
        "published heap admission constant disagrees with the ledger");

    auto exact = production.tryReserve(
        kMaximumAggregateSnapshotGpuHeapAdmissionBytes);
    require(exact.has_value(), "exact 384 MiB reservation failed");
    require(
        !production.tryReserve(1U).has_value(),
        "aggregate reservation exceeded 384 MiB");
    exact.reset();
    require(
        production.reservedBytes() == 0U,
        "exact 384 MiB token destruction left a debit");

    SnapshotGpuBudgetLedger ledger(10U);
    auto first = ledger.tryReserve(6U);
    auto second = ledger.tryReserve(4U);
    require(
        first.has_value() && second.has_value(),
        "split exact-boundary reservations failed");
    require(
        ledger.reservedBytes() == 10U,
        "exact-boundary reservations were not aggregated");
    require(
        !ledger.tryReserve(1U).has_value(),
        "one-past-boundary reservation succeeded");
    first.reset();
    require(
        ledger.reservedBytes() == 4U,
        "token destruction did not release its exact debit");
    second.reset();
    require(ledger.reservedBytes() == 0U, "final token leaked");
}

void testMoveOnlySingleConsumptionAndZero() {
    SnapshotGpuBudgetLedger ledger(16U);
    auto sourceOptional = ledger.tryReserve(7U);
    auto source = take(sourceOptional, "source reservation failed");
    auto owner = std::move(source);

    require(!source.active(), "move left the source token active");
    require(owner.active(), "move did not transfer token ownership");
    require(owner.bytes() == 7U, "move changed the token debit");
    source.reset();
    require(
        ledger.reservedBytes() == 7U,
        "moved-from reset consumed the live owner's debit");
    owner.reset();
    require(
        ledger.reservedBytes() == 0U,
        "first owner reset did not consume the debit");
    owner.reset();
    require(
        ledger.reservedBytes() == 0U,
        "second owner reset released the debit twice");

    auto destinationOptional = ledger.tryReserve(3U);
    auto incomingOptional = ledger.tryReserve(5U);
    auto destination = take(
        destinationOptional, "move-assignment destination failed");
    auto incoming = take(
        incomingOptional, "move-assignment source failed");
    destination = std::move(incoming);
    require(
        ledger.reservedBytes() == 5U &&
            destination.active() &&
            !incoming.active(),
        "move assignment did not consume the old owner exactly once");
    incoming.reset();
    destination.reset();
    destination.reset();
    require(
        ledger.reservedBytes() == 0U,
        "move-assigned token was not single-consumption");

    auto zeroOptional = ledger.tryReserve(0U);
    auto zero = take(zeroOptional, "zero-byte reservation failed");
    require(zero.active(), "zero-byte token was not active");
    require(zero.bytes() == 0U, "zero-byte token carried a debit");
    require(
        zero.reconcile(0U),
        "zero-byte reconciliation failed");
    zero.reset();
    zero.reset();
    require(
        ledger.reservedBytes() == 0U && !ledger.closed(),
        "zero-byte single consumption changed or closed the ledger");
}

void testMeasuredCurrentHeapAllocationIsChargedOnce() {
    SnapshotGpuBudgetLedger ledger(384U);
    auto published = ledger.tryReserve(96U);
    auto prospective = ledger.tryReserve(256U);
    require(
        published.has_value() && prospective.has_value(),
        "prospective private reservation was not admitted");
    require(
        ledger.reservedBytes() == 352U,
        "prospective capacity was not charged before allocation");

    require(
        prospective->reconcile(128U),
        "exact current-heap-allocation reconciliation failed");
    require(
        prospective->bytes() == 128U &&
            ledger.reservedBytes() == 224U,
        "heap reconciliation did not return only unretained capacity");

    auto nextProspective = ledger.tryReserve(160U);
    require(
        nextProspective.has_value(),
        "suballocated resources appear to have been charged again");
    require(
        ledger.reservedBytes() == 384U,
        "post-reconcile exact boundary was not reached");
    require(
        !ledger.tryReserve(1U).has_value(),
        "post-reconcile aggregate exceeded its exact boundary");
}

void testPostCreationRoundingObtainsSupplementalAdmission() {
    SnapshotGpuBudgetLedger ledger(32U);
    auto plannedOptional = ledger.tryReserve(24U);
    auto planned = take(
        plannedOptional, "heap-plan reservation failed");

    // Metal may page-round a heap beyond its descriptor size. The measured
    // current allocation must obtain the difference before publication.
    constexpr std::size_t measuredCurrentAllocation = 28U;
    auto supplementOptional = ledger.tryReserve(
        measuredCurrentAllocation - planned.bytes());
    auto supplement = take(
        supplementOptional, "page-rounding supplement failed");
    require(
        planned.absorb(std::move(supplement)),
        "page-rounding supplement could not be consolidated");
    require(
        planned.bytes() == measuredCurrentAllocation &&
            !supplement.active() &&
            ledger.reservedBytes() == measuredCurrentAllocation,
        "supplemental admission did not preserve the exact aggregate");

    auto deniedPlan = ledger.tryReserve(4U);
    require(
        deniedPlan.has_value() &&
            !ledger.tryReserve(1U).has_value(),
        "post-measurement aggregate admission boundary changed");
}

void testPostCreationRoundingCanRejectCandidate() {
    SnapshotGpuBudgetLedger ledger(10U);
    auto plannedOptional = ledger.tryReserve(9U);
    auto planned = take(
        plannedOptional, "rounding-rejection setup failed");

    require(
        !ledger.tryReserve(2U).has_value(),
        "unavailable page-rounding supplement was admitted");
    require(
        planned.active() &&
            planned.bytes() == 9U &&
            ledger.reservedBytes() == 9U,
        "failed supplement corrupted the plan reservation");

    // Renderer teardown destroys the unpublishable heap before this token.
    planned.reset();
    require(
        ledger.reservedBytes() == 0U && !ledger.closed(),
        "rejected rounded candidate leaked or closed the ledger");
}

void testAbsorbRejectsUnrelatedOwnership() {
    SnapshotGpuBudgetLedger firstLedger(8U);
    SnapshotGpuBudgetLedger secondLedger(8U);
    auto firstOptional = firstLedger.tryReserve(3U);
    auto secondOptional = secondLedger.tryReserve(4U);
    auto first = take(
        firstOptional, "first absorb-ownership setup failed");
    auto second = take(
        secondOptional, "second absorb-ownership setup failed");

    require(
        !first.absorb(std::move(second)) &&
            first.active() &&
            second.active() &&
            firstLedger.reservedBytes() == 3U &&
            secondLedger.reservedBytes() == 4U,
        "absorb crossed ledger ownership domains");
    require(
        !first.absorb(std::move(first)) &&
            first.active() &&
            firstLedger.reservedBytes() == 3U,
        "self-absorb corrupted token ownership");
}

void testBootstrapSplitPreservesAggregate() {
    SnapshotGpuBudgetLedger ledger(64U);
    auto bootstrapOptional = ledger.tryReserve(64U);
    auto bootstrap = take(
        bootstrapOptional, "bootstrap prospective reserve failed");
    require(
        bootstrap.reconcile(20U),
        "bootstrap exact reconciliation failed");

    auto fallbackOptional = bootstrap.split(4U);
    auto fallback = take(
        fallbackOptional, "fallback debit split failed");
    require(
        bootstrap.bytes() == 16U &&
            fallback.bytes() == 4U &&
            ledger.reservedBytes() == 20U,
        "splitting resource ownership changed the aggregate debit");

    bootstrap.reset();
    require(
        ledger.reservedBytes() == 4U,
        "snapshot teardown released the persistent fallback debit");
    fallback.reset();
    require(
        ledger.reservedBytes() == 0U,
        "persistent fallback teardown leaked its debit");
}

void testUnderestimatedProspectiveReserveClosesLedger() {
    SnapshotGpuBudgetLedger ledger(32U);
    auto reservationOptional = ledger.tryReserve(8U);
    auto reservation = take(
        reservationOptional, "underestimate setup failed");

    require(
        !reservation.reconcile(9U),
        "reconciliation grew a prospective debit");
    require(
        ledger.closed(),
        "underestimated prospective debit did not close the ledger");
    require(
        ledger.reservedBytes() == 8U,
        "failed reconciliation changed the existing debit");
    require(
        !ledger.tryReserve(0U).has_value(),
        "closed ledger admitted a zero-byte reservation");

    reservation.reset();
    require(
        ledger.reservedBytes() == 0U && ledger.closed(),
        "closed-ledger token reset reopened accounting");
}

void testReservationOutlivesLedgerFacade() {
    std::optional<SnapshotGpuBudgetReservation> survivor;
    {
        SnapshotGpuBudgetLedger ledger(8U);
        survivor = ledger.tryReserve(8U);
        require(
            survivor.has_value(),
            "lifetime reservation setup failed");
    }
    require(
        survivor->active() && survivor->bytes() == 8U,
        "ledger facade destruction invalidated a live token");
    survivor.reset();
}

void testConcurrentExactBoundary() {
    constexpr std::size_t threadCount = 16U;
    constexpr std::size_t debitBytes = 4U;
    constexpr std::size_t maximumBytes = 32U;
    SnapshotGpuBudgetLedger ledger(maximumBytes);
    std::atomic<bool> start{false};
    std::atomic<std::size_t> attempted{0U};
    std::atomic<std::size_t> admitted{0U};
    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for (std::size_t index = 0U; index < threadCount; ++index) {
        threads.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            auto reservation = ledger.tryReserve(debitBytes);
            if (reservation.has_value()) {
                admitted.fetch_add(1U, std::memory_order_relaxed);
            }
            attempted.fetch_add(1U, std::memory_order_release);
            while (attempted.load(std::memory_order_acquire) <
                   threadCount) {
                std::this_thread::yield();
            }
            reservation.reset();
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& thread : threads) {
        thread.join();
    }

    require(
        admitted.load(std::memory_order_relaxed) ==
            maximumBytes / debitBytes,
        "CAS admission did not stop at the exact concurrent boundary");
    require(
        ledger.reservedBytes() == 0U && !ledger.closed(),
        "concurrent token destruction leaked or closed the ledger");
}

void testConcurrentCloseAndReserveLinearize() {
    constexpr std::size_t iterations = 128U;
    for (std::size_t iteration = 0U;
         iteration < iterations;
         ++iteration) {
        SnapshotGpuBudgetLedger ledger(2U);
        auto poisonOptional = ledger.tryReserve(1U);
        auto poison = take(
            poisonOptional, "close-race setup failed");
        std::optional<SnapshotGpuBudgetReservation> contender;
        std::atomic<bool> start{false};

        std::thread closer([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            (void)poison.reconcile(2U);
        });
        std::thread reserver([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            contender = ledger.tryReserve(1U);
        });
        start.store(true, std::memory_order_release);
        closer.join();
        reserver.join();

        const std::size_t expected =
            1U + (contender.has_value() ? 1U : 0U);
        require(
            ledger.closed() &&
                ledger.reservedBytes() == expected,
            "reserve and close did not produce one CAS ordering");
        require(
            !ledger.tryReserve(0U).has_value(),
            "post-close reservation escaped the CAS domain");
        contender.reset();
        poison.reset();
        require(
            ledger.closed() && ledger.reservedBytes() == 0U,
            "close-race cleanup reopened or leaked the ledger");
    }
}

void testConcurrentProspectiveReconcileStress() {
    constexpr std::size_t threadCount = 12U;
    constexpr std::size_t iterations = 10'000U;
    SnapshotGpuBudgetLedger ledger(128U);
    std::atomic<bool> invariantFailed{false};
    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for (std::size_t index = 0U; index < threadCount; ++index) {
        threads.emplace_back([&, index] {
            const std::size_t prospective =
                8U + (index % 5U);
            const std::size_t exact = prospective / 2U;
            for (std::size_t iteration = 0U;
                 iteration < iterations;
                 ++iteration) {
                auto reservation =
                    ledger.tryReserve(prospective);
                if (!reservation.has_value()) {
                    std::this_thread::yield();
                    continue;
                }
                if (!reservation->reconcile(exact) ||
                    ledger.reservedBytes() >
                        ledger.maximumBytes()) {
                    invariantFailed.store(
                        true, std::memory_order_relaxed);
                }
                reservation.reset();
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    require(
        !invariantFailed.load(std::memory_order_relaxed),
        "concurrent reserve/reconcile violated a ledger invariant");
    require(
        ledger.reservedBytes() == 0U && !ledger.closed(),
        "concurrent reserve/reconcile left a debit or closed ledger");
}

} // namespace

int main() {
    try {
        testDefaultAndExactBoundaries();
        testMoveOnlySingleConsumptionAndZero();
        testMeasuredCurrentHeapAllocationIsChargedOnce();
        testPostCreationRoundingObtainsSupplementalAdmission();
        testPostCreationRoundingCanRejectCandidate();
        testAbsorbRejectsUnrelatedOwnership();
        testBootstrapSplitPreservesAggregate();
        testUnderestimatedProspectiveReserveClosesLedger();
        testReservationOutlivesLedgerFacade();
        testConcurrentExactBoundary();
        testConcurrentCloseAndReserveLinearize();
        testConcurrentProspectiveReconcileStress();
        std::cout << "SnapshotGpuBudgetLedger tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "SnapshotGpuBudgetLedger tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
