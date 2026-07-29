#include "cli/command_parser.h"

#include <array>
#include <chrono>
#include <stdexcept>
#include <string_view>

#include <gtest/gtest.h>

namespace {

TEST(CliCommandParserTest, ParsesRawProjectionQueryOptions) {
  constexpr std::array arguments{
      std::string_view{"emberdb_cli"},
      std::string_view{"query"},
      std::string_view{"--provider"},
      std::string_view{"statsbomb"},
      std::string_view{"--match-id"},
      std::string_view{"12345"},
      std::string_view{"--input"},
      std::string_view{"events.json"},
      std::string_view{"--filter"},
      std::string_view{"minute=12"},
      std::string_view{"--project"},
      std::string_view{"player_name,minute"}};

  const auto options = emberdb::cli::parseOptions(arguments);
  const auto query = emberdb::cli::makeEventQuery(options);

  EXPECT_EQ(options.command, emberdb::cli::Command::Query);
  EXPECT_EQ(options.provider, "statsbomb");
  EXPECT_EQ(options.match_id, 12345);
  ASSERT_EQ(query.filters.size(), 1U);
  EXPECT_EQ(query.filters[0].column(), emberdb::FootballEventColumn::Minute);
  EXPECT_EQ(query.filters[0].value(),
            emberdb::FootballEventValue{std::int32_t{12}});
  EXPECT_EQ(query.projection,
            (std::vector{emberdb::FootballEventColumn::PlayerName,
                         emberdb::FootballEventColumn::Minute}));
}

TEST(CliCommandParserTest, ParsesGroupedAggregationOptions) {
  constexpr std::array arguments{
      std::string_view{"emberdb_cli"},
      std::string_view{"query"},
      std::string_view{"--database"},
      std::string_view{"match.ember"},
      std::string_view{"--group-by"},
      std::string_view{"event_type"},
      std::string_view{"--aggregate"},
      std::string_view{"count(*)"},
      std::string_view{"--aggregate"},
      std::string_view{"avg(start_x)"}};

  const auto options = emberdb::cli::parseOptions(arguments);
  const auto query = emberdb::cli::makeAggregationQuery(options);

  EXPECT_EQ(options.database, "match.ember");
  EXPECT_EQ(query.group_by,
            (std::vector{emberdb::FootballEventColumn::EventType}));
  ASSERT_EQ(query.aggregates.size(), 2U);
  EXPECT_EQ(query.aggregates[0].name(), "count(*)");
  EXPECT_EQ(query.aggregates[1].name(), "avg(start_x)");
}

TEST(CliCommandParserTest, ParsesReconciliationDecisionOptions) {
  constexpr std::array arguments{
      std::string_view{"emberdb_cli"},
      std::string_view{"reconcile"},
      std::string_view{"reject"},
      std::string_view{"--review"},
      std::string_view{"review.json"},
      std::string_view{"--candidate-id"},
      std::string_view{"7"},
      std::string_view{"--reason"},
      std::string_view{"Wrong fixture"}};

  const auto options = emberdb::cli::parseOptions(arguments);

  EXPECT_EQ(options.command, emberdb::cli::Command::ReconcileReject);
  EXPECT_EQ(options.review, "review.json");
  EXPECT_EQ(options.candidate_id, 7U);
  EXPECT_EQ(options.reason, "Wrong fixture");
}

TEST(CliCommandParserTest, PreservesCommandValidationFailures) {
  constexpr std::array mixed_sources{
      std::string_view{"emberdb_cli"},
      std::string_view{"query"},
      std::string_view{"--database"},
      std::string_view{"match.ember"},
      std::string_view{"--provider"},
      std::string_view{"statsbomb"},
      std::string_view{"--project"},
      std::string_view{"event_type"}};
  EXPECT_THROW(
      static_cast<void>(emberdb::cli::parseOptions(mixed_sources)),
      std::runtime_error);

  constexpr std::array missing_direction{
      std::string_view{"emberdb_cli"},
      std::string_view{"import"},
      std::string_view{"--provider"},
      std::string_view{"metrica"},
      std::string_view{"--match-id"},
      std::string_view{"42"},
      std::string_view{"--input"},
      std::string_view{"events.csv"}};
  EXPECT_THROW(
      static_cast<void>(emberdb::cli::parseOptions(missing_direction)),
      std::runtime_error);
}

}  // namespace
