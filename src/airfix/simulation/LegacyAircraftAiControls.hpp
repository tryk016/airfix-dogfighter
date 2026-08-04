#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace airfix::simulation {

inline constexpr std::size_t legacyAircraftAiControlChannelCount = 8U;
inline constexpr float legacyAircraftAiRawMinimum = -100.0F;
inline constexpr float legacyAircraftAiRawMaximum = 100.0F;
inline constexpr std::uint32_t legacyAircraftAiHasChangedScaleBits =
    0x3C23D70AU;
inline constexpr std::uint64_t legacyAircraftAiGetRelativeScaleBits =
    0x3F847AE147AE147BULL;
inline constexpr std::uint64_t legacyAircraftAiHalfRangeBits =
    0x3FE0000000000000ULL;

enum class LegacyAircraftAiNumericPolicy : std::uint8_t {
  startupPc53RoundToNearestEven,
  unsupported,
};

enum class LegacyAircraftAiControlStatus : std::uint8_t {
  complete,
  invalidChannel,
  unsupportedNumericPolicy,
  numericEnvironmentUnavailable,
};

// Exact recovered 0x80-byte AIControls data layout. This value type contains
// no scheduler, task, target, vehicle, event queue, or platform state.
struct LegacyAircraftAiControlsState final {
  std::array<float, legacyAircraftAiControlChannelCount> minimum{};
  std::array<float, legacyAircraftAiControlChannelCount> maximum{};
  std::array<float, legacyAircraftAiControlChannelCount> raw{};
  std::array<float, legacyAircraftAiControlChannelCount> cachedRelative{};

  [[nodiscard]] friend constexpr bool
  operator==(const LegacyAircraftAiControlsState &,
             const LegacyAircraftAiControlsState &) noexcept = default;
};

static_assert(sizeof(LegacyAircraftAiControlsState) == 0x80U);
static_assert(offsetof(LegacyAircraftAiControlsState, minimum) == 0x00U);
static_assert(offsetof(LegacyAircraftAiControlsState, maximum) == 0x20U);
static_assert(offsetof(LegacyAircraftAiControlsState, raw) == 0x40U);
static_assert(offsetof(LegacyAircraftAiControlsState, cachedRelative) == 0x60U);

struct LegacyAircraftAiChangedResult final {
  LegacyAircraftAiControlStatus status{
      LegacyAircraftAiControlStatus::invalidChannel};
  bool changed{};

  [[nodiscard]] constexpr bool complete() const noexcept {
    return status == LegacyAircraftAiControlStatus::complete;
  }
};

struct LegacyAircraftAiRelativeResult final {
  LegacyAircraftAiControlStatus status{
      LegacyAircraftAiControlStatus::invalidChannel};
  std::optional<double> retainedValue;
  std::optional<std::uint32_t> cachedBinary32Bits;

  [[nodiscard]] constexpr bool complete() const noexcept {
    return status == LegacyAircraftAiControlStatus::complete &&
           retainedValue.has_value() && cachedBinary32Bits.has_value();
  }
};

// Stores the two binary32 range values exactly. The native function performs
// no ordering or finiteness validation; this bounded port differs only by
// rejecting an out-of-range channel index.
[[nodiscard]] bool
legacyAircraftAiSetRange(LegacyAircraftAiControlsState &state,
                         std::size_t channel, float minimum,
                         float maximum) noexcept;

// Reproduces the recovered comparison branches: finite input is clamped to
// [-100,+100], infinities select their endpoint, and unordered NaN selects
// -100. An invalid channel is rejected without mutation.
[[nodiscard]] bool
legacyAircraftAiSetValue(LegacyAircraftAiControlsState &state,
                         std::size_t channel, float value) noexcept;

// GetAbsolute returns the clamped raw array entry despite its legacy name.
[[nodiscard]] std::optional<float>
legacyAircraftAiGetAbsolute(const LegacyAircraftAiControlsState &state,
                            std::size_t channel) noexcept;

// AddValue performs the recovered GetAbsolute -> x87 add -> binary32 spill ->
// SetValue sequence. It fails closed when the requested numeric policy cannot
// be guaranteed by the current thread environment.
[[nodiscard]] LegacyAircraftAiControlStatus legacyAircraftAiAddValue(
    LegacyAircraftAiControlsState &state, std::size_t channel, float delta,
    LegacyAircraftAiNumericPolicy policy =
        LegacyAircraftAiNumericPolicy::startupPc53RoundToNearestEven) noexcept;

// HasChanged compares the cached binary32 value with the retained mapped
// candidate. It deliberately uses the recovered binary32 0.01 scale, unlike
// GetRelative. Equality and unordered comparisons report unchanged.
[[nodiscard]] LegacyAircraftAiChangedResult legacyAircraftAiHasChanged(
    const LegacyAircraftAiControlsState &state, std::size_t channel,
    LegacyAircraftAiNumericPolicy policy =
        LegacyAircraftAiNumericPolicy::startupPc53RoundToNearestEven) noexcept;

// GetRelative uses the recovered binary64 0.01 scale, spills one mapped value
// to cachedRelative, then recomputes and returns the retained PC53 result.
[[nodiscard]] LegacyAircraftAiRelativeResult legacyAircraftAiGetRelative(
    LegacyAircraftAiControlsState &state, std::size_t channel,
    LegacyAircraftAiNumericPolicy policy =
        LegacyAircraftAiNumericPolicy::startupPc53RoundToNearestEven) noexcept;

[[nodiscard]] bool legacyAircraftAiNumericPolicyAvailable(
    LegacyAircraftAiNumericPolicy policy) noexcept;

// Applies only the five exact AirCraft dispatcher ranges. It does not assign
// raw values or seed the cache and makes no claim about constructor defaults.
void legacyAircraftConfigureAiDispatcherRanges(
    LegacyAircraftAiControlsState &state) noexcept;

[[nodiscard]] bool legacyAircraftHasExactAiDispatcherRanges(
    const LegacyAircraftAiControlsState &state) noexcept;

} // namespace airfix::simulation
