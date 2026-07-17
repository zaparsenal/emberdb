#include "emberdb/ingestion/statsbomb_metadata_adapter.h"

#include <chrono>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

namespace {

std::filesystem::path fixture(const std::string& name) {
  return std::filesystem::path(EMBERDB_TEST_FIXTURES_DIR) / name;
}

TEST(StatsBombMetadataAdapterTest, LoadsMatchTeamsAndReconciliationFields) {
  const emberdb::StatsBombMetadataAdapter adapter;
  const auto metadata = adapter.loadMatches(fixture("statsbomb_matches.json"));

  ASSERT_EQ(metadata.matches.size(), 1U);
  ASSERT_EQ(metadata.teams.size(), 2U);
  const auto& match = metadata.matches[0];
  EXPECT_EQ(match.reference, (emberdb::ProviderMatchReference{"StatsBomb", "12345"}));
  EXPECT_EQ(match.competition_id, "2");
  EXPECT_EQ(match.competition_name, "Premier League");
  EXPECT_EQ(match.season_id, "44");
  EXPECT_EQ(match.season_name, "2023/2024");
  EXPECT_EQ(match.home_team, (emberdb::ProviderTeamReference{
                                 "StatsBomb", "10", std::nullopt}));
  EXPECT_EQ(match.away_team, (emberdb::ProviderTeamReference{
                                 "StatsBomb", "20", std::nullopt}));
  EXPECT_EQ(match.home_score, 2);
  EXPECT_EQ(match.away_score, 1);
  const auto expected_kickoff =
      std::chrono::sys_days{std::chrono::year{2023} / 8 / 12} +
      std::chrono::hours{15};
  EXPECT_EQ(match.kickoff, expected_kickoff);
  EXPECT_EQ(metadata.teams[0].name, "Ember FC");
}

TEST(StatsBombMetadataAdapterTest, LoadsLineupPlayersWithTeamReferences) {
  const emberdb::StatsBombMetadataAdapter adapter;
  const auto metadata = adapter.loadLineups(fixture("statsbomb_lineups.json"));

  ASSERT_EQ(metadata.teams.size(), 2U);
  ASSERT_EQ(metadata.players.size(), 2U);
  EXPECT_EQ(metadata.players[0].reference,
            (emberdb::ProviderPlayerReference{"StatsBomb", "99", std::nullopt}));
  EXPECT_EQ(metadata.players[0].name, "Alex Forward");
  EXPECT_EQ(metadata.players[0].current_team,
            (emberdb::ProviderTeamReference{"StatsBomb", "10", std::nullopt}));
}

TEST(StatsBombMetadataAdapterTest, PreservesMissingKickoffAndScores) {
  const emberdb::StatsBombMetadataAdapter adapter;
  const auto metadata =
      adapter.loadMatches(fixture("statsbomb_missing_match_metadata.json"));

  ASSERT_EQ(metadata.matches.size(), 1U);
  EXPECT_FALSE(metadata.matches[0].kickoff);
  EXPECT_FALSE(metadata.matches[0].home_score);
  EXPECT_FALSE(metadata.matches[0].away_score);
}

TEST(StatsBombMetadataAdapterTest, RejectsInvalidTopLevelWithFileContext) {
  const emberdb::StatsBombMetadataAdapter adapter;
  EXPECT_THROW(
      {
        try {
          static_cast<void>(adapter.loadMatches(fixture("not_an_array.json")));
        } catch (const std::runtime_error& error) {
          EXPECT_NE(std::string(error.what()).find("not_an_array.json"),
                    std::string::npos);
          throw;
        }
      },
      std::runtime_error);
}

}  // namespace
