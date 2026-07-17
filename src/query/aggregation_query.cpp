#include "emberdb/query/aggregation_query.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "emberdb/storage/football_event_table.h"

namespace emberdb {
namespace {

struct Accumulator {
  std::uint64_t count{};
  std::int64_t integer_sum{};
  long double numeric_sum{};
  std::optional<FootballEventValue> extremum;
};

struct GroupState {
  std::vector<FootballEventCell> key;
  std::vector<Accumulator> accumulators;
};

bool isNumeric(FootballEventColumn column) noexcept {
  const auto type = columnValueType(column);
  return type == FootballEventValueType::Integer ||
         type == FootballEventValueType::Number;
}

bool matchesFilters(const FootballEventTable& table, std::size_t row,
                    const std::vector<EqualityPredicate>& filters) {
  return std::all_of(filters.begin(), filters.end(),
                     [&](const EqualityPredicate& filter) {
                       const auto value = table.cell(filter.column(), row);
                       return value && *value == filter.value();
                     });
}

void increment(std::uint64_t& value) {
  if (value == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error("Aggregate row count overflow");
  }
  ++value;
}

void addInteger(std::int64_t& total, std::int32_t value) {
  const auto widened = static_cast<std::int64_t>(value);
  if ((widened > 0 && total > std::numeric_limits<std::int64_t>::max() - widened) ||
      (widened < 0 && total < std::numeric_limits<std::int64_t>::min() - widened)) {
    throw std::overflow_error("Integer aggregate overflow");
  }
  total += widened;
}

void updateAccumulator(Accumulator& accumulator, const AggregateExpression& expression,
                       const FootballEventCell& cell) {
  if (expression.function() == AggregateFunction::Count && !expression.column()) {
    increment(accumulator.count);
    return;
  }
  if (!cell) {
    return;
  }

  switch (expression.function()) {
    case AggregateFunction::Count:
      increment(accumulator.count);
      break;
    case AggregateFunction::Sum:
      increment(accumulator.count);
      if (std::holds_alternative<std::int32_t>(*cell)) {
        addInteger(accumulator.integer_sum, std::get<std::int32_t>(*cell));
      } else {
        accumulator.numeric_sum +=
            static_cast<long double>(std::get<double>(*cell));
      }
      break;
    case AggregateFunction::Average:
      increment(accumulator.count);
      if (std::holds_alternative<std::int32_t>(*cell)) {
        accumulator.numeric_sum +=
            static_cast<long double>(std::get<std::int32_t>(*cell));
      } else {
        accumulator.numeric_sum +=
            static_cast<long double>(std::get<double>(*cell));
      }
      break;
    case AggregateFunction::Minimum:
      if (!accumulator.extremum || *cell < *accumulator.extremum) {
        accumulator.extremum = *cell;
      }
      break;
    case AggregateFunction::Maximum:
      if (!accumulator.extremum || *accumulator.extremum < *cell) {
        accumulator.extremum = *cell;
      }
      break;
  }
}

AggregationValue toAggregationValue(const FootballEventValue& value) {
  return std::visit([](const auto& typed_value) -> AggregationValue {
    return typed_value;
  }, value);
}

AggregationCell finalize(const Accumulator& accumulator,
                         const AggregateExpression& expression) {
  switch (expression.function()) {
    case AggregateFunction::Count:
      return AggregationValue{accumulator.count};
    case AggregateFunction::Sum:
      if (accumulator.count == 0) {
        return std::nullopt;
      }
      if (columnValueType(*expression.column()) == FootballEventValueType::Integer) {
        return AggregationValue{accumulator.integer_sum};
      }
      return AggregationValue{static_cast<double>(accumulator.numeric_sum)};
    case AggregateFunction::Average:
      if (accumulator.count == 0) {
        return std::nullopt;
      }
      return AggregationValue{static_cast<double>(
          accumulator.numeric_sum / static_cast<long double>(accumulator.count))};
    case AggregateFunction::Minimum:
    case AggregateFunction::Maximum:
      if (!accumulator.extremum) {
        return std::nullopt;
      }
      return toAggregationValue(*accumulator.extremum);
  }
  return std::nullopt;
}

std::vector<FootballEventCell> groupKey(const FootballEventTable& table,
                                        const std::vector<FootballEventColumn>& columns,
                                        std::size_t row) {
  std::vector<FootballEventCell> key;
  key.reserve(columns.size());
  for (const auto column : columns) {
    key.push_back(table.cell(column, row));
  }
  return key;
}

AggregationCell toAggregationCell(const FootballEventCell& cell) {
  if (!cell) {
    return std::nullopt;
  }
  return toAggregationValue(*cell);
}

}  // namespace

std::string_view aggregateFunctionName(AggregateFunction function) noexcept {
  switch (function) {
    case AggregateFunction::Count:
      return "count";
    case AggregateFunction::Sum:
      return "sum";
    case AggregateFunction::Average:
      return "avg";
    case AggregateFunction::Minimum:
      return "min";
    case AggregateFunction::Maximum:
      return "max";
  }
  return "unknown";
}

AggregateExpression::AggregateExpression(
    AggregateFunction function, std::optional<FootballEventColumn> column)
    : function_(function), column_(column) {
  if (function_ != AggregateFunction::Count && !column_) {
    throw std::invalid_argument(std::string(aggregateFunctionName(function_)) +
                                " requires an input column");
  }
  if ((function_ == AggregateFunction::Sum ||
       function_ == AggregateFunction::Average) &&
      !isNumeric(*column_)) {
    throw std::invalid_argument(std::string(aggregateFunctionName(function_)) +
                                " requires an integer or numeric column");
  }
}

AggregateFunction AggregateExpression::function() const noexcept { return function_; }

std::optional<FootballEventColumn> AggregateExpression::column() const noexcept {
  return column_;
}

std::string AggregateExpression::name() const {
  return std::string(aggregateFunctionName(function_)) + "(" +
         (column_ ? std::string(columnName(*column_)) : "*") + ")";
}

AggregationResult::AggregationResult(
    std::vector<std::string> column_names,
    std::vector<std::vector<AggregationCell>> rows)
    : column_names_(std::move(column_names)), rows_(std::move(rows)) {
  if (std::any_of(rows_.begin(), rows_.end(), [&](const auto& row) {
        return row.size() != column_names_.size();
      })) {
    throw std::invalid_argument("Aggregation result row width does not match its columns");
  }
}

const std::vector<std::string>& AggregationResult::columnNames() const noexcept {
  return column_names_;
}

std::size_t AggregationResult::rowCount() const noexcept { return rows_.size(); }

std::size_t AggregationResult::columnCount() const noexcept {
  return column_names_.size();
}

const AggregationCell& AggregationResult::cell(std::size_t row,
                                               std::size_t column) const {
  if (row >= rowCount() || column >= columnCount()) {
    throw std::out_of_range("AggregationResult cell index is out of range");
  }
  return rows_[row][column];
}

AggregationResult executeAggregationQuery(const FootballEventTable& table,
                                          const AggregationQuery& query) {
  if (query.aggregates.empty()) {
    throw std::invalid_argument("An aggregation query requires at least one aggregate");
  }
  for (std::size_t index = 0; index < query.group_by.size(); ++index) {
    if (std::find(query.group_by.begin(),
                  query.group_by.begin() + static_cast<std::ptrdiff_t>(index),
                  query.group_by[index]) !=
        query.group_by.begin() + static_cast<std::ptrdiff_t>(index)) {
      throw std::invalid_argument("Duplicate group column '" +
                                  std::string(columnName(query.group_by[index])) + "'");
    }
  }
  for (std::size_t index = 0; index < query.aggregates.size(); ++index) {
    const auto duplicate = std::find_if(
        query.aggregates.begin(),
        query.aggregates.begin() + static_cast<std::ptrdiff_t>(index),
        [&](const AggregateExpression& candidate) {
          return candidate.function() == query.aggregates[index].function() &&
                 candidate.column() == query.aggregates[index].column();
        });
    if (duplicate !=
        query.aggregates.begin() + static_cast<std::ptrdiff_t>(index)) {
      throw std::invalid_argument("Duplicate aggregate '" +
                                  query.aggregates[index].name() + "'");
    }
  }

  std::vector<GroupState> groups;
  if (query.group_by.empty()) {
    groups.push_back({{}, std::vector<Accumulator>(query.aggregates.size())});
  }

  for (std::size_t row = 0; row < table.rowCount(); ++row) {
    if (!matchesFilters(table, row, query.filters)) {
      continue;
    }
    const auto key = groupKey(table, query.group_by, row);
    auto group = groups.begin();
    if (!query.group_by.empty()) {
      group = std::find_if(groups.begin(), groups.end(),
                           [&](const GroupState& candidate) {
                             return candidate.key == key;
                           });
      if (group == groups.end()) {
        groups.push_back({key, std::vector<Accumulator>(query.aggregates.size())});
        group = std::prev(groups.end());
      }
    }

    for (std::size_t index = 0; index < query.aggregates.size(); ++index) {
      const auto column = query.aggregates[index].column();
      const auto cell = column ? table.cell(*column, row) : FootballEventCell{};
      updateAccumulator(group->accumulators[index], query.aggregates[index], cell);
    }
  }

  std::vector<std::string> column_names;
  column_names.reserve(query.group_by.size() + query.aggregates.size());
  for (const auto column : query.group_by) {
    column_names.emplace_back(columnName(column));
  }
  for (const auto& expression : query.aggregates) {
    column_names.push_back(expression.name());
  }

  std::vector<std::vector<AggregationCell>> rows;
  rows.reserve(groups.size());
  for (const auto& group : groups) {
    std::vector<AggregationCell> result_row;
    result_row.reserve(column_names.size());
    for (const auto& cell : group.key) {
      result_row.push_back(toAggregationCell(cell));
    }
    for (std::size_t index = 0; index < query.aggregates.size(); ++index) {
      result_row.push_back(finalize(group.accumulators[index],
                                    query.aggregates[index]));
    }
    rows.push_back(std::move(result_row));
  }
  return AggregationResult(std::move(column_names), std::move(rows));
}

}  // namespace emberdb
