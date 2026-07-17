#include "emberdb/ingestion/wyscout_event_adapter.h"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

namespace {

std::filesystem::path fixture(const std::string& name) {
  return std::filesystem::path(EMBERDB_TEST_FIXTURES_DIR) / name;
}

TEST(WyscoutEventAdapterTest, FiltersCompetitionFileAndMapsDocumentedFields) {
  const emberdb::WyscoutEventAdapter adapter;
  const auto events = adapter.loadEvents(fixture("wyscout_events.json"), {2576335});

  ASSERT_EQ(events.size(), 2U);
  EXPECT_EQ(events[0].provider_event_id, "180864419");
  EXPECT_EQ(events[0].match_id, 2576335);
  EXPECT_EQ(events[0].period, 1);
  EXPECT_EQ(events[0].time.timestamp, std::chrono::milliseconds(2418));
  EXPECT_EQ(events[0].time.minute, 0);
  EXPECT_EQ(events[0].time.second, 2);
  EXPECT_EQ(events[0].team_id, 3161);
  EXPECT_EQ(events[0].player_id, 3344);
  EXPECT_EQ(events[0].event_type, "Pass");
  EXPECT_EQ(events[0].outcome, "Accurate");
  EXPECT_EQ(events[0].provider, "Wyscout");
  EXPECT_FALSE(events[0].team_name);
  EXPECT_FALSE(events[0].player_name);
  EXPECT_FALSE(events[0].possession_id);
  EXPECT_EQ(events[1].period, 2);
  EXPECT_EQ(events[1].time.minute, 47);
  EXPECT_EQ(events[1].time.second, 5);
  EXPECT_FALSE(events[1].player_id);
}

TEST(WyscoutEventAdapterTest, PreservesAlreadyAttackRelativeCoordinates) {
  const emberdb::WyscoutEventAdapter adapter;
  const auto events = adapter.loadEvents(fixture("wyscout_events.json"), {2576335});

  ASSERT_TRUE(events[0].start_location);
  ASSERT_TRUE(events[0].end_location);
  EXPECT_EQ(*events[0].start_location, (emberdb::Coordinate{49.0, 50.0}));
  EXPECT_EQ(*events[0].end_location, (emberdb::Coordinate{38.0, 58.0}));
  EXPECT_EQ(events[0].start_location, events[0].source_start_location);
  EXPECT_EQ(events[0].end_location, events[0].source_end_location);
  ASSERT_TRUE(events[1].start_location);
  EXPECT_FALSE(events[1].end_location);
}

TEST(WyscoutEventAdapterTest, RejectsCoordinatesOutsideDocumentedBounds) {
  const emberdb::WyscoutEventAdapter adapter;
  EXPECT_THROW(
      {
        try {
          static_cast<void>(adapter.loadEvents(
              fixture("wyscout_invalid_coordinates.json"), {2576335}));
        } catch (const std::runtime_error& error) {
          const std::string message(error.what());
          EXPECT_NE(message.find("index 0"), std::string::npos);
          EXPECT_NE(message.find("positions"), std::string::npos);
          throw;
        }
      },
      std::runtime_error);
}

TEST(WyscoutEventAdapterTest, RejectsMissingMatchWithFileAndIdContext) {
  const emberdb::WyscoutEventAdapter adapter;
  EXPECT_THROW(
      {
        try {
          static_cast<void>(
              adapter.loadEvents(fixture("wyscout_events.json"), {123}));
        } catch (const std::runtime_error& error) {
          const std::string message(error.what());
          EXPECT_NE(message.find("wyscout_events.json"), std::string::npos);
          EXPECT_NE(message.find("123"), std::string::npos);
          throw;
        }
      },
      std::runtime_error);
}

TEST(WyscoutEventAdapterTest, RejectsUnreadableFileWithPathContext) {
  const emberdb::WyscoutEventAdapter adapter;
  const auto path = fixture("missing-wyscout.json");
  EXPECT_THROW(
      {
        try {
          static_cast<void>(adapter.loadEvents(path, {2576335}));
        } catch (const std::runtime_error& error) {
          EXPECT_NE(std::string(error.what()).find(path.string()), std::string::npos);
          throw;
        }
      },
      std::runtime_error);
}

}  // namespace
