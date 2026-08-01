#include "emberdb/identity/event_identity_coverage.h"

#include <chrono>
#include <optional>

#include <gtest/gtest.h>

#include "emberdb/storage/football_event_table.h"

namespace {

emberdb::FootballEvent event(std::string provider_event_id,
                             emberdb::Identifier match_id,
                             std::optional<emberdb::Identifier> team_id,
                             std::optional<std::string> team_name,
                             std::optional<emberdb::Identifier> player_id,
                             std::optional<std::string> player_name,
                             std::string provider) {
  return emberdb::FootballEvent{std::move(provider_event_id),
                                match_id,
                                1,
                                {std::chrono::milliseconds{1000}, 1, 0},
                                std::nullopt,
                                team_id,
                                std::move(team_name),
                                player_id,
                                std::move(player_name),
                                "Pass",
                                std::nullopt,
                                std::nullopt,
                                std::nullopt,
                                std::move(provider),
                                std::nullopt,
                                std::nullopt};
}

emberdb::CanonicalIdentityCatalog catalog() {
  emberdb::CanonicalIdentityCatalog result;
  result.addCompetition({{20}, "Premier League"});
  result.addSeason({{30}, {20}, "2025/2026"});
  result.addTeam({{1}, "North FC"});
  result.addTeam({{2}, "South FC"});
  result.addPlayer({{10}, "Alex Forward"});
  result.addMatch({{100},
                   {30},
                   std::chrono::sys_seconds{
                       std::chrono::seconds{1'700'000'000}},
                   {1},
                   {2},
                   2,
                   1});
  result.mapMatch({"Wyscout", "2499719"}, {100});
  result.mapMatch({"Metrica", "2"}, {100});
  result.mapTeam({"Wyscout", "1609", std::nullopt}, {1});
  result.mapTeam({"Metrica", "Home", "2"}, {2});
  result.mapPlayer({"Wyscout", "25413", std::nullopt}, {10});
  return result;
}

emberdb::FootballEventTable events() {
  emberdb::FootballEventTable result;
  result.append(event("wyscout-1", 2499719, 1609, "North FC", 25413,
                      "Alex Forward", "Wyscout"));
  result.append(event("statsbomb-2", 88, 55, "Unknown FC", std::nullopt,
                      std::nullopt, "StatsBomb"));
  result.append(event("metrica-3", 2, std::nullopt, "Home", std::nullopt,
                      "Player9", "Metrica"));
  return result;
}

TEST(EventIdentityCoverageTest,
     ReportsResolvedMissingAndAbsentIdentitiesSeparately) {
  const auto table = events();
  const auto identities = catalog();

  const auto report = emberdb::analyzeEventIdentityCoverage(
      table, identities, "fixtures/mixed-events.json");

  EXPECT_EQ(report.total_events, 3U);
  EXPECT_EQ(report.matches,
            (emberdb::EventIdentityCoverageCounts{3, 2, 1, 0}));
  EXPECT_EQ(report.teams,
            (emberdb::EventIdentityCoverageCounts{3, 2, 1, 0}));
  EXPECT_EQ(report.players,
            (emberdb::EventIdentityCoverageCounts{2, 1, 1, 1}));

  ASSERT_EQ(report.events.size(), 3U);
  EXPECT_EQ(report.events[0].match_status,
            emberdb::EventIdentityStatus::Resolved);
  EXPECT_EQ(report.events[0].team_status,
            emberdb::EventIdentityStatus::Resolved);
  EXPECT_EQ(report.events[0].player_status,
            emberdb::EventIdentityStatus::Resolved);
  EXPECT_EQ(report.events[0].canonical_identity.match_id,
            emberdb::CanonicalMatchId{100});
  EXPECT_EQ(report.events[0].canonical_identity.team_id,
            emberdb::CanonicalTeamId{1});
  EXPECT_EQ(report.events[0].canonical_identity.player_id,
            emberdb::CanonicalPlayerId{10});

  EXPECT_EQ(report.events[1].match_status,
            emberdb::EventIdentityStatus::Missing);
  EXPECT_EQ(report.events[1].team_status,
            emberdb::EventIdentityStatus::Missing);
  EXPECT_EQ(report.events[1].player_status,
            emberdb::EventIdentityStatus::Absent);
  EXPECT_EQ(report.events[1].provider, "StatsBomb");
  EXPECT_EQ(report.events[1].provider_event_id, "statsbomb-2");
  EXPECT_EQ(report.events[1].provider_match_id, 88);
  EXPECT_EQ(report.events[1].provider_team_id, 55);
  EXPECT_EQ(report.events[1].provider_team_name, "Unknown FC");
  EXPECT_FALSE(report.events[1].provider_player_id);
  EXPECT_FALSE(report.events[1].provider_player_name);

  EXPECT_EQ(report.events[2].source, "fixtures/mixed-events.json");
  EXPECT_EQ(report.events[2].team_status,
            emberdb::EventIdentityStatus::Resolved);
  EXPECT_EQ(report.events[2].player_status,
            emberdb::EventIdentityStatus::Missing);
  EXPECT_EQ(report.events[2].provider_team_name, "Home");
  EXPECT_EQ(report.events[2].provider_player_name, "Player9");
}

TEST(EventIdentityCoverageTest, IsDeterministicAndDoesNotMutateInputs) {
  const auto table = events();
  const auto identities = catalog();
  const auto first_event_before = table.row(0);
  const auto team_mapping_count = identities.teamMappings().size();
  const auto player_mapping_count = identities.playerMappings().size();
  const auto match_mapping_count = identities.matchMappings().size();

  const auto first = emberdb::analyzeEventIdentityCoverage(
      table, identities, "fixtures/mixed-events.json");
  const auto second = emberdb::analyzeEventIdentityCoverage(
      table, identities, "fixtures/mixed-events.json");

  EXPECT_EQ(first, second);
  EXPECT_EQ(table.row(0).provider_event_id,
            first_event_before.provider_event_id);
  EXPECT_EQ(table.row(0).match_id, first_event_before.match_id);
  EXPECT_EQ(table.row(0).team_id, first_event_before.team_id);
  EXPECT_EQ(table.row(0).player_id, first_event_before.player_id);
  EXPECT_EQ(identities.teamMappings().size(), team_mapping_count);
  EXPECT_EQ(identities.playerMappings().size(), player_mapping_count);
  EXPECT_EQ(identities.matchMappings().size(), match_mapping_count);
}

TEST(EventIdentityCoverageTest, ReportsEmptyTablesWithoutDiagnostics) {
  const emberdb::FootballEventTable table;
  const emberdb::CanonicalIdentityCatalog identities;

  const auto report =
      emberdb::analyzeEventIdentityCoverage(table, identities, "empty.ember");

  EXPECT_EQ(report.source, "empty.ember");
  EXPECT_EQ(report.total_events, 0U);
  EXPECT_EQ(report.matches, emberdb::EventIdentityCoverageCounts{});
  EXPECT_EQ(report.teams, emberdb::EventIdentityCoverageCounts{});
  EXPECT_EQ(report.players, emberdb::EventIdentityCoverageCounts{});
  EXPECT_TRUE(report.events.empty());
}

}  // namespace
