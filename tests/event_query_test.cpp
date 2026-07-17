#include "emberdb/query/event_query.h"

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>

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
