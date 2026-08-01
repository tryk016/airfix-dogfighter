#include "airfix/render/LegacyAircraftHealthGaugePlan.hpp"

#include <atomic>
#include <cmath>
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
using namespace recovered_legacy_aircraft_health_gauge;

static_assert(noexcept(buildLegacyAircraftHealthGaugePlan({})));
static_assert(maximumDamageMaskQuads == 32U);
static_assert(maximumCommands == 34U);

void require(const bool condition, const char *const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] bool close(const float actual, const float expected,
                         const float tolerance = 2.0e-4F) noexcept {
  return std::fabs(actual - expected) <= tolerance;
}

[[nodiscard]] LegacyAircraftHealthGaugeInput validInput() noexcept {
  return {
      .activeWindowPresent = false,
      .cameraAttachedAtEntry = true,
      .typeHudEnabled = true,
      .cameraAttachedAfterLayout = true,
      .screenWidth = 640U,
      .screenHeight = 480U,
      .displayedHealth = 50.0F,
      .maximumHealth = 100.0F,
      .armourMeterTextureAvailable = true,
      .armourTextureAvailable = true,
  };
}

void requireRejected(const LegacyAircraftHealthGaugeInput &input,
                     const LegacyAircraftHealthGaugePlanStatus expected,
                     const char *const message) {
  const auto plan = buildLegacyAircraftHealthGaugePlan(input);
  require(!plan.ready() && plan.status == expected && plan.commandCount == 0U &&
              plan.command(0U) == nullptr,
          message);
}

void testNativeGateOrder() {
  auto input = validInput();
  input.activeWindowPresent = true;
  input.cameraAttachedAtEntry = false;
  input.typeHudEnabled = false;
  input.cameraAttachedAfterLayout = false;
  requireRejected(input,
                  LegacyAircraftHealthGaugePlanStatus::activeWindowPresent,
                  "active-window gate did not remain first");

  input.activeWindowPresent = false;
  requireRejected(input,
                  LegacyAircraftHealthGaugePlanStatus::cameraNotAttachedAtEntry,
                  "entry camera gate did not remain second");

  input.cameraAttachedAtEntry = true;
  requireRejected(input, LegacyAircraftHealthGaugePlanStatus::typeHudDisabled,
                  "type HUD gate did not remain third");

  input.typeHudEnabled = true;
  requireRejected(input,
                  LegacyAircraftHealthGaugePlanStatus::cameraDetachedBeforeDraw,
                  "repeated camera gate was not preserved");
}

void testHalfHealthGeometryAndOrdering() {
  const auto plan = buildLegacyAircraftHealthGaugePlan(validInput());
  require(plan.ready(), "half-health gauge plan was rejected");
  require(close(plan.damageSweepRadians, piRadians * 0.5F) &&
              plan.textureOrigin == LegacyCanvasPoint{8.0F, 344.0F},
          "half-health sweep or normal-screen anchor changed");
  require(plan.commandCount == 18U &&
              plan.command(0U)->kind ==
                  LegacyAircraftHealthGaugeCommandKind::armourMeterTexture &&
              plan.command(17U)->kind ==
                  LegacyAircraftHealthGaugeCommandKind::armourTexture &&
              plan.command(18U) == nullptr,
          "half-health native layer order or segment count changed");

  const auto *firstMask = plan.command(1U);
  const auto *lastMask = plan.command(16U);
  require(firstMask != nullptr && lastMask != nullptr &&
              firstMask->kind ==
                  LegacyAircraftHealthGaugeCommandKind::damageMaskQuad &&
              firstMask->colourArgb == damageMaskArgb &&
              close(firstMask->quad[0].x, 28.15938F) &&
              close(firstMask->quad[0].y, 364.15936F) &&
              close(firstMask->quad[3].x, 40.88730F) &&
              close(firstMask->quad[3].y, 376.88730F),
          "first recovered mask edge changed");
  require(close(lastMask->quad[1].x, 28.15938F) &&
              close(lastMask->quad[1].y, 451.84064F) &&
              close(lastMask->quad[2].x, 40.88730F) &&
              close(lastMask->quad[2].y, 439.11270F),
          "half-health final edge changed");
}

void testFullAndEmptyHealthBoundaries() {
  auto input = validInput();
  input.displayedHealth = input.maximumHealth;
  input.armourMeterTextureAvailable = false;
  input.armourTextureAvailable = false;
  const auto full = buildLegacyAircraftHealthGaugePlan(input);
  require(full.ready() && full.damageSweepRadians == 0.0F &&
              full.commandCount == 1U &&
              full.command(0U)->kind ==
                  LegacyAircraftHealthGaugeCommandKind::damageMaskQuad &&
              full.command(0U)->quad[0] == full.command(0U)->quad[1] &&
              full.command(0U)->quad[2] == full.command(0U)->quad[3],
          "native full-health degenerate final quad was not retained");

  input.displayedHealth = 0.0F;
  input.armourMeterTextureAvailable = true;
  input.armourTextureAvailable = true;
  const auto empty = buildLegacyAircraftHealthGaugePlan(input);
  require(empty.ready() && close(empty.damageSweepRadians, piRadians) &&
              empty.commandCount == maximumCommands &&
              empty.command(1U)->kind ==
                  LegacyAircraftHealthGaugeCommandKind::damageMaskQuad &&
              empty.command(maximumCommands - 1U)->kind ==
                  LegacyAircraftHealthGaugeCommandKind::armourTexture,
          "empty-health maximum segment budget changed");
}

void testTextureLayersAreIndependent() {
  auto input = validInput();
  input.armourMeterTextureAvailable = false;
  const auto foregroundOnly = buildLegacyAircraftHealthGaugePlan(input);
  require(foregroundOnly.ready() && foregroundOnly.commandCount == 17U &&
              foregroundOnly.command(0U)->kind ==
                  LegacyAircraftHealthGaugeCommandKind::damageMaskQuad &&
              foregroundOnly.command(16U)->kind ==
                  LegacyAircraftHealthGaugeCommandKind::armourTexture,
          "missing meter background suppressed or reordered the foreground");

  input.armourMeterTextureAvailable = true;
  input.armourTextureAvailable = false;
  const auto backgroundOnly = buildLegacyAircraftHealthGaugePlan(input);
  require(backgroundOnly.ready() && backgroundOnly.commandCount == 17U &&
              backgroundOnly.command(0U)->kind ==
                  LegacyAircraftHealthGaugeCommandKind::armourMeterTexture &&
              backgroundOnly.command(16U)->kind ==
                  LegacyAircraftHealthGaugeCommandKind::damageMaskQuad,
          "missing armour foreground suppressed or reordered the background");
}

void testNarrowScreenAnchor() {
  auto input = validInput();
  input.screenWidth = 639U;
  input.screenHeight = 240U;
  const auto narrow = buildLegacyAircraftHealthGaugePlan(input);
  require(narrow.ready() &&
              narrow.textureOrigin == LegacyCanvasPoint{8.0F, 6.0F},
          "sub-640 screen did not use the recovered top anchor");

  input.screenWidth = 640U;
  const auto threshold = buildLegacyAircraftHealthGaugePlan(input);
  require(threshold.ready() &&
              threshold.textureOrigin == LegacyCanvasPoint{8.0F, 104.0F},
          "640-wide screen did not use the bottom-relative anchor");
}

void testInvalidValuesFailAtomically() {
  auto input = validInput();
  input.screenWidth = 0U;
  requireRejected(input,
                  LegacyAircraftHealthGaugePlanStatus::invalidScreenExtent,
                  "zero screen extent was accepted");

  input = validInput();
  input.displayedHealth = std::numeric_limits<float>::quiet_NaN();
  requireRejected(input, LegacyAircraftHealthGaugePlanStatus::nonFiniteHealth,
                  "NaN displayed health was accepted");

  input = validInput();
  input.maximumHealth = 0.0F;
  requireRejected(input,
                  LegacyAircraftHealthGaugePlanStatus::maximumHealthNotPositive,
                  "zero maximum health was accepted");

  input = validInput();
  input.displayedHealth = 101.0F;
  requireRejected(
      input, LegacyAircraftHealthGaugePlanStatus::displayedHealthOutOfRange,
      "over-maximum displayed health was accepted");

  input.displayedHealth = -1.0F;
  requireRejected(
      input, LegacyAircraftHealthGaugePlanStatus::displayedHealthOutOfRange,
      "negative displayed health was accepted");
}

void testBuildDoesNotAllocate() {
  const auto input = validInput();
  allocationCount.store(0U, std::memory_order_relaxed);
  trackAllocations.store(true, std::memory_order_release);
  bool ready = true;
  std::size_t checksum = 0U;
  for (std::size_t index = 0U; index < 1024U; ++index) {
    const auto plan = buildLegacyAircraftHealthGaugePlan(input);
    ready = ready && plan.ready();
    checksum += plan.commandCount;
  }
  trackAllocations.store(false, std::memory_order_release);
  require(ready && checksum != 0U &&
              allocationCount.load(std::memory_order_relaxed) == 0U,
          "health-gauge plan allocated during steady-state builds");
}

} // namespace

int main() {
  try {
    testNativeGateOrder();
    testHalfHealthGeometryAndOrdering();
    testFullAndEmptyHealthBoundaries();
    testTextureLayersAreIndependent();
    testNarrowScreenAnchor();
    testInvalidValuesFailAtomically();
    testBuildDoesNotAllocate();
    std::cout << "LegacyAircraftHealthGaugePlan tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    trackAllocations.store(false, std::memory_order_release);
    std::cerr << "LegacyAircraftHealthGaugePlan tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
