#include "emberdb/ingestion/metrica_event_adapter.h"
#include "emberdb/identity/canonical_identity.h"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

namespace {

std::filesystem::path fixture(const std::string& name) {
  return std::filesystem::path(EMBERDB_TEST_FIXTURES_DIR) / name;
}

emberdb::ImportContext context(emberdb::AttackingDirection direction) {
  return {42, direction};
}

TEST(MetricaEventAdapterTest, MapsCsvFieldsAndDerivesStableEventIdentity) {
  const emberdb::MetricaEventAdapter adapter;
  const auto events = adapter.loadEvents(
      fixture("metrica_events.csv"),
      context(emberdb::AttackingDirection::LeftToRight));

  ASSERT_EQ(events.size(), 3U);
  EXPECT_EQ(events[0].provider_event_id, "metrica:42:100:0");
  EXPECT_EQ(events[0].match_id, 42);
  EXPECT_EQ(events[0].period, 1);
  EXPECT_EQ(events[0].time.timestamp, std::chrono::milliseconds(12500));
  EXPECT_EQ(events[0].time.minute, 0);
  EXPECT_EQ(events[0].time.second, 12);
  EXPECT_EQ(events[0].team_name, "Home");
  EXPECT_EQ(events[0].player_name, "Player9");
  EXPECT_EQ(events[0].event_type, "Pass");
  EXPECT_FALSE(events[0].outcome);
  EXPECT_EQ(events[0].provider, "Metrica");
  EXPECT_FALSE(events[0].team_id);
  EXPECT_FALSE(events[0].player_id);
  EXPECT_FALSE(events[0].possession_id);
  EXPECT_EQ(events[1].event_type, "Shot");
  EXPECT_EQ(events[1].outcome, "ON TARGET-GOAL");
  EXPECT_EQ(events[2].event_type, "Set Piece");
  EXPECT_EQ(events[2].time.minute, 48);
  EXPECT_EQ(events[2].time.second, 5);
}

TEST(MetricaEventAdapterTest, NormalizesEachTeamAndHalfToLeftToRight) {
  const emberdb::MetricaEventAdapter adapter;
  const auto events = adapter.loadEvents(
      fixture("metrica_events.csv"),
      context(emberdb::AttackingDirection::LeftToRight));

  ASSERT_TRUE(events[0].start_location);
  ASSERT_TRUE(events[0].end_location);
  EXPECT_EQ(*events[0].start_location, (emberdb::Coordinate{25.0, 40.0}));
  EXPECT_EQ(*events[0].end_location, (emberdb::Coordinate{75.0, 60.0}));
  ASSERT_TRUE(events[1].start_location);
  EXPECT_DOUBLE_EQ(events[1].start_location->x, 20.0);
  EXPECT_DOUBLE_EQ(events[1].start_location->y, 30.0);
  ASSERT_TRUE(events[1].source_start_location);
  EXPECT_EQ(*events[1].source_start_location, (emberdb::Coordinate{0.8, 0.3}));
  EXPECT_FALSE(events[2].start_location);
  EXPECT_FALSE(events[2].source_start_location);
}

TEST(MetricaEventAdapterTest, PreservesOffPitchEndButDoesNotCanonicalizeIt) {
  const emberdb::MetricaEventAdapter adapter;
  const auto events = adapter.loadEvents(
      fixture("metrica_events.csv"),
      context(emberdb::AttackingDirection::LeftToRight));

  ASSERT_TRUE(events[1].source_end_location);
  EXPECT_EQ(*events[1].source_end_location, (emberdb::Coordinate{1.02, 0.45}));
  EXPECT_FALSE(events[1].end_location);
}

TEST(MetricaEventAdapterTest, UsesConfiguredHomeDirectionAndFlipsAtHalfTime) {
  const emberdb::MetricaEventAdapter adapter;
  const auto events = adapter.loadEvents(
      fixture("metrica_events.csv"),
      context(emberdb::AttackingDirection::RightToLeft));

  ASSERT_TRUE(events[0].start_location);
  EXPECT_DOUBLE_EQ(events[0].start_location->x, 75.0);
  ASSERT_TRUE(events[1].start_location);
  EXPECT_DOUBLE_EQ(events[1].start_location->x, 80.0);
}

TEST(MetricaEventAdapterTest, RequiresDirectionMetadata) {
  const emberdb::MetricaEventAdapter adapter;
  EXPECT_THROW(
      static_cast<void>(adapter.loadEvents(fixture("metrica_events.csv"), {42})),
      std::runtime_error);
}

TEST(MetricaEventAdapterTest, ResolvesExplicitMatchScopedTeamMappings) {
  const emberdb::MetricaEventAdapter adapter;
  const auto events = adapter.loadEvents(
      fixture("metrica_events.csv"),
      context(emberdb::AttackingDirection::LeftToRight));
  emberdb::CanonicalIdentityCatalog catalog;
  catalog.addTeam({{1}, "Ember FC"});
  catalog.addTeam({{2}, "Ash United"});
  catalog.addMatch({{100}, "Example League", "2023/2024", std::nullopt,
                    {1}, {2}, std::nullopt, std::nullopt});
  catalog.mapMatch({"Metrica", "42"}, {100});
  catalog.mapMetricaTeams("42", {1}, {2});

  EXPECT_EQ(catalog.resolveEvent(events[0]).match_id,
            emberdb::CanonicalMatchId{100});
  EXPECT_EQ(catalog.resolveEvent(events[0]).team_id,
            emberdb::CanonicalTeamId{1});
  EXPECT_EQ(catalog.resolveEvent(events[1]).team_id,
            emberdb::CanonicalTeamId{2});
  EXPECT_EQ(catalog.resolveEvent(events[2]).team_id,
            emberdb::CanonicalTeamId{1});
}

TEST(MetricaEventAdapterTest, PreservesOffPitchStartButDoesNotCanonicalizeIt) {
  const emberdb::MetricaEventAdapter adapter;
  const auto events = adapter.loadEvents(
      fixture("metrica_invalid_coordinates.csv"),
      context(emberdb::AttackingDirection::LeftToRight));

  ASSERT_EQ(events.size(), 1U);
  ASSERT_TRUE(events[0].source_start_location);
  EXPECT_EQ(*events[0].source_start_location, (emberdb::Coordinate{1.01, 0.4}));
  EXPECT_FALSE(events[0].start_location);
}

TEST(MetricaEventAdapterTest, RejectsUnreadableFileWithPathContext) {
  const emberdb::MetricaEventAdapter adapter;
  const auto path = fixture("missing-metrica.csv");
  EXPECT_THROW(
      {
        try {
          static_cast<void>(adapter.loadEvents(
              path, context(emberdb::AttackingDirection::LeftToRight)));
        } catch (const std::runtime_error& error) {
          EXPECT_NE(std::string(error.what()).find(path.string()), std::string::npos);
          throw;
        }
      },
      std::runtime_error);
}

}  // namespace
