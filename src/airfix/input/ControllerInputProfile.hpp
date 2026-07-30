#pragma once

#include "airfix/input/InputRouter.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace airfix::input {

inline constexpr std::uint32_t controllerInputProfileRecordSchemaVersion = 1U;
inline constexpr std::size_t controllerProfileAxisCount = 4U;
inline constexpr std::size_t controllerProfileBindingCapacity = 48U;
inline constexpr std::size_t controllerInputProfileNoIndex =
    std::numeric_limits<std::size_t>::max();

enum class ControllerAxisElement : std::uint8_t {
  leftStickX = 0,
  leftStickY = 1,
  rightStickX = 2,
  rightStickY = 3,
  count = 4,
};

enum class ControllerResponseCurve : std::uint8_t {
  linear = 0,
  squared = 1,
  cubic = 2,
  count = 3,
};

// Storage-neutral semantic fields. A codec must serialize named values rather
// than the in-memory representation. The default transform is exact identity
// for every valid symmetric-Q15 input.
struct ControllerAxisCalibrationRecord final {
  std::uint16_t innerDeadzoneQ15{};
  std::uint16_t outerSaturationQ15{static_cast<std::uint16_t>(q15One)};
  std::uint16_t sensitivityPermille{1000U};
  ControllerResponseCurve responseCurve{ControllerResponseCurve::linear};
  std::uint8_t inverted{};

  [[nodiscard]] friend constexpr bool
  operator==(const ControllerAxisCalibrationRecord &,
             const ControllerAxisCalibrationRecord &) noexcept = default;
};

// Records deliberately retain SourceKind so malformed or forged documents can
// be rejected instead of silently acquiring another input source. V1 accepts
// only SourceKind::controller and the standardized controller ControlIds.
struct ControllerBindingRecord final {
  SourceKind sourceKind{SourceKind::none};
  ControlId control{};
  PhysicalEventKind physicalKind{PhysicalEventKind::digital};
  BindingTargetKind targetKind{BindingTargetKind::digital};
  std::uint8_t target{};
  ContextMask contexts{};
  Q15 scale{q15One};
  Q15 meaningfulThreshold{1};
  std::uint8_t blocksNeutralGate{1U};

  [[nodiscard]] friend constexpr bool
  operator==(const ControllerBindingRecord &,
             const ControllerBindingRecord &) noexcept = default;
};

// Fixed-capacity and allocation-free. Entries at and after bindingCount must
// remain default-initialized so ignored tail bytes cannot carry hidden state.
struct ControllerInputProfileRecord final {
  std::uint32_t schemaVersion{controllerInputProfileRecordSchemaVersion};
  std::array<ControllerAxisCalibrationRecord, controllerProfileAxisCount>
      axes{};
  std::array<ControllerBindingRecord, controllerProfileBindingCapacity>
      bindings{};
  std::uint8_t bindingCount{};

  [[nodiscard]] friend constexpr bool
  operator==(const ControllerInputProfileRecord &,
             const ControllerInputProfileRecord &) noexcept = default;
};

enum class ControllerInputProfileIssueKind : std::uint8_t {
  unsupportedSchema = 0,
  invalidInnerDeadzone,
  invalidOuterSaturation,
  invalidSensitivity,
  invalidResponseCurve,
  invalidInversion,
  bindingCountOutOfRange,
  nonCanonicalUnusedBinding,
  nonControllerSource,
  invalidControl,
  invalidPhysicalKind,
  physicalControlKindMismatch,
  invalidTargetKind,
  invalidTarget,
  invalidContexts,
  invalidScale,
  invalidMeaningfulThreshold,
  invalidBlocksNeutralGate,
  triggerRequiresBinaryBinding,
  contextConflict,
  bindingTableCapacityExceeded,
  missingPause,
  missingMenuConfirm,
  missingMenuCancel,
  missingMenuNavigateX,
  missingMenuNavigateY,
  bindingCompilationFailed,
};

struct ControllerInputProfileIssue final {
  ControllerInputProfileIssueKind kind{
      ControllerInputProfileIssueKind::unsupportedSchema};
  // Axis or binding index where applicable; noIndex for profile-wide issues.
  std::size_t index{controllerInputProfileNoIndex};

  [[nodiscard]] friend constexpr bool
  operator==(const ControllerInputProfileIssue &,
             const ControllerInputProfileIssue &) noexcept = default;
};

struct ControllerInputProfileResolveResult;

class ResolvedControllerInputProfile final {
public:
  ResolvedControllerInputProfile(
      const ResolvedControllerInputProfile &) noexcept = default;
  ResolvedControllerInputProfile &
  operator=(const ResolvedControllerInputProfile &) noexcept = default;

  [[nodiscard]] constexpr std::uint32_t schemaVersion() const noexcept {
    return record_.schemaVersion;
  }

  [[nodiscard]] constexpr const ControllerAxisCalibrationRecord *
  axisCalibration(const ControllerAxisElement axis) const noexcept {
    const auto index = static_cast<std::size_t>(axis);
    return index < record_.axes.size() ? &record_.axes[index] : nullptr;
  }

  [[nodiscard]] constexpr std::span<const ControllerBindingRecord>
  bindings() const noexcept {
    return std::span<const ControllerBindingRecord>{
        record_.bindings.data(),
        static_cast<std::size_t>(record_.bindingCount),
    };
  }

  [[nodiscard]] constexpr const ControllerInputProfileRecord &
  record() const noexcept {
    return record_;
  }

private:
  explicit constexpr ResolvedControllerInputProfile(
      const ControllerInputProfileRecord &record) noexcept
      : record_(record) {}

  ControllerInputProfileRecord record_{};

  friend struct ControllerInputProfileResolveResult;
  friend ControllerInputProfileResolveResult
  resolveControllerInputProfile(const ControllerInputProfileRecord &) noexcept;
};

struct ControllerInputProfileResolveResult final {
  std::optional<ResolvedControllerInputProfile> profile;
  std::optional<ControllerInputProfileIssue> issue;

  [[nodiscard]] constexpr bool complete() const noexcept {
    return profile.has_value() && !issue.has_value();
  }
};

struct ControllerInputProfileCompileResult final {
  std::optional<BindingTable> bindings;
  std::optional<ControllerInputProfileIssue> issue;

  [[nodiscard]] constexpr bool complete() const noexcept {
    return bindings.has_value() && !issue.has_value();
  }
};

[[nodiscard]] ControllerInputProfileRecord
makeDefaultControllerInputProfileRecord() noexcept;

// Validation is strict and atomic. No resolved profile is returned when any
// field, conflict, recovery binding, or final table capacity is invalid.
[[nodiscard]] ControllerInputProfileResolveResult resolveControllerInputProfile(
    const ControllerInputProfileRecord &record) noexcept;

// Preserves all default non-controller bindings, removes all default
// controller bindings, and atomically appends the resolved controller profile.
[[nodiscard]] ControllerInputProfileCompileResult
compileControllerInputBindings(
    const ResolvedControllerInputProfile &profile) noexcept;

// Deterministic integer-only calibration. Each division rounds to nearest,
// ties to even. Invalid -32768 or a forged axis enum returns no value.
[[nodiscard]] std::optional<Q15>
transformControllerAxis(Q15 raw, ControllerAxisElement axis,
                        const ResolvedControllerInputProfile &profile) noexcept;

} // namespace airfix::input
