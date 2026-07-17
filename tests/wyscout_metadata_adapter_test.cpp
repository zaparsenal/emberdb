#include "emberdb/ingestion/wyscout_metadata_adapter.h"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

namespace {

std::filesystem::path fixture(const std::string& name) {
  return std::filesystem::path(EMBERDB_TEST_FIXTURES_DIR) / name;
}

TEST(WyscoutMetadataAdapterTest, LoadsCompetitionTeamsAndPlayers) {
  const emberdb::WyscoutMetadataAdapter adapter;
  const auto competitions =
      adapter.loadCompetitions(fixture("wyscout_competitions.json"));
  const auto teams = adapter.loadTeams(fixture("wyscout_teams.json"));
  const auto players = adapter.loadPlayers(fixture("wyscout_players.json"));

  ASSERT_EQ(competitions.competitions.size(), 1U);
  EXPECT_EQ(competitions.competitions[0].id, "364");
  EXPECT_EQ(competitions.competitions[0].name, "English first division");
  ASSERT_EQ(teams.teams.size(), 2U);
  EXPECT_EQ(teams.teams[0].reference,
            (emberdb::ProviderTeamReference{"Wyscout", "1609", std::nullopt}));
  EXPECT_EQ(teams.teams[0].name, "Arsenal");
  ASSERT_EQ(players.players.size(), 4U);
  EXPECT_EQ(players.players[0].name, "M. Ozil");
  EXPECT_EQ(players.players[0].current_team,
            (emberdb::ProviderTeamReference{"Wyscout", "1609", std::nullopt}));
  EXPECT_EQ(players.players[1].name, "Free Agent");
  EXPECT_FALSE(players.players[1].current_team);
  EXPECT_FALSE(players.players[2].current_team);
  EXPECT_FALSE(players.players[3].current_team);
}

TEST(WyscoutMetadataAdapterTest, LoadsMatchSidesScoresAndUtcKickoff) {
  const emberdb::WyscoutMetadataAdapter adapter;
  const auto metadata = adapter.loadMatches(fixture("wyscout_matches.json"));

  ASSERT_EQ(metadata.matches.size(), 1U);
  const auto& match = metadata.matches[0];
  EXPECT_EQ(match.reference,
            (emberdb::ProviderMatchReference{"Wyscout", "2499719"}));
  EXPECT_EQ(match.competition_id, "364");
  EXPECT_FALSE(match.competition_name);
  EXPECT_EQ(match.season_id, "181150");
  EXPECT_FALSE(match.season_name);
  EXPECT_EQ(match.home_team,
            (emberdb::ProviderTeamReference{"Wyscout", "1609", std::nullopt}));
  EXPECT_EQ(match.away_team,
            (emberdb::ProviderTeamReference{"Wyscout", "1631", std::nullopt}));
  EXPECT_EQ(match.home_score, 4);
  EXPECT_EQ(match.away_score, 3);
  const auto expected_kickoff =
      std::chrono::sys_days{std::chrono::year{2017} / 8 / 11} +
      std::chrono::hours{18} + std::chrono::minutes{45};
  EXPECT_EQ(match.kickoff, expected_kickoff);
}

TEST(WyscoutMetadataAdapterTest, RejectsIncompleteTeamSidesWithContext) {
  const emberdb::WyscoutMetadataAdapter adapter;
  EXPECT_THROW(
      {
        try {
          static_cast<void>(
              adapter.loadMatches(fixture("wyscout_invalid_metadata.json")));
        } catch (const std::runtime_error& error) {
          const std::string message(error.what());
          EXPECT_NE(message.find("wyscout_invalid_metadata.json"), std::string::npos);
          EXPECT_NE(message.find("index 0"), std::string::npos);
          EXPECT_NE(message.find("home team and one away team"), std::string::npos);
          throw;
        }
      },
      std::runtime_error);
}

}  // namespace
