#include "emberdb/reconciliation/entity_reconciliation.h"

#include <stdexcept>

#include <gtest/gtest.h>

namespace {

emberdb::CanonicalIdentityCatalog catalog() {
  emberdb::CanonicalIdentityCatalog result;
  result.addCompetition({{20}, "Premier League"});
  result.addCompetition({{21}, "Other League"});
  result.addSeason({{30}, {20}, "2023/2024"});
  result.addSeason({{31}, {21}, "2023/2024"});
  result.addTeam({{1}, "North FC"});
  result.addPlayer({{10}, "Alex Forward"});
  return result;
}

TEST(EntityReconciliationTest,
     GeneratesDeterministicExactNameCandidatesWithEvidence) {
  emberdb::ProviderMetadata metadata;
  metadata.competitions.push_back(
      {{"StatsBomb", "2"}, "  PREMIER   LEAGUE "});
  metadata.teams.push_back(
      {{"StatsBomb", "10", std::nullopt}, "north fc"});
  metadata.teams.push_back(
      {{"StatsBomb", "10", std::nullopt}, "north fc"});
  metadata.players.push_back(
      {{"StatsBomb", "99", std::nullopt}, "Alex Forward", std::nullopt});
  const auto identities = catalog();

  const auto competitions = emberdb::findEntityCandidates(
      metadata, emberdb::IdentityEntityType::Competition, identities,
      "statsbomb competitions fixture");
  const auto teams = emberdb::findEntityCandidates(
      metadata, emberdb::IdentityEntityType::Team, identities,
      "statsbomb teams fixture");
  const auto players = emberdb::findEntityCandidates(
      metadata, emberdb::IdentityEntityType::Player, identities,
      "statsbomb lineups fixture");

  ASSERT_EQ(competitions.size(), 1U);
  EXPECT_EQ(competitions[0].canonical_id, 20);
  EXPECT_EQ(competitions[0].provider_identity.id, "2");
  EXPECT_EQ(competitions[0].name.status,
            emberdb::ReconciliationStatus::Agreeing);
  EXPECT_EQ(competitions[0].name.provider_value,
            "  PREMIER   LEAGUE ");
  EXPECT_DOUBLE_EQ(competitions[0].confidence, 1.0);
  ASSERT_EQ(teams.size(), 1U);
  EXPECT_EQ(teams[0].canonical_id, 1);
  ASSERT_EQ(players.size(), 1U);
  EXPECT_EQ(players[0].source, "statsbomb lineups fixture");
}

TEST(EntityReconciliationTest,
     UsesExplicitCompetitionContextForSeasonCandidates) {
  emberdb::ProviderMetadata metadata;
  metadata.seasons.push_back(
      {{"StatsBomb", "44"}, {"StatsBomb", "2"}, "2023/2024"});
  auto identities = catalog();

  const auto without_mapping = emberdb::findEntityCandidates(
      metadata, emberdb::IdentityEntityType::Season, identities,
      "statsbomb matches fixture");
  identities.mapCompetition({"StatsBomb", "2"}, {20});
  const auto with_mapping = emberdb::findEntityCandidates(
      metadata, emberdb::IdentityEntityType::Season, identities,
      "statsbomb matches fixture");

  ASSERT_EQ(without_mapping.size(), 2U);
  EXPECT_DOUBLE_EQ(without_mapping[0].confidence, 0.9);
  EXPECT_EQ(without_mapping[0].context.status,
            emberdb::ReconciliationStatus::Missing);
  ASSERT_EQ(with_mapping.size(), 1U);
  EXPECT_EQ(with_mapping[0].canonical_id, 30);
  EXPECT_DOUBLE_EQ(with_mapping[0].confidence, 1.0);
  EXPECT_EQ(with_mapping[0].context.status,
            emberdb::ReconciliationStatus::Agreeing);
}

TEST(EntityReconciliationTest,
     SkipsMissingNamesAndAlreadyMappedProviderIdentities) {
  emberdb::ProviderMetadata metadata;
  metadata.seasons.push_back(
      {{"Wyscout", "181150"}, {"Wyscout", "364"}, std::nullopt});
  metadata.teams.push_back(
      {{"Wyscout", "1609", std::nullopt}, "North FC"});
  auto identities = catalog();
  identities.mapTeam({"Wyscout", "1609", std::nullopt}, {1});

  EXPECT_TRUE(emberdb::findEntityCandidates(
                  metadata, emberdb::IdentityEntityType::Season, identities,
                  "wyscout matches fixture")
                  .empty());
  EXPECT_TRUE(emberdb::findEntityCandidates(
                  metadata, emberdb::IdentityEntityType::Team, identities,
                  "wyscout teams fixture")
                  .empty());
}

TEST(EntityReconciliationTest, SkipsDeprecatedAndMergedCanonicalEntities) {
  emberdb::ProviderMetadata metadata;
  metadata.teams.push_back(
      {{"StatsBomb", "10", std::nullopt}, "North FC"});
  metadata.players.push_back(
      {{"StatsBomb", "99", std::nullopt}, "Alex Forward", std::nullopt});
  auto identities = catalog();
  identities.addTeam({{2}, "North City"});
  identities.deprecateTeam({1});
  identities.addPlayer({{11}, "Alex F."});
  identities.mergePlayer({10}, {11});

  EXPECT_TRUE(emberdb::findEntityCandidates(
                  metadata, emberdb::IdentityEntityType::Team, identities,
                  "statsbomb teams fixture")
                  .empty());
  EXPECT_TRUE(emberdb::findEntityCandidates(
                  metadata, emberdb::IdentityEntityType::Player, identities,
                  "statsbomb players fixture")
                  .empty());
}

TEST(EntityReconciliationTest, RejectsConflictingDuplicateProviderMetadata) {
  emberdb::ProviderMetadata metadata;
  metadata.teams.push_back(
      {{"StatsBomb", "10", std::nullopt}, "North FC"});
  metadata.teams.push_back(
      {{"StatsBomb", "10", std::nullopt}, "Different FC"});

  EXPECT_THROW(
      static_cast<void>(emberdb::findEntityCandidates(
          metadata, emberdb::IdentityEntityType::Team, catalog(), "fixture")),
      std::invalid_argument);
}

}  // namespace
