#include "airfix/content/MissionLaunchSelectionStore.hpp"

#include "airfix/io/DurableFile.hpp"

#include "airfix/crypto/Sha256.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char *currentName = "mission-selection.afmission";

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
    root_ = std::filesystem::temp_directory_path() /
            ("airfix-mission-selection-" + std::to_string(stamp));
    if (!std::filesystem::create_directory(root_)) {
      throw std::runtime_error("cannot create mission-selection test root");
    }
    store_ = root_ / "selection";
  }

  ~TempDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  [[nodiscard]] const std::filesystem::path &store() const { return store_; }

private:
  std::filesystem::path root_;
  std::filesystem::path store_;
};

[[nodiscard]] airfix::content::MissionLaunchSelection
selection(const std::uint32_t startIndex) {
  return {
      .setupLogicalPath = "Setup\\Synthetic.setup",
      .levelLogicalPath = "Levels\\Synthetic.level",
      .playerObjectLogicalPath = "Objects\\Synthetic.object",
      .requestedStartIndex = startIndex,
  };
}

void writeRaw(const std::filesystem::path &path,
              const std::vector<std::uint8_t> &bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("cannot write mission-selection fixture");
  }
}

void putU32(std::vector<std::uint8_t> &bytes, const std::size_t offset,
            const std::uint32_t value) {
  for (std::size_t index = 0U; index < 4U; ++index) {
    bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
  }
}

void repairEnvelope(std::vector<std::uint8_t> &bytes) {
  airfix::crypto::Sha256 hash;
  hash.update(std::span<const std::uint8_t>(bytes).first(16U));
  hash.update(std::span<const std::uint8_t>(bytes).subspan(48U));
  const auto digest = hash.finish();
  std::copy(digest.begin(), digest.end(), bytes.begin() + 16);
}

void testSaveRotationAndRecovery() {
  TempDirectory temporary;
  const auto missing =
      airfix::content::loadMissionLaunchSelection(temporary.store());
  require(!missing.selection.has_value() && !missing.persistenceBlocked,
          "missing mission selection did not remain optional");

  const auto first = selection(1U);
  const auto firstSave =
      airfix::content::saveMissionLaunchSelection(temporary.store(), first);
  require(firstSave.status ==
                  airfix::content::MissionSelectionSaveStatus::committed &&
              !firstSave.backupRotated,
          "first mission selection save had the wrong result");
  require(airfix::content::loadMissionLaunchSelection(temporary.store())
                  .selection == first,
          "mission selection did not survive reload");

  const auto unchanged =
      airfix::content::saveMissionLaunchSelection(temporary.store(), first);
  require(unchanged.status ==
              airfix::content::MissionSelectionSaveStatus::unchanged,
          "idempotent mission selection save rewrote state");

  const auto second = selection(7U);
  const auto secondSave =
      airfix::content::saveMissionLaunchSelection(temporary.store(), second);
  require(secondSave.backupRotated,
          "changed mission selection did not rotate current state");

  auto bytes = airfix::io::readBoundedRegularFile(
      temporary.store() / currentName,
      airfix::content::maximumMissionSelectionDocumentBytes);
  bytes.back() ^= 1U;
  writeRaw(temporary.store() / currentName, bytes);
  const auto recovered =
      airfix::content::loadMissionLaunchSelection(temporary.store());
  require(recovered.selection == first && recovered.recoveredFromBackup(),
          "corrupt current did not recover the validated backup");
}

void testMalformedReplacementAndWrongTypeBlock() {
  TempDirectory temporary;
  std::filesystem::create_directory(temporary.store());
  writeRaw(temporary.store() / currentName,
           std::vector<std::uint8_t>{'B', 'A', 'D'});
  const auto candidate = selection(4U);
  (void)airfix::content::saveMissionLaunchSelection(temporary.store(),
                                                    candidate);
  require(airfix::content::loadMissionLaunchSelection(temporary.store())
                  .selection == candidate,
          "malformed current could not be replaced");

  TempDirectory wrongType;
  std::filesystem::create_directory(wrongType.store());
  std::filesystem::create_directory(wrongType.store() / currentName);
  const auto blocked =
      airfix::content::loadMissionLaunchSelection(wrongType.store());
  require(
      blocked.persistenceBlocked &&
          blocked.current ==
              airfix::content::MissionSelectionFileStatus::wrongTypeOrLinked,
      "wrong-type mission selection did not block persistence");
  try {
    (void)airfix::content::saveMissionLaunchSelection(wrongType.store(),
                                                      candidate);
  } catch (const airfix::content::MissionSelectionStoreError &error) {
    require(
        error.kind() ==
            airfix::content::MissionSelectionStoreErrorKind::persistenceBlocked,
        "wrong-type store returned the wrong failure");
    return;
  }
  throw std::runtime_error("wrong-type mission selection was overwritten");
}

void testFutureSchemaIsPreserved() {
  TempDirectory temporary;
  (void)airfix::content::saveMissionLaunchSelection(temporary.store(),
                                                    selection(5U));
  auto future = airfix::io::readBoundedRegularFile(
      temporary.store() / currentName,
      airfix::content::maximumMissionSelectionDocumentBytes);
  putU32(future, 8U, 2U);
  repairEnvelope(future);
  writeRaw(temporary.store() / currentName, future);

  const auto loaded =
      airfix::content::loadMissionLaunchSelection(temporary.store());
  require(!loaded.selection.has_value() && loaded.persistenceBlocked &&
              loaded.current ==
                  airfix::content::MissionSelectionFileStatus::futureSchema,
          "future mission-selection schema was interpreted or downgraded");
  try {
    (void)airfix::content::saveMissionLaunchSelection(temporary.store(),
                                                      selection(6U));
  } catch (const airfix::content::MissionSelectionStoreError &error) {
    require(
        error.kind() ==
            airfix::content::MissionSelectionStoreErrorKind::persistenceBlocked,
        "future schema returned the wrong store failure");
    return;
  }
  throw std::runtime_error("future mission-selection schema was overwritten");
}

} // namespace

int main() {
  try {
    testSaveRotationAndRecovery();
    testMalformedReplacementAndWrongTypeBlock();
    testFutureSchemaIsPreserved();
    std::cout << "Mission launch selection store tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Mission launch selection store tests failed: " << error.what()
              << '\n';
    return 1;
  }
}
