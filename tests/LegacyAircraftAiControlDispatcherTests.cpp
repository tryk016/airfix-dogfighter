#include "airfix/simulation/LegacyAircraftAiControlDispatcher.hpp"

#include <array>
#include <bit>
#include <cfenv>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

using airfix::simulation::LegacyAircraftAiControlsState;
using airfix::simulation::LegacyAircraftAiDispatchedEvent;
using airfix::simulation::LegacyAircraftAiDispatchStatus;
using airfix::simulation::LegacyAircraftAiNativeEvent;

void require(const bool condition, const char *const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

enum class CallbackKind : std::uint8_t { save, process };

struct CallbackRecord final {
  CallbackKind kind{CallbackKind::save};
  LegacyAircraftAiDispatchedEvent event;
};

struct SinkContext final {
  LegacyAircraftAiControlsState *controls{};
  std::array<CallbackRecord, 16U> records{};
  std::size_t recordCount{};
  bool cachePublishedBeforeSave{true};
  bool mutatePitchToCachedMinimum{};
  bool corruptRangesAfterFirstProcess{};
};

[[nodiscard]] std::uint32_t
expectedCacheBits(const LegacyAircraftAiNativeEvent event) noexcept {
  switch (event) {
  case LegacyAircraftAiNativeEvent::thrustSet:
    return 0x42FF0000U;
  case LegacyAircraftAiNativeEvent::pitchSet:
  case LegacyAircraftAiNativeEvent::bankSet:
  case LegacyAircraftAiNativeEvent::primaryAttack:
  case LegacyAircraftAiNativeEvent::secondaryAttack:
    return 0x00000000U;
  }
  return 0U;
}

void saveCallback(void *const rawContext,
                  const LegacyAircraftAiDispatchedEvent &event) noexcept {
  auto &context = *static_cast<SinkContext *>(rawContext);
  if (context.recordCount < context.records.size()) {
    context.records[context.recordCount++] = {CallbackKind::save, event};
  }
  if (context.controls == nullptr ||
      std::bit_cast<std::uint32_t>(
          context.controls->cachedRelative[event.channel]) !=
          expectedCacheBits(event.event)) {
    context.cachePublishedBeforeSave = false;
  }
}

void processCallback(void *const rawContext,
                     const LegacyAircraftAiDispatchedEvent &event) noexcept {
  auto &context = *static_cast<SinkContext *>(rawContext);
  if (context.recordCount < context.records.size()) {
    context.records[context.recordCount++] = {CallbackKind::process, event};
  }
  if (context.controls == nullptr ||
      event.event != LegacyAircraftAiNativeEvent::thrustSet) {
    return;
  }
  if (context.mutatePitchToCachedMinimum) {
    static_cast<void>(airfix::simulation::legacyAircraftAiSetValue(
        *context.controls, 1U, -100.0F));
  }
  if (context.corruptRangesAfterFirstProcess) {
    context.controls->maximum[1U] = 31.0F;
  }
}

[[nodiscard]] airfix::simulation::LegacyAircraftAiEventSink
sinkFor(SinkContext &context) noexcept {
  return {
      .context = &context,
      .save = saveCallback,
      .process = processCallback,
  };
}

void seedMaximumCache(LegacyAircraftAiControlsState &state) {
  for (const std::size_t channel : {0U, 1U, 3U, 5U, 6U}) {
    require(
        airfix::simulation::legacyAircraftAiSetValue(state, channel, 100.0F),
        "maximum cache setup failed");
    require(airfix::simulation::legacyAircraftAiGetRelative(state, channel)
                .complete(),
            "maximum cache seed failed");
  }
}

void seedMinimumCache(LegacyAircraftAiControlsState &state) {
  for (const std::size_t channel : {0U, 1U, 3U, 5U, 6U}) {
    require(
        airfix::simulation::legacyAircraftAiSetValue(state, channel, -100.0F),
        "minimum cache setup failed");
    require(airfix::simulation::legacyAircraftAiGetRelative(state, channel)
                .complete(),
            "minimum cache seed failed");
  }
}

void setWitnessRaw(LegacyAircraftAiControlsState &state) {
  require(
      airfix::simulation::legacyAircraftAiSetValue(state, 0U, 50.0F) &&
          airfix::simulation::legacyAircraftAiSetValue(state, 1U, 0.0F) &&
          airfix::simulation::legacyAircraftAiSetValue(state, 3U, 0.0F) &&
          airfix::simulation::legacyAircraftAiSetValue(state, 5U, -100.0F) &&
          airfix::simulation::legacyAircraftAiSetValue(state, 6U, -100.0F),
      "witness raw setup failed");
}

void testFiveEventWitnessOrderAndPayloads() {
  LegacyAircraftAiControlsState state;
  airfix::simulation::legacyAircraftConfigureAiDispatcherRanges(state);
  seedMaximumCache(state);
  setWitnessRaw(state);

  SinkContext context{.controls = &state};
  const auto result = airfix::simulation::legacyAircraftDispatchAiControlEvents(
      state, sinkFor(context));
  require(result.completed() && result.emittedCount == 5U,
          "five-event witness did not complete");
  require(context.recordCount == 10U && context.cachePublishedBeforeSave,
          "callbacks were missing or observed a stale cache");

  constexpr std::array<LegacyAircraftAiDispatchedEvent, 5U> expected{{
      {LegacyAircraftAiNativeEvent::thrustSet, 0U, 128},
      {LegacyAircraftAiNativeEvent::pitchSet, 1U, 0},
      {LegacyAircraftAiNativeEvent::bankSet, 3U, 0},
      {LegacyAircraftAiNativeEvent::primaryAttack, 5U, 0},
      {LegacyAircraftAiNativeEvent::secondaryAttack, 6U, 0},
  }};
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    require(context.records[index * 2U].kind == CallbackKind::save &&
                context.records[index * 2U].event == expected[index] &&
                context.records[index * 2U + 1U].kind ==
                    CallbackKind::process &&
                context.records[index * 2U + 1U].event == expected[index],
            "save/process ordering or witness payload changed");
  }
}

void testStableMinimumProducesNoEvents() {
  LegacyAircraftAiControlsState state;
  airfix::simulation::legacyAircraftConfigureAiDispatcherRanges(state);
  seedMinimumCache(state);
  SinkContext context{.controls = &state};
  const auto result = airfix::simulation::legacyAircraftDispatchAiControlEvents(
      state, sinkFor(context));
  require(result.completed() && result.emittedCount == 0U &&
              context.recordCount == 0U,
          "stable minimum controls emitted an event");
}

void testRawZeroCanRepeatUnderRecoveredPolicy() {
  LegacyAircraftAiControlsState state;
  airfix::simulation::legacyAircraftConfigureAiDispatcherRanges(state);
  seedMinimumCache(state);
  require(airfix::simulation::legacyAircraftAiSetValue(state, 1U, 0.0F),
          "zero-repeat setup failed");
  SinkContext context{.controls = &state};
  const auto sink = sinkFor(context);
  const auto first =
      airfix::simulation::legacyAircraftDispatchAiControlEvents(state, sink);
  const auto second =
      airfix::simulation::legacyAircraftDispatchAiControlEvents(state, sink);
  require(first.completed() && second.completed() && first.emittedCount == 1U &&
              second.emittedCount == 1U && context.recordCount == 4U,
          "identical raw zero was incorrectly deduplicated");
  for (std::size_t index = 0U; index < context.recordCount; ++index) {
    const auto &record = context.records[index];
    require(record.event.event == LegacyAircraftAiNativeEvent::pitchSet &&
                record.event.payload == 0,
            "zero-repeat dispatcher emitted the wrong event");
  }
}

void testProcessMutationAffectsNextChannel() {
  LegacyAircraftAiControlsState state;
  airfix::simulation::legacyAircraftConfigureAiDispatcherRanges(state);
  seedMaximumCache(state);
  require(
      airfix::simulation::legacyAircraftAiSetValue(state, 1U, -100.0F) &&
          airfix::simulation::legacyAircraftAiGetRelative(state, 1U).complete(),
      "pitch minimum cache setup failed");
  setWitnessRaw(state);
  SinkContext context{
      .controls = &state,
      .mutatePitchToCachedMinimum = true,
  };
  const auto result = airfix::simulation::legacyAircraftDispatchAiControlEvents(
      state, sinkFor(context));
  require(result.completed() && result.emittedCount == 4U,
          "process-side mutation was not visible to the next channel");
  for (std::size_t index = 0U; index < context.recordCount; ++index) {
    require(context.records[index].event.event !=
                LegacyAircraftAiNativeEvent::pitchSet,
            "dispatcher pre-batched pitch before thrust Process");
  }
}

void testInvalidPreconditionsFailBeforeMutation() {
  LegacyAircraftAiControlsState state;
  airfix::simulation::legacyAircraftConfigureAiDispatcherRanges(state);
  seedMaximumCache(state);
  setWitnessRaw(state);
  const auto before = state;
  SinkContext context{.controls = &state};

  auto invalidSink = sinkFor(context);
  invalidSink.process = nullptr;
  const auto missingCallback =
      airfix::simulation::legacyAircraftDispatchAiControlEvents(state,
                                                                invalidSink);
  require(missingCallback.status ==
                  LegacyAircraftAiDispatchStatus::invalidSink &&
              missingCallback.emittedCount == 0U && state == before &&
              context.recordCount == 0U,
          "invalid sink caused partial dispatch");

  state.maximum[3U] = 31.0F;
  const auto invalidRange =
      airfix::simulation::legacyAircraftDispatchAiControlEvents(
          state, sinkFor(context));
  require(invalidRange.status ==
                  LegacyAircraftAiDispatchStatus::invalidConfiguration &&
              invalidRange.emittedCount == 0U && context.recordCount == 0U,
          "invalid range caused partial dispatch");
}

void testCallbackCorruptionStopsAfterCompletedRow() {
  LegacyAircraftAiControlsState state;
  airfix::simulation::legacyAircraftConfigureAiDispatcherRanges(state);
  seedMaximumCache(state);
  setWitnessRaw(state);
  SinkContext context{
      .controls = &state,
      .corruptRangesAfterFirstProcess = true,
  };
  const auto result = airfix::simulation::legacyAircraftDispatchAiControlEvents(
      state, sinkFor(context));
  require(result.status ==
                  LegacyAircraftAiDispatchStatus::invalidConfiguration &&
              result.emittedCount == 1U && context.recordCount == 2U,
          "mid-dispatch corruption did not preserve the immediate boundary");
}

void testNumericEnvironmentFailsBeforeCallbacks() {
  LegacyAircraftAiControlsState state;
  airfix::simulation::legacyAircraftConfigureAiDispatcherRanges(state);
  seedMaximumCache(state);
  setWitnessRaw(state);
  const auto before = state;
  SinkContext context{.controls = &state};
  const int originalRounding = std::fegetround();
  if (originalRounding != -1 && std::fesetround(FE_DOWNWARD) == 0) {
    const auto result =
        airfix::simulation::legacyAircraftDispatchAiControlEvents(
            state, sinkFor(context));
    require(
        result.status ==
                LegacyAircraftAiDispatchStatus::numericEnvironmentUnavailable &&
            result.emittedCount == 0U && context.recordCount == 0U &&
            state == before,
        "non-nearest environment caused partial dispatch");
    require(std::fesetround(originalRounding) == 0,
            "rounding environment could not be restored");
  }
}

} // namespace

int main() {
  try {
    testFiveEventWitnessOrderAndPayloads();
    testStableMinimumProducesNoEvents();
    testRawZeroCanRepeatUnderRecoveredPolicy();
    testProcessMutationAffectsNextChannel();
    testInvalidPreconditionsFailBeforeMutation();
    testCallbackCorruptionStopsAfterCompletedRow();
    testNumericEnvironmentFailsBeforeCallbacks();
    std::cout << "Legacy aircraft AI control dispatcher tests passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
