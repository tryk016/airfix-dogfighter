#include "airfix/input/ControllerInputRuntimeConfiguration.hpp"

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using airfix::input::AnalogAxis;
using airfix::input::BindingTargetKind;
using airfix::input::ControllerInputProfileRecord;
using airfix::input::InputContext;
using airfix::input::SourceKind;

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] ControllerInputProfileRecord remappedRecord() {
  auto record = airfix::input::makeDefaultControllerInputProfileRecord();
  bool changed = false;
  for (std::size_t index = 0U; index < record.bindingCount; ++index) {
    auto &binding = record.bindings[index];
    if (binding.control == airfix::input::controls::controller::leftStickX &&
        binding.targetKind == BindingTargetKind::analog &&
        binding.target == static_cast<std::uint8_t>(AnalogAxis::flightBank)) {
      binding.target = static_cast<std::uint8_t>(AnalogAxis::flightPitch);
      changed = true;
      break;
    }
  }
  require(changed, "default left-stick binding was not found");
  record.axes[0U].inverted = 1U;
  return record;
}

[[nodiscard]] ControllerInputProfileRecord sparseRecord() {
  auto record = airfix::input::makeDefaultControllerInputProfileRecord();
  std::size_t writeIndex = 0U;
  for (std::size_t readIndex = 0U; readIndex < record.bindingCount;
       ++readIndex) {
    const auto &binding = record.bindings[readIndex];
    if (binding.control == airfix::input::controls::controller::rightStickX ||
        binding.control == airfix::input::controls::controller::faceLeft) {
      continue;
    }
    record.bindings[writeIndex] = binding;
    ++writeIndex;
  }
  require(writeIndex < record.bindingCount,
          "sparse profile did not remove any binding");
  for (std::size_t index = writeIndex; index < record.bindings.size();
       ++index) {
    record.bindings[index] = {};
  }
  record.bindingCount = static_cast<std::uint8_t>(writeIndex);
  return record;
}

void testConfigurationKeepsResolvedProfileAndCompiledBindingsTogether() {
  const auto record = remappedRecord();
  const auto resolved = airfix::input::resolveControllerInputProfile(record);
  require(resolved.complete(), "remapped profile did not resolve");

  const auto prepared =
      airfix::input::prepareControllerInputRuntimeConfiguration(
          *resolved.profile);
  require(prepared.complete(), "runtime configuration did not prepare");
  require(prepared.configuration->profile().record() == record,
          "runtime configuration changed the resolved profile");

  bool foundRemap = false;
  std::size_t controllerBindingCount = 0U;
  for (const auto &binding : prepared.configuration->bindings()) {
    if (binding.sourceKind != SourceKind::controller) {
      continue;
    }
    ++controllerBindingCount;
    if (binding.control == airfix::input::controls::controller::leftStickX &&
        binding.targetKind == BindingTargetKind::analog &&
        binding.target == static_cast<std::uint8_t>(AnalogAxis::flightPitch) &&
        (binding.contexts &
         airfix::input::contextMask(InputContext::gameplay)) != 0U) {
      foundRemap = true;
    }
  }
  require(foundRemap, "runtime configuration lost the remapped binding");
  require(controllerBindingCount == record.bindingCount,
          "runtime configuration changed controller binding count");
}

void testCanonicalDefaultPrepares() {
  const auto resolved = airfix::input::resolveControllerInputProfile(
      airfix::input::makeDefaultControllerInputProfileRecord());
  require(resolved.complete(), "default profile did not resolve");
  const auto prepared =
      airfix::input::prepareControllerInputRuntimeConfiguration(
          *resolved.profile);
  require(prepared.complete(), "default runtime configuration did not prepare");
  require(!prepared.issue.has_value(),
          "complete runtime configuration retained an issue");
}

void testConfigurationTracksOnlyMappedPhysicalControls() {
  const auto record = sparseRecord();
  const auto resolved = airfix::input::resolveControllerInputProfile(record);
  require(resolved.complete(), "sparse profile did not resolve");
  const auto prepared =
      airfix::input::prepareControllerInputRuntimeConfiguration(
          *resolved.profile);
  require(prepared.complete(), "sparse configuration did not prepare");

  require(!prepared.configuration->usesControllerControl(
              airfix::input::controls::controller::rightStickX),
          "configuration marked an unmapped axis as used");
  require(!prepared.configuration->usesControllerControl(
              airfix::input::controls::controller::faceLeft),
          "configuration marked an unmapped button as used");
  require(prepared.configuration->usesControllerControl(
              airfix::input::controls::controller::leftStickX),
          "configuration omitted a mapped axis");
  require(prepared.configuration->usesControllerControl(
              airfix::input::controls::controller::facePrimary),
          "configuration omitted a mapped button");
  require(!prepared.configuration->usesControllerControl({}),
          "configuration accepted an invalid physical control");
}

} // namespace

int main() {
  try {
    testConfigurationKeepsResolvedProfileAndCompiledBindingsTogether();
    testCanonicalDefaultPrepares();
    testConfigurationTracksOnlyMappedPhysicalControls();
  } catch (const std::exception &error) {
    std::cerr << "ControllerInputRuntimeConfigurationTests failed: "
              << error.what() << '\n';
    return 1;
  }

  std::cout << "ControllerInputRuntimeConfigurationTests passed\n";
  return 0;
}
