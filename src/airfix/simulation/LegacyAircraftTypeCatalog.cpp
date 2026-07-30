#include "airfix/simulation/LegacyAircraftTypeCatalog.hpp"

#include <array>

namespace airfix::simulation {
namespace {

constexpr std::string_view aircraftObjectPrefix =
    "Game\\Objects\\AirCrafts\\";
constexpr std::string_view unitObjectPrefix =
    "Game\\Objects\\Units\\";
constexpr std::string_view playerObjectSuffix = ".object";

constexpr std::array<LegacyAircraftTypeRecord, legacyAircraftTypeCount>
    catalog{{
        {
            .id = LegacyAircraftTypeId::mustang,
            .registeredName = "AcMustang",
            .parameters =
                {
                    0x3F800000U,
                    0x42E60000U,
                    0x42E60000U,
                    0x42960000U,
                    0x42C80000U,
                    0x42960000U,
                    0x43160000U,
                    1U,
                    0x3D90ABB4U,
                    0x3D4FBFC6U,
                },
        },
        {
            .id = LegacyAircraftTypeId::hellcat,
            .registeredName = "AcHellcat",
            .parameters =
                {
                    0x3F800000U,
                    0x42E60000U,
                    0x42E60000U,
                    0x42C80000U,
                    0x43160000U,
                    0x43160000U,
                    0x43160000U,
                    1U,
                    0x3DA86D72U,
                    0x3D88CE70U,
                },
        },
        {
            .id = LegacyAircraftTypeId::dauntless,
            .registeredName = "AcDauntless",
            .parameters =
                {
                    0x3F800000U,
                    0x42960000U,
                    0x42B40000U,
                    0x43160000U,
                    0x42C80000U,
                    0x43160000U,
                    0x43480000U,
                    1U,
                    0x3DA32F44U,
                    0x3D486057U,
                },
        },
        {
            .id = LegacyAircraftTypeId::zero,
            .registeredName = "AcZero",
            .parameters =
                {
                    0x3F800000U,
                    0x42960000U,
                    0x42B40000U,
                    0x42C80000U,
                    0x42C80000U,
                    0x43160000U,
                    0x43160000U,
                    3U,
                    0x3D516334U,
                    0x3D1E98DDU,
                },
        },
        {
            .id = LegacyAircraftTypeId::stuka,
            .registeredName = "AcStuka",
            .parameters =
                {
                    0x3F800000U,
                    0x42B40000U,
                    0x42960000U,
                    0x43160000U,
                    0x43480000U,
                    0x43160000U,
                    0x43480000U,
                    3U,
                    0x3D944673U,
                    0x3D1AD42CU,
                },
        },
        {
            .id = LegacyAircraftTypeId::bf109,
            .registeredName = "AcBf109",
            .parameters =
                {
                    0x3F800000U,
                    0x42E60000U,
                    0x42E60000U,
                    0x42960000U,
                    0x42C80000U,
                    0x42C80000U,
                    0x42960000U,
                    3U,
                    0x3D4985F0U,
                    0x3D4985F0U,
                },
        },
        {
            .id = LegacyAircraftTypeId::fw190,
            .registeredName = "AcFw190",
            .parameters =
                {
                    0x3F800000U,
                    0x42B40000U,
                    0x42E60000U,
                    0x42960000U,
                    0x43160000U,
                    0x42960000U,
                    0x42C80000U,
                    3U,
                    0x3D81450FU,
                    0x3D4CA2DBU,
                },
        },
        {
            .id = LegacyAircraftTypeId::me262,
            .registeredName = "AcMe262",
            .parameters =
                {
                    0x3F800000U,
                    0x43250000U,
                    0x42B40000U,
                    0x43160000U,
                    0x42C80000U,
                    0x42960000U,
                    0x43160000U,
                    3U,
                    0x3D67AB75U,
                    0x3D2C5C14U,
                },
        },
        {
            .id = LegacyAircraftTypeId::fiatG50,
            .registeredName = "AcFiatG50",
            .parameters =
                {
                    0x3F800000U,
                    0x42B40000U,
                    0x42E60000U,
                    0x42960000U,
                    0x43160000U,
                    0x42960000U,
                    0x42C80000U,
                    3U,
                    0x3D6AF252U,
                    0x3D2CD9E7U,
                },
        },
        {
            .id = LegacyAircraftTypeId::spitfire,
            .registeredName = "AcSpitfire",
            .parameters =
                {
                    0x3F800000U,
                    0x42E60000U,
                    0x42E60000U,
                    0x42C80000U,
                    0x42C80000U,
                    0x42C80000U,
                    0x42C80000U,
                    1U,
                    0x3D93DD97U,
                    0x3D425072U,
                },
        },
        {
            .id = LegacyAircraftTypeId::hurricane,
            .registeredName = "AcHurricane",
            .parameters =
                {
                    0x3F800000U,
                    0x42B40000U,
                    0x42B40000U,
                    0x43160000U,
                    0x42960000U,
                    0x42C80000U,
                    0x42C80000U,
                    1U,
                    0x3D8DA3C2U,
                    0x3D54FDF3U,
                },
        },
        {
            .id = LegacyAircraftTypeId::typhoon,
            .registeredName = "AcTyphoon",
            .parameters =
                {
                    0x3F800000U,
                    0x42B40000U,
                    0x42B40000U,
                    0x42C80000U,
                    0x43160000U,
                    0x42C80000U,
                    0x43160000U,
                    1U,
                    0x3D94EE39U,
                    0x3D4FE9B8U,
                },
        },
        {
            .id = LegacyAircraftTypeId::blackWidow,
            .registeredName = "AcBlackWidow",
            .parameters =
                {
                    0x3FB33333U,
                    0x42960000U,
                    0x42B40000U,
                    0x43160000U,
                    0x43480000U,
                    0x43160000U,
                    0x43480000U,
                    1U,
                    0x3DA8198FU,
                    0x3D397785U,
                },
        },
        {
            .id = LegacyAircraftTypeId::komet,
            .registeredName = "AcKomet",
            .parameters =
                {
                    0x3F800000U,
                    0x43250000U,
                    0x42B40000U,
                    0x43160000U,
                    0x42C80000U,
                    0x42960000U,
                    0x43160000U,
                    3U,
                    0x3D8D4FDFU,
                    0x3CC5436BU,
                },
        },
        {
            .id = LegacyAircraftTypeId::junker86,
            .registeredName = "AuJunker86",
            .parameters =
                {
                    0x3F800000U,
                    0x42960000U,
                    0x42B40000U,
                    0x43160000U,
                    0x43160000U,
                    0x42C80000U,
                    0x43480000U,
                    3U,
                    0x3D23D70AU,
                    0x3D23D70AU,
                },
        },
        {
            .id = LegacyAircraftTypeId::lancaster,
            .registeredName = "AuLancaster",
            .parameters =
                {
                    0x3F800000U,
                    0x42960000U,
                    0x42B40000U,
                    0x43160000U,
                    0x43160000U,
                    0x43160000U,
                    0x43480000U,
                    1U,
                    0x3D23D70AU,
                    0x3D23D70AU,
                },
        },
        {
            .id = LegacyAircraftTypeId::b17,
            .registeredName = "AuB17",
            .parameters =
                {
                    0x3F800000U,
                    0x42960000U,
                    0x42B40000U,
                    0x43160000U,
                    0x43160000U,
                    0x42C80000U,
                    0x43480000U,
                    1U,
                    0x3D23D70AU,
                    0x3D23D70AU,
                },
        },
    }};

[[nodiscard]] constexpr bool validPositiveFiniteBits(
    const std::uint32_t bits) noexcept {
    return (bits & 0x80000000U) == 0U &&
        (bits & 0x7F800000U) != 0U &&
        (bits & 0x7F800000U) != 0x7F800000U;
}

[[nodiscard]] constexpr bool validCatalog() noexcept {
    for (std::size_t index = 0U; index < catalog.size(); ++index) {
        const auto& record = catalog[index];
        const auto& parameters = record.parameters;
        if (static_cast<std::size_t>(record.id) != index ||
            record.registeredName.empty() ||
            !validPositiveFiniteBits(parameters.instanceExtentBits) ||
            !validPositiveFiniteBits(parameters.thrustPercentBits) ||
            !validPositiveFiniteBits(parameters.forceDampingPercentBits) ||
            !validPositiveFiniteBits(parameters.rigidBodyMassScaleBits) ||
            !validPositiveFiniteBits(parameters.initialHealthBits) ||
            !validPositiveFiniteBits(
                parameters.inheritedInstanceParameterBits) ||
            !validPositiveFiniteBits(parameters.unusedTupleValueBits) ||
            (parameters.kindCode != 1U && parameters.kindCode != 3U) ||
            !validPositiveFiniteBits(parameters.parameter9Bits) ||
            !validPositiveFiniteBits(parameters.parameter10Bits)) {
            return false;
        }
        for (std::size_t other = index + 1U;
             other < catalog.size();
             ++other) {
            if (record.registeredName == catalog[other].registeredName) {
                return false;
            }
        }
    }
    return true;
}

static_assert(validCatalog());

} // namespace

std::span<const LegacyAircraftTypeRecord, legacyAircraftTypeCount>
legacyAircraftTypeCatalog() noexcept {
    return catalog;
}

const LegacyAircraftTypeRecord*
findLegacyAircraftType(const LegacyAircraftTypeId id) noexcept {
    const auto index = static_cast<std::size_t>(id);
    return index < catalog.size() ? &catalog[index] : nullptr;
}

const LegacyAircraftTypeRecord*
findLegacyAircraftType(const std::string_view registeredName) noexcept {
    for (const auto& record : catalog) {
        if (record.registeredName == registeredName) {
            return &record;
        }
    }
    return nullptr;
}

const LegacyAircraftTypeRecord*
findLegacyAircraftTypeForObjectLogicalPath(
    const std::string_view canonicalLogicalPath) noexcept {
    const bool aircraftPath =
        canonicalLogicalPath.starts_with(aircraftObjectPrefix);
    const bool unitPath =
        canonicalLogicalPath.starts_with(unitObjectPrefix);
    if ((!aircraftPath && !unitPath) ||
        !canonicalLogicalPath.ends_with(playerObjectSuffix)) {
        return nullptr;
    }
    const auto nameOffset = aircraftPath
        ? aircraftObjectPrefix.size()
        : unitObjectPrefix.size();
    if (canonicalLogicalPath.size() <=
        nameOffset + playerObjectSuffix.size()) {
        return nullptr;
    }
    const auto nameSize = canonicalLogicalPath.size() -
        nameOffset - playerObjectSuffix.size();
    const auto* record = findLegacyAircraftType(
        canonicalLogicalPath.substr(nameOffset, nameSize));
    if (record == nullptr) {
        return nullptr;
    }
    const bool auxiliaryUnitRecord =
        record->registeredName.starts_with("Au");
    return auxiliaryUnitRecord == unitPath ? record : nullptr;
}

} // namespace airfix::simulation
