#include "emberdb/identity/canonical_identity.h"

#include <chrono>
#include <stdexcept>

#include <gtest/gtest.h>

namespace {

emberdb::CanonicalIdentityCatalog catalogWithMatch() {
  emberdb::CanonicalIdentityCatalog catalog;
  catalog.addCompetition({{20}, "Premier League"});
  catalog.addSeason({{30}, {20}, "2017/2018"});
  catalog.addTeam({{1}, "North FC"});
  catalog.addTeam({{2}, "South FC"});
  catalog.addPlayer({{10}, "Alex Forward"});
  catalog.addMatch({{100}, {30},
                    std::chrono::sys_seconds{std::chrono::seconds{1'500'000'000}},
                    {1}, {2}, 4, 3});
  return catalog;
}

TEST(CanonicalIdentityCatalogTest, MapsMultipleProvidersToCanonicalEntities) {
  auto catalog = catalogWithMatch();
  catalog.mapCompetition({"StatsBomb", "2"}, {20});
  catalog.mapCompetition({"Wyscout", "364"}, {20});
  catalog.mapSeason({"StatsBomb", "44"}, {30});
  catalog.mapSeason({"Wyscout", "181150"}, {30});
  catalog.mapTeam({"StatsBomb", "10", std::nullopt}, {1});
  catalog.mapTeam({"Wyscout", "1609", std::nullopt}, {1});
  catalog.mapPlayer({"StatsBomb", "99", std::nullopt}, {10});
  catalog.mapPlayer({"Wyscout", "25413", std::nullopt}, {10});
  catalog.mapMatch({"StatsBomb", "12345"}, {100});
  catalog.mapMatch({"Wyscout", "2499719"}, {100});

  EXPECT_EQ(catalog.resolveCompetition({"Wyscout", "364"}),
            emberdb::CanonicalCompetitionId{20});
  EXPECT_EQ(catalog.resolveSeason({"Wyscout", "181150"}),
            emberdb::CanonicalSeasonId{30});
  EXPECT_EQ(catalog.resolveTeam({"StatsBomb", "10", std::nullopt}),
            emberdb::CanonicalTeamId{1});
  EXPECT_EQ(catalog.resolveTeam({"Wyscout", "1609", std::nullopt}),
            emberdb::CanonicalTeamId{1});
  EXPECT_EQ(catalog.resolvePlayer({"Wyscout", "25413", std::nullopt}),
            emberdb::CanonicalPlayerId{10});
  EXPECT_EQ(catalog.resolveMatch({"StatsBomb", "12345"}),
            emberdb::CanonicalMatchId{100});
  EXPECT_EQ(catalog.competitions().size(), 1U);
  EXPECT_EQ(catalog.seasons().size(), 1U);
  EXPECT_EQ(catalog.teams().size(), 2U);
  EXPECT_EQ(catalog.players().size(), 1U);
  EXPECT_EQ(catalog.matches().size(), 1U);
  EXPECT_EQ(catalog.competitionMappings().size(), 2U);
  EXPECT_EQ(catalog.seasonMappings().size(), 2U);
  EXPECT_EQ(catalog.teamMappings().size(), 2U);
  EXPECT_EQ(catalog.playerMappings().size(), 2U);
  EXPECT_EQ(catalog.matchMappings().size(), 2U);
}

TEST(CanonicalIdentityCatalogTest, ResolvesEventIdentityWithoutMutatingProviderIds) {
  auto catalog = catalogWithMatch();
  catalog.mapTeam({"Wyscout", "1609", std::nullopt}, {1});
  catalog.mapPlayer({"Wyscout", "25413", std::nullopt}, {10});
  catalog.mapMatch({"Wyscout", "2499719"}, {100});
  emberdb::FootballEvent event;
  event.match_id = 2499719;
  event.team_id = 1609;
  event.player_id = 25413;
  event.provider = "Wyscout";

  const auto identity = catalog.resolveEvent(event);

  EXPECT_EQ(identity.match_id, emberdb::CanonicalMatchId{100});
  EXPECT_EQ(identity.team_id, emberdb::CanonicalTeamId{1});
  EXPECT_EQ(identity.player_id, emberdb::CanonicalPlayerId{10});
  EXPECT_EQ(event.match_id, 2499719);
  EXPECT_EQ(event.team_id, 1609);
}

TEST(CanonicalIdentityCatalogTest, ScopesMetricaHomeAndAwayByMatch) {
  auto catalog = catalogWithMatch();
  catalog.mapMetricaTeams("1", {1}, {2});
  catalog.mapMetricaTeams("2", {2}, {1});
  emberdb::FootballEvent first;
  first.match_id = 1;
  first.team_name = "Home";
  first.provider = "Metrica";
  emberdb::FootballEvent second = first;
  second.match_id = 2;

  EXPECT_EQ(catalog.resolveEvent(first).team_id, emberdb::CanonicalTeamId{1});
  EXPECT_EQ(catalog.resolveEvent(second).team_id, emberdb::CanonicalTeamId{2});
  EXPECT_FALSE(catalog.resolveTeam({"Metrica", "Home", std::nullopt}));
}

TEST(CanonicalIdentityCatalogTest, RejectsConflictingAndDanglingMappings) {
  auto catalog = catalogWithMatch();
  catalog.mapTeam({"StatsBomb", "10", std::nullopt}, {1});
  EXPECT_THROW(catalog.mapTeam({"StatsBomb", "10", std::nullopt}, {2}),
               std::invalid_argument);
  EXPECT_THROW(catalog.mapPlayer({"StatsBomb", "99", std::nullopt}, {999}),
               std::invalid_argument);
  EXPECT_THROW(catalog.mapMatch({"StatsBomb", "12345"}, {999}),
               std::invalid_argument);
}

TEST(CanonicalIdentityCatalogTest,
     ValidatesCompetitionAndSeasonRelationshipsAndMappings) {
  emberdb::CanonicalIdentityCatalog catalog;
  EXPECT_THROW(catalog.addSeason({{10}, {1}, "2023/2024"}),
               std::invalid_argument);
  catalog.addCompetition({{1}, "Premier League"});
  catalog.addSeason({{10}, {1}, "2023/2024"});
  EXPECT_THROW(catalog.mapCompetition({"StatsBomb", "2"}, {99}),
               std::invalid_argument);
  EXPECT_THROW(catalog.mapSeason({"StatsBomb", "44"}, {99}),
               std::invalid_argument);
  catalog.mapCompetition({"StatsBomb", "2"}, {1});
  catalog.mapSeason({"StatsBomb", "44"}, {10});
}

TEST(CanonicalIdentityCatalogTest, ValidatesCanonicalMatchRelationships) {
  emberdb::CanonicalIdentityCatalog catalog;
  catalog.addTeam({{1}, "North FC"});
  EXPECT_THROW(
      catalog.addMatch({{100}, {10}, std::nullopt, {1}, {2}, 1, 0}),
      std::invalid_argument);
  catalog.addTeam({{2}, "South FC"});
  EXPECT_THROW(
      catalog.addMatch({{100}, {10}, std::nullopt, {1}, {2}, 1, 0}),
      std::invalid_argument);
  EXPECT_THROW(
      catalog.addMatch(emberdb::CanonicalMatch::legacy(
          {100}, {"League", "Season"}, std::nullopt, {1}, {2}, 1, 0)),
      std::invalid_argument);
  catalog.addCompetition({{20}, "League"});
  catalog.addSeason({{10}, {20}, "Season"});
  EXPECT_THROW(
      catalog.addMatch(
          {{100}, {10}, std::nullopt, {1}, {2}, 1, std::nullopt}),
      std::invalid_argument);
  catalog.deprecateSeason({10});
  EXPECT_THROW(
      catalog.addMatch({{100}, {10}, std::nullopt, {1}, {2}, 1, 0}),
      std::invalid_argument);
}

TEST(CanonicalIdentityCatalogTest,
     RenamesAndDeprecatesWithoutErasingHistoricalMappings) {
  emberdb::CanonicalIdentityCatalog catalog;
  catalog.addCompetition({{20}, "Premier League"});
  catalog.addSeason({{30}, {20}, "2023/2024"});
  catalog.addTeam({{1}, "North FC"});
  catalog.addTeam({{2}, "South FC"});
  catalog.addPlayer({{10}, "Alex Forward"});
  catalog.addMatch({{100}, {30}, std::nullopt,
                    {1}, {2}, std::nullopt, std::nullopt});
  catalog.mapPlayer({"StatsBomb", "99", std::nullopt}, {10});

  catalog.renameCompetition({20}, "Premier Division");
  catalog.renameSeason({30}, "2023-24");
  catalog.renameTeam({1}, "North City");
  catalog.renamePlayer({10}, "Alex A. Forward");
  catalog.deprecatePlayer({10});

  EXPECT_EQ(catalog.competition({20})->name, "Premier Division");
  EXPECT_EQ(catalog.season({30})->name, "2023-24");
  EXPECT_EQ(catalog.team({1})->name, "North City");
  EXPECT_EQ(catalog.player({10})->name, "Alex A. Forward");
  EXPECT_EQ(catalog.player({10})->status,
            emberdb::CanonicalEntityStatus::Deprecated);
  ASSERT_TRUE(catalog.match({100})->season_id);
  EXPECT_EQ(catalog.match({100})->season_id,
            emberdb::CanonicalSeasonId{30});
  ASSERT_TRUE(catalog.matchLabels({100}));
  EXPECT_EQ(catalog.matchLabels({100})->competition, "Premier Division");
  EXPECT_EQ(catalog.matchLabels({100})->season, "2023-24");
  EXPECT_EQ(catalog.resolvePlayer({"StatsBomb", "99", std::nullopt}),
            emberdb::CanonicalPlayerId{10});
  EXPECT_THROW(
      catalog.mapPlayer({"Wyscout", "7", std::nullopt}, {10}),
      std::invalid_argument);
  EXPECT_THROW(catalog.renamePlayer({10}, "Another Name"),
               std::invalid_argument);
}

TEST(CanonicalIdentityCatalogTest,
     RejectsCompetitionDeprecationWhileASeasonRemainsActive) {
  emberdb::CanonicalIdentityCatalog catalog;
  catalog.addCompetition({{20}, "Premier League"});
  catalog.addSeason({{30}, {20}, "2023/2024"});

  EXPECT_THROW(catalog.deprecateCompetition({20}), std::invalid_argument);

  EXPECT_EQ(catalog.competition({20})->status,
            emberdb::CanonicalEntityStatus::Active);
  EXPECT_EQ(catalog.season({30})->status,
            emberdb::CanonicalEntityStatus::Active);

  catalog.deprecateSeason({30});
  catalog.deprecateCompetition({20});

  EXPECT_EQ(catalog.competition({20})->status,
            emberdb::CanonicalEntityStatus::Deprecated);
  EXPECT_THROW(catalog.mapSeason({"StatsBomb", "44"}, {30}),
               std::invalid_argument);
}

TEST(CanonicalIdentityCatalogTest,
     MergesEntitiesAndRepointsMappingsAndTypedDependencies) {
  emberdb::CanonicalIdentityCatalog catalog;
  catalog.addCompetition({{20}, "Premier League"});
  catalog.addCompetition({{21}, "Premier League Duplicate"});
  catalog.addSeason({{30}, {21}, "2023/2024 Duplicate"});
  catalog.addSeason({{31}, {20}, "2023/2024"});
  catalog.addTeam({{1}, "North FC"});
  catalog.addTeam({{2}, "South FC"});
  catalog.addTeam({{3}, "North FC Duplicate"});
  catalog.addPlayer({{10}, "Alex Forward"});
  catalog.addPlayer({{11}, "A. Forward"});
  catalog.addPlayer({{12}, "Forward, Alex"});
  catalog.addMatch({{100}, {30}, std::nullopt, {3}, {2},
                    std::nullopt, std::nullopt});
  catalog.mapCompetition({"StatsBomb", "2"}, {21});
  catalog.mapSeason({"StatsBomb", "44"}, {30});
  catalog.mapTeam({"StatsBomb", "10", std::nullopt}, {3});
  catalog.mapPlayer({"StatsBomb", "99", std::nullopt}, {12});

  catalog.mergeCompetition({21}, {20});
  catalog.mergeSeason({30}, {31});
  catalog.mergeTeam({3}, {1});
  catalog.mergePlayer({12}, {11});
  catalog.mergePlayer({11}, {10});

  EXPECT_EQ(catalog.competition({21})->status,
            emberdb::CanonicalEntityStatus::Merged);
  EXPECT_EQ(catalog.competition({21})->merged_into,
            emberdb::CanonicalCompetitionId{20});
  EXPECT_EQ(catalog.season({30})->merged_into,
            emberdb::CanonicalSeasonId{31});
  EXPECT_EQ(catalog.team({3})->merged_into,
            emberdb::CanonicalTeamId{1});
  EXPECT_EQ(catalog.player({12})->merged_into,
            emberdb::CanonicalPlayerId{10});
  EXPECT_EQ(catalog.resolveCompetition({"StatsBomb", "2"}),
            emberdb::CanonicalCompetitionId{20});
  EXPECT_EQ(catalog.resolveSeason({"StatsBomb", "44"}),
            emberdb::CanonicalSeasonId{31});
  EXPECT_EQ(catalog.resolveTeam({"StatsBomb", "10", std::nullopt}),
            emberdb::CanonicalTeamId{1});
  EXPECT_EQ(catalog.resolvePlayer({"StatsBomb", "99", std::nullopt}),
            emberdb::CanonicalPlayerId{10});
  EXPECT_EQ(catalog.season({31})->competition_id,
            emberdb::CanonicalCompetitionId{20});
  EXPECT_EQ(catalog.match({100})->season_id,
            emberdb::CanonicalSeasonId{31});
  ASSERT_TRUE(catalog.matchLabels({100}));
  EXPECT_EQ(catalog.matchLabels({100})->competition, "Premier League");
  EXPECT_EQ(catalog.matchLabels({100})->season, "2023/2024");
  EXPECT_EQ(catalog.match({100})->home_team_id,
            emberdb::CanonicalTeamId{1});
}

TEST(CanonicalIdentityCatalogTest,
     RejectsTeamMergeThatWouldCollapseMatchSidesAtomically) {
  auto catalog = catalogWithMatch();
  catalog.mapTeam({"StatsBomb", "10", std::nullopt}, {1});

  EXPECT_THROW(catalog.mergeTeam({1}, {2}), std::invalid_argument);

  EXPECT_EQ(catalog.team({1})->status,
            emberdb::CanonicalEntityStatus::Active);
  EXPECT_EQ(catalog.resolveTeam({"StatsBomb", "10", std::nullopt}),
            emberdb::CanonicalTeamId{1});
  EXPECT_EQ(catalog.match({100})->home_team_id,
            emberdb::CanonicalTeamId{1});
}

TEST(CanonicalIdentityCatalogTest,
     PreservesExplicitLegacyLabelsWithoutAmbiguousRenamePropagation) {
  emberdb::CanonicalIdentityCatalog catalog;
  catalog.addCompetition({{20}, "Premier League"});
  catalog.addSeason({{30}, {20}, "2017/2018"});
  catalog.addTeam({{1}, "North FC"});
  catalog.addTeam({{2}, "South FC"});
  catalog.restoreLegacyMatch(emberdb::CanonicalMatch::legacy(
      {100}, {"Premier League", "2017/2018"}, std::nullopt, {1}, {2},
      std::nullopt, std::nullopt));

  catalog.renameCompetition({20}, "Premier Division");
  catalog.renameSeason({30}, "2017-18");

  EXPECT_FALSE(catalog.match({100})->season_id);
  ASSERT_TRUE(catalog.match({100})->legacy_ancestry);
  ASSERT_TRUE(catalog.matchLabels({100}));
  EXPECT_EQ(catalog.matchLabels({100})->competition, "Premier League");
  EXPECT_EQ(catalog.matchLabels({100})->season, "2017/2018");
}

}  // namespace
