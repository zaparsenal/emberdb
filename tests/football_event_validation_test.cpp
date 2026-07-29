#include "emberdb/common/football_event.h"

#include <chrono>
#include <limits>
#include <stdexcept>

#include <gtest/gtest.h>

namespace {

emberdb::FootballEvent validEvent() {
  return {"provider-id",
          42,
          1,
          {std::chrono::milliseconds{1234}, 7, 8},
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

TEST(FootballEventValidationTest, AcceptsCompleteAndExplicitlyNullableEvents) {
  auto event = validEvent();
  EXPECT_NO_THROW(emberdb::validateFootballEvent(event));

  event.possession_id.reset();
  event.team_id.reset();
  event.team_name.reset();
  event.player_id.reset();
  event.player_name.reset();
  event.outcome.reset();
  event.start_location.reset();
  event.end_location.reset();
  event.source_start_location.reset();
  event.source_end_location.reset();
  EXPECT_NO_THROW(emberdb::validateFootballEvent(event));
}

TEST(FootballEventValidationTest, RejectsBlankRequiredAndOptionalText) {
  auto event = validEvent();
  event.provider_event_id = " ";
  EXPECT_THROW(emberdb::validateFootballEvent(event), std::invalid_argument);

  event = validEvent();
  event.event_type.clear();
  EXPECT_THROW(emberdb::validateFootballEvent(event), std::invalid_argument);

  event = validEvent();
  event.provider.clear();
  EXPECT_THROW(emberdb::validateFootballEvent(event), std::invalid_argument);

  event = validEvent();
  event.player_name = "\t";
  EXPECT_THROW(emberdb::validateFootballEvent(event), std::invalid_argument);
}

TEST(FootballEventValidationTest, RejectsInvalidIdentifiersAndMatchTime) {
  auto event = validEvent();
  event.match_id = 0;
  EXPECT_THROW(emberdb::validateFootballEvent(event), std::invalid_argument);

  event = validEvent();
  event.player_id = -1;
  EXPECT_THROW(emberdb::validateFootballEvent(event), std::invalid_argument);

  event = validEvent();
  event.period = 0;
  EXPECT_THROW(emberdb::validateFootballEvent(event), std::invalid_argument);

  event = validEvent();
  event.time.timestamp = std::chrono::milliseconds{-1};
  EXPECT_THROW(emberdb::validateFootballEvent(event), std::invalid_argument);

  event = validEvent();
  event.time.second = 60;
  EXPECT_THROW(emberdb::validateFootballEvent(event), std::invalid_argument);
}

TEST(FootballEventValidationTest, RejectsInvalidCanonicalAndSourceCoordinates) {
  auto event = validEvent();
  event.start_location = emberdb::Coordinate{100.1, 50.0};
  EXPECT_THROW(emberdb::validateFootballEvent(event), std::invalid_argument);

  event = validEvent();
  event.source_start_location = emberdb::Coordinate{
      std::numeric_limits<double>::infinity(), 40.0};
  EXPECT_THROW(emberdb::validateFootballEvent(event), std::invalid_argument);
}

}  // namespace
