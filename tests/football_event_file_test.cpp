#include "emberdb/storage/football_event_file.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

namespace {

class ScopedTestFile {
 public:
  explicit ScopedTestFile(std::string name)
      : path_(std::filesystem::path(testing::TempDir()) / std::move(name)) {
    cleanup();
  }
  ~ScopedTestFile() { cleanup(); }

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

 private:
  void cleanup() const {
    std::error_code error;
    std::filesystem::remove(path_, error);
    auto temporary = path_;
    temporary += ".tmp";
    std::filesystem::remove(temporary, error);
  }

  std::filesystem::path path_;
};

emberdb::FootballEvent completeEvent() {
  return emberdb::FootballEvent{"provider-id",
                                42,
                                1,
                                {std::chrono::milliseconds(1234), 7, 8},
                                3,
                                11,
                                "Ember FC",
                                22,
                                "Sam Striker",
                                "Pass",
                                "Complete",
                                emberdb::Coordinate{1.5, 2.5},
                                emberdb::Coordinate{3.5, 4.5},
                                "StatsBomb",
                                emberdb::Coordinate{1.8, 2.8},
                                emberdb::Coordinate{3.8, 4.8}};
}

void expectEventEquals(const emberdb::FootballEvent& actual,
                       const emberdb::FootballEvent& expected) {
  EXPECT_EQ(actual.provider_event_id, expected.provider_event_id);
  EXPECT_EQ(actual.match_id, expected.match_id);
  EXPECT_EQ(actual.period, expected.period);
  EXPECT_EQ(actual.time, expected.time);
  EXPECT_EQ(actual.possession_id, expected.possession_id);
  EXPECT_EQ(actual.team_id, expected.team_id);
  EXPECT_EQ(actual.team_name, expected.team_name);
  EXPECT_EQ(actual.player_id, expected.player_id);
  EXPECT_EQ(actual.player_name, expected.player_name);
  EXPECT_EQ(actual.event_type, expected.event_type);
  EXPECT_EQ(actual.outcome, expected.outcome);
  EXPECT_EQ(actual.start_location, expected.start_location);
  EXPECT_EQ(actual.end_location, expected.end_location);
  EXPECT_EQ(actual.provider, expected.provider);
  EXPECT_EQ(actual.source_start_location, expected.source_start_location);
  EXPECT_EQ(actual.source_end_location, expected.source_end_location);
}

emberdb::FootballEventTable tableWithCompleteAndNullRows() {
  emberdb::FootballEventTable table;
  table.append(completeEvent());
  auto nullable = completeEvent();
  nullable.provider_event_id = "nullable-id";
  nullable.possession_id.reset();
  nullable.team_id.reset();
  nullable.team_name.reset();
  nullable.player_id.reset();
  nullable.player_name.reset();
  nullable.outcome.reset();
  nullable.start_location.reset();
  nullable.end_location.reset();
  nullable.source_start_location.reset();
  nullable.source_end_location.reset();
  table.append(nullable);
  return table;
}

TEST(FootballEventFileTest, RoundTripsEveryTypedAndNullableColumnExactly) {
  const ScopedTestFile file("emberdb-round-trip.ember");
  const auto original = tableWithCompleteAndNullRows();

  emberdb::saveFootballEventTable(original, file.path());
  const auto restored = emberdb::loadFootballEventTable(file.path());

  ASSERT_EQ(restored.rowCount(), original.rowCount());
  EXPECT_TRUE(restored.validate());
  for (std::size_t row = 0; row < original.rowCount(); ++row) {
    expectEventEquals(restored.row(row), original.row(row));
  }
  EXPECT_EQ(emberdb::kFootballEventFileFormatVersion, 2U);
}

TEST(FootballEventFileTest, RoundTripsAnEmptyTable) {
  const ScopedTestFile file("emberdb-empty-round-trip.ember");
  const emberdb::FootballEventTable empty;
  emberdb::saveFootballEventTable(empty, file.path());
  const auto restored = emberdb::loadFootballEventTable(file.path());
  EXPECT_EQ(restored.rowCount(), 0U);
  EXPECT_TRUE(restored.validate());
}

TEST(FootballEventFileTest, RefusesToOverwriteAnExistingDatabase) {
  const ScopedTestFile file("emberdb-existing-output.ember");
  const auto table = tableWithCompleteAndNullRows();
  emberdb::saveFootballEventTable(table, file.path());
  EXPECT_THROW(emberdb::saveFootballEventTable(table, file.path()),
               std::runtime_error);
}

TEST(FootballEventFileTest, RejectsUnsupportedFormatVersions) {
  const ScopedTestFile file("emberdb-invalid-version.ember");
  emberdb::saveFootballEventTable(tableWithCompleteAndNullRows(), file.path());
  std::fstream stream(file.path(), std::ios::binary | std::ios::in | std::ios::out);
  stream.seekp(8);
  const char version[] = {3, 0};
  stream.write(version, 2);
  stream.close();

  EXPECT_THROW(
      {
        try {
          static_cast<void>(emberdb::loadFootballEventTable(file.path()));
        } catch (const std::runtime_error& error) {
          EXPECT_NE(std::string(error.what()).find("version"), std::string::npos);
          throw;
        }
      },
      std::runtime_error);
}

TEST(FootballEventFileTest, RejectsChecksumCorruption) {
  const ScopedTestFile file("emberdb-checksum-corruption.ember");
  emberdb::saveFootballEventTable(tableWithCompleteAndNullRows(), file.path());
  std::fstream stream(file.path(), std::ios::binary | std::ios::in | std::ios::out);
  stream.seekg(-1, std::ios::end);
  char value{};
  stream.read(&value, 1);
  value ^= 1;
  stream.seekp(-1, std::ios::end);
  stream.write(&value, 1);
  stream.close();

  EXPECT_THROW(
      {
        try {
          static_cast<void>(emberdb::loadFootballEventTable(file.path()));
        } catch (const std::runtime_error& error) {
          EXPECT_NE(std::string(error.what()).find("checksum"), std::string::npos);
          throw;
        }
      },
      std::runtime_error);
}

TEST(FootballEventFileTest, RejectsTruncatedFiles) {
  const ScopedTestFile file("emberdb-truncated.ember");
  emberdb::saveFootballEventTable(tableWithCompleteAndNullRows(), file.path());
  std::filesystem::resize_file(file.path(),
                               std::filesystem::file_size(file.path()) - 1U);
  EXPECT_THROW(static_cast<void>(emberdb::loadFootballEventTable(file.path())),
               std::runtime_error);
}

TEST(FootballEventFileTest, RejectsUnreadableFilesWithPathContext) {
  const ScopedTestFile file("emberdb-does-not-exist.ember");
  EXPECT_THROW(
      {
        try {
          static_cast<void>(emberdb::loadFootballEventTable(file.path()));
        } catch (const std::runtime_error& error) {
          EXPECT_NE(std::string(error.what()).find(file.path().string()),
                    std::string::npos);
          throw;
        }
      },
      std::runtime_error);
}

}  // namespace
