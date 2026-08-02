#include "AirfixWindowsUiAutomation.hpp"

// Windows SDK COM/UIA headers require this order when WIN32_LEAN_AND_MEAN is
// enabled. Keep it stable across clang-format versions.
// clang-format off
#include <Windows.h>
#include <CommCtrl.h>
#include <Ole2.h>
#include <UIAutomation.h>
#include <OleAuto.h>
// clang-format on

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <limits>
#include <mutex>
#include <new>
#include <string_view>
#include <utility>

namespace airfix::windows {

struct AirfixWindowsUiAutomationSharedState final {
  mutable std::mutex mutex;
  HWND window{};
  std::optional<AirfixWindowsUiSemanticTree> tree;
  AirfixWindowsUiAutomationActionQueue actions;
};

namespace {

constexpr UINT_PTR airfixUiAutomationSubclassId = 0xA1F10001U;

[[nodiscard]] constexpr bool
validScreen(const AirfixWindowsRenderSettingsScreen screen) noexcept {
  switch (screen) {
  case AirfixWindowsRenderSettingsScreen::pause:
  case AirfixWindowsRenderSettingsScreen::displaySettings:
  case AirfixWindowsRenderSettingsScreen::controllerCalibration:
  case AirfixWindowsRenderSettingsScreen::controllerAxisCalibration:
  case AirfixWindowsRenderSettingsScreen::controllerButtonBindings:
  case AirfixWindowsRenderSettingsScreen::controllerBindingConflict:
    return true;
  }
  return false;
}

[[nodiscard]] bool
validSemanticTree(const AirfixWindowsUiSemanticTree &tree) noexcept {
  if (!tree.complete() || tree.accessibilityGeneration == 0U ||
      !validScreen(tree.screen) || tree.nodes[0].runtimeId != 1U ||
      tree.nodes[0].parentIndex != airfixWindowsUiSemanticNoParent ||
      tree.nodes[0].role != AirfixWindowsUiSemanticRole::window ||
      tree.nodes[1].runtimeId != 2U || tree.nodes[1].parentIndex != 0U ||
      tree.nodes[1].role != AirfixWindowsUiSemanticRole::heading ||
      tree.nodes[2].runtimeId != 3U || tree.nodes[2].parentIndex != 0U ||
      tree.nodes[2].role != AirfixWindowsUiSemanticRole::status) {
    return false;
  }

  for (std::uint8_t index = 0U; index < tree.nodeCount; ++index) {
    const auto &node = tree.nodes[index];
    if (node.runtimeId == 0U || (index != 0U && node.parentIndex >= index)) {
      return false;
    }
    for (std::uint8_t previous = 0U; previous < index; ++previous) {
      if (tree.nodes[previous].runtimeId == node.runtimeId) {
        return false;
      }
    }
    switch (node.role) {
    case AirfixWindowsUiSemanticRole::window:
    case AirfixWindowsUiSemanticRole::heading:
    case AirfixWindowsUiSemanticRole::status:
      if (node.item != AirfixWindowsRenderSettingsItem::count) {
        return false;
      }
      break;
    case AirfixWindowsUiSemanticRole::action:
    case AirfixWindowsUiSemanticRole::adjustableValue:
    case AirfixWindowsUiSemanticRole::decrementButton:
    case AirfixWindowsUiSemanticRole::incrementButton:
      if (node.item >= AirfixWindowsRenderSettingsItem::count) {
        return false;
      }
      break;
    }
  }
  return true;
}

[[nodiscard]] std::optional<std::uint8_t>
nodeIndex(const AirfixWindowsUiSemanticTree &tree,
          const std::uint16_t runtimeId) noexcept {
  for (std::uint8_t index = 0U; index < tree.nodeCount; ++index) {
    if (tree.nodes[index].runtimeId == runtimeId) {
      return index;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::uint16_t>
selectedRuntimeId(const AirfixWindowsUiSemanticTree &tree) noexcept {
  for (std::uint8_t index = 0U; index < tree.nodeCount; ++index) {
    if (tree.nodes[index].selected && tree.nodes[index].focusable) {
      return tree.nodes[index].runtimeId;
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool
hasSemanticAction(const AirfixWindowsUiSemanticNode &node,
                  const AirfixWindowsAccessibilityAction action) noexcept {
  switch (action) {
  case AirfixWindowsAccessibilityAction::focus:
    return airfixWindowsUiSemanticHasAction(
        node.actions, AirfixWindowsUiSemanticAction::focus);
  case AirfixWindowsAccessibilityAction::invoke:
    return airfixWindowsUiSemanticHasAction(
        node.actions, AirfixWindowsUiSemanticAction::invoke);
  case AirfixWindowsAccessibilityAction::decrement:
    return airfixWindowsUiSemanticHasAction(
        node.actions, AirfixWindowsUiSemanticAction::decrement);
  case AirfixWindowsAccessibilityAction::increment:
    return airfixWindowsUiSemanticHasAction(
        node.actions, AirfixWindowsUiSemanticAction::increment);
  }
  return false;
}

[[nodiscard]] HRESULT enqueueAction(
    const std::shared_ptr<AirfixWindowsUiAutomationSharedState> &state,
    const AirfixWindowsRenderSettingsScreen expectedScreen,
    const std::uint16_t runtimeId,
    const AirfixWindowsAccessibilityAction action) noexcept {
  std::lock_guard lock{state->mutex};
  if (!state->tree.has_value() || state->tree->screen != expectedScreen) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  const auto index = nodeIndex(*state->tree, runtimeId);
  if (!index.has_value()) {
    return UIA_E_ELEMENTNOTAVAILABLE;
  }
  const auto &node = state->tree->nodes[*index];
  if (!hasSemanticAction(node, action) ||
      node.item >= AirfixWindowsRenderSettingsItem::count) {
    return UIA_E_NOTSUPPORTED;
  }
  if (!node.enabled && action != AirfixWindowsAccessibilityAction::focus) {
    return UIA_E_ELEMENTNOTENABLED;
  }
  const AirfixWindowsUiAutomationAction request{
      .screen = state->tree->screen,
      .accessibilityGeneration = state->tree->accessibilityGeneration,
      .item = node.item,
      .action = action,
  };
  return state->actions.tryPush(request)
             ? S_OK
             : HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY);
}

[[nodiscard]] bool
copyNode(const std::shared_ptr<AirfixWindowsUiAutomationSharedState> &state,
         const AirfixWindowsRenderSettingsScreen expectedScreen,
         const std::uint16_t runtimeId, AirfixWindowsUiSemanticTree &tree,
         std::uint8_t &index, HWND &window) noexcept {
  std::lock_guard lock{state->mutex};
  if (!state->tree.has_value() || state->tree->screen != expectedScreen ||
      state->window == nullptr) {
    return false;
  }
  const auto found = nodeIndex(*state->tree, runtimeId);
  if (!found.has_value()) {
    return false;
  }
  tree = *state->tree;
  index = *found;
  window = state->window;
  return true;
}

[[nodiscard]] HRESULT setVariantBool(VARIANT *value,
                                     const bool enabled) noexcept {
  VariantInit(value);
  value->vt = VT_BOOL;
  value->boolVal = enabled ? VARIANT_TRUE : VARIANT_FALSE;
  return S_OK;
}

[[nodiscard]] HRESULT setVariantInt(VARIANT *value, const int number) noexcept {
  VariantInit(value);
  value->vt = VT_I4;
  value->lVal = number;
  return S_OK;
}

[[nodiscard]] HRESULT setVariantText(VARIANT *value,
                                     const std::wstring_view text) noexcept {
  VariantInit(value);
  value->vt = VT_BSTR;
  value->bstrVal =
      SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
  if (value->bstrVal == nullptr && !text.empty()) {
    value->vt = VT_EMPTY;
    return E_OUTOFMEMORY;
  }
  return S_OK;
}

[[nodiscard]] int
controlTypeFor(const AirfixWindowsUiSemanticRole role) noexcept {
  switch (role) {
  case AirfixWindowsUiSemanticRole::window:
    return UIA_PaneControlTypeId;
  case AirfixWindowsUiSemanticRole::heading:
    return UIA_TextControlTypeId;
  case AirfixWindowsUiSemanticRole::status:
    return UIA_StatusBarControlTypeId;
  case AirfixWindowsUiSemanticRole::action:
  case AirfixWindowsUiSemanticRole::decrementButton:
  case AirfixWindowsUiSemanticRole::incrementButton:
    return UIA_ButtonControlTypeId;
  case AirfixWindowsUiSemanticRole::adjustableValue:
    return UIA_GroupControlTypeId;
  }
  return UIA_CustomControlTypeId;
}

class AirfixWindowsRawElementProvider final
    : public IRawElementProviderSimple,
      public IRawElementProviderFragment,
      public IRawElementProviderFragmentRoot,
      public IInvokeProvider,
      public IValueProvider {
public:
  AirfixWindowsRawElementProvider(
      std::shared_ptr<AirfixWindowsUiAutomationSharedState> state,
      const AirfixWindowsRenderSettingsScreen screen,
      const std::uint16_t runtimeId) noexcept
      : state_{std::move(state)}, screen_{screen}, runtimeId_{runtimeId} {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,
                                           void **object) noexcept override {
    if (object == nullptr) {
      return E_INVALIDARG;
    }
    *object = nullptr;
    if (id == __uuidof(IUnknown) || id == __uuidof(IRawElementProviderSimple)) {
      *object = static_cast<IRawElementProviderSimple *>(this);
    } else if (id == __uuidof(IRawElementProviderFragment)) {
      *object = static_cast<IRawElementProviderFragment *>(this);
    } else if (id == __uuidof(IRawElementProviderFragmentRoot) &&
               runtimeId_ == 1U) {
      *object = static_cast<IRawElementProviderFragmentRoot *>(this);
    } else if (id == __uuidof(IInvokeProvider)) {
      *object = static_cast<IInvokeProvider *>(this);
    } else if (id == __uuidof(IValueProvider)) {
      *object = static_cast<IValueProvider *>(this);
    } else {
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++references_; }

  ULONG STDMETHODCALLTYPE Release() noexcept override {
    const auto remaining = --references_;
    if (remaining == 0U) {
      delete this;
    }
    return remaining;
  }

  HRESULT STDMETHODCALLTYPE
  get_ProviderOptions(ProviderOptions *options) noexcept override {
    if (options == nullptr) {
      return E_INVALIDARG;
    }
    *options = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider |
                                            ProviderOptions_UseComThreading);
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetPatternProvider(
      PATTERNID patternId, IUnknown **provider) noexcept override {
    if (provider == nullptr) {
      return E_INVALIDARG;
    }
    *provider = nullptr;
    AirfixWindowsUiSemanticTree tree;
    std::uint8_t index{};
    HWND window{};
    if (!copyNode(state_, screen_, runtimeId_, tree, index, window)) {
      return UIA_E_ELEMENTNOTAVAILABLE;
    }
    const auto &node = tree.nodes[index];
    if (patternId == UIA_InvokePatternId &&
        (node.role == AirfixWindowsUiSemanticRole::action ||
         node.role == AirfixWindowsUiSemanticRole::decrementButton ||
         node.role == AirfixWindowsUiSemanticRole::incrementButton)) {
      return QueryInterface(__uuidof(IInvokeProvider),
                            reinterpret_cast<void **>(provider));
    }
    if (patternId == UIA_ValuePatternId && !node.value.empty()) {
      return QueryInterface(__uuidof(IValueProvider),
                            reinterpret_cast<void **>(provider));
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID propertyId,
                                             VARIANT *value) noexcept override {
    if (value == nullptr) {
      return E_INVALIDARG;
    }
    VariantInit(value);
    AirfixWindowsUiSemanticTree tree;
    std::uint8_t index{};
    HWND window{};
    if (!copyNode(state_, screen_, runtimeId_, tree, index, window)) {
      return UIA_E_ELEMENTNOTAVAILABLE;
    }
    const auto &node = tree.nodes[index];
    switch (propertyId) {
    case UIA_NamePropertyId:
      return setVariantText(value, node.name.view());
    case UIA_ControlTypePropertyId:
      return setVariantInt(value, controlTypeFor(node.role));
    case UIA_IsEnabledPropertyId:
      return setVariantBool(value, node.enabled);
    case UIA_IsKeyboardFocusablePropertyId:
      return setVariantBool(value, node.focusable);
    case UIA_HasKeyboardFocusPropertyId:
      return setVariantBool(value, node.selected);
    case UIA_IsOffscreenPropertyId:
      return setVariantBool(value, node.offscreen || !node.visible);
    case UIA_IsControlElementPropertyId:
    case UIA_IsContentElementPropertyId:
      return setVariantBool(value, true);
    case UIA_AutomationIdPropertyId: {
      std::array<wchar_t, 32U> identifier{};
      const int length = std::swprintf(identifier.data(), identifier.size(),
                                       L"airfix-%u", runtimeId_);
      if (length <= 0) {
        return E_FAIL;
      }
      return setVariantText(
          value, {identifier.data(), static_cast<std::size_t>(length)});
    }
    case UIA_FrameworkIdPropertyId:
      return setVariantText(value, L"Airfix");
    case UIA_ItemStatusPropertyId:
      return setVariantText(value, node.value.view());
    case UIA_LiveSettingPropertyId:
      return node.role == AirfixWindowsUiSemanticRole::status
                 ? setVariantInt(value, static_cast<int>(LiveSetting::Polite))
                 : S_OK;
    default:
      return S_OK;
    }
  }

  HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
      IRawElementProviderSimple **provider) noexcept override {
    if (provider == nullptr) {
      return E_INVALIDARG;
    }
    *provider = nullptr;
    if (runtimeId_ != 1U) {
      return S_OK;
    }
    AirfixWindowsUiSemanticTree tree;
    std::uint8_t index{};
    HWND window{};
    if (!copyNode(state_, screen_, runtimeId_, tree, index, window)) {
      return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return UiaHostProviderFromHwnd(window, provider);
  }

  HRESULT STDMETHODCALLTYPE
  Navigate(NavigateDirection direction,
           IRawElementProviderFragment **provider) noexcept override {
    if (provider == nullptr) {
      return E_INVALIDARG;
    }
    *provider = nullptr;
    AirfixWindowsUiSemanticTree tree;
    std::uint8_t index{};
    HWND window{};
    if (!copyNode(state_, screen_, runtimeId_, tree, index, window)) {
      return UIA_E_ELEMENTNOTAVAILABLE;
    }

    std::optional<std::uint8_t> target;
    switch (direction) {
    case NavigateDirection_Parent:
      if (tree.nodes[index].parentIndex != airfixWindowsUiSemanticNoParent) {
        target = tree.nodes[index].parentIndex;
      }
      break;
    case NavigateDirection_FirstChild:
      for (std::uint8_t candidate = 0U; candidate < tree.nodeCount;
           ++candidate) {
        if (tree.nodes[candidate].parentIndex == index) {
          target = candidate;
          break;
        }
      }
      break;
    case NavigateDirection_LastChild:
      for (std::uint8_t candidate = tree.nodeCount; candidate > 0U;
           --candidate) {
        const auto tested = static_cast<std::uint8_t>(candidate - 1U);
        if (tree.nodes[tested].parentIndex == index) {
          target = tested;
          break;
        }
      }
      break;
    case NavigateDirection_NextSibling:
      for (std::uint8_t candidate = static_cast<std::uint8_t>(index + 1U);
           candidate < tree.nodeCount; ++candidate) {
        if (tree.nodes[candidate].parentIndex ==
            tree.nodes[index].parentIndex) {
          target = candidate;
          break;
        }
      }
      break;
    case NavigateDirection_PreviousSibling:
      for (std::uint8_t candidate = index; candidate > 0U; --candidate) {
        const auto tested = static_cast<std::uint8_t>(candidate - 1U);
        if (tree.nodes[tested].parentIndex == tree.nodes[index].parentIndex) {
          target = tested;
          break;
        }
      }
      break;
    }
    if (!target.has_value()) {
      return S_OK;
    }
    return makeFragment(tree.nodes[*target].runtimeId, provider);
  }

  HRESULT STDMETHODCALLTYPE
  GetRuntimeId(SAFEARRAY **runtimeId) noexcept override {
    if (runtimeId == nullptr) {
      return E_INVALIDARG;
    }
    AirfixWindowsUiSemanticTree tree;
    std::uint8_t node{};
    HWND window{};
    if (!copyNode(state_, screen_, runtimeId_, tree, node, window)) {
      *runtimeId = nullptr;
      return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *runtimeId = SafeArrayCreateVector(VT_I4, 0U, 2U);
    if (*runtimeId == nullptr) {
      return E_OUTOFMEMORY;
    }
    LONG index = 0;
    int value = UiaAppendRuntimeId;
    HRESULT result = SafeArrayPutElement(*runtimeId, &index, &value);
    if (SUCCEEDED(result)) {
      index = 1;
      value = static_cast<int>(runtimeId_);
      result = SafeArrayPutElement(*runtimeId, &index, &value);
    }
    if (FAILED(result)) {
      SafeArrayDestroy(*runtimeId);
      *runtimeId = nullptr;
    }
    return result;
  }

  HRESULT STDMETHODCALLTYPE
  get_BoundingRectangle(UiaRect *rectangle) noexcept override {
    if (rectangle == nullptr) {
      return E_INVALIDARG;
    }
    *rectangle = {};
    AirfixWindowsUiSemanticTree tree;
    std::uint8_t index{};
    HWND window{};
    if (!copyNode(state_, screen_, runtimeId_, tree, index, window)) {
      return UIA_E_ELEMENTNOTAVAILABLE;
    }
    const auto &node = tree.nodes[index];
    if (node.offscreen || !node.visible || node.bounds.width == 0U ||
        node.bounds.height == 0U) {
      return S_OK;
    }
    POINT origin{};
    if (!ClientToScreen(window, &origin)) {
      return UIA_E_ELEMENTNOTAVAILABLE;
    }
    rectangle->left = static_cast<double>(origin.x + node.bounds.x);
    rectangle->top = static_cast<double>(origin.y + node.bounds.y);
    rectangle->width = static_cast<double>(node.bounds.width);
    rectangle->height = static_cast<double>(node.bounds.height);
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE
  GetEmbeddedFragmentRoots(SAFEARRAY **roots) noexcept override {
    if (roots == nullptr) {
      return E_INVALIDARG;
    }
    *roots = nullptr;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE SetFocus() noexcept override {
    return enqueueAction(state_, screen_, runtimeId_,
                         AirfixWindowsAccessibilityAction::focus);
  }

  HRESULT STDMETHODCALLTYPE get_FragmentRoot(
      IRawElementProviderFragmentRoot **provider) noexcept override {
    if (provider == nullptr) {
      return E_INVALIDARG;
    }
    *provider = nullptr;
    AirfixWindowsUiSemanticTree tree;
    std::uint8_t index{};
    HWND window{};
    if (!copyNode(state_, screen_, runtimeId_, tree, index, window)) {
      return UIA_E_ELEMENTNOTAVAILABLE;
    }
    auto *root = new (std::nothrow)
        AirfixWindowsRawElementProvider{state_, tree.screen, 1U};
    if (root == nullptr) {
      return E_OUTOFMEMORY;
    }
    *provider = static_cast<IRawElementProviderFragmentRoot *>(root);
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(
      const double x, const double y,
      IRawElementProviderFragment **provider) noexcept override {
    if (provider == nullptr || !std::isfinite(x) || !std::isfinite(y) ||
        x < static_cast<double>(std::numeric_limits<LONG>::min()) ||
        x > static_cast<double>(std::numeric_limits<LONG>::max()) ||
        y < static_cast<double>(std::numeric_limits<LONG>::min()) ||
        y > static_cast<double>(std::numeric_limits<LONG>::max())) {
      return E_INVALIDARG;
    }
    *provider = nullptr;
    AirfixWindowsUiSemanticTree tree;
    std::uint8_t index{};
    HWND window{};
    if (!copyNode(state_, screen_, runtimeId_, tree, index, window)) {
      return UIA_E_ELEMENTNOTAVAILABLE;
    }
    POINT point{static_cast<LONG>(x), static_cast<LONG>(y)};
    if (!ScreenToClient(window, &point)) {
      return UIA_E_ELEMENTNOTAVAILABLE;
    }
    for (std::uint8_t candidate = tree.nodeCount; candidate > 0U; --candidate) {
      const auto tested = static_cast<std::uint8_t>(candidate - 1U);
      const auto &node = tree.nodes[tested];
      if (node.visible && !node.offscreen &&
          node.bounds.contains(static_cast<float>(point.x),
                               static_cast<float>(point.y))) {
        return makeFragment(node.runtimeId, provider);
      }
    }
    return makeFragment(1U, provider);
  }

  HRESULT STDMETHODCALLTYPE
  GetFocus(IRawElementProviderFragment **provider) noexcept override {
    if (provider == nullptr) {
      return E_INVALIDARG;
    }
    *provider = nullptr;
    AirfixWindowsUiSemanticTree tree;
    std::uint8_t index{};
    HWND window{};
    if (!copyNode(state_, screen_, runtimeId_, tree, index, window)) {
      return UIA_E_ELEMENTNOTAVAILABLE;
    }
    for (std::uint8_t candidate = 0U; candidate < tree.nodeCount; ++candidate) {
      if (tree.nodes[candidate].selected && tree.nodes[candidate].focusable) {
        return makeFragment(tree.nodes[candidate].runtimeId, provider);
      }
    }
    return makeFragment(1U, provider);
  }

  HRESULT STDMETHODCALLTYPE Invoke() noexcept override {
    AirfixWindowsUiSemanticTree tree;
    std::uint8_t index{};
    HWND window{};
    if (!copyNode(state_, screen_, runtimeId_, tree, index, window)) {
      return UIA_E_ELEMENTNOTAVAILABLE;
    }
    const auto role = tree.nodes[index].role;
    switch (role) {
    case AirfixWindowsUiSemanticRole::action:
      return enqueueAction(state_, screen_, runtimeId_,
                           AirfixWindowsAccessibilityAction::invoke);
    case AirfixWindowsUiSemanticRole::decrementButton:
      return enqueueAction(state_, screen_, runtimeId_,
                           AirfixWindowsAccessibilityAction::decrement);
    case AirfixWindowsUiSemanticRole::incrementButton:
      return enqueueAction(state_, screen_, runtimeId_,
                           AirfixWindowsAccessibilityAction::increment);
    default:
      return UIA_E_NOTSUPPORTED;
    }
  }

  HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR) noexcept override {
    return UIA_E_NOTSUPPORTED;
  }

  HRESULT STDMETHODCALLTYPE get_Value(BSTR *value) noexcept override {
    if (value == nullptr) {
      return E_INVALIDARG;
    }
    *value = nullptr;
    AirfixWindowsUiSemanticTree tree;
    std::uint8_t index{};
    HWND window{};
    if (!copyNode(state_, screen_, runtimeId_, tree, index, window)) {
      return UIA_E_ELEMENTNOTAVAILABLE;
    }
    const auto text = tree.nodes[index].value.view();
    *value = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
    if (*value == nullptr && !text.empty()) {
      return E_OUTOFMEMORY;
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL *readOnly) noexcept override {
    if (readOnly == nullptr) {
      return E_INVALIDARG;
    }
    AirfixWindowsUiSemanticTree tree;
    std::uint8_t index{};
    HWND window{};
    if (!copyNode(state_, screen_, runtimeId_, tree, index, window)) {
      return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *readOnly = TRUE;
    return S_OK;
  }

private:
  HRESULT makeFragment(const std::uint16_t runtimeId,
                       IRawElementProviderFragment **provider) noexcept {
    auto *created = new (std::nothrow)
        AirfixWindowsRawElementProvider{state_, screen_, runtimeId};
    if (created == nullptr) {
      return E_OUTOFMEMORY;
    }
    *provider = static_cast<IRawElementProviderFragment *>(created);
    return S_OK;
  }

  std::atomic<ULONG> references_{1U};
  std::shared_ptr<AirfixWindowsUiAutomationSharedState> state_;
  AirfixWindowsRenderSettingsScreen screen_;
  std::uint16_t runtimeId_{};
};

LRESULT CALLBACK airfixUiAutomationSubclassProcedure(
    HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam,
    const UINT_PTR subclassId, const DWORD_PTR referenceData) noexcept {
  auto *context =
      reinterpret_cast<std::shared_ptr<AirfixWindowsUiAutomationSharedState> *>(
          referenceData);
  if (context == nullptr || !*context) {
    return DefSubclassProc(window, message, wParam, lParam);
  }
  const auto state = *context;
  if (message == WM_GETOBJECT &&
      lParam == static_cast<LPARAM>(UiaRootObjectId)) {
    AirfixWindowsRenderSettingsScreen screen{};
    {
      std::lock_guard lock{state->mutex};
      if (state->window != window || !state->tree.has_value()) {
        return DefSubclassProc(window, message, wParam, lParam);
      }
      screen = state->tree->screen;
    }
    auto *root =
        new (std::nothrow) AirfixWindowsRawElementProvider{state, screen, 1U};
    if (root == nullptr) {
      return 0;
    }
    const auto result = UiaReturnRawElementProvider(
        window, wParam, lParam, static_cast<IRawElementProviderSimple *>(root));
    root->Release();
    return result;
  }
  if (message == WM_NCDESTROY) {
    {
      std::lock_guard lock{state->mutex};
      state->window = nullptr;
      state->tree.reset();
    }
    state->actions.clear();
    RemoveWindowSubclass(window, airfixUiAutomationSubclassProcedure,
                         subclassId);
    delete context;
  }
  return DefSubclassProc(window, message, wParam, lParam);
}

} // namespace

bool AirfixWindowsUiAutomationActionQueue::tryPush(
    const AirfixWindowsUiAutomationAction &action) noexcept {
  std::lock_guard lock{mutex_};
  if (count_ >= actions_.size()) {
    return false;
  }
  actions_[write_] = action;
  write_ = (write_ + 1U) % actions_.size();
  ++count_;
  return true;
}

std::optional<AirfixWindowsUiAutomationAction>
AirfixWindowsUiAutomationActionQueue::pop() noexcept {
  std::lock_guard lock{mutex_};
  if (count_ == 0U) {
    return std::nullopt;
  }
  const auto action = actions_[read_];
  read_ = (read_ + 1U) % actions_.size();
  --count_;
  return action;
}

void AirfixWindowsUiAutomationActionQueue::clear() noexcept {
  std::lock_guard lock{mutex_};
  read_ = 0U;
  write_ = 0U;
  count_ = 0U;
}

std::size_t AirfixWindowsUiAutomationActionQueue::size() const noexcept {
  std::lock_guard lock{mutex_};
  return count_;
}

AirfixWindowsUiAutomationHost::AirfixWindowsUiAutomationHost() noexcept =
    default;

AirfixWindowsUiAutomationHost::~AirfixWindowsUiAutomationHost() { detach(); }

bool AirfixWindowsUiAutomationHost::attach(void *nativeWindow) noexcept {
  detach();
  if (nativeWindow == nullptr) {
    return false;
  }
  const auto window = static_cast<HWND>(nativeWindow);
  if (!IsWindow(window)) {
    return false;
  }
  try {
    state_ = std::make_shared<AirfixWindowsUiAutomationSharedState>();
  } catch (...) {
    return false;
  }

  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
    return false;
  }
  ownsComInitialization_ = SUCCEEDED(initialized);

  INITCOMMONCONTROLSEX commonControls{
      .dwSize = sizeof(INITCOMMONCONTROLSEX),
      .dwICC = ICC_STANDARD_CLASSES,
  };
  (void)InitCommonControlsEx(&commonControls);
  auto *context = new (std::nothrow)
      std::shared_ptr<AirfixWindowsUiAutomationSharedState>{state_};
  if (context == nullptr) {
    if (ownsComInitialization_) {
      CoUninitialize();
      ownsComInitialization_ = false;
    }
    state_.reset();
    return false;
  }
  if (!SetWindowSubclass(window, airfixUiAutomationSubclassProcedure,
                         airfixUiAutomationSubclassId,
                         reinterpret_cast<DWORD_PTR>(context))) {
    delete context;
    if (ownsComInitialization_) {
      CoUninitialize();
      ownsComInitialization_ = false;
    }
    state_.reset();
    return false;
  }
  {
    std::lock_guard lock{state_->mutex};
    state_->window = window;
    state_->tree.reset();
  }
  state_->actions.clear();
  subclassContext_ = context;
  return true;
}

void AirfixWindowsUiAutomationHost::detach() noexcept {
  if (state_) {
    HWND window{};
    {
      std::lock_guard lock{state_->mutex};
      window = state_->window;
      state_->window = nullptr;
      state_->tree.reset();
    }
    state_->actions.clear();
    if (window != nullptr && IsWindow(window) && subclassContext_ != nullptr &&
        RemoveWindowSubclass(window, airfixUiAutomationSubclassProcedure,
                             airfixUiAutomationSubclassId)) {
      delete static_cast<
          std::shared_ptr<AirfixWindowsUiAutomationSharedState> *>(
          subclassContext_);
    }
  }
  subclassContext_ = nullptr;
  state_.reset();
  if (ownsComInitialization_) {
    CoUninitialize();
    ownsComInitialization_ = false;
  }
}

bool AirfixWindowsUiAutomationHost::attached() const noexcept {
  if (!state_) {
    return false;
  }
  std::lock_guard lock{state_->mutex};
  return state_->window != nullptr;
}

bool AirfixWindowsUiAutomationHost::publish(
    const std::optional<AirfixWindowsUiSemanticTree> &tree) noexcept {
  if (!state_ || (tree.has_value() && !validSemanticTree(*tree))) {
    return false;
  }
  std::optional<AirfixWindowsUiSemanticTree> previous;
  {
    std::lock_guard lock{state_->mutex};
    if (state_->window == nullptr) {
      return false;
    }
    if (tree.has_value() && state_->tree.has_value() &&
        (tree->accessibilityGeneration <
             state_->tree->accessibilityGeneration ||
         (tree->screen != state_->tree->screen &&
          tree->accessibilityGeneration ==
              state_->tree->accessibilityGeneration))) {
      return false;
    }
    previous = state_->tree;
    state_->tree = tree;
  }
  if (!tree.has_value()) {
    state_->actions.clear();
    return true;
  }
  if (!UiaClientsAreListening()) {
    return true;
  }

  auto *root = new (std::nothrow)
      AirfixWindowsRawElementProvider{state_, tree->screen, 1U};
  if (root == nullptr) {
    return true;
  }
  if (!previous.has_value() || previous->screen != tree->screen) {
    (void)UiaRaiseStructureChangedEvent(
        static_cast<IRawElementProviderSimple *>(root),
        StructureChangeType_ChildrenInvalidated, nullptr, 0);
  } else {
    (void)UiaRaiseAutomationEvent(
        static_cast<IRawElementProviderSimple *>(root),
        UIA_LayoutInvalidatedEventId);
  }
  root->Release();

  const auto previousFocus =
      previous.has_value() ? selectedRuntimeId(*previous) : std::nullopt;
  const auto currentFocus = selectedRuntimeId(*tree);
  if (currentFocus.has_value() && currentFocus != previousFocus) {
    auto *focused = new (std::nothrow)
        AirfixWindowsRawElementProvider{state_, tree->screen, *currentFocus};
    if (focused != nullptr) {
      (void)UiaRaiseAutomationEvent(
          static_cast<IRawElementProviderSimple *>(focused),
          UIA_AutomationFocusChangedEventId);
      focused->Release();
    }
  }
  if (previous.has_value() && previous->nodes[2].name != tree->nodes[2].name) {
    auto *status = new (std::nothrow)
        AirfixWindowsRawElementProvider{state_, tree->screen, 3U};
    if (status != nullptr) {
      (void)UiaRaiseAutomationEvent(
          static_cast<IRawElementProviderSimple *>(status),
          UIA_LiveRegionChangedEventId);
      status->Release();
    }
  }
  return true;
}

std::optional<AirfixWindowsUiAutomationAction>
AirfixWindowsUiAutomationHost::popAction() noexcept {
  if (!state_) {
    return std::nullopt;
  }
  return state_->actions.pop();
}

std::size_t AirfixWindowsUiAutomationHost::pendingActionCount() const noexcept {
  if (!state_) {
    return 0U;
  }
  return state_->actions.size();
}

} // namespace airfix::windows
