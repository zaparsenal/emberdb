#include "emberdb/storage/football_event_table.h"

#include <chrono>
#include <optional>
#include <stdexcept>

#include <gtest/gtest.h>

namespace {

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
                                "StatsBomb"};
}

TEST(FootballEventTableTest, AppendsRowsAndKeepsColumnLengthsConsistent) {
  emberdb::FootballEventTable table;
  EXPECT_EQ(table.rowCount(), 0U);
  EXPECT_TRUE(table.validate());

  table.append(completeEvent());
  table.append(completeEvent());

  EXPECT_EQ(table.rowCount(), 2U);
  EXPECT_TRUE(table.validate());
  EXPECT_EQ(table.playerDataCount(), 2U);
  EXPECT_EQ(table.startLocationCount(), 2U);
  EXPECT_EQ(table.endLocationCount(), 2U);
}

TEST(FootballEventTableTest, RoundTripsTypedValues) {
  emberdb::FootballEventTable table;
  const auto original = completeEvent();
  table.append(original);

  const auto restored = table.row(0);
  EXPECT_EQ(restored.provider_event_id, original.provider_event_id);
  EXPECT_EQ(restored.match_id, original.match_id);
  EXPECT_EQ(restored.time, original.time);
  EXPECT_EQ(restored.player_id, original.player_id);
  EXPECT_EQ(restored.start_location, original.start_location);
  EXPECT_EQ(restored.end_location, original.end_location);
}

TEST(FootballEventTableTest, PreservesNullColumns) {
  auto event = completeEvent();
  event.possession_id.reset();
  event.player_id.reset();
  event.player_name.reset();
  event.outcome.reset();
  event.start_location.reset();
  event.end_location.reset();

  emberdb::FootballEventTable table;
  table.append(event);
  const auto restored = table.row(0);

  EXPECT_FALSE(restored.possession_id);
  EXPECT_FALSE(restored.player_id);
  EXPECT_FALSE(restored.player_name);
  EXPECT_FALSE(restored.outcome);
  EXPECT_FALSE(restored.start_location);
  EXPECT_FALSE(restored.end_location);
  EXPECT_EQ(table.playerDataCount(), 0U);
}

TEST(FootballEventTableTest, RejectsOutOfRangeRows) {
  const emberdb::FootballEventTable table;
  EXPECT_THROW(static_cast<void>(table.row(0)), std::out_of_range);
}

}  // namespace
