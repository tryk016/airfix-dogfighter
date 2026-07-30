#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace airfix::simulation {

enum class LegacyAircraftTypeId : std::uint8_t {
    mustang,
    hellcat,
    dauntless,
    zero,
    stuka,
    bf109,
    fw190,
    me262,
    fiatG50,
    spitfire,
    hurricane,
    typhoon,
    blackWidow,
    komet,
    junker86,
    lancaster,
    b17,
};

inline constexpr std::size_t legacyAircraftTypeCount = 17U;

// Exact 32-bit words supplied by AirCraft.type's registration routine. The
// constructor maps the fields as documented below; retaining the source words
// avoids applying a floating-point policy before the future 12-ms flight
// runtime explicitly selects one.
struct LegacyAircraftTypeParameters final {
    std::uint32_t instanceExtentBits{};
    std::uint32_t thrustPercentBits{};
    std::uint32_t forceDampingPercentBits{};
    std::uint32_t rigidBodyMassScaleBits{};
    std::uint32_t initialHealthBits{};
    std::uint32_t inheritedInstanceParameterBits{};
    std::uint32_t unusedTupleValueBits{};
    std::uint32_t kindCode{};
    std::uint32_t parameter9Bits{};
    std::uint32_t parameter10Bits{};

    [[nodiscard]] constexpr float instanceExtent() const noexcept {
        return std::bit_cast<float>(instanceExtentBits);
    }

    [[nodiscard]] constexpr float thrustPercent() const noexcept {
        return std::bit_cast<float>(thrustPercentBits);
    }

    [[nodiscard]] constexpr float forceDampingPercent() const noexcept {
        return std::bit_cast<float>(forceDampingPercentBits);
    }

    [[nodiscard]] constexpr float rigidBodyMassScale() const noexcept {
        return std::bit_cast<float>(rigidBodyMassScaleBits);
    }

    [[nodiscard]] constexpr float initialHealth() const noexcept {
        return std::bit_cast<float>(initialHealthBits);
    }

    [[nodiscard]] constexpr float inheritedInstanceParameter() const noexcept {
        return std::bit_cast<float>(inheritedInstanceParameterBits);
    }

    [[nodiscard]] constexpr float unusedTupleValue() const noexcept {
        return std::bit_cast<float>(unusedTupleValueBits);
    }

    [[nodiscard]] constexpr float parameter9() const noexcept {
        return std::bit_cast<float>(parameter9Bits);
    }

    [[nodiscard]] constexpr float parameter10() const noexcept {
        return std::bit_cast<float>(parameter10Bits);
    }

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyAircraftTypeParameters&,
        const LegacyAircraftTypeParameters&) noexcept = default;
};

struct LegacyAircraftTypeRecord final {
    LegacyAircraftTypeId id{};
    std::string_view registeredName;
    LegacyAircraftTypeParameters parameters;

    [[nodiscard]] friend constexpr bool operator==(
        const LegacyAircraftTypeRecord&,
        const LegacyAircraftTypeRecord&) noexcept = default;
};

[[nodiscard]] std::span<
    const LegacyAircraftTypeRecord,
    legacyAircraftTypeCount>
legacyAircraftTypeCatalog() noexcept;

[[nodiscard]] const LegacyAircraftTypeRecord*
findLegacyAircraftType(LegacyAircraftTypeId id) noexcept;

// Registration names are identifiers, not user-facing labels. Resolution is
// exact and case-sensitive, matching the canonical AirCraft.type registry.
[[nodiscard]] const LegacyAircraftTypeRecord*
findLegacyAircraftType(std::string_view registeredName) noexcept;

// Resolves only the two verified authenticated Resource.up object layouts:
// the 14 Ac* records under Game\Objects\AirCrafts and the three Au* records
// under Game\Objects\Units. It allocates nothing and does not normalize, log,
// or retain the caller's path.
[[nodiscard]] const LegacyAircraftTypeRecord*
findLegacyAircraftTypeForObjectLogicalPath(
    std::string_view canonicalLogicalPath) noexcept;

} // namespace airfix::simulation
