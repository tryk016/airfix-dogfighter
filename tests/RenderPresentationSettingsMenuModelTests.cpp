#include "airfix/settings/RenderPresentationSettingsMenuModel.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace {

using airfix::render::RenderPresentationSettings;
using airfix::render::RenderPresentationSettingsDelta;
using airfix::render::RenderPresentationSettingsIssueKind;
using airfix::render::ScenePresentationMode;
using airfix::render::VisualProfile;
using airfix::settings::RenderPresentationSettingsMenuApplyTicket;
using airfix::settings::RenderPresentationSettingsMenuCapabilities;
using airfix::settings::RenderPresentationSettingsMenuEditStatus;
using airfix::settings::RenderPresentationSettingsMenuModel;
using airfix::settings::RenderPresentationSettingsMenuPhase;

static_assert(
    std::is_trivially_copyable_v<RenderPresentationSettingsMenuApplyTicket>);
static_assert(
    std::is_trivially_copyable_v<RenderPresentationSettingsMenuModel>);
static_assert(noexcept(RenderPresentationSettingsMenuModel::create({})));

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

[[nodiscard]] RenderPresentationSettings
settings(const float scale = 100.0F,
         const ScenePresentationMode presentation =
             ScenePresentationMode::widescreenHorPlus,
         const VisualProfile profile = VisualProfile::classic,
         const bool diagnostics = false) {
  return {
      .renderScalePercent = scale,
      .scenePresentation = presentation,
      .visualProfile = profile,
      .diagnosticsOverlayEnabled = diagnostics,
  };
}

[[nodiscard]] RenderPresentationSettingsMenuModel
model(const RenderPresentationSettings &applied = {},
      const bool persistenceAvailable = true,
      const std::uint64_t initialSerial = 0U) {
  auto created = RenderPresentationSettingsMenuModel::create(
      applied,
      RenderPresentationSettingsMenuCapabilities{
          .persistenceAvailable = persistenceAvailable,
      },
      initialSerial);
  require(created.has_value(), "valid menu model was not created");
  return *created;
}

void testCreationAcceptsAllBoundaryCombinations() {
  constexpr float scales[]{50.0F, 200.0F};
  constexpr ScenePresentationMode presentations[]{
      ScenePresentationMode::widescreenHorPlus,
      ScenePresentationMode::originalFourByThree,
  };
  constexpr VisualProfile profiles[]{
      VisualProfile::classic,
      VisualProfile::enhanced,
  };
  constexpr bool diagnosticStates[]{false, true};

  for (const float scale : scales) {
    for (const auto presentation : presentations) {
      for (const auto profile : profiles) {
        for (const bool diagnostics : diagnosticStates) {
          const auto applied =
              settings(scale, presentation, profile, diagnostics);
          const auto created =
              RenderPresentationSettingsMenuModel::create(applied);
          require(created.has_value(),
                  "valid boundary combination was rejected");
          require(created->appliedSettings() == applied &&
                      created->draftSettings() == applied,
                  "created snapshots differ from applied");
          require(!created->dirty() &&
                      created->delta() == RenderPresentationSettingsDelta{} &&
                      !created->canApply() && created->canCancel() &&
                      created->phase() ==
                          RenderPresentationSettingsMenuPhase::idle,
                  "created model is not clean and idle");
        }
      }
    }
  }
}

void testCreationRejectsEveryInvalidAppliedClass() {
  constexpr float invalidScales[]{
      std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::infinity(),
      -std::numeric_limits<float>::infinity(),
      49.999F,
      200.001F,
  };
  for (const float scale : invalidScales) {
    require(!RenderPresentationSettingsMenuModel::create(settings(scale))
                 .has_value(),
            "invalid applied scale created a model");
  }

  auto invalid = settings();
  invalid.scenePresentation = static_cast<ScenePresentationMode>(0xFFU);
  require(!RenderPresentationSettingsMenuModel::create(invalid).has_value(),
          "forged presentation created a model");

  invalid = settings();
  invalid.visualProfile = static_cast<VisualProfile>(0xFFU);
  require(!RenderPresentationSettingsMenuModel::create(invalid).has_value(),
          "forged profile created a model");
}

void testTypedEditsAreAtomicAndProduceExactDelta() {
  auto menu = model(settings(75.0F, ScenePresentationMode::widescreenHorPlus,
                             VisualProfile::classic, false));

  require(menu.setRenderScalePercent(150.0F).accepted(),
          "valid scale edit failed");
  require(menu.draftSettings() ==
              settings(150.0F, ScenePresentationMode::widescreenHorPlus,
                       VisualProfile::classic, false),
          "scale edit changed unrelated fields");
  require(menu.delta() ==
              RenderPresentationSettingsDelta{
                  .scaleTargetsChanged = true,
                  .layoutChanged = true,
              },
          "scale edit produced wrong delta");

  require(menu.setScenePresentation(ScenePresentationMode::originalFourByThree)
              .accepted(),
          "valid presentation edit failed");
  require(menu.setVisualProfile(VisualProfile::enhanced).accepted(),
          "enhanced preview selector was unavailable");
  require(menu.setDiagnosticsOverlayEnabled(true).accepted(),
          "valid diagnostics edit failed");
  require(menu.draftSettings() ==
              settings(150.0F, ScenePresentationMode::originalFourByThree,
                       VisualProfile::enhanced, true),
          "combined typed edits produced wrong draft");
  require(menu.delta() ==
              RenderPresentationSettingsDelta{
                  .scaleTargetsChanged = true,
                  .layoutChanged = true,
                  .diagnosticsChanged = true,
                  .visualProfileChanged = true,
              },
          "combined typed edits produced wrong delta");

  const auto beforeInvalid = menu.draftSettings();
  const auto beforeInvalidDelta = menu.delta();
  const auto invalidScale =
      menu.setRenderScalePercent(std::numeric_limits<float>::quiet_NaN());
  require(invalidScale.status ==
                  RenderPresentationSettingsMenuEditStatus::invalidSettings &&
              invalidScale.issue.has_value() &&
              invalidScale.issue->kind ==
                  RenderPresentationSettingsIssueKind::nonFiniteRenderScale,
          "NaN edit did not report its exact issue");
  require(menu.draftSettings() == beforeInvalid &&
              menu.delta() == beforeInvalidDelta,
          "invalid scale edit partially mutated the model");

  const auto invalidPresentation =
      menu.setScenePresentation(static_cast<ScenePresentationMode>(0xFFU));
  require(
      invalidPresentation.status ==
              RenderPresentationSettingsMenuEditStatus::invalidSettings &&
          invalidPresentation.issue.has_value() &&
          invalidPresentation.issue->kind ==
              RenderPresentationSettingsIssueKind::unsupportedScenePresentation,
      "forged presentation edit reported the wrong issue");
  require(menu.draftSettings() == beforeInvalid &&
              menu.delta() == beforeInvalidDelta,
          "forged presentation partially mutated the model");

  const auto invalidProfile =
      menu.setVisualProfile(static_cast<VisualProfile>(0xFFU));
  require(invalidProfile.status ==
                  RenderPresentationSettingsMenuEditStatus::invalidSettings &&
              invalidProfile.issue.has_value() &&
              invalidProfile.issue->kind ==
                  RenderPresentationSettingsIssueKind::unsupportedVisualProfile,
          "forged profile edit reported the wrong issue");
  require(menu.draftSettings() == beforeInvalid &&
              menu.delta() == beforeInvalidDelta,
          "forged profile partially mutated the model");
}

void testEverySingleFieldDeltaIsExact() {
  {
    auto menu = model();
    require(
        menu.setScenePresentation(ScenePresentationMode::originalFourByThree)
            .accepted(),
        "presentation-only edit failed");
    require(menu.delta() ==
                RenderPresentationSettingsDelta{
                    .layoutChanged = true,
                },
            "presentation-only edit produced wrong delta");
  }
  {
    auto menu = model();
    require(menu.setVisualProfile(VisualProfile::enhanced).accepted(),
            "profile-only edit failed");
    require(menu.delta() ==
                RenderPresentationSettingsDelta{
                    .visualProfileChanged = true,
                },
            "profile-only edit produced wrong delta");
  }
  {
    auto menu = model();
    require(menu.setDiagnosticsOverlayEnabled(true).accepted(),
            "diagnostics-only edit failed");
    require(menu.delta() ==
                RenderPresentationSettingsDelta{
                    .diagnosticsChanged = true,
                },
            "diagnostics-only edit produced wrong delta");
  }
}

void testScaleEditBoundariesAndInvalidClassesAreAtomic() {
  auto menu = model();
  require(menu.setRenderScalePercent(50.0F).accepted() &&
              menu.draftSettings().renderScalePercent == 50.0F,
          "minimum render scale edit was rejected");
  require(menu.setRenderScalePercent(200.0F).accepted() &&
              menu.draftSettings().renderScalePercent == 200.0F,
          "maximum render scale edit was rejected");

  struct InvalidScale final {
    float value;
    RenderPresentationSettingsIssueKind issue;
  };
  const InvalidScale invalidScales[]{
      {
          std::numeric_limits<float>::quiet_NaN(),
          RenderPresentationSettingsIssueKind::nonFiniteRenderScale,
      },
      {
          std::numeric_limits<float>::infinity(),
          RenderPresentationSettingsIssueKind::nonFiniteRenderScale,
      },
      {
          -std::numeric_limits<float>::infinity(),
          RenderPresentationSettingsIssueKind::nonFiniteRenderScale,
      },
      {
          std::nextafter(50.0F, 0.0F),
          RenderPresentationSettingsIssueKind::renderScaleOutOfRange,
      },
      {
          std::nextafter(200.0F, std::numeric_limits<float>::infinity()),
          RenderPresentationSettingsIssueKind::renderScaleOutOfRange,
      },
  };
  for (const auto &invalid : invalidScales) {
    const auto before = menu.draftSettings();
    const auto beforeDelta = menu.delta();
    const auto result = menu.setRenderScalePercent(invalid.value);
    require(result.status ==
                    RenderPresentationSettingsMenuEditStatus::invalidSettings &&
                result.issue.has_value() && result.issue->kind == invalid.issue,
            "invalid scale class reported the wrong edit result");
    require(menu.draftSettings() == before && menu.delta() == beforeDelta,
            "invalid scale class partially mutated the model");
  }
}

void testEditingBackToAppliedClearsDirtyState() {
  auto menu = model();
  require(!menu.beginApply().has_value(), "clean draft issued an Apply ticket");
  require(menu.setRenderScalePercent(50.0F).accepted() && menu.dirty() &&
              menu.canApply(),
          "first edit did not dirty model");
  require(menu.setRenderScalePercent(100.0F).accepted(),
          "edit back to applied failed");
  require(!menu.dirty() && !menu.canApply() &&
              menu.delta() == RenderPresentationSettingsDelta{},
          "edit back to applied did not clear delta");
}

void testPersistenceCapabilityControlsOnlyNewApply() {
  auto menu = model({}, false);
  require(menu.setVisualProfile(VisualProfile::enhanced).accepted(),
          "persistence capability blocked draft editing");
  require(menu.dirty() && !menu.canApply() && !menu.beginApply().has_value(),
          "unavailable persistence issued a ticket");

  menu.setPersistenceAvailable(true);
  require(menu.persistenceAvailable() && menu.canApply(),
          "restored persistence did not enable Apply");
  const auto ticket = menu.beginApply();
  require(ticket.has_value(), "restored persistence did not apply");

  menu.setPersistenceAvailable(false);
  require(!menu.persistenceAvailable(),
          "persistence capability did not update while applying");
  require(menu.finishApplySuccess(*ticket),
          "capability change invalidated in-flight exact ticket");
  require(menu.appliedSettings().visualProfile == VisualProfile::enhanced &&
              !menu.dirty(),
          "successful in-flight Enhanced selector was not promoted");
}

void testApplyFreezesEditsAndCancel() {
  auto menu = model();
  require(menu.setRenderScalePercent(125.0F).accepted(),
          "freeze fixture edit failed");
  const auto ticket = menu.beginApply();
  require(ticket.has_value(), "freeze fixture did not begin");
  require(menu.phase() == RenderPresentationSettingsMenuPhase::applying &&
              !menu.canApply() && !menu.canCancel(),
          "applying phase did not freeze actions");

  const auto frozenDraft = menu.draftSettings();
  const auto frozenDelta = menu.delta();
  const auto edit = menu.setDiagnosticsOverlayEnabled(true);
  require(edit.status ==
                  RenderPresentationSettingsMenuEditStatus::applyInProgress &&
              !edit.issue.has_value(),
          "frozen edit reported wrong status");
  require(!menu.cancelDraft(), "Cancel discarded an in-flight candidate");
  require(!menu.beginApply().has_value(),
          "concurrent Apply issued a second ticket");
  require(menu.draftSettings() == frozenDraft && menu.delta() == frozenDelta,
          "frozen actions mutated candidate");
  require(menu.finishApplyFailure(*ticket),
          "freeze fixture failure was rejected");
}

void testSuccessPromotesOnlyExactTicket() {
  auto menu = model();
  require(menu.setRenderScalePercent(175.0F).accepted(),
          "success fixture edit failed");
  const auto ticket = menu.beginApply();
  require(ticket.has_value() && ticket->serial == 1U &&
              ticket->candidate == menu.draftSettings() &&
              ticket->delta == menu.delta(),
          "first Apply ticket did not capture exact state");

  auto forgedSerial = *ticket;
  ++forgedSerial.serial;
  require(!menu.finishApplySuccess(forgedSerial),
          "forged serial completed Apply");
  require(!menu.finishApplyFailure(forgedSerial),
          "forged serial failed active Apply");
  auto forgedCandidate = *ticket;
  forgedCandidate.candidate.renderScalePercent = 50.0F;
  require(!menu.finishApplySuccess(forgedCandidate),
          "forged candidate completed Apply");
  auto forgedDelta = *ticket;
  forgedDelta.delta.layoutChanged = false;
  require(!menu.finishApplySuccess(forgedDelta),
          "forged delta completed Apply");
  require(menu.phase() == RenderPresentationSettingsMenuPhase::applying,
          "stale callbacks changed active phase");

  require(menu.finishApplySuccess(*ticket),
          "exact success ticket was rejected");
  require(menu.appliedSettings() == ticket->candidate &&
              menu.draftSettings() == ticket->candidate && !menu.dirty() &&
              !menu.canApply() && menu.canCancel(),
          "successful Apply did not promote and clean candidate");
  require(!menu.finishApplySuccess(*ticket) &&
              !menu.finishApplyFailure(*ticket),
          "duplicate completion was accepted");
}

void testFailurePreservesDraftAndRetryUsesNewSerial() {
  auto menu = model();
  require(menu.setDiagnosticsOverlayEnabled(true).accepted(),
          "failure fixture edit failed");
  const auto first = menu.beginApply();
  require(first.has_value(), "failure fixture did not begin");
  require(menu.finishApplyFailure(*first), "exact failure ticket was rejected");
  require(menu.appliedSettings() == settings() &&
              menu.draftSettings().diagnosticsOverlayEnabled && menu.dirty() &&
              menu.canApply(),
          "failure did not preserve retryable draft");

  const auto second = menu.beginApply();
  require(second.has_value() && second->serial == first->serial + 1U &&
              second->candidate == first->candidate &&
              second->delta == first->delta,
          "retry did not preserve candidate with a new serial");
  require(!menu.finishApplyFailure(*first),
          "stale failure callback cleared newer retry");
  require(!menu.finishApplySuccess(*first),
          "stale success callback promoted newer retry");
  require(menu.finishApplySuccess(*second), "exact retry success was rejected");
}

void testCancelRevertsOnlyWhileIdle() {
  const auto applied =
      settings(80.0F, ScenePresentationMode::originalFourByThree,
               VisualProfile::enhanced, true);
  auto menu = model(applied);
  require(menu.cancelDraft(), "clean idle Cancel was rejected");
  require(menu.setRenderScalePercent(120.0F).accepted() &&
              menu.setVisualProfile(VisualProfile::classic).accepted(),
          "cancel fixture edits failed");
  require(menu.cancelDraft(), "dirty idle Cancel was rejected");
  require(menu.appliedSettings() == applied &&
              menu.draftSettings() == applied && !menu.dirty() &&
              menu.delta() == RenderPresentationSettingsDelta{},
          "Cancel did not restore exact applied snapshot");
}

void testSerialExhaustionFailsClosedWithoutWrap() {
  auto menu = model({}, true, std::numeric_limits<std::uint64_t>::max() - 1U);
  require(menu.setRenderScalePercent(125.0F).accepted(),
          "exhaustion fixture edit failed");
  const auto last = menu.beginApply();
  require(last.has_value() &&
              last->serial == std::numeric_limits<std::uint64_t>::max() &&
              menu.exhausted(),
          "last representable menu serial was not issued");
  require(menu.finishApplyFailure(*last),
          "last representable ticket could not fail");
  require(menu.dirty() && !menu.canApply() && !menu.beginApply().has_value(),
          "menu serial wrapped after exhaustion");

  auto alreadyExhausted =
      model({}, true, std::numeric_limits<std::uint64_t>::max());
  require(alreadyExhausted.setRenderScalePercent(125.0F).accepted(),
          "already-exhausted model blocked editing");
  require(alreadyExhausted.exhausted() && !alreadyExhausted.canApply() &&
              !alreadyExhausted.beginApply().has_value(),
          "initial maximum serial did not fail closed");
}

} // namespace

int main() {
  try {
    testCreationAcceptsAllBoundaryCombinations();
    testCreationRejectsEveryInvalidAppliedClass();
    testTypedEditsAreAtomicAndProduceExactDelta();
    testEverySingleFieldDeltaIsExact();
    testScaleEditBoundariesAndInvalidClassesAreAtomic();
    testEditingBackToAppliedClearsDirtyState();
    testPersistenceCapabilityControlsOnlyNewApply();
    testApplyFreezesEditsAndCancel();
    testSuccessPromotesOnlyExactTicket();
    testFailurePreservesDraftAndRetryUsesNewSerial();
    testCancelRevertsOnlyWhileIdle();
    testSerialExhaustionFailsClosedWithoutWrap();
    std::cout << "render-presentation menu model tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "render-presentation menu model test failure: " << error.what()
              << '\n';
    return 1;
  }
}
