#include "airfix/render/LegacyAircraftHudWeaponPanels.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {

std::atomic<bool> trackAllocations{false};
std::atomic<std::size_t> allocationCount{0U};

void recordAllocation() noexcept {
  if (trackAllocations.load(std::memory_order_relaxed)) {
    allocationCount.fetch_add(1U, std::memory_order_relaxed);
  }
}

} // namespace

void *operator new(const std::size_t size) {
  recordAllocation();
  if (void *memory = std::malloc(size == 0U ? 1U : size); memory != nullptr) {
    return memory;
  }
  throw std::bad_alloc();
}

void *operator new[](const std::size_t size) { return ::operator new(size); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { ::operator delete(memory); }
void operator delete(void *memory, std::size_t) noexcept {
  ::operator delete(memory);
}
void operator delete[](void *memory, std::size_t) noexcept {
  ::operator delete(memory);
}

namespace {

using namespace airfix::render;
using namespace recovered_legacy_aircraft_hud_weapon_panels;

static_assert(noexcept(buildLegacyAircraftHudWeaponPanelsPlan({})));
static_assert(maximumCommands == 14U);
static_assert(legacyAircraftHudWeaponStatusColour(-1) == 0xBF269A1AU);
static_assert(legacyAircraftHudWeaponStatusColour(2) == 0xBFEDC610U);
static_assert(legacyAircraftHudWeaponStatusColour(9) == 0xBFBD1700U);

void require(const bool condition, const char *const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] LegacyAircraftHudWeaponPanelsInput validInput() noexcept {
  const auto digits = makeLegacyAircraftHudRollingDigitsState(123);
  const LegacyAircraftHudWeaponPanelSlotInput primary{
      .weaponPresent = true,
      .digitStateAvailable = true,
      .digitState = digits,
      .quantizedAmmo = 123,
      .iconCatalogMatch = true,
      .iconTextureAvailable = true,
      .iconTextureIndex = 4U,
      .quantizedStatusIndex = -9,
  };
  const LegacyAircraftHudWeaponPanelSlotInput secondary{
      .weaponPresent = true,
      .digitStateAvailable = true,
      .digitState = digits,
      .quantizedAmmo = 123,
      .iconCatalogMatch = true,
      .iconTextureAvailable = true,
      .iconTextureIndex = 8U,
      .quantizedStatusIndex = 99,
  };
  return {
      .activeWindowPresent = false,
      .cameraAttachedAtEntry = true,
      .typeHudEnabled = true,
      .cameraAttachedAfterLayout = true,
      .screenWidth = 640U,
      .screenHeight = 480U,
      .accumulatedElapsedSeconds = 0.0F,
      .primaryBackgroundTextureAvailable = true,
      .secondaryBackgroundTextureAvailable = true,
      .rollingDigitAtlasAvailable = true,
      .primary = primary,
      .secondary = secondary,
  };
}

void requireRejected(const LegacyAircraftHudWeaponPanelsInput &input,
                     const LegacyAircraftHudWeaponPanelsPlanStatus status,
                     const char *const message) {
  const auto plan = buildLegacyAircraftHudWeaponPanelsPlan(input);
  require(!plan.ready() && plan.status == status && plan.commandCount == 0U &&
              plan.command(0U) == nullptr,
          message);
}

void testGateOrder() {
  auto input = validInput();
  input.activeWindowPresent = true;
  input.cameraAttachedAtEntry = false;
  input.typeHudEnabled = false;
  input.cameraAttachedAfterLayout = false;
  requireRejected(input,
                  LegacyAircraftHudWeaponPanelsPlanStatus::activeWindowPresent,
                  "active-window gate was not first");
  input.activeWindowPresent = false;
  requireRejected(
      input, LegacyAircraftHudWeaponPanelsPlanStatus::cameraNotAttachedAtEntry,
      "entry camera gate was not second");
  input.cameraAttachedAtEntry = true;
  requireRejected(input,
                  LegacyAircraftHudWeaponPanelsPlanStatus::typeHudDisabled,
                  "type HUD gate was not third");
  input.typeHudEnabled = true;
  requireRejected(
      input, LegacyAircraftHudWeaponPanelsPlanStatus::cameraDetachedBeforeDraw,
      "repeated camera gate was not preserved");
}

void testExactGeometryOrderAndColours() {
  const auto plan = buildLegacyAircraftHudWeaponPanelsPlan(validInput());
  require(plan.ready() && plan.commandCount == maximumCommands &&
              plan.sourceScreenWidth == 640U &&
              plan.sourceScreenHeight == 480U &&
              plan.command(maximumCommands) == nullptr,
          "complete native weapon-panel plan was rejected");

  const auto *primaryBackground = plan.command(0U);
  const auto *secondaryBackground = plan.command(1U);
  const auto *primaryFirstDigit = plan.command(2U);
  const auto *primaryIcon = plan.command(6U);
  const auto *primaryStatus = plan.command(7U);
  const auto *secondaryFirstDigit = plan.command(8U);
  const auto *secondaryIcon = plan.command(12U);
  const auto *secondaryStatus = plan.command(13U);
  require(primaryBackground != nullptr && secondaryBackground != nullptr &&
              primaryFirstDigit != nullptr && primaryIcon != nullptr &&
              primaryStatus != nullptr && secondaryFirstDigit != nullptr &&
              secondaryIcon != nullptr && secondaryStatus != nullptr,
          "complete command sequence was not addressable");
  require(
      primaryBackground->slot == LegacyAircraftHudWeaponPanelSlot::primary &&
          primaryBackground->kind ==
              LegacyAircraftHudWeaponPanelCommandKind::background &&
          secondaryBackground->slot ==
              LegacyAircraftHudWeaponPanelSlot::secondary &&
          secondaryBackground->kind ==
              LegacyAircraftHudWeaponPanelCommandKind::background &&
          primaryFirstDigit->kind ==
              LegacyAircraftHudWeaponPanelCommandKind::digit &&
          primaryIcon->kind == LegacyAircraftHudWeaponPanelCommandKind::icon &&
          primaryStatus->kind ==
              LegacyAircraftHudWeaponPanelCommandKind::statusOverlay &&
          secondaryFirstDigit->kind ==
              LegacyAircraftHudWeaponPanelCommandKind::digit &&
          secondaryIcon->kind ==
              LegacyAircraftHudWeaponPanelCommandKind::icon &&
          secondaryStatus->kind ==
              LegacyAircraftHudWeaponPanelCommandKind::statusOverlay,
      "background-then-primary-then-secondary native order changed");

  require(primaryBackground->destinationRect ==
                  LegacyCanvasRect{223.0F, 408.0F, 64.0F, 64.0F} &&
              secondaryBackground->destinationRect ==
                  LegacyCanvasRect{353.0F, 408.0F, 64.0F, 64.0F} &&
              primaryFirstDigit->destinationRect ==
                  LegacyCanvasRect{239.0F, 449.0F, 7.0F, 9.0F} &&
              secondaryFirstDigit->destinationRect ==
                  LegacyCanvasRect{369.0F, 449.0F, 7.0F, 9.0F} &&
              primaryIcon->destinationRect ==
                  LegacyCanvasRect{229.0F, 414.0F, 64.0F, 32.0F} &&
              primaryStatus->destinationRect ==
                  LegacyCanvasRect{229.0F, 414.0F, 52.0F, 30.0F} &&
              secondaryIcon->destinationRect ==
                  LegacyCanvasRect{359.0F, 414.0F, 64.0F, 32.0F} &&
              secondaryStatus->destinationRect ==
                  LegacyCanvasRect{359.0F, 414.0F, 52.0F, 30.0F},
          "recovered 640x480 panel geometry changed");
  require(primaryIcon->textureIndex == 4U &&
              secondaryIcon->textureIndex == 8U &&
              primaryStatus->colourArgb == 0xBF269A1AU &&
              secondaryStatus->colourArgb == 0xBFBD1700U,
          "icon identity or clamped status palette changed");
}

void testIndependentOptionalLayers() {
  auto input = validInput();
  input.primaryBackgroundTextureAvailable = false;
  input.secondary.weaponPresent = false;
  const auto primaryOnly = buildLegacyAircraftHudWeaponPanelsPlan(input);
  require(primaryOnly.ready() && primaryOnly.commandCount == 7U &&
              primaryOnly.command(0U)->slot ==
                  LegacyAircraftHudWeaponPanelSlot::secondary &&
              primaryOnly.command(1U)->kind ==
                  LegacyAircraftHudWeaponPanelCommandKind::digit,
          "missing background or absent secondary slot changed other layers");

  input = validInput();
  input.primary.iconTextureAvailable = false;
  input.secondary.iconCatalogMatch = false;
  const auto sparse = buildLegacyAircraftHudWeaponPanelsPlan(input);
  require(sparse.ready() && sparse.commandCount == 11U &&
              sparse.command(6U)->kind ==
                  LegacyAircraftHudWeaponPanelCommandKind::statusOverlay &&
              sparse.command(10U)->kind ==
                  LegacyAircraftHudWeaponPanelCommandKind::digit,
          "catalog match and icon availability were not independent");

  input = validInput();
  input.rollingDigitAtlasAvailable = false;
  input.primary.quantizedAmmo = 456;
  const auto noAtlas = buildLegacyAircraftHudWeaponPanelsPlan(input);
  require(noAtlas.ready() && noAtlas.commandCount == 6U &&
              noAtlas.primaryDigitState != input.primary.digitState,
          "missing digit atlas incorrectly suppressed native state advance");
}

void testValidationAndNoPartialStateMutation() {
  auto input = validInput();
  input.screenWidth = 0U;
  requireRejected(input,
                  LegacyAircraftHudWeaponPanelsPlanStatus::invalidScreenExtent,
                  "zero screen width was accepted");
  input = validInput();
  input.accumulatedElapsedSeconds = std::numeric_limits<float>::quiet_NaN();
  requireRejected(
      input, LegacyAircraftHudWeaponPanelsPlanStatus::elapsedSecondsNotFinite,
      "NaN elapsed value was accepted");
  input = validInput();
  input.accumulatedElapsedSeconds = -0.1F;
  const auto rejected = buildLegacyAircraftHudWeaponPanelsPlan(input);
  require(
      !rejected.ready() &&
          rejected.status ==
              LegacyAircraftHudWeaponPanelsPlanStatus::elapsedSecondsNegative &&
          rejected.primaryDigitState == input.primary.digitState &&
          rejected.secondaryDigitState == input.secondary.digitState,
      "rejected plan published partially advanced digit state");
}

void testAllocationFree() {
  allocationCount.store(0U, std::memory_order_relaxed);
  trackAllocations.store(true, std::memory_order_relaxed);
  const auto plan = buildLegacyAircraftHudWeaponPanelsPlan(validInput());
  trackAllocations.store(false, std::memory_order_relaxed);
  require(plan.ready() && allocationCount.load(std::memory_order_relaxed) == 0U,
          "weapon-panel planning allocated memory");
}

} // namespace

int main() {
  try {
    testGateOrder();
    testExactGeometryOrderAndColours();
    testIndependentOptionalLayers();
    testValidationAndNoPartialStateMutation();
    testAllocationFree();
    std::cout << "LegacyAircraftHudWeaponPanels tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "LegacyAircraftHudWeaponPanels tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
