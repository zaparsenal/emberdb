#include "cli/renderer.h"

#include <chrono>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

namespace {

TEST(CliRendererTest, RendersTypedProjectionResultsExactly) {
  const emberdb::EventQueryResult result{
      {emberdb::FootballEventColumn::PlayerName,
       emberdb::FootballEventColumn::Minute,
       emberdb::FootballEventColumn::Timestamp},
      {{{emberdb::FootballEventValue{std::string{"Alex Forward"}}},
        {emberdb::FootballEventValue{std::int32_t{12}}},
        {emberdb::FootballEventValue{std::chrono::milliseconds{754567}}}}}};
  std::ostringstream output;

  emberdb::cli::printQueryResult(output, result);

  EXPECT_EQ(output.str(),
            "Matched 1 event\n"
            "player_name\tminute\ttimestamp\n"
            "Alex Forward\t12\t754567\n");
}

TEST(CliRendererTest, RendersNullAggregationResultsExactly) {
  const emberdb::AggregationResult result{
      {"event_type", "count(*)", "avg(start_x)"},
      {{{emberdb::AggregationValue{std::string{"Pass"}}},
        {emberdb::AggregationValue{std::uint64_t{1}}},
        std::nullopt}}};
  std::ostringstream output;

  emberdb::cli::printAggregationResult(output, result);

  EXPECT_EQ(output.str(),
            "Result rows: 1\n"
            "event_type\tcount(*)\tavg(start_x)\n"
            "Pass\t1\tNULL\n");
}

TEST(CliRendererTest, RendersCandidateDecisionProvenance) {
  emberdb::MatchCandidateRecord candidate;
  candidate.id = 7;
  candidate.status = emberdb::MatchCandidateStatus::Accepted;
  candidate.accepted_match_id = emberdb::CanonicalMatchId{100};
  candidate.decision_provenance = emberdb::ReviewProvenance{
      "reviewer@example.com", "provider match pages", "Metadata agrees",
      std::chrono::sys_seconds{std::chrono::seconds{1'700'000'000}}};
  std::ostringstream output;

  emberdb::cli::printCandidateInspection(output, candidate);

  EXPECT_NE(output.str().find("Decision actor: reviewer@example.com"),
            std::string::npos);
  EXPECT_NE(output.str().find("Decision source: provider match pages"),
            std::string::npos);
  EXPECT_NE(output.str().find("Decision reason: Metadata agrees"),
            std::string::npos);
}

TEST(CliRendererTest, RendersCatalogSummaryAndAuditHistory) {
  emberdb::MatchReviewStore store;
  const emberdb::ReviewProvenance provenance{
      "reviewer", "provider catalog", "Verified identity",
      std::chrono::sys_seconds{std::chrono::seconds{1'700'000'000}}};
  store.addCompetition({{20}, "Premier League"}, provenance);
  store.mapCompetition({"StatsBomb", "2"}, {20}, provenance);
  store.addPlayer({{10}, "Alex Forward"}, provenance);
  store.addPlayer({{11}, "A. Forward"}, provenance);
  store.renameCatalogEntity(emberdb::CatalogEntityType::Player, 10,
                            "Alex A. Forward", provenance);
  store.mergeCatalogEntity(emberdb::CatalogEntityType::Player, 11, 10,
                           provenance);
  std::ostringstream summary;
  std::ostringstream history;

  emberdb::cli::printCatalogSummary(summary, store);
  emberdb::cli::printCatalogHistory(history, store.catalogChanges());

  EXPECT_NE(summary.str().find("Review revision: 6"), std::string::npos);
  EXPECT_NE(summary.str().find("competition\tStatsBomb:2\t20"),
            std::string::npos);
  EXPECT_NE(summary.str().find("player\t10\tAlex A. Forward\tactive\tNULL"),
            std::string::npos);
  EXPECT_NE(summary.str().find("player\t11\tA. Forward\tmerged\t10"),
            std::string::npos);
  EXPECT_NE(history.str().find("2\tmap\tcompetition\t20"),
            std::string::npos);
  EXPECT_NE(history.str().find("5\trename\tplayer\t10\tAlex A. Forward"),
            std::string::npos);
  EXPECT_NE(history.str().find(
                "6\tmerge\tplayer\t11\tA. Forward\tNULL\t10"),
            std::string::npos);
  EXPECT_NE(history.str().find("reviewer\tprovider catalog\tVerified identity"),
            std::string::npos);
}

TEST(CliRendererTest, RendersEntityCandidateEvidenceAndDecision) {
  emberdb::EntityCandidateRecord candidate;
  candidate.id = 7;
  candidate.status = emberdb::MatchCandidateStatus::Accepted;
  candidate.reconciliation = {
      emberdb::IdentityEntityType::Player,
      {"StatsBomb", "99", std::nullopt},
      10,
      "lineups.json",
      {emberdb::ReconciliationStatus::Agreeing, "Alex Forward",
       "Alex Forward"},
      {emberdb::ReconciliationStatus::Missing, std::nullopt, std::nullopt},
      1.0};
  candidate.decision_provenance = emberdb::ReviewProvenance{
      "reviewer", "provider profile", "Verified identity",
      std::chrono::sys_seconds{std::chrono::seconds{1'700'000'000}}};
  std::ostringstream inspection;
  std::ostringstream accepted;

  emberdb::cli::printEntityCandidateInspection(inspection, candidate);
  emberdb::cli::printEntityCandidateAccepted(accepted, candidate);

  EXPECT_NE(inspection.str().find(
                "7\taccepted\tplayer\t1\tStatsBomb:99\t10"),
            std::string::npos);
  EXPECT_NE(inspection.str().find(
                "name\tagreeing\tAlex Forward\tAlex Forward"),
            std::string::npos);
  EXPECT_NE(inspection.str().find("Decision source: provider profile"),
            std::string::npos);
  EXPECT_EQ(accepted.str(),
            "Accepted entity candidate 7: player StatsBomb:99 -> 10\n");
}

TEST(CliRendererTest, RendersUsageForEveryCommandFamily) {
  std::ostringstream output;

  emberdb::cli::printUsage(output);

  EXPECT_NE(output.str().find("emberdb_cli import"), std::string::npos);
  EXPECT_NE(output.str().find("emberdb_cli query"), std::string::npos);
  EXPECT_NE(output.str().find("emberdb_cli reconcile generate"),
            std::string::npos);
  EXPECT_NE(output.str().find("emberdb_cli reconcile reject"),
            std::string::npos);
  EXPECT_NE(output.str().find("emberdb_cli catalog init"), std::string::npos);
  EXPECT_NE(output.str().find("emberdb_cli catalog history"),
            std::string::npos);
  EXPECT_NE(output.str().find("emberdb_cli catalog rename"),
            std::string::npos);
  EXPECT_NE(output.str().find("emberdb_cli catalog deprecate"),
            std::string::npos);
  EXPECT_NE(output.str().find("emberdb_cli catalog merge"),
            std::string::npos);
  EXPECT_NE(output.str().find("emberdb_cli catalog candidates generate"),
            std::string::npos);
  EXPECT_NE(output.str().find("emberdb_cli catalog candidates reject"),
            std::string::npos);
}

}  // namespace
