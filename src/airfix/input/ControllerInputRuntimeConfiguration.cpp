#include "airfix/input/ControllerInputRuntimeConfiguration.hpp"

namespace airfix::input {

ControllerInputRuntimeConfigurationResult
prepareControllerInputRuntimeConfiguration(
    const ResolvedControllerInputProfile &profile) noexcept {
  static_assert(controls::controller::leftStickX.value == 1U);
  static_assert(controls::controller::dpadRight.value ==
                ControllerInputRuntimeConfiguration::physicalControlCount);

  ControllerInputRuntimeConfigurationResult result;
  const auto compiled = compileControllerInputBindings(profile);
  if (!compiled.complete()) {
    result.issue = compiled.issue;
    return result;
  }

  std::array<bool, ControllerInputRuntimeConfiguration::physicalControlCount>
      usedControls{};
  for (const auto &binding : profile.bindings()) {
    const auto index = static_cast<std::size_t>(binding.control.value - 1U);
    usedControls[index] = true;
  }

  result.configuration = ControllerInputRuntimeConfiguration{
      profile, *compiled.bindings, usedControls};
  return result;
}

} // namespace airfix::input
