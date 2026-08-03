#include "airfix/campaign/CampaignStateStore.hpp"
#include "airfix/crypto/Sha256.hpp"
#include "airfix/io/DurableFile.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view currentName = "campaign-state.afcs";
constexpr std::string_view backupName = "campaign-state.afcs.backup";
constexpr std::string_view currentPartialName = "campaign-state.afcs.partial";

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class TempDirectory final {
public:
  TempDirectory() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0U; attempt < 100U; ++attempt) {
      root_ = std::filesystem::temp_directory_path() /
              ("airfix-campaign-state-store-" + std::to_string(stamp) + "-" +
               std::to_string(attempt));
      std::error_code error;
      if (std::filesystem::create_directory(root_, error)) {
        campaign_ = root_ / "campaign";
        return;
      }
      if (error && error != std::errc::file_exists) {
        throw std::runtime_error("cannot create campaign state test root");
      }
    }
    throw std::runtime_error("cannot allocate campaign state test root");
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  [[nodiscard]] const std::filesystem::path &campaign() const {
    return campaign_;
  }

  [[nodiscard]] std::filesystem::path current() const {
    return campaign_ / currentName;
  }

  [[nodiscard]] std::filesystem::path backup() const {
    return campaign_ / backupName;
  }

  [[nodiscard]] std::filesystem::path currentPartial() const {
    return campaign_ / currentPartialName;
  }

private:
  std::filesystem::path root_;
  std::filesystem::path campaign_;
};

[[nodiscard]] airfix::campaign::CampaignStateRecord
state(const std::int32_t marker) {
  return {
      .schemaVersion = airfix::campaign::campaignStateSchemaVersion,
      .storedThread = marker % 2 == 0 ? airfix::campaign::CampaignSide::axis
                                      : airfix::campaign::CampaignSide::allied,
      .axisMaximum = marker,
      .alliedMaximum = -marker,
      .cumulativeScore = marker * 100,
      .acki = marker + 1,
      .guki = marker + 2,
      .wuki = marker + 3,
      .fook = marker + 4,
      .frik = marker + 5,
      .deat = marker + 6,
      .pkil = marker + 7,
      .pdea = marker + 8,
  };
}

[[nodiscard]] std::vector<std::uint8_t>
readFile(const std::filesystem::path &path) {
  return airfix::io::readBoundedRegularFile(
      path, airfix::campaign::maximumCampaignStateDocumentBytes);
}

void writeRaw(const std::filesystem::path &path,
              const std::span<const std::uint8_t> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output || (!bytes.empty() &&
                  !output.write(reinterpret_cast<const char *>(bytes.data()),
                                static_cast<std::streamsize>(bytes.size())))) {
    throw std::runtime_error("cannot write campaign state fixture");
  }
}

void repairFutureEnvelope(std::vector<std::uint8_t> &bytes) {
  bytes[8U] = 2U;
  bytes[9U] = 0U;
  bytes[10U] = 0U;
  bytes[11U] = 0U;
  airfix::crypto::Sha256 hash;
  hash.update(std::span<const std::uint8_t>(bytes).first(16U));
  hash.update(std::span<const std::uint8_t>(bytes).subspan(48U));
  const auto digest = hash.finish();
  std::copy(digest.begin(), digest.end(), bytes.begin() + 16);
}

void requireStoreError(
    const std::function<void()> &action,
    const airfix::campaign::CampaignStateStoreErrorKind kind,
    const std::filesystem::path &privatePath,
    const std::optional<airfix::campaign::CampaignStateRecord>
        &expectedRequested = std::nullopt) {
  try {
    action();
  } catch (const airfix::campaign::CampaignStateStoreError &error) {
    require(error.kind() == kind, "campaign store error kind mismatch");
    require(std::string(error.what()).find(privatePath.string()) ==
                std::string::npos,
            "campaign store error leaked a host path");
    require(error.requestedState() == expectedRequested,
            "campaign store error retained the wrong requested state");
    return;
  }
  throw std::runtime_error("expected campaign state store error");
}

void testMissingSaveUnchangedRotationAndFallback() {
  TempDirectory temporary;
  const auto missing =
      airfix::campaign::loadCampaignState(temporary.campaign());
  require(missing.state == airfix::campaign::CampaignStateRecord{} &&
              missing.source ==
                  airfix::campaign::CampaignStateLoadSource::defaults &&
              missing.current.status ==
                  airfix::campaign::CampaignStateFileStatus::missing &&
              missing.backup.status ==
                  airfix::campaign::CampaignStateFileStatus::missing &&
              !missing.persistenceBlocked &&
              !airfix::campaign::campaignStateNeedsRepair(missing),
          "missing campaign store did not select clean empty defaults");

  const auto first = state(3);
  const auto firstSave =
      airfix::campaign::saveCampaignState(temporary.campaign(), first);
  require(firstSave.status ==
                  airfix::campaign::CampaignStateSaveStatus::committed &&
              !firstSave.backupRotated,
          "first campaign save returned the wrong result");
  const auto firstReload =
      airfix::campaign::loadCampaignState(temporary.campaign());
  require(firstReload.state == first &&
              firstReload.source ==
                  airfix::campaign::CampaignStateLoadSource::current &&
              firstReload.current.status ==
                  airfix::campaign::CampaignStateFileStatus::valid &&
              firstReload.current.schemaVersion ==
                  airfix::campaign::campaignStateSchemaVersion &&
              firstReload.backup.status ==
                  airfix::campaign::CampaignStateFileStatus::missing &&
              !airfix::campaign::campaignStateNeedsRepair(firstReload),
          "saved campaign state did not survive restart as current");

  const auto unchanged =
      airfix::campaign::saveCampaignState(temporary.campaign(), first);
  require(unchanged.status ==
                  airfix::campaign::CampaignStateSaveStatus::unchanged &&
              !unchanged.backupRotated,
          "idempotent campaign save rewrote storage");

  const auto second = state(8);
  const auto secondSave =
      airfix::campaign::saveCampaignState(temporary.campaign(), second);
  require(secondSave.backupRotated,
          "changed campaign state did not rotate valid current");

  auto corrupt = readFile(temporary.current());
  corrupt.back() ^= 1U;
  writeRaw(temporary.current(), corrupt);
  const auto recovered =
      airfix::campaign::loadCampaignState(temporary.campaign());
  require(recovered.state == first && recovered.recoveredFromBackup() &&
              airfix::campaign::campaignStateNeedsRepair(recovered),
          "corrupt current did not select the validated backup");
}

void testMalformedCurrentNeverRotates() {
  TempDirectory temporary;
  const auto first = state(1);
  const auto second = state(2);
  (void)airfix::campaign::saveCampaignState(temporary.campaign(), first);
  (void)airfix::campaign::saveCampaignState(temporary.campaign(), second);
  const auto backupBefore = readFile(temporary.backup());

  writeRaw(temporary.current(),
           std::array<std::uint8_t, 4U>{'B', 'A', 'D', '!'});
  const auto replacement = state(9);
  const auto saved =
      airfix::campaign::saveCampaignState(temporary.campaign(), replacement);
  require(!saved.backupRotated &&
              readFile(temporary.backup()) == backupBefore &&
              airfix::campaign::loadCampaignState(temporary.campaign()).state ==
                  replacement,
          "malformed current was promoted or not safely replaced");
}

void testFutureCurrentAndBackupPreservation() {
  {
    TempDirectory temporary;
    (void)airfix::campaign::saveCampaignState(temporary.campaign(), state(2));
    (void)airfix::campaign::saveCampaignState(temporary.campaign(), state(4));
    auto future = readFile(temporary.current());
    repairFutureEnvelope(future);
    writeRaw(temporary.current(), future);

    const auto loaded =
        airfix::campaign::loadCampaignState(temporary.campaign());
    require(loaded.state == airfix::campaign::CampaignStateRecord{} &&
                loaded.source ==
                    airfix::campaign::CampaignStateLoadSource::defaults &&
                loaded.current.status ==
                    airfix::campaign::CampaignStateFileStatus::futureSchema &&
                loaded.current.schemaVersion == 2U &&
                loaded.persistenceBlocked &&
                !airfix::campaign::campaignStateNeedsRepair(loaded),
            "future current was interpreted or fell back");

    const auto before = readFile(temporary.current());
    const auto requested = state(6);
    requireStoreError(
        [&] {
          (void)airfix::campaign::saveCampaignState(temporary.campaign(),
                                                    requested);
        },
        airfix::campaign::CampaignStateStoreErrorKind::persistenceBlocked,
        temporary.campaign(), requested);
    require(readFile(temporary.current()) == before,
            "downgrade save rewrote future current bytes");
  }

  {
    TempDirectory temporary;
    const auto current = state(5);
    (void)airfix::campaign::saveCampaignState(temporary.campaign(), current);
    auto future = airfix::campaign::encodeCampaignStateDocument(state(7));
    repairFutureEnvelope(future);
    writeRaw(temporary.backup(), future);

    const auto currentBefore = readFile(temporary.current());
    const auto backupBefore = readFile(temporary.backup());
    const auto loaded =
        airfix::campaign::loadCampaignState(temporary.campaign());
    require(loaded.state == current &&
                loaded.source ==
                    airfix::campaign::CampaignStateLoadSource::current &&
                loaded.backup.status ==
                    airfix::campaign::CampaignStateFileStatus::futureSchema &&
                loaded.backup.schemaVersion == 2U && loaded.persistenceBlocked,
            "future backup displaced current or did not block persistence");

    const auto requested = state(10);
    requireStoreError(
        [&] {
          (void)airfix::campaign::saveCampaignState(temporary.campaign(),
                                                    requested);
        },
        airfix::campaign::CampaignStateStoreErrorKind::persistenceBlocked,
        temporary.campaign(), requested);
    require(readFile(temporary.current()) == currentBefore &&
                readFile(temporary.backup()) == backupBefore,
            "blocked save changed a retained future backup");
  }
}

struct ReplaceHookContext final {
  enum class Mode {
    throwBefore,
    replaceThenThrow,
  };

  Mode mode{Mode::throwBefore};
  bool throwOnCurrentOnly{};
};

void replaceHook(const std::filesystem::path &prepared,
                 const std::filesystem::path &target, void *const opaque) {
  auto &context = *static_cast<ReplaceHookContext *>(opaque);
  const bool current = target.filename() == currentName;
  if (context.throwOnCurrentOnly && !current) {
    airfix::io::replaceFileDurable(prepared, target);
    return;
  }
  if (context.mode == ReplaceHookContext::Mode::replaceThenThrow) {
    airfix::io::replaceFileDurable(prepared, target);
  }
  throw std::runtime_error("synthetic campaign replacement failure");
}

void throwRetry(const std::filesystem::path &, const std::filesystem::path &,
                void *) {
  throw std::runtime_error("synthetic campaign durability failure");
}

void testFailureAndAmbiguousReadback() {
  TempDirectory temporary;
  const auto first = state(2);
  (void)airfix::campaign::saveCampaignState(temporary.campaign(), first);

  ReplaceHookContext before{
      .mode = ReplaceHookContext::Mode::throwBefore,
      .throwOnCurrentOnly = true,
  };
  const airfix::campaign::testing::CampaignStateStoreHooks beforeHooks{
      .replace = replaceHook,
      .context = &before,
  };
  const auto beforeCandidate = state(3);
  requireStoreError(
      [&] {
        (void)airfix::campaign::testing::saveCampaignStateWithHooks(
            temporary.campaign(), beforeCandidate, beforeHooks);
      },
      airfix::campaign::CampaignStateStoreErrorKind::saveFailed,
      temporary.campaign(), beforeCandidate);
  require(airfix::campaign::loadCampaignState(temporary.campaign()).state ==
              first,
          "pre-publication failure changed active campaign state");

  ReplaceHookContext after{
      .mode = ReplaceHookContext::Mode::replaceThenThrow,
      .throwOnCurrentOnly = true,
  };
  const airfix::campaign::testing::CampaignStateStoreHooks afterHooks{
      .replace = replaceHook,
      .context = &after,
  };
  const auto afterCandidate = state(4);
  const auto confirmed = airfix::campaign::testing::saveCampaignStateWithHooks(
      temporary.campaign(), afterCandidate, afterHooks);
  require(confirmed.status == airfix::campaign::CampaignStateSaveStatus::
                                  committedAfterReadback &&
              airfix::campaign::loadCampaignState(temporary.campaign()).state ==
                  afterCandidate,
          "post-publication failure was not confirmed by exact readback");

  const airfix::campaign::testing::CampaignStateStoreHooks unknownHooks{
      .replace = replaceHook,
      .retryDurability = throwRetry,
      .context = &after,
  };
  const auto unknownCandidate = state(5);
  requireStoreError(
      [&] {
        (void)airfix::campaign::testing::saveCampaignStateWithHooks(
            temporary.campaign(), unknownCandidate, unknownHooks);
      },
      airfix::campaign::CampaignStateStoreErrorKind::commitUnknown,
      temporary.campaign(), unknownCandidate);
  require(airfix::campaign::loadCampaignState(temporary.campaign()).state ==
              unknownCandidate,
          "commit-unknown readback did not expose the published state");
}

void testWrongTypesOversizeAndLinks() {
  {
    TempDirectory temporary;
    std::filesystem::create_directory(temporary.campaign());
    std::filesystem::create_directory(temporary.current());
    const auto loaded =
        airfix::campaign::loadCampaignState(temporary.campaign());
    require(
        loaded.current.status ==
                airfix::campaign::CampaignStateFileStatus::wrongTypeOrLinked &&
            loaded.persistenceBlocked,
        "wrong-type current did not fail closed");
    const auto candidate = state(4);
    requireStoreError(
        [&] {
          (void)airfix::campaign::saveCampaignState(temporary.campaign(),
                                                    candidate);
        },
        airfix::campaign::CampaignStateStoreErrorKind::persistenceBlocked,
        temporary.campaign(), candidate);
  }

  {
    TempDirectory temporary;
    std::filesystem::create_directory(temporary.campaign());
    const std::vector<std::uint8_t> oversized(
        airfix::campaign::maximumCampaignStateDocumentBytes + 1U, 0xA5U);
    writeRaw(temporary.current(), oversized);
    const auto loaded =
        airfix::campaign::loadCampaignState(temporary.campaign());
    require(loaded.current.status ==
                    airfix::campaign::CampaignStateFileStatus::oversized &&
                loaded.source ==
                    airfix::campaign::CampaignStateLoadSource::defaults &&
                !loaded.persistenceBlocked &&
                airfix::campaign::campaignStateNeedsRepair(loaded),
            "oversized current did not select replaceable defaults");
    const auto replacement = state(6);
    (void)airfix::campaign::saveCampaignState(temporary.campaign(),
                                              replacement);
    require(airfix::campaign::loadCampaignState(temporary.campaign()).state ==
                replacement,
            "oversized current could not be replaced safely");
  }

  {
    TempDirectory temporary;
    const auto current = state(7);
    (void)airfix::campaign::saveCampaignState(temporary.campaign(), current);
    std::error_code error;
    std::filesystem::create_hard_link(
        temporary.current(), temporary.campaign() / "linked-alias.afcs", error);
    require(!error, "cannot create campaign state hard-link fixture");
    const auto loaded =
        airfix::campaign::loadCampaignState(temporary.campaign());
    require(
        loaded.current.status ==
                airfix::campaign::CampaignStateFileStatus::wrongTypeOrLinked &&
            loaded.persistenceBlocked,
        "multiply linked current did not block persistence");
  }
}

void testPreparedEntrySafety() {
  {
    TempDirectory temporary;
    (void)airfix::campaign::saveCampaignState(temporary.campaign(), state(2));
    writeRaw(temporary.currentPartial(),
             std::array<std::uint8_t, 3U>{1U, 2U, 3U});
    const auto saved =
        airfix::campaign::saveCampaignState(temporary.campaign(), state(3));
    require(saved.status ==
                    airfix::campaign::CampaignStateSaveStatus::committed &&
                !std::filesystem::exists(temporary.currentPartial()),
            "owned stale partial was not safely replaced and removed");
  }

  {
    TempDirectory temporary;
    const auto first = state(4);
    (void)airfix::campaign::saveCampaignState(temporary.campaign(), first);
    require(std::filesystem::create_directory(temporary.currentPartial()),
            "cannot create unsafe campaign partial fixture");
    requireStoreError(
        [&] {
          (void)airfix::campaign::saveCampaignState(temporary.campaign(),
                                                    state(5));
        },
        airfix::campaign::CampaignStateStoreErrorKind::saveFailed,
        temporary.campaign());
    require(
        std::filesystem::is_directory(temporary.currentPartial()) &&
            airfix::campaign::loadCampaignState(temporary.campaign()).state ==
                first,
        "unsafe partial was removed or changed active state");
  }
}

void testInvalidStateAndDirectory() {
  {
    TempDirectory temporary;
    auto invalid = state(2);
    invalid.storedThread = static_cast<airfix::campaign::CampaignSide>(2U);
    requireStoreError(
        [&] {
          (void)airfix::campaign::saveCampaignState(temporary.campaign(),
                                                    invalid);
        },
        airfix::campaign::CampaignStateStoreErrorKind::invalidState,
        temporary.campaign());
    require(!std::filesystem::exists(temporary.campaign()),
            "invalid state created persistent storage");
  }

  {
    const auto relative = std::filesystem::path{"relative-campaign"};
    const auto loaded = airfix::campaign::loadCampaignState(relative);
    require(loaded.state == airfix::campaign::CampaignStateRecord{} &&
                loaded.source ==
                    airfix::campaign::CampaignStateLoadSource::defaults &&
                loaded.current.status ==
                    airfix::campaign::CampaignStateFileStatus::ioUnavailable &&
                loaded.persistenceBlocked,
            "relative campaign load did not fail closed to defaults");
    requireStoreError(
        [&] { (void)airfix::campaign::saveCampaignState(relative, state(2)); },
        airfix::campaign::CampaignStateStoreErrorKind::invalidDirectory,
        relative);
  }
}

void testLinkedCampaignDirectoryAndCurrent() {
  {
    TempDirectory temporary;
    const auto target = temporary.campaign().parent_path() / "campaign-target";
    require(std::filesystem::create_directory(target),
            "cannot create linked campaign target");
    writeRaw(target / currentName,
             airfix::campaign::encodeCampaignStateDocument(state(3)));

    std::error_code error;
    std::filesystem::create_directory_symlink(target, temporary.campaign(),
                                              error);
    if (!error) {
      const auto loaded =
          airfix::campaign::loadCampaignState(temporary.campaign());
      require(loaded.state == airfix::campaign::CampaignStateRecord{} &&
                  loaded.current.status ==
                      airfix::campaign::CampaignStateFileStatus::
                          wrongTypeOrLinked &&
                  loaded.persistenceBlocked,
              "linked campaign directory was followed");
      requireStoreError(
          [&] {
            (void)airfix::campaign::saveCampaignState(temporary.campaign(),
                                                      state(4));
          },
          airfix::campaign::CampaignStateStoreErrorKind::invalidDirectory,
          temporary.campaign());
    }
  }

  {
    TempDirectory temporary;
    std::filesystem::create_directory(temporary.campaign());
    const auto target = temporary.campaign() / "symlink-target.afcs";
    writeRaw(target, airfix::campaign::encodeCampaignStateDocument(state(5)));
    std::error_code error;
    std::filesystem::create_symlink(target.filename(), temporary.current(),
                                    error);
    if (!error) {
      const auto loaded =
          airfix::campaign::loadCampaignState(temporary.campaign());
      require(loaded.current.status ==
                      airfix::campaign::CampaignStateFileStatus::
                          wrongTypeOrLinked &&
                  loaded.persistenceBlocked,
              "linked current campaign state was followed");
    }
  }
}

} // namespace

int main() {
  try {
    testMissingSaveUnchangedRotationAndFallback();
    testMalformedCurrentNeverRotates();
    testFutureCurrentAndBackupPreservation();
    testFailureAndAmbiguousReadback();
    testWrongTypesOversizeAndLinks();
    testPreparedEntrySafety();
    testInvalidStateAndDirectory();
    testLinkedCampaignDirectoryAndCurrent();
    std::cout << "Campaign state store tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Campaign state store tests failed: " << error.what() << '\n';
    return 1;
  }
}
