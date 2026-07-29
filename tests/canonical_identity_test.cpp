#include "emberdb/identity/canonical_identity.h"

#include <chrono>
#include <stdexcept>

#include <gtest/gtest.h>

namespace {

emberdb::CanonicalIdentityCatalog catalogWithMatch() {
  emberdb::CanonicalIdentityCatalog catalog;
  catalog.addTeam({{1}, "North FC"});
  catalog.addTeam({{2}, "South FC"});
  catalog.addPlayer({{10}, "Alex Forward"});
  catalog.addMatch({{100}, "Premier League", "2017/2018",
                    std::chrono::sys_seconds{std::chrono::seconds{1'500'000'000}},
                    {1}, {2}, 4, 3});
  return catalog;
}

TEST(CanonicalIdentityCatalogTest, MapsMultipleProvidersToCanonicalEntities) {
  auto catalog = catalogWithMatch();
  catalog.mapTeam({"StatsBomb", "10", std::nullopt}, {1});
  catalog.mapTeam({"Wyscout", "1609", std::nullopt}, {1});
  catalog.mapPlayer({"StatsBomb", "99", std::nullopt}, {10});
  catalog.mapPlayer({"Wyscout", "25413", std::nullopt}, {10});
  catalog.mapMatch({"StatsBomb", "12345"}, {100});
  catalog.mapMatch({"Wyscout", "2499719"}, {100});

  EXPECT_EQ(catalog.resolveTeam({"StatsBomb", "10", std::nullopt}),
            emberdb::CanonicalTeamId{1});
  EXPECT_EQ(catalog.resolveTeam({"Wyscout", "1609", std::nullopt}),
            emberdb::CanonicalTeamId{1});
  EXPECT_EQ(catalog.resolvePlayer({"Wyscout", "25413", std::nullopt}),
            emberdb::CanonicalPlayerId{10});
  EXPECT_EQ(catalog.resolveMatch({"StatsBomb", "12345"}),
            emberdb::CanonicalMatchId{100});
  EXPECT_EQ(catalog.teams().size(), 2U);
  EXPECT_EQ(catalog.players().size(), 1U);
  EXPECT_EQ(catalog.matches().size(), 1U);
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

TEST(CanonicalIdentityCatalogTest, ValidatesCanonicalMatchRelationships) {
  emberdb::CanonicalIdentityCatalog catalog;
  catalog.addTeam({{1}, "North FC"});
  EXPECT_THROW(
      catalog.addMatch({{100}, "League", "Season", std::nullopt, {1}, {2}, 1, 0}),
      std::invalid_argument);
  catalog.addTeam({{2}, "South FC"});
  EXPECT_THROW(
      catalog.addMatch({{100}, "League", "Season", std::nullopt, {1}, {2}, 1,
                        std::nullopt}),
      std::invalid_argument);
}

}  // namespace
