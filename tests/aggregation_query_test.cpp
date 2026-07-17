#include "emberdb/query/aggregation_query.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "emberdb/storage/football_event_table.h"

namespace {

emberdb::FootballEvent event(std::string id, std::string type,
                             std::optional<std::string> player, std::int32_t minute,
                             std::optional<emberdb::Coordinate> start) {
  return emberdb::FootballEvent{std::move(id),
                                42,
                                1,
                                {std::chrono::milliseconds(1000), minute, 0},
                                std::nullopt,
                                10,
                                "Ember FC",
                                player ? std::optional<emberdb::Identifier>{99} : std::nullopt,
                                std::move(player),
                                std::move(type),
                                std::nullopt,
                                start,
                                std::nullopt,
                                "StatsBomb"};
}

emberdb::FootballEventTable table() {
  emberdb::FootballEventTable result;
  result.append(event("pass-1", "Pass", "Alex Forward", 12,
                      emberdb::Coordinate{42.5, 31.25}));
  result.append(event("carry-1", "Carry", "Alex Forward", 12,
                      emberdb::Coordinate{71.0, 22.5}));
  result.append(event("pass-2", "Pass", std::nullopt, 13, std::nullopt));
  return result;
}

TEST(AggregationQueryTest, GroupsAndComputesTypedAggregatesInFirstSeenOrder) {
  const auto result = emberdb::executeAggregationQuery(
      table(),
      {{},
       {emberdb::FootballEventColumn::EventType},
       {{emberdb::AggregateFunction::Count},
        {emberdb::AggregateFunction::Count, emberdb::FootballEventColumn::PlayerName},
        {emberdb::AggregateFunction::Average, emberdb::FootballEventColumn::StartX},
        {emberdb::AggregateFunction::Minimum, emberdb::FootballEventColumn::Minute},
        {emberdb::AggregateFunction::Maximum, emberdb::FootballEventColumn::Minute}}});

  ASSERT_EQ(result.rowCount(), 2U);
  ASSERT_EQ(result.columnCount(), 6U);
  EXPECT_EQ(result.columnNames()[0], "event_type");
  EXPECT_EQ(result.columnNames()[1], "count(*)");
  EXPECT_EQ(std::get<std::string>(*result.cell(0, 0)), "Pass");
  EXPECT_EQ(std::get<std::uint64_t>(*result.cell(0, 1)), 2U);
  EXPECT_EQ(std::get<std::uint64_t>(*result.cell(0, 2)), 1U);
  EXPECT_DOUBLE_EQ(std::get<double>(*result.cell(0, 3)), 42.5);
  EXPECT_EQ(std::get<std::int32_t>(*result.cell(0, 4)), 12);
  EXPECT_EQ(std::get<std::int32_t>(*result.cell(0, 5)), 13);
  EXPECT_EQ(std::get<std::string>(*result.cell(1, 0)), "Carry");
}

TEST(AggregationQueryTest, SumsIntegersAndNumbersWithTypedResults) {
  const auto result = emberdb::executeAggregationQuery(
      table(),
      {{},
       {},
       {{emberdb::AggregateFunction::Sum, emberdb::FootballEventColumn::Minute},
        {emberdb::AggregateFunction::Sum, emberdb::FootballEventColumn::StartX}}});

  ASSERT_EQ(result.rowCount(), 1U);
  EXPECT_EQ(std::get<std::int64_t>(*result.cell(0, 0)), 37);
  EXPECT_DOUBLE_EQ(std::get<double>(*result.cell(0, 1)), 113.5);
}

TEST(AggregationQueryTest, AppliesFiltersBeforeGrouping) {
  const auto result = emberdb::executeAggregationQuery(
      table(),
      {{{emberdb::FootballEventColumn::EventType, std::string("Pass")}},
       {emberdb::FootballEventColumn::Minute},
       {{emberdb::AggregateFunction::Count}}});

  ASSERT_EQ(result.rowCount(), 2U);
  EXPECT_EQ(std::get<std::int32_t>(*result.cell(0, 0)), 12);
  EXPECT_EQ(std::get<std::uint64_t>(*result.cell(0, 1)), 1U);
  EXPECT_EQ(std::get<std::int32_t>(*result.cell(1, 0)), 13);
}

TEST(AggregationQueryTest, CoalescesNullGroupKeysAndPreservesThem) {
  auto events = table();
  events.append(event("pass-3", "Pass", std::nullopt, 14, std::nullopt));
  const auto result = emberdb::executeAggregationQuery(
      events,
      {{},
       {emberdb::FootballEventColumn::PlayerName},
       {{emberdb::AggregateFunction::Count}}});

  ASSERT_EQ(result.rowCount(), 2U);
  EXPECT_FALSE(result.cell(1, 0));
  EXPECT_EQ(std::get<std::uint64_t>(*result.cell(1, 1)), 2U);
}

TEST(AggregationQueryTest, ReturnsOneGlobalRowForEmptyInput) {
  const emberdb::FootballEventTable empty;
  const auto result = emberdb::executeAggregationQuery(
      empty,
      {{},
       {},
       {{emberdb::AggregateFunction::Count},
        {emberdb::AggregateFunction::Sum, emberdb::FootballEventColumn::Minute},
        {emberdb::AggregateFunction::Minimum, emberdb::FootballEventColumn::EventType}}});

  ASSERT_EQ(result.rowCount(), 1U);
  EXPECT_EQ(std::get<std::uint64_t>(*result.cell(0, 0)), 0U);
  EXPECT_FALSE(result.cell(0, 1));
  EXPECT_FALSE(result.cell(0, 2));
}

TEST(AggregationQueryTest, ReturnsNoGroupsForEmptyGroupedInput) {
  const emberdb::FootballEventTable empty;
  const auto result = emberdb::executeAggregationQuery(
      empty,
      {{},
       {emberdb::FootballEventColumn::EventType},
       {{emberdb::AggregateFunction::Count}}});
  EXPECT_EQ(result.rowCount(), 0U);
}

TEST(AggregationQueryTest, RejectsInvalidAndDuplicateExpressions) {
  EXPECT_THROW((emberdb::AggregateExpression{emberdb::AggregateFunction::Sum,
                                             emberdb::FootballEventColumn::EventType}),
               std::invalid_argument);
  EXPECT_THROW((emberdb::AggregateExpression{emberdb::AggregateFunction::Average}),
               std::invalid_argument);
  EXPECT_THROW(
      static_cast<void>(emberdb::executeAggregationQuery(
          table(),
          {{},
           {emberdb::FootballEventColumn::EventType,
            emberdb::FootballEventColumn::EventType},
           {{emberdb::AggregateFunction::Count}}})),
      std::invalid_argument);
  EXPECT_THROW(
      static_cast<void>(emberdb::executeAggregationQuery(
          table(),
          {{},
           {},
           {{emberdb::AggregateFunction::Count},
            {emberdb::AggregateFunction::Count}}})),
      std::invalid_argument);
}

}  // namespace
