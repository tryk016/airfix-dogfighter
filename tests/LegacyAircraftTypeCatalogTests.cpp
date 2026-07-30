#include "airfix/simulation/LegacyAircraftTypeCatalog.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

using airfix::simulation::LegacyAircraftTypeId;
using airfix::simulation::LegacyAircraftTypeParameters;
using airfix::simulation::LegacyAircraftTypeRecord;

struct ExpectedRecord final {
    LegacyAircraftTypeId id{};
    std::string_view name;
    LegacyAircraftTypeParameters parameters;
};

constexpr std::array<ExpectedRecord, 17U> expected{{
    {LegacyAircraftTypeId::mustang, "AcMustang",
     {0x3F800000U, 0x42E60000U, 0x42E60000U, 0x42960000U,
      0x42C80000U, 0x42960000U, 0x43160000U, 1U, 0x3D90ABB4U,
      0x3D4FBFC6U}},
    {LegacyAircraftTypeId::hellcat, "AcHellcat",
     {0x3F800000U, 0x42E60000U, 0x42E60000U, 0x42C80000U,
      0x43160000U, 0x43160000U, 0x43160000U, 1U, 0x3DA86D72U,
      0x3D88CE70U}},
    {LegacyAircraftTypeId::dauntless, "AcDauntless",
     {0x3F800000U, 0x42960000U, 0x42B40000U, 0x43160000U,
      0x42C80000U, 0x43160000U, 0x43480000U, 1U, 0x3DA32F44U,
      0x3D486057U}},
    {LegacyAircraftTypeId::zero, "AcZero",
     {0x3F800000U, 0x42960000U, 0x42B40000U, 0x42C80000U,
      0x42C80000U, 0x43160000U, 0x43160000U, 3U, 0x3D516334U,
      0x3D1E98DDU}},
    {LegacyAircraftTypeId::stuka, "AcStuka",
     {0x3F800000U, 0x42B40000U, 0x42960000U, 0x43160000U,
      0x43480000U, 0x43160000U, 0x43480000U, 3U, 0x3D944673U,
      0x3D1AD42CU}},
    {LegacyAircraftTypeId::bf109, "AcBf109",
     {0x3F800000U, 0x42E60000U, 0x42E60000U, 0x42960000U,
      0x42C80000U, 0x42C80000U, 0x42960000U, 3U, 0x3D4985F0U,
      0x3D4985F0U}},
    {LegacyAircraftTypeId::fw190, "AcFw190",
     {0x3F800000U, 0x42B40000U, 0x42E60000U, 0x42960000U,
      0x43160000U, 0x42960000U, 0x42C80000U, 3U, 0x3D81450FU,
      0x3D4CA2DBU}},
    {LegacyAircraftTypeId::me262, "AcMe262",
     {0x3F800000U, 0x43250000U, 0x42B40000U, 0x43160000U,
      0x42C80000U, 0x42960000U, 0x43160000U, 3U, 0x3D67AB75U,
      0x3D2C5C14U}},
    {LegacyAircraftTypeId::fiatG50, "AcFiatG50",
     {0x3F800000U, 0x42B40000U, 0x42E60000U, 0x42960000U,
      0x43160000U, 0x42960000U, 0x42C80000U, 3U, 0x3D6AF252U,
      0x3D2CD9E7U}},
    {LegacyAircraftTypeId::spitfire, "AcSpitfire",
     {0x3F800000U, 0x42E60000U, 0x42E60000U, 0x42C80000U,
      0x42C80000U, 0x42C80000U, 0x42C80000U, 1U, 0x3D93DD97U,
      0x3D425072U}},
    {LegacyAircraftTypeId::hurricane, "AcHurricane",
     {0x3F800000U, 0x42B40000U, 0x42B40000U, 0x43160000U,
      0x42960000U, 0x42C80000U, 0x42C80000U, 1U, 0x3D8DA3C2U,
      0x3D54FDF3U}},
    {LegacyAircraftTypeId::typhoon, "AcTyphoon",
     {0x3F800000U, 0x42B40000U, 0x42B40000U, 0x42C80000U,
      0x43160000U, 0x42C80000U, 0x43160000U, 1U, 0x3D94EE39U,
      0x3D4FE9B8U}},
    {LegacyAircraftTypeId::blackWidow, "AcBlackWidow",
     {0x3FB33333U, 0x42960000U, 0x42B40000U, 0x43160000U,
      0x43480000U, 0x43160000U, 0x43480000U, 1U, 0x3DA8198FU,
      0x3D397785U}},
    {LegacyAircraftTypeId::komet, "AcKomet",
     {0x3F800000U, 0x43250000U, 0x42B40000U, 0x43160000U,
      0x42C80000U, 0x42960000U, 0x43160000U, 3U, 0x3D8D4FDFU,
      0x3CC5436BU}},
    {LegacyAircraftTypeId::junker86, "AuJunker86",
     {0x3F800000U, 0x42960000U, 0x42B40000U, 0x43160000U,
      0x43160000U, 0x42C80000U, 0x43480000U, 3U, 0x3D23D70AU,
      0x3D23D70AU}},
    {LegacyAircraftTypeId::lancaster, "AuLancaster",
     {0x3F800000U, 0x42960000U, 0x42B40000U, 0x43160000U,
      0x43160000U, 0x43160000U, 0x43480000U, 1U, 0x3D23D70AU,
      0x3D23D70AU}},
    {LegacyAircraftTypeId::b17, "AuB17",
     {0x3F800000U, 0x42960000U, 0x42B40000U, 0x43160000U,
      0x43160000U, 0x42C80000U, 0x43480000U, 1U, 0x3D23D70AU,
      0x3D23D70AU}},
}};

static_assert(std::is_trivially_copyable_v<LegacyAircraftTypeParameters>);
static_assert(std::is_trivially_copyable_v<LegacyAircraftTypeRecord>);

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void testExactCatalog() {
    const auto catalog =
        airfix::simulation::legacyAircraftTypeCatalog();
    require(catalog.size() == expected.size(), "catalog size changed");
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        require(
            catalog[index].id == expected[index].id &&
                catalog[index].registeredName == expected[index].name &&
                catalog[index].parameters == expected[index].parameters,
            "catalog record changed");
        require(
            airfix::simulation::findLegacyAircraftType(
                expected[index].id) == &catalog[index] &&
                airfix::simulation::findLegacyAircraftType(
                    expected[index].name) == &catalog[index],
            "catalog lookup did not preserve record identity");
    }
}

void testTypedFloatViewsPreserveBits() {
    for (const auto& record :
         airfix::simulation::legacyAircraftTypeCatalog()) {
        const auto& value = record.parameters;
        require(
            std::bit_cast<std::uint32_t>(value.instanceExtent()) ==
                    value.instanceExtentBits &&
                std::bit_cast<std::uint32_t>(value.thrustPercent()) ==
                    value.thrustPercentBits &&
                std::bit_cast<std::uint32_t>(
                    value.forceDampingPercent()) ==
                    value.forceDampingPercentBits &&
                std::bit_cast<std::uint32_t>(
                    value.rigidBodyMassScale()) ==
                    value.rigidBodyMassScaleBits &&
                std::bit_cast<std::uint32_t>(value.initialHealth()) ==
                    value.initialHealthBits &&
                std::bit_cast<std::uint32_t>(
                    value.inheritedInstanceParameter()) ==
                    value.inheritedInstanceParameterBits &&
                std::bit_cast<std::uint32_t>(
                    value.unusedTupleValue()) ==
                    value.unusedTupleValueBits &&
                std::bit_cast<std::uint32_t>(value.parameter9()) ==
                    value.parameter9Bits &&
                std::bit_cast<std::uint32_t>(value.parameter10()) ==
                    value.parameter10Bits,
            "typed float view changed an exact source word");
    }
}

void testLookupFailsClosed() {
    require(
        airfix::simulation::findLegacyAircraftType(
            static_cast<LegacyAircraftTypeId>(0xFFU)) == nullptr &&
            airfix::simulation::findLegacyAircraftType("") == nullptr &&
            airfix::simulation::findLegacyAircraftType("acspitfire") ==
                nullptr &&
            airfix::simulation::findLegacyAircraftType("AcSpitfire ") ==
                nullptr,
        "invalid registered identifier resolved");
}

void testCanonicalObjectPathResolution() {
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        const auto& record = expected[index];
        std::string path = record.name.starts_with("Au")
            ? "Game\\Objects\\Units\\"
            : "Game\\Objects\\AirCrafts\\";
        path.append(record.name);
        path.append(".object");
        require(
            airfix::simulation::
                    findLegacyAircraftTypeForObjectLogicalPath(path) ==
                &airfix::simulation::legacyAircraftTypeCatalog()[index],
            "canonical aircraft object path did not resolve");
    }

    constexpr std::array<std::string_view, 10U> invalid{{
        "",
        "AcSpitfire.object",
        "Game/Objects/AirCrafts/AcSpitfire.object",
        "game\\objects\\aircrafts\\AcSpitfire.object",
        "Game\\Objects\\AirCrafts\\.object",
        "Game\\Objects\\AirCrafts\\AcSpitfire",
        "Game\\Objects\\AirCrafts\\AcSpitfire.object\\tail",
        "Game\\Objects\\AirCrafts\\Unknown.object",
        "Game\\Objects\\Units\\AcSpitfire.object",
        "Game\\Objects\\AirCrafts\\AuB17.object",
    }};
    for (const auto path : invalid) {
        require(
            airfix::simulation::
                    findLegacyAircraftTypeForObjectLogicalPath(path) ==
                nullptr,
            "non-canonical player object path resolved");
    }
}

} // namespace

int main() {
    try {
        testExactCatalog();
        testTypedFloatViewsPreserveBits();
        testLookupFailsClosed();
        testCanonicalObjectPathResolution();
        std::cout << "Legacy aircraft type catalog tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "Legacy aircraft type catalog tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}
