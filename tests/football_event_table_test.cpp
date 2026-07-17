#include "emberdb/storage/football_event_table.h"

#include <chrono>
#include <limits>
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
                                "StatsBomb",
                                emberdb::Coordinate{1.8, 2.8},
                                emberdb::Coordinate{3.8, 4.8}};
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
  EXPECT_EQ(restored.source_start_location, original.source_start_location);
  EXPECT_EQ(restored.source_end_location, original.source_end_location);
}

TEST(FootballEventTableTest, PreservesNullColumns) {
  auto event = completeEvent();
  event.possession_id.reset();
  event.player_id.reset();
  event.player_name.reset();
  event.outcome.reset();
  event.start_location.reset();
  event.end_location.reset();
  event.source_start_location.reset();
  event.source_end_location.reset();

  emberdb::FootballEventTable table;
  table.append(event);
  const auto restored = table.row(0);

  EXPECT_FALSE(restored.possession_id);
  EXPECT_FALSE(restored.player_id);
  EXPECT_FALSE(restored.player_name);
  EXPECT_FALSE(restored.outcome);
  EXPECT_FALSE(restored.start_location);
  EXPECT_FALSE(restored.end_location);
  EXPECT_FALSE(restored.source_start_location);
  EXPECT_FALSE(restored.source_end_location);
  EXPECT_EQ(table.playerDataCount(), 0U);
}

TEST(FootballEventTableTest, RejectsOutOfRangeRows) {
  const emberdb::FootballEventTable table;
  EXPECT_THROW(static_cast<void>(table.row(0)), std::out_of_range);
}

TEST(FootballEventTableTest, RejectsInvalidCanonicalAndSourceCoordinates) {
  auto invalid_canonical = completeEvent();
  invalid_canonical.start_location = emberdb::Coordinate{100.1, 50.0};
  emberdb::FootballEventTable table;
  EXPECT_THROW(table.append(invalid_canonical), std::invalid_argument);
  EXPECT_EQ(table.rowCount(), 0U);

  auto invalid_source = completeEvent();
  invalid_source.source_start_location = emberdb::Coordinate{
      std::numeric_limits<double>::infinity(), 40.0};
  EXPECT_THROW(table.append(invalid_source), std::invalid_argument);
  EXPECT_EQ(table.rowCount(), 0U);
}

}  // namespace
