#include "AirfixWindowsUiAutomation.hpp"

// clang-format off
#include <Windows.h>
#include <Ole2.h>
#include <UIAutomation.h>
#include <OleAuto.h>
// clang-format on

#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

void require(const bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Interface> class ComPointer final {
public:
  ComPointer() = default;
  ComPointer(const ComPointer &) = delete;
  ComPointer &operator=(const ComPointer &) = delete;
  ComPointer(ComPointer &&other) noexcept
      : value_{std::exchange(other.value_, nullptr)} {}
  ComPointer &operator=(ComPointer &&other) noexcept {
    if (this == &other) {
      return *this;
    }
    if (value_ != nullptr) {
      value_->Release();
    }
    value_ = std::exchange(other.value_, nullptr);
    return *this;
  }
  ~ComPointer() {
    if (value_ != nullptr) {
      value_->Release();
    }
  }

  [[nodiscard]] Interface **put() noexcept { return &value_; }
  [[nodiscard]] Interface *get() const noexcept { return value_; }
  [[nodiscard]] Interface *operator->() const noexcept { return value_; }

private:
  Interface *value_{};
};

LRESULT CALLBACK testWindowProcedure(HWND window, UINT message, WPARAM wParam,
                                     LPARAM lParam) noexcept {
  return DefWindowProcW(window, message, wParam, lParam);
}

class TestWindow final {
public:
  TestWindow() {
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW definition{};
    definition.lpfnWndProc = testWindowProcedure;
    definition.hInstance = instance;
    definition.lpszClassName = L"AirfixUiAutomationTestWindow";
    atom_ = RegisterClassW(&definition);
    require(atom_ != 0U || GetLastError() == ERROR_CLASS_ALREADY_EXISTS,
            "test window class registration failed");
    window_ =
        CreateWindowExW(0U, definition.lpszClassName,
                        L"Airfix UI Automation test", WS_OVERLAPPEDWINDOW, 40,
                        40, 960, 540, nullptr, nullptr, instance, nullptr);
    require(window_ != nullptr, "test window creation failed");
  }

  ~TestWindow() {
    if (window_ != nullptr) {
      DestroyWindow(window_);
    }
    if (atom_ != 0U) {
      UnregisterClassW(L"AirfixUiAutomationTestWindow",
                       GetModuleHandleW(nullptr));
    }
  }

  [[nodiscard]] HWND get() const noexcept { return window_; }

  void destroy() noexcept {
    if (window_ != nullptr) {
      DestroyWindow(window_);
      window_ = nullptr;
    }
  }

private:
  HWND window_{};
  ATOM atom_{};
};

[[nodiscard]] const airfix::windows::AirfixWindowsUiSemanticNode &
findItemNode(const airfix::windows::AirfixWindowsUiSemanticTree &tree,
             const airfix::windows::AirfixWindowsRenderSettingsItem item,
             const airfix::windows::AirfixWindowsUiSemanticRole role) {
  for (std::uint8_t index = 0U; index < tree.nodeCount; ++index) {
    if (tree.nodes[index].item == item && tree.nodes[index].role == role) {
      return tree.nodes[index];
    }
  }
  throw std::runtime_error("semantic item node was not found");
}

[[nodiscard]] ComPointer<IUIAutomationElement>
findAutomationId(IUIAutomation &automation, IUIAutomationElement &root,
                 const std::uint16_t runtimeId) {
  wchar_t identifier[32]{};
  const int length =
      swprintf_s(identifier, L"airfix-%u", static_cast<unsigned>(runtimeId));
  require(length > 0, "automation identifier formatting failed");

  VARIANT value{};
  VariantInit(&value);
  value.vt = VT_BSTR;
  value.bstrVal = SysAllocString(identifier);
  require(value.bstrVal != nullptr, "automation identifier allocation failed");
  ComPointer<IUIAutomationCondition> condition;
  const HRESULT conditionResult = automation.CreatePropertyCondition(
      UIA_AutomationIdPropertyId, value, condition.put());
  VariantClear(&value);
  require(SUCCEEDED(conditionResult) && condition.get() != nullptr,
          "automation property condition failed");

  ComPointer<IUIAutomationElement> element;
  require(SUCCEEDED(root.FindFirst(TreeScope_Subtree, condition.get(),
                                   element.put())) &&
              element.get() != nullptr,
          "automation element was not found");
  return element;
}

[[nodiscard]] std::optional<airfix::windows::AirfixWindowsUiAutomationAction>
waitForAction(
    airfix::windows::AirfixWindowsUiAutomationHost &host,
    const airfix::windows::AirfixWindowsAccessibilityAction expected) {
  for (std::size_t attempt = 0U; attempt < 2'000U; ++attempt) {
    while (const auto action = host.popAction()) {
      if (action->action == expected) {
        return action;
      }
      require(action->action ==
                  airfix::windows::AirfixWindowsAccessibilityAction::focus,
              "UI Automation produced an unexpected action");
    }
    Sleep(1U);
  }
  return std::nullopt;
}

void run() {
  using namespace airfix::windows;

  AirfixWindowsUiAutomationActionQueue queue;
  for (std::size_t index = 0U; index < airfixWindowsUiAutomationActionCapacity;
       ++index) {
    require(queue.tryPush({
                .screen = AirfixWindowsRenderSettingsScreen::displaySettings,
                .accessibilityGeneration = index + 1U,
                .item = AirfixWindowsRenderSettingsItem::renderScale,
                .action = AirfixWindowsAccessibilityAction::increment,
            }),
            "bounded UI Automation queue filled too early");
  }
  require(!queue.tryPush({}),
          "bounded UI Automation queue accepted an overflow action");
  require(queue.size() == airfixWindowsUiAutomationActionCapacity,
          "bounded UI Automation queue count was incorrect");
  for (std::size_t index = 0U; index < airfixWindowsUiAutomationActionCapacity;
       ++index) {
    const auto action = queue.pop();
    require(action.has_value() && action->accessibilityGeneration == index + 1U,
            "bounded UI Automation queue lost FIFO ordering");
  }
  require(!queue.pop().has_value(),
          "bounded UI Automation queue did not drain exactly");
  require(queue.tryPush({}),
          "bounded UI Automation queue did not accept after drain");
  queue.clear();
  require(queue.size() == 0U && !queue.pop().has_value(),
          "bounded UI Automation queue clear failed");

  auto panel = AirfixWindowsRenderSettingsPanel::create(
      {}, true, {.width = 960U, .height = 540U, .dpiScale = 1.0F});
  require(panel.has_value(), "settings panel creation failed");
  const auto pause = panel->snapshot();
  const auto openDisplay = panel->consumeAccessibilityAction(
      pause.screen, pause.accessibilityGeneration,
      AirfixWindowsRenderSettingsItem::displaySettings,
      AirfixWindowsAccessibilityAction::invoke);
  require(openDisplay.accepted(), "display screen activation failed");

  const auto built = buildAirfixWindowsUiSemanticTree(panel->snapshot());
  require(built.complete(), "semantic tree construction failed");
  const auto tree = *built.tree;
  const auto &renderScale =
      findItemNode(tree, AirfixWindowsRenderSettingsItem::renderScale,
                   AirfixWindowsUiSemanticRole::adjustableValue);
  const auto &increaseRenderScale =
      findItemNode(tree, AirfixWindowsRenderSettingsItem::renderScale,
                   AirfixWindowsUiSemanticRole::incrementButton);

  TestWindow window;
  AirfixWindowsUiAutomationHost host;
  require(host.attach(window.get()), "UI Automation host attachment failed");
  require(host.attached(), "UI Automation host did not report attachment");
  require(host.publish(tree), "semantic publication failed");

  {
    ComPointer<IUIAutomation> automation;
    require(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr,
                                       CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(automation.put()))) &&
                automation.get() != nullptr,
            "UI Automation client creation failed");
    ComPointer<IUIAutomationElement> root;
    require(
        SUCCEEDED(automation->ElementFromHandle(window.get(), root.put())) &&
            root.get() != nullptr,
        "UI Automation root discovery failed");

    BSTR rootName{};
    require(SUCCEEDED(root->get_CurrentName(&rootName)) && rootName != nullptr,
            "UI Automation root name was unavailable");
    const std::wstring_view rootNameView{rootName, SysStringLen(rootName)};
    require(rootNameView == L"Airfix Dogfighter settings",
            "UI Automation root name was incorrect");
    SysFreeString(rootName);

    auto statusElement = findAutomationId(*automation.get(), *root.get(), 3U);
    VARIANT liveSetting{};
    VariantInit(&liveSetting);
    require(SUCCEEDED(statusElement->GetCurrentPropertyValue(
                UIA_LiveSettingPropertyId, &liveSetting)) &&
                liveSetting.vt == VT_I4 &&
                liveSetting.lVal == static_cast<long>(LiveSetting::Polite),
            "UI Automation status was not a polite live region");
    VariantClear(&liveSetting);

    auto valueElement =
        findAutomationId(*automation.get(), *root.get(), renderScale.runtimeId);
    require(SUCCEEDED(valueElement->SetFocus()),
            "UI Automation focus request failed");
    const auto focus =
        waitForAction(host, AirfixWindowsAccessibilityAction::focus);
    require(focus.has_value() &&
                *focus ==
                    AirfixWindowsUiAutomationAction{
                        .screen = tree.screen,
                        .accessibilityGeneration = tree.accessibilityGeneration,
                        .item = AirfixWindowsRenderSettingsItem::renderScale,
                        .action = AirfixWindowsAccessibilityAction::focus,
                    },
            "UI Automation focus action was incorrect");

    auto increaseElement = findAutomationId(*automation.get(), *root.get(),
                                            increaseRenderScale.runtimeId);
    ComPointer<IUIAutomationInvokePattern> invoke;
    require(SUCCEEDED(increaseElement->GetCurrentPatternAs(
                UIA_InvokePatternId, IID_PPV_ARGS(invoke.put()))) &&
                invoke.get() != nullptr,
            "UI Automation invoke pattern was unavailable");
    require(SUCCEEDED(invoke->Invoke()),
            "UI Automation increment invocation failed");
    const auto increment =
        waitForAction(host, AirfixWindowsAccessibilityAction::increment);
    require(increment.has_value() &&
                increment->action ==
                    AirfixWindowsAccessibilityAction::increment &&
                increment->item == AirfixWindowsRenderSettingsItem::renderScale,
            "UI Automation increment action was incorrect");

    const auto previousSettings = panel->snapshot().draftSettings;
    const auto consumed = panel->consumeAccessibilityAction(
        increment->screen, increment->accessibilityGeneration, increment->item,
        increment->action);
    require(consumed.accepted(),
            "owner thread rejected a valid UI Automation action");
    const auto updatedSnapshot = panel->snapshot();
    require(updatedSnapshot.accessibilityGeneration !=
                    increment->accessibilityGeneration &&
                updatedSnapshot.draftSettings != previousSettings,
            "UI Automation action did not update the guarded panel state");
    const auto updatedTree = buildAirfixWindowsUiSemanticTree(updatedSnapshot);
    require(updatedTree.complete() && host.publish(*updatedTree.tree),
            "updated UI Automation snapshot publication failed");
    require(!host.publish(tree),
            "UI Automation host accepted an older active generation");
    require(SUCCEEDED(invoke->Invoke()),
            "stable UI Automation element did not survive a value update");
    const auto nextIncrement =
        waitForAction(host, AirfixWindowsAccessibilityAction::increment);
    require(nextIncrement.has_value() &&
                nextIncrement->accessibilityGeneration ==
                    updatedSnapshot.accessibilityGeneration,
            "UI Automation action did not use the current generation");

    const auto closeDisplay = panel->consumeAccessibilityAction(
        updatedSnapshot.screen, updatedSnapshot.accessibilityGeneration,
        AirfixWindowsRenderSettingsItem::cancel,
        AirfixWindowsAccessibilityAction::invoke);
    require(closeDisplay.accepted() &&
                panel->snapshot().screen ==
                    AirfixWindowsRenderSettingsScreen::pause,
            "display settings did not return to pause for stale-element test");
    const auto pauseTree = buildAirfixWindowsUiSemanticTree(panel->snapshot());
    require(pauseTree.complete() && host.publish(*pauseTree.tree),
            "pause semantic publication failed");
    require(FAILED(invoke->Invoke()),
            "element cached on a previous screen remained actionable");

    require(host.publish(std::nullopt),
            "UI Automation fragment withdrawal failed");
    require(FAILED(valueElement->SetFocus()),
            "withdrawn UI Automation element remained actionable");
    require(host.pendingActionCount() == 0U,
            "fragment withdrawal retained queued actions");
  }

  auto invalid = tree;
  invalid.accessibilityGeneration = 0U;
  require(!host.publish(invalid), "invalid semantic tree was published");
  invalid = tree;
  invalid.nodes[2].role = AirfixWindowsUiSemanticRole::heading;
  require(!host.publish(invalid),
          "semantic tree with a displaced status root was published");
  window.destroy();
  require(!host.attached(),
          "destroyed window retained its UI Automation attachment");
  host.detach();
  require(!host.attached(), "UI Automation host did not detach");
}

} // namespace

int main() {
  try {
    run();
    std::cout << "Windows UI Automation tests passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "Windows UI Automation tests failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
