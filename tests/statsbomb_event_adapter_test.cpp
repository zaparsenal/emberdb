#include "emberdb/ingestion/statsbomb_event_adapter.h"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

namespace {

std::filesystem::path fixture(const std::string& name) {
  return std::filesystem::path(EMBERDB_TEST_FIXTURES_DIR) / name;
}

TEST(StatsBombEventAdapterTest, LoadsCompleteEventAndNormalizesFields) {
  const emberdb::StatsBombEventAdapter adapter;
  const auto events = adapter.loadEvents(fixture("complete_events.json"), {12345});

  ASSERT_EQ(events.size(), 2U);
  const auto& event = events.front();
  EXPECT_EQ(event.provider_event_id, "evt-pass-1");
  EXPECT_EQ(event.match_id, 12345);
  EXPECT_EQ(event.period, 1);
  EXPECT_EQ(event.time.timestamp, std::chrono::milliseconds(754567));
  EXPECT_EQ(event.time.minute, 12);
  EXPECT_EQ(event.time.second, 34);
  EXPECT_EQ(event.possession_id, 18);
  EXPECT_EQ(event.team_id, 10);
  EXPECT_EQ(event.team_name, "Ember FC");
  EXPECT_EQ(event.player_id, 99);
  EXPECT_EQ(event.player_name, "Alex Forward");
  EXPECT_EQ(event.event_type, "Pass");
  EXPECT_EQ(event.outcome, "Incomplete");
  EXPECT_EQ(event.provider, "StatsBomb");
}

TEST(StatsBombEventAdapterTest, LoadsStartAndPassOrCarryEndCoordinates) {
  const emberdb::StatsBombEventAdapter adapter;
  const auto events = adapter.loadEvents(fixture("complete_events.json"), {1});

  ASSERT_EQ(events.size(), 2U);
  ASSERT_TRUE(events[0].start_location);
  ASSERT_TRUE(events[0].end_location);
  ASSERT_TRUE(events[0].source_start_location);
  ASSERT_TRUE(events[0].source_end_location);
  EXPECT_DOUBLE_EQ(events[0].start_location->x, 42.5 / 120.0 * 100.0);
  EXPECT_DOUBLE_EQ(events[0].start_location->y, 31.25 / 80.0 * 100.0);
  EXPECT_DOUBLE_EQ(events[0].end_location->x, 71.0 / 120.0 * 100.0);
  EXPECT_DOUBLE_EQ(events[0].end_location->y, 22.5 / 80.0 * 100.0);
  EXPECT_EQ(*events[0].source_start_location,
            (emberdb::Coordinate{42.5, 31.25}));
  EXPECT_EQ(*events[0].source_end_location, (emberdb::Coordinate{71.0, 22.5}));
  ASSERT_TRUE(events[1].end_location);
  ASSERT_TRUE(events[1].source_end_location);
  EXPECT_DOUBLE_EQ(events[1].end_location->x, 78.0 / 120.0 * 100.0);
  EXPECT_DOUBLE_EQ(events[1].end_location->y, 29.0 / 80.0 * 100.0);
  EXPECT_EQ(*events[1].source_end_location, (emberdb::Coordinate{78.0, 29.0}));
}

TEST(StatsBombEventAdapterTest, PreservesNullsForMissingOptionalFieldsAndPlayer) {
  const emberdb::StatsBombEventAdapter adapter;
  const auto events = adapter.loadEvents(fixture("missing_optional_fields.json"), {55});

  ASSERT_EQ(events.size(), 1U);
  const auto& event = events.front();
  EXPECT_FALSE(event.possession_id);
  EXPECT_FALSE(event.player_id);
  EXPECT_FALSE(event.player_name);
  EXPECT_FALSE(event.outcome);
  EXPECT_FALSE(event.start_location);
  EXPECT_FALSE(event.end_location);
  EXPECT_FALSE(event.source_start_location);
  EXPECT_FALSE(event.source_end_location);
}

TEST(StatsBombEventAdapterTest, RejectsCoordinatesOutsideStatsBombPitchBounds) {
  const emberdb::StatsBombEventAdapter adapter;
  EXPECT_THROW(
      {
        try {
          static_cast<void>(adapter.loadEvents(fixture("invalid_coordinates.json"), {1}));
        } catch (const std::runtime_error& error) {
          const std::string message(error.what());
          EXPECT_NE(message.find("index 0"), std::string::npos);
          EXPECT_NE(message.find("location"), std::string::npos);
          EXPECT_NE(message.find("120"), std::string::npos);
          throw;
        }
      },
      std::runtime_error);
}

TEST(StatsBombEventAdapterTest, RejectsInvalidJsonWithFileContext) {
  const emberdb::StatsBombEventAdapter adapter;
  EXPECT_THROW(
      {
        try {
          static_cast<void>(adapter.loadEvents(fixture("invalid.json"), {1}));
        } catch (const std::runtime_error& error) {
          EXPECT_NE(std::string(error.what()).find("invalid.json"), std::string::npos);
          throw;
        }
      },
      std::runtime_error);
}

TEST(StatsBombEventAdapterTest, RejectsInvalidTopLevelJson) {
  const emberdb::StatsBombEventAdapter adapter;
  EXPECT_THROW(static_cast<void>(adapter.loadEvents(fixture("not_an_array.json"), {1})),
               std::runtime_error);
}

TEST(StatsBombEventAdapterTest, RejectsUnreadableFileWithPathContext) {
  const emberdb::StatsBombEventAdapter adapter;
  const auto path = fixture("does-not-exist.json");
  EXPECT_THROW(
      {
        try {
          static_cast<void>(adapter.loadEvents(path, {1}));
        } catch (const std::runtime_error& error) {
          EXPECT_NE(std::string(error.what()).find(path.string()), std::string::npos);
          throw;
        }
      },
      std::runtime_error);
}

}  // namespace
