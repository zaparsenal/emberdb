#include "emberdb/query/event_query.h"

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

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
                                "StatsBomb",
                                std::nullopt,
                                std::nullopt};
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

TEST(EventQueryTest, FiltersAndProjectsTypedColumnsInSourceOrder) {
  const auto result = emberdb::executeQuery(
      table(),
      {{{emberdb::FootballEventColumn::EventType, std::string("Pass")}},
       {emberdb::FootballEventColumn::PlayerName, emberdb::FootballEventColumn::Minute,
        emberdb::FootballEventColumn::StartX, emberdb::FootballEventColumn::StartY}});

  ASSERT_EQ(result.rowCount(), 2U);
  ASSERT_EQ(result.columnCount(), 4U);
  ASSERT_TRUE(result.cell(0, 0));
  EXPECT_EQ(std::get<std::string>(*result.cell(0, 0)), "Alex Forward");
  EXPECT_EQ(std::get<std::int32_t>(*result.cell(0, 1)), 12);
  EXPECT_DOUBLE_EQ(std::get<double>(*result.cell(0, 2)), 42.5);
  EXPECT_DOUBLE_EQ(std::get<double>(*result.cell(0, 3)), 31.25);
  EXPECT_FALSE(result.cell(1, 0));
  EXPECT_FALSE(result.cell(1, 2));
  EXPECT_FALSE(result.cell(1, 3));
}

TEST(EventQueryTest, CombinesEqualityFiltersWithAndSemantics) {
  const auto result = emberdb::executeQuery(
      table(),
      {{{emberdb::FootballEventColumn::EventType, std::string("Pass")},
        {emberdb::FootballEventColumn::Minute, std::int32_t{13}}},
       {emberdb::FootballEventColumn::ProviderEventId}});

  ASSERT_EQ(result.rowCount(), 1U);
  EXPECT_EQ(std::get<std::string>(*result.cell(0, 0)), "pass-2");
}

TEST(EventQueryTest, RejectsFilterValuesWithTheWrongType) {
  EXPECT_THROW(
      (emberdb::EqualityPredicate{emberdb::FootballEventColumn::Minute,
                                  std::string("12")}),
      std::invalid_argument);
}

TEST(EventQueryTest, ValidatesPredicateOperatorAndOperandForms) {
  EXPECT_THROW(
      (emberdb::EventPredicate{emberdb::FootballEventColumn::Minute,
                               emberdb::FilterOperator::Less}),
      std::invalid_argument);
  EXPECT_THROW(
      (emberdb::EventPredicate{emberdb::FootballEventColumn::PlayerName,
                               emberdb::FilterOperator::IsNull,
                               std::string("unused")}),
      std::invalid_argument);
  EXPECT_THROW(
      (emberdb::EventPredicate{emberdb::FootballEventColumn::StartX,
                               emberdb::FilterOperator::Greater,
                               std::int32_t{40}}),
      std::invalid_argument);
}

TEST(EventQueryTest, SupportsAllTypedComparisonOperators) {
  const auto events = table();
  const auto selected = [&](emberdb::FilterOperator operation,
                            emberdb::FootballEventValue value) {
    return emberdb::selectRows(
        events,
        {{{emberdb::FootballEventColumn::Minute, operation, std::move(value)}},
         std::nullopt});
  };

  EXPECT_EQ(selected(emberdb::FilterOperator::Equal, std::int32_t{12}),
            (std::vector<std::size_t>{0, 1}));
  EXPECT_EQ(selected(emberdb::FilterOperator::NotEqual, std::int32_t{12}),
            (std::vector<std::size_t>{2}));
  EXPECT_EQ(selected(emberdb::FilterOperator::Less, std::int32_t{13}),
            (std::vector<std::size_t>{0, 1}));
  EXPECT_EQ(selected(emberdb::FilterOperator::LessOrEqual, std::int32_t{12}),
            (std::vector<std::size_t>{0, 1}));
  EXPECT_EQ(selected(emberdb::FilterOperator::Greater, std::int32_t{12}),
            (std::vector<std::size_t>{2}));
  EXPECT_EQ(selected(emberdb::FilterOperator::GreaterOrEqual,
                     std::int32_t{13}),
            (std::vector<std::size_t>{2}));
  EXPECT_EQ(
      emberdb::selectRows(
          events,
          {{{emberdb::FootballEventColumn::EventType,
             emberdb::FilterOperator::Less, std::string("Pass")}},
           std::nullopt}),
      (std::vector<std::size_t>{1}));
  EXPECT_EQ(
      emberdb::selectRows(
          events,
          {{{emberdb::FootballEventColumn::StartX,
             emberdb::FilterOperator::Greater, 50.0}},
           std::nullopt}),
      (std::vector<std::size_t>{1}));
}

TEST(EventQueryTest, AppliesExplicitNullSemantics) {
  const auto events = table();
  EXPECT_EQ(
      emberdb::selectRows(
          events,
          {{{emberdb::FootballEventColumn::PlayerName,
             emberdb::FilterOperator::IsNull}},
           std::nullopt}),
      (std::vector<std::size_t>{2}));
  EXPECT_EQ(
      emberdb::selectRows(
          events,
          {{{emberdb::FootballEventColumn::PlayerName,
             emberdb::FilterOperator::IsNotNull}},
           std::nullopt}),
      (std::vector<std::size_t>{0, 1}));

  // Comparisons with null are unknown/non-matching, including !=.
  EXPECT_EQ(
      emberdb::selectRows(
          events,
          {{{emberdb::FootballEventColumn::PlayerName,
             emberdb::FilterOperator::NotEqual, std::string("Nobody")}},
           std::nullopt}),
      (std::vector<std::size_t>{0, 1}));
  EXPECT_TRUE(emberdb::selectRows(
                  events,
                  {{{emberdb::FootballEventColumn::EventType,
                     emberdb::FilterOperator::IsNull}},
                   std::nullopt})
                  .empty());
}

TEST(EventQueryTest, LimitsMatchingRowsInStableSourceOrder) {
  const auto events = table();
  const auto selected = emberdb::selectRows(
      events,
      {{{emberdb::FootballEventColumn::Minute,
         emberdb::FilterOperator::GreaterOrEqual, std::int32_t{12}}},
       2U});
  EXPECT_EQ(selected, (std::vector<std::size_t>{0, 1}));

  const auto result = emberdb::executeQuery(
      events,
      {{{emberdb::FootballEventColumn::Minute,
         emberdb::FilterOperator::GreaterOrEqual, std::int32_t{12}}},
       {emberdb::FootballEventColumn::ProviderEventId},
       1U});
  ASSERT_EQ(result.rowCount(), 1U);
  EXPECT_EQ(std::get<std::string>(*result.cell(0, 0)), "pass-1");

  const auto empty = emberdb::executeQuery(
      events, {{}, {emberdb::FootballEventColumn::ProviderEventId}, 0U});
  EXPECT_EQ(empty.rowCount(), 0U);
}

TEST(EventQueryTest, RejectsEmptyAndDuplicateProjections) {
  EXPECT_THROW(static_cast<void>(emberdb::executeQuery(table(), {})),
               std::invalid_argument);
  EXPECT_THROW(
      static_cast<void>(emberdb::executeQuery(
          table(), {{}, {emberdb::FootballEventColumn::Minute,
                         emberdb::FootballEventColumn::Minute}})),
      std::invalid_argument);
}

TEST(EventQueryTest, RejectsInconsistentResultRows) {
  EXPECT_THROW(
      (emberdb::EventQueryResult{{emberdb::FootballEventColumn::Minute}, {{}}}),
      std::invalid_argument);
}

TEST(EventQueryTest, ResolvesStableProviderNeutralColumnNames) {
  EXPECT_EQ(emberdb::columnFromName("event_type"),
            emberdb::FootballEventColumn::EventType);
  EXPECT_EQ(emberdb::columnName(emberdb::FootballEventColumn::StartX), "start_x");
  EXPECT_EQ(emberdb::columnFromName("source_start_x"),
            emberdb::FootballEventColumn::SourceStartX);
  EXPECT_FALSE(emberdb::columnFromName("raw_statsbomb_type"));
}

}  // namespace
