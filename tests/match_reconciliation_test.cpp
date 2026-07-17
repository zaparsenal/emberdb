#include "emberdb/reconciliation/match_reconciliation.h"

#include <chrono>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

namespace {

emberdb::CanonicalIdentityCatalog catalog() {
  emberdb::CanonicalIdentityCatalog result;
  result.addTeam({{1}, "Arsenal"});
  result.addTeam({{2}, "Leicester City"});
  result.addTeam({{3}, "Other FC"});
  result.mapTeam({"StatsBomb", "10", std::nullopt}, {1});
  result.mapTeam({"StatsBomb", "20", std::nullopt}, {2});
  result.mapTeam({"Wyscout", "1609", std::nullopt}, {1});
  result.mapTeam({"Wyscout", "1631", std::nullopt}, {2});
  result.mapTeam({"Wyscout", "9999", std::nullopt}, {3});
  return result;
}

std::chrono::sys_seconds kickoff() {
  return std::chrono::sys_days{std::chrono::year{2017} / 8 / 11} +
         std::chrono::hours{18} + std::chrono::minutes{45};
}

emberdb::ProviderMatchMetadata statsbombMatch() {
  return {{"StatsBomb", "12345"},
          "2",
          "Premier League",
          "44",
          "2017/2018",
          kickoff(),
          {"StatsBomb", "10", std::nullopt},
          {"StatsBomb", "20", std::nullopt},
          4,
          3};
}

emberdb::ProviderMatchMetadata wyscoutMatch() {
  return {{"Wyscout", "2499719"},
          "364",
          "English first division",
          "181150",
          std::nullopt,
          kickoff() + std::chrono::minutes{2},
          {"Wyscout", "1609", std::nullopt},
          {"Wyscout", "1631", std::nullopt},
          4,
          3};
}

TEST(MatchReconciliationTest, ProducesExplainableCrossProviderCandidate) {
  const auto identities = catalog();
  const auto result = emberdb::reconcileMatches(
      statsbombMatch(), wyscoutMatch(), identities);

  EXPECT_TRUE(result.is_candidate);
  EXPECT_DOUBLE_EQ(result.confidence, 0.90);
  EXPECT_EQ(result.home_team.status, emberdb::ReconciliationStatus::Agreeing);
  EXPECT_EQ(result.home_team.left_source, "StatsBomb");
  EXPECT_EQ(result.home_team.right_source, "Wyscout");
  EXPECT_EQ(result.home_team.left_value, "10");
  EXPECT_EQ(result.home_team.right_value, "1609");
  EXPECT_EQ(result.home_team.canonical_value, "1");
  EXPECT_EQ(result.kickoff.status, emberdb::ReconciliationStatus::Agreeing);
  EXPECT_EQ(result.score.status, emberdb::ReconciliationStatus::Agreeing);
  EXPECT_EQ(result.competition.status, emberdb::ReconciliationStatus::Uncertain);
  EXPECT_EQ(result.season.status, emberdb::ReconciliationStatus::Missing);
}

TEST(MatchReconciliationTest, DoesNotOverwriteOrAcceptConflictingTeams) {
  const auto identities = catalog();
  auto right = wyscoutMatch();
  right.home_team = {"Wyscout", "9999", std::nullopt};

  const auto result =
      emberdb::reconcileMatches(statsbombMatch(), right, identities);

  EXPECT_FALSE(result.is_candidate);
  EXPECT_EQ(result.home_team.status, emberdb::ReconciliationStatus::Conflicting);
  EXPECT_FALSE(result.home_team.canonical_value);
  EXPECT_EQ(right.home_team.id, "9999");
}

TEST(MatchReconciliationTest, ClassifiesKickoffToleranceWithoutFuzzyMatching) {
  const auto identities = catalog();
  auto right = wyscoutMatch();
  right.kickoff = kickoff() + std::chrono::hours{3};
  const auto uncertain =
      emberdb::reconcileMatches(statsbombMatch(), right, identities);
  EXPECT_TRUE(uncertain.is_candidate);
  EXPECT_EQ(uncertain.kickoff.status, emberdb::ReconciliationStatus::Uncertain);
  EXPECT_DOUBLE_EQ(uncertain.confidence, 0.80);

  right.kickoff = kickoff() + std::chrono::hours{25};
  const auto conflict =
      emberdb::reconcileMatches(statsbombMatch(), right, identities);
  EXPECT_FALSE(conflict.is_candidate);
  EXPECT_EQ(conflict.kickoff.status, emberdb::ReconciliationStatus::Conflicting);
}

TEST(MatchReconciliationTest, RequiresExplicitTeamMappings) {
  emberdb::CanonicalIdentityCatalog empty_catalog;
  const auto result = emberdb::reconcileMatches(
      statsbombMatch(), wyscoutMatch(), empty_catalog);

  EXPECT_FALSE(result.is_candidate);
  EXPECT_EQ(result.home_team.status, emberdb::ReconciliationStatus::Missing);
  EXPECT_EQ(result.away_team.status, emberdb::ReconciliationStatus::Missing);
}

TEST(MatchReconciliationTest, RanksOnlyQualifiedCandidatesDeterministically) {
  const auto identities = catalog();
  auto less_certain = wyscoutMatch();
  less_certain.reference.id = "2499720";
  less_certain.kickoff = kickoff() + std::chrono::hours{3};
  auto conflict = wyscoutMatch();
  conflict.reference.id = "2499721";
  conflict.home_score = 0;

  const auto candidates = emberdb::findMatchCandidates(
      {statsbombMatch()}, {less_certain, conflict, wyscoutMatch()}, identities);

  ASSERT_EQ(candidates.size(), 2U);
  EXPECT_EQ(candidates[0].right_match.id, "2499719");
  EXPECT_EQ(candidates[1].right_match.id, "2499720");
  EXPECT_GT(candidates[0].confidence, candidates[1].confidence);
}

TEST(MatchReconciliationTest, ValidatesScoringOptions) {
  const auto identities = catalog();
  emberdb::MatchReconciliationOptions options;
  options.kickoff_tolerance = std::chrono::hours{2};
  options.uncertain_kickoff_tolerance = std::chrono::hours{1};
  EXPECT_THROW(static_cast<void>(emberdb::reconcileMatches(
                   statsbombMatch(), wyscoutMatch(), identities, options)),
               std::invalid_argument);
  options.uncertain_kickoff_tolerance = std::chrono::hours{3};
  options.minimum_confidence = 1.1;
  EXPECT_THROW(static_cast<void>(emberdb::reconcileMatches(
                   statsbombMatch(), wyscoutMatch(), identities, options)),
               std::invalid_argument);
}

}  // namespace
