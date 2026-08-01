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

TEST(CliRendererTest, RendersCatalogValidationCoverageAndDetails) {
  emberdb::CatalogValidationReport report;
  report.entity_type = emberdb::IdentityEntityType::Team;
  report.summary.provider_records = 2;
  report.summary.unmapped_exact_matches = 1;
  report.summary.inactive_exact_matches = 1;
  report.records = {
      {emberdb::IdentityEntityType::Team,
       {"Wyscout", "1609", std::nullopt},
       "Arsenal",
       emberdb::CatalogValidationOutcome::ExactMatch,
       {1},
       emberdb::ReconciliationStatus::Agreeing,
       emberdb::CatalogValidationContextStatus::NotApplicable},
      {emberdb::IdentityEntityType::Team,
       {"Wyscout", "1631", std::nullopt},
       "Leicester City",
       emberdb::CatalogValidationOutcome::InactiveExactMatch,
       {2},
       emberdb::ReconciliationStatus::Agreeing,
       emberdb::CatalogValidationContextStatus::NotApplicable}};
  std::ostringstream output;

  emberdb::cli::printCatalogValidation(output, report, 19);

  EXPECT_NE(output.str().find("Catalog validation: team"),
            std::string::npos);
  EXPECT_NE(output.str().find("Review revision: 19"),
            std::string::npos);
  EXPECT_NE(output.str().find("Unmapped exact matches: 1"),
            std::string::npos);
  EXPECT_NE(output.str().find(
                "Wyscout:1631\tLeicester City\tinactive_exact_match\t2"),
            std::string::npos);
}

TEST(CliRendererTest, RendersDeterministicEventIdentityCoverage) {
  emberdb::EventIdentityCoverageReport report;
  report.source = "events.ember";
  report.total_events = 1;
  report.matches = {1, 1, 0, 0};
  report.teams = {1, 0, 1, 0};
  report.players = {0, 0, 0, 1};
  emberdb::EventIdentityDiagnostic event;
  event.source = report.source;
  event.provider = "StatsBomb";
  event.provider_event_id = "event-1";
  event.provider_match_id = 12345;
  event.provider_team_id = 10;
  event.provider_team_name = "Ember FC";
  event.match_status = emberdb::EventIdentityStatus::Resolved;
  event.team_status = emberdb::EventIdentityStatus::Missing;
  event.player_status = emberdb::EventIdentityStatus::Absent;
  event.canonical_identity.match_id = emberdb::CanonicalMatchId{100};
  report.events.push_back(event);
  std::ostringstream output;

  emberdb::cli::printEventIdentityCoverage(output, report, 7);

  EXPECT_EQ(
      output.str(),
      "Event identity coverage\n"
      "Source: events.ember\n"
      "Review revision: 7\n"
      "Events: 1\n\n"
      "entity\tpresent\tresolved\tmissing\tabsent\n"
      "match\t1\t1\t0\t0\n"
      "team\t1\t0\t1\t0\n"
      "player\t0\t0\t0\t1\n\n"
      "index\tsource\tprovider\tprovider_event_id\tprovider_match_id\t"
      "match_status\tcanonical_match_id\tprovider_team_id\t"
      "provider_team_name\tteam_status\tcanonical_team_id\t"
      "provider_player_id\tprovider_player_name\tplayer_status\t"
      "canonical_player_id\n"
      "0\tevents.ember\tStatsBomb\tevent-1\t12345\tresolved\t100\t10\t"
      "Ember FC\tmissing\tNULL\tNULL\tNULL\tabsent\tNULL\n");
}

TEST(CliRendererTest, RendersDeterministicCatalogManifestReview) {
  emberdb::CatalogManifestReport report;
  report.base_revision = 4;
  report.planned_revision = 5;
  report.summary.create = 1;
  report.summary.unchanged = 1;
  report.results = {
      {emberdb::CatalogEntityType::Team, 1, "North FC", std::nullopt,
       std::nullopt, std::nullopt, emberdb::CatalogManifestAction::Create,
       "", 0},
      {emberdb::CatalogEntityType::Team, 1, "North FC", "StatsBomb", "10",
       std::nullopt, emberdb::CatalogManifestAction::Unchanged,
       "provider mapping already matches", 1}};
  std::ostringstream output;

  emberdb::cli::printCatalogManifestReport(output, report, true, false);

  EXPECT_EQ(output.str(),
            "Catalog manifest import\n"
            "Mode: dry-run\n"
            "Manifest version: 1\n"
            "Store revision: 4\n"
            "Planned revision: 5\n"
            "Batch: ready\n"
            "Create: 1\n"
            "Unchanged: 1\n"
            "Conflicts: 0\n"
            "Invalid: 0\n"
            "\n"
            "team 1 North FC\n"
            "  action: create\n"
            "\n"
            "team 1 North FC\n"
            "  provider mapping: StatsBomb:10\n"
            "  action: unchanged\n"
            "  reason: provider mapping already matches\n");
}

TEST(CliRendererTest, RendersUsageForEveryCommandFamily) {
  std::ostringstream output;

  emberdb::cli::printUsage(output);

  EXPECT_NE(output.str().find("emberdb_cli import"), std::string::npos);
  EXPECT_NE(output.str().find("emberdb_cli query"), std::string::npos);
  EXPECT_NE(output.str().find("emberdb_cli reconcile generate"),
            std::string::npos);
  EXPECT_NE(output.str().find("emberdb_cli catalog import"),
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
  EXPECT_NE(output.str().find("emberdb_cli catalog validate"),
            std::string::npos);
  EXPECT_NE(output.str().find("emberdb_cli catalog coverage"),
            std::string::npos);
  EXPECT_NE(output.str().find("emberdb_cli catalog candidates generate"),
            std::string::npos);
  EXPECT_NE(output.str().find("emberdb_cli catalog candidates reject"),
            std::string::npos);
}

}  // namespace
