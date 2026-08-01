#include "airfix/render/LegacyAircraftHudInstruments.hpp"

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
using namespace recovered_legacy_aircraft_hud_instruments;

static_assert(noexcept(buildLegacyAircraftHudInstrumentsPlan({})));
static_assert(maximumCommands == 4U);

void require(const bool condition, const char *const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] bool close(const float actual, const float expected,
                         const float tolerance = 2.0e-4F) noexcept {
  return std::fabs(actual - expected) <= tolerance;
}

[[nodiscard]] LegacyAircraftHudInstrumentsInput validInput() noexcept {
  return {
      .activeWindowPresent = false,
      .cameraAttachedAtEntry = true,
      .typeHudEnabled = true,
      .cameraAttachedAfterLayout = true,
      .screenWidth = 640U,
      .screenHeight = 480U,
      .rightNormalizedValue = 0.5F,
      .leftNormalizedValue = 0.5F,
      .leftTintArgb = 0xFF804020U,
      .rightFaceTextureAvailable = true,
      .leftFaceTextureAvailable = true,
      .indicatorTextureAvailable = true,
  };
}

void requireRejected(const LegacyAircraftHudInstrumentsInput &input,
                     const LegacyAircraftHudInstrumentsPlanStatus status,
                     const char *const message) {
  const auto plan = buildLegacyAircraftHudInstrumentsPlan(input);
  require(!plan.ready() && plan.status == status && plan.commandCount == 0U &&
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
                  LegacyAircraftHudInstrumentsPlanStatus::activeWindowPresent,
                  "active-window gate was not first");
  input.activeWindowPresent = false;
  requireRejected(
      input, LegacyAircraftHudInstrumentsPlanStatus::cameraNotAttachedAtEntry,
      "entry camera gate was not second");
  input.cameraAttachedAtEntry = true;
  requireRejected(input,
                  LegacyAircraftHudInstrumentsPlanStatus::typeHudDisabled,
                  "type HUD gate was not third");
  input.typeHudEnabled = true;
  requireRejected(
      input, LegacyAircraftHudInstrumentsPlanStatus::cameraDetachedBeforeDraw,
      "repeated camera gate was not preserved");
}

void testNeutralGeometryAndOrder() {
  const auto plan = buildLegacyAircraftHudInstrumentsPlan(validInput());
  require(plan.ready() && plan.commandCount == 4U &&
              plan.sourceScreenWidth == 640U && plan.sourceScreenHeight == 480U,
          "neutral instrument plan was rejected");

  const auto *rightFace = plan.command(0U);
  const auto *rightIndicator = plan.command(1U);
  const auto *leftFace = plan.command(2U);
  const auto *leftIndicator = plan.command(3U);
  require(rightFace != nullptr && rightIndicator != nullptr &&
              leftFace != nullptr && leftIndicator != nullptr &&
              plan.command(4U) == nullptr,
          "instrument command lookup was not bounded");
  require(rightFace->side == LegacyAircraftHudInstrumentSide::right &&
              rightFace->kind == LegacyAircraftHudInstrumentCommandKind::face &&
              rightIndicator->side == LegacyAircraftHudInstrumentSide::right &&
              rightIndicator->kind ==
                  LegacyAircraftHudInstrumentCommandKind::indicator &&
              leftFace->side == LegacyAircraftHudInstrumentSide::left &&
              leftFace->kind == LegacyAircraftHudInstrumentCommandKind::face &&
              leftIndicator->side == LegacyAircraftHudInstrumentSide::left &&
              leftIndicator->kind ==
                  LegacyAircraftHudInstrumentCommandKind::indicator,
          "native right-then-left face/indicator order changed");

  require(
      rightFace->destinationQuad[0U] == LegacyCanvasPoint{426.0F, 408.0F} &&
          rightFace->destinationQuad[2U] == LegacyCanvasPoint{490.0F, 472.0F} &&
          leftFace->destinationQuad[0U] == LegacyCanvasPoint{150.0F, 408.0F} &&
          leftFace->destinationQuad[2U] == LegacyCanvasPoint{214.0F, 472.0F},
      "recovered face placement changed");
  require(rightIndicator->destinationQuad[0U] ==
                  LegacyCanvasPoint{455.0F, 417.0F} &&
              rightIndicator->destinationQuad[1U] ==
                  LegacyCanvasPoint{462.0F, 417.0F} &&
              rightIndicator->destinationQuad[2U] ==
                  LegacyCanvasPoint{462.0F, 447.0F} &&
              rightIndicator->destinationQuad[3U] ==
                  LegacyCanvasPoint{455.0F, 447.0F},
          "neutral rotated-indicator quad changed");
  require(rightFace->tintArgb == rightInstrumentTintArgb &&
              rightIndicator->tintArgb == rightInstrumentTintArgb &&
              leftFace->tintArgb == 0xFF804020U &&
              leftIndicator->tintArgb == 0xFF804020U,
          "native tint ownership changed");
}

void testIndependentMissingTextures() {
  auto input = validInput();
  input.rightFaceTextureAvailable = false;
  const auto noRightFace = buildLegacyAircraftHudInstrumentsPlan(input);
  require(noRightFace.ready() && noRightFace.commandCount == 3U &&
              noRightFace.command(0U)->kind ==
                  LegacyAircraftHudInstrumentCommandKind::indicator,
          "missing right face did not preserve its indicator");

  input = validInput();
  input.indicatorTextureAvailable = false;
  const auto facesOnly = buildLegacyAircraftHudInstrumentsPlan(input);
  require(facesOnly.ready() && facesOnly.commandCount == 2U &&
              facesOnly.command(0U)->side ==
                  LegacyAircraftHudInstrumentSide::right &&
              facesOnly.command(1U)->side ==
                  LegacyAircraftHudInstrumentSide::left,
          "missing shared indicator did not preserve both faces");

  input.rightFaceTextureAvailable = false;
  input.leftFaceTextureAvailable = false;
  const auto empty = buildLegacyAircraftHudInstrumentsPlan(input);
  require(empty.ready() && empty.commandCount == 0U,
          "all missing textures did not produce an empty ready plan");
}

void testFiniteExtrapolationAndFailures() {
  auto input = validInput();
  input.rightNormalizedValue = -2.0F;
  input.leftNormalizedValue = 3.0F;
  const auto extrapolated = buildLegacyAircraftHudInstrumentsPlan(input);
  require(extrapolated.ready() && extrapolated.commandCount == 4U,
          "finite native extrapolation was incorrectly clamped or rejected");
  for (std::size_t index = 0U; index < extrapolated.commandCount; ++index) {
    for (const auto point : extrapolated.command(index)->destinationQuad) {
      require(std::isfinite(point.x) && std::isfinite(point.y),
              "finite extrapolation produced non-finite geometry");
    }
  }

  input = validInput();
  input.screenWidth = 0U;
  requireRejected(input,
                  LegacyAircraftHudInstrumentsPlanStatus::invalidScreenExtent,
                  "zero screen extent was accepted");
  input = validInput();
  input.rightNormalizedValue = std::numeric_limits<float>::quiet_NaN();
  requireRejected(
      input, LegacyAircraftHudInstrumentsPlanStatus::normalizedValueNotFinite,
      "NaN right value was accepted");
  input = validInput();
  input.leftNormalizedValue = std::numeric_limits<float>::infinity();
  requireRejected(
      input, LegacyAircraftHudInstrumentsPlanStatus::normalizedValueNotFinite,
      "infinite left value was accepted");
}

void testAllocationFree() {
  allocationCount.store(0U, std::memory_order_relaxed);
  trackAllocations.store(true, std::memory_order_relaxed);
  const auto plan = buildLegacyAircraftHudInstrumentsPlan(validInput());
  trackAllocations.store(false, std::memory_order_relaxed);
  require(plan.ready() && allocationCount.load(std::memory_order_relaxed) == 0U,
          "instrument planning allocated memory");
}

} // namespace

int main() {
  try {
    testNativeGateOrder();
    testNeutralGeometryAndOrder();
    testIndependentMissingTextures();
    testFiniteExtrapolationAndFailures();
    testAllocationFree();
    std::cout << "LegacyAircraftHudInstruments tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "LegacyAircraftHudInstruments tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
