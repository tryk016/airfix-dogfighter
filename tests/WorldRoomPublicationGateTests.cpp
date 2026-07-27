#include "airfix/content/WorldRoomPublicationGate.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {

using airfix::content::ContentRevision;
using airfix::content::WorldRoomPublicationGate;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] ContentRevision revision(
    const std::uint64_t generation,
    const std::uint64_t size,
    const std::uint8_t digestSeed) {
    ContentRevision result{
        .generation = generation,
        .pack = {
            .size = size,
            .sha256 = {},
        },
    };
    for (std::size_t index = 0U;
         index < result.pack.sha256.size();
         ++index) {
        result.pack.sha256[index] = static_cast<std::uint8_t>(
            digestSeed + static_cast<std::uint8_t>(index));
    }
    return result;
}

void testCurrentTicketAccepted() {
    const auto current = revision(7U, 4'096U, 0x10U);
    WorldRoomPublicationGate gate(current);
    const auto ticket = gate.begin(current);

    require(ticket.has_value(), "current revision did not receive a ticket");
    require(ticket->serial == 1U, "first ticket serial was not one");
    require(gate.hasOutstandingTicket(),
        "current ticket was not marked outstanding");
    require(gate.isCurrent(*ticket),
        "current progress/failure ticket was rejected");
    require(gate.accepts(*ticket, current),
        "current ticket and result revision were rejected");
}

void testConsumeFinalizesExactlyOnceAndAllowsNextTicket() {
    const auto current = revision(13U, 4'096U, 0x60U);
    WorldRoomPublicationGate gate(current);
    const auto first = gate.begin(current);

    require(first.has_value(), "consume fixture did not receive a ticket");
    require(gate.consume(*first, current),
        "current ticket was not consumed");
    require(!gate.hasOutstandingTicket(),
        "successful consume left a ticket outstanding");
    require(!gate.accepts(*first, current),
        "consumed ticket remained acceptable");
    require(!gate.consume(*first, current),
        "ticket was consumed twice");

    const auto next = gate.begin(current);
    require(next.has_value() && next->serial == first->serial + 1U,
        "new ticket after consume was not issued monotonically");
    require(gate.accepts(*next, current),
        "new ticket after consume was not current");
}

void testRejectedConsumeLeavesCurrentTicketOutstanding() {
    const auto current = revision(14U, 8'192U, 0x70U);
    const auto wrongRevision = revision(15U, 8'192U, 0x70U);
    WorldRoomPublicationGate gate(current);
    const auto stale = gate.begin(current);
    const auto active = gate.begin(current);
    require(stale.has_value() && active.has_value(),
        "stale consume fixture did not receive tickets");

    require(!gate.consume(*stale, current),
        "stale serial was consumed");
    require(gate.accepts(*active, current),
        "stale serial consume changed current state");

    const airfix::content::WorldRoomPublicationTicket wrongTicket{
        .serial = active->serial,
        .expectedRevision = wrongRevision,
    };
    require(!gate.consume(wrongTicket, wrongRevision),
        "revision-mismatched ticket was consumed");
    require(!gate.consume(*active, wrongRevision),
        "revision-mismatched result was consumed");
    require(gate.hasOutstandingTicket() &&
            gate.accepts(*active, current),
        "revision rejection changed outstanding state");
}

void testConsumeRequiresOwnerThread() {
    const auto current = revision(16U, 16'384U, 0x80U);
    WorldRoomPublicationGate gate(current);
    const auto ticket = gate.begin(current);
    require(ticket.has_value(),
        "wrong-thread consume fixture did not receive a ticket");

    bool consumedOnWorker = true;
    std::thread worker([&] {
        consumedOnWorker = gate.consume(*ticket, current);
    });
    worker.join();

    require(!consumedOnWorker,
        "non-owner thread consumed a publication ticket");
    require(gate.hasOutstandingTicket() &&
            gate.accepts(*ticket, current),
        "wrong-thread consume changed owner state");
    require(gate.consume(*ticket, current),
        "owner thread could not consume after wrong-thread rejection");
}

void testAbandonFinalizesOnlyTheExactCurrentTicket() {
    const auto current = revision(17U, 32'768U, 0x90U);
    const auto wrongRevision = revision(18U, 32'768U, 0x90U);
    WorldRoomPublicationGate gate(current);
    const auto stale = gate.begin(current);
    const auto active = gate.begin(current);
    require(stale.has_value() && active.has_value(),
        "abandon fixture did not receive tickets");

    require(!gate.abandon(*stale, current),
        "stale ticket abandoned a newer request");
    require(!gate.abandon(*active, wrongRevision),
        "mismatched result revision abandoned the current request");
    require(gate.accepts(*active, current),
        "rejected abandon changed the current request");
    require(gate.abandon(*active, current),
        "exact current ticket could not be abandoned");
    require(!gate.hasOutstandingTicket() &&
            !gate.accepts(*active, current),
        "abandoned ticket remained current");

    const auto next = gate.begin(current);
    require(next.has_value() && next->serial == active->serial + 1U,
        "abandon did not permit the next monotonic request");
}

void testNewerRequestRejectsOldTicket() {
    const auto current = revision(8U, 8'192U, 0x20U);
    WorldRoomPublicationGate gate(current);
    const auto oldTicket = gate.begin(current);
    const auto newTicket = gate.begin(current);

    require(oldTicket.has_value() && newTicket.has_value(),
        "sequential current requests did not receive tickets");
    require(newTicket->serial == oldTicket->serial + 1U,
        "request serial did not increase monotonically");
    require(!gate.accepts(*oldTicket, current),
        "newer request did not make the old ticket stale");
    require(!gate.isCurrent(*oldTicket),
        "newer request left old progress/failure callbacks current");
    require(gate.isCurrent(*newTicket),
        "newest progress/failure callback was rejected");
    require(gate.accepts(*newTicket, current),
        "newest request ticket was rejected");
}

void testInvalidationAndClearRejectTickets() {
    const auto current = revision(9U, 16'384U, 0x30U);
    WorldRoomPublicationGate gate(current);
    const auto invalidated = gate.begin(current);
    require(invalidated.has_value(), "invalidation fixture has no ticket");

    gate.invalidate();
    require(!gate.isCurrent(*invalidated),
        "explicit invalidation left progress/failure callbacks current");
    require(!gate.accepts(*invalidated, current),
        "explicit invalidation left a ticket publishable");
    require(gate.activeRevision() == current,
        "invalidation unexpectedly cleared the active revision");

    const auto cleared = gate.begin(current);
    require(cleared.has_value(), "request after invalidation was refused");
    gate.clearActiveRevision();
    require(!gate.accepts(*cleared, current),
        "clearing the active revision left a ticket publishable");
    require(!gate.activeRevision().has_value(),
        "active revision was not cleared");
    require(!gate.begin(current).has_value(),
        "request began without an active revision");
}

void testRevisionIdentityMismatches() {
    const auto current = revision(10U, 32'768U, 0x40U);
    const auto wrongGeneration = revision(11U, 32'768U, 0x40U);
    const auto wrongSize = revision(10U, 32'769U, 0x40U);
    const auto wrongDigest = revision(10U, 32'768U, 0x41U);
    WorldRoomPublicationGate gate(current);

    require(!gate.begin(wrongGeneration).has_value(),
        "generation-mismatched request received a ticket");
    require(!gate.begin(wrongSize).has_value(),
        "size-mismatched request received a ticket");
    require(!gate.begin(wrongDigest).has_value(),
        "digest-mismatched request received a ticket");

    const auto ticket = gate.begin(current);
    require(ticket.has_value(), "matching request did not receive a ticket");
    require(!gate.accepts(*ticket, wrongGeneration),
        "generation-mismatched result was accepted");
    require(!gate.accepts(*ticket, wrongSize),
        "size-mismatched result was accepted");
    require(!gate.accepts(*ticket, wrongDigest),
        "digest-mismatched result was accepted");

    gate.setActiveRevision(wrongGeneration);
    require(!gate.accepts(*ticket, current),
        "active generation change left the old ticket publishable");
    gate.setActiveRevision(wrongSize);
    require(!gate.accepts(*ticket, current),
        "active size change left the old ticket publishable");
    gate.setActiveRevision(wrongDigest);
    require(!gate.accepts(*ticket, current),
        "active digest change left the old ticket publishable");
}

void testSerialOverflowRefusesNewTicket() {
    const auto current = revision(12U, 65'536U, 0x50U);
    WorldRoomPublicationGate gate(
        current,
        std::numeric_limits<std::uint64_t>::max() - 1U);
    const auto finalTicket = gate.begin(current);

    require(finalTicket.has_value() &&
            finalTicket->serial ==
                std::numeric_limits<std::uint64_t>::max(),
        "last representable serial was not issued");
    require(gate.exhausted(), "maximum serial was not marked exhausted");
    require(gate.accepts(*finalTicket, current),
        "last representable ticket was not initially current");
    require(!gate.begin(current).has_value(),
        "serial wrapped or issued a ticket after UINT64_MAX");
    require(gate.accepts(*finalTicket, current),
        "refused overflow attempt unexpectedly invalidated current ticket");

    gate.invalidate();
    require(!gate.accepts(*finalTicket, current),
        "maximum-serial ticket survived explicit invalidation");
    require(!gate.begin(current).has_value(),
        "invalidated exhausted gate issued another ticket");
}

} // namespace

int main() {
    try {
        testCurrentTicketAccepted();
        testConsumeFinalizesExactlyOnceAndAllowsNextTicket();
        testRejectedConsumeLeavesCurrentTicketOutstanding();
        testConsumeRequiresOwnerThread();
        testAbandonFinalizesOnlyTheExactCurrentTicket();
        testNewerRequestRejectsOldTicket();
        testInvalidationAndClearRejectTickets();
        testRevisionIdentityMismatches();
        testSerialOverflowRefusesNewTicket();
        std::cout << "World room publication gate tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "World room publication gate test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
