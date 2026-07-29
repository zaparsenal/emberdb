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

TEST(CliRendererTest, RendersUsageForEveryCommandFamily) {
  std::ostringstream output;

  emberdb::cli::printUsage(output);

  EXPECT_NE(output.str().find("emberdb_cli import"), std::string::npos);
  EXPECT_NE(output.str().find("emberdb_cli query"), std::string::npos);
  EXPECT_NE(output.str().find("emberdb_cli reconcile generate"),
            std::string::npos);
  EXPECT_NE(output.str().find("emberdb_cli reconcile reject"),
            std::string::npos);
}

}  // namespace
