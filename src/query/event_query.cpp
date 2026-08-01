#include "emberdb/query/event_query.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

#include "emberdb/storage/football_event_table.h"

namespace emberdb {
namespace {

bool isNullOperator(FilterOperator operation) noexcept {
  return operation == FilterOperator::IsNull ||
         operation == FilterOperator::IsNotNull;
}

bool isComparisonOperator(FilterOperator operation) noexcept {
  switch (operation) {
    case FilterOperator::Equal:
    case FilterOperator::NotEqual:
    case FilterOperator::Less:
    case FilterOperator::LessOrEqual:
    case FilterOperator::Greater:
    case FilterOperator::GreaterOrEqual:
      return true;
    case FilterOperator::IsNull:
    case FilterOperator::IsNotNull:
      return false;
  }
  return false;
}

bool matches(const FootballEventCell& cell, const EventPredicate& predicate) {
  if (predicate.operation() == FilterOperator::IsNull) {
    return !cell;
  }
  if (predicate.operation() == FilterOperator::IsNotNull) {
    return cell.has_value();
  }
  if (!cell) {
    return false;
  }

  const auto& operand = *predicate.operand();
  switch (predicate.operation()) {
    case FilterOperator::Equal:
      return *cell == operand;
    case FilterOperator::NotEqual:
      return *cell != operand;
    case FilterOperator::Less:
      return *cell < operand;
    case FilterOperator::LessOrEqual:
      return *cell < operand || *cell == operand;
    case FilterOperator::Greater:
      return operand < *cell;
    case FilterOperator::GreaterOrEqual:
      return operand < *cell || *cell == operand;
    case FilterOperator::IsNull:
    case FilterOperator::IsNotNull:
      break;
  }
  return false;
}

}  // namespace

EventPredicate::EventPredicate(FootballEventColumn column,
                               FootballEventValue value)
    : EventPredicate(column, FilterOperator::Equal, std::move(value)) {}

EventPredicate::EventPredicate(FootballEventColumn column,
                               FilterOperator operation,
                               FootballEventValue value)
    : column_(column), operation_(operation), operand_(std::move(value)) {
  if (isNullOperator(operation_)) {
    throw std::invalid_argument("Null filter for column '" +
                                std::string(columnName(column_)) +
                                "' must not have a value");
  }
  if (!isComparisonOperator(operation_)) {
    throw std::invalid_argument("Unsupported filter operation for column '" +
                                std::string(columnName(column_)) + "'");
  }
  if (columnValueType(column_) != valueType(*operand_)) {
    throw std::invalid_argument("Filter value has the wrong type for column '" +
                                std::string(columnName(column_)) + "'");
  }
}

EventPredicate::EventPredicate(FootballEventColumn column,
                               FilterOperator operation)
    : column_(column), operation_(operation) {
  if (!isNullOperator(operation_)) {
    throw std::invalid_argument("Comparison filter for column '" +
                                std::string(columnName(column_)) +
                                "' requires a value");
  }
}

FootballEventColumn EventPredicate::column() const noexcept { return column_; }

FilterOperator EventPredicate::operation() const noexcept { return operation_; }

const std::optional<FootballEventValue>& EventPredicate::operand() const noexcept {
  return operand_;
}

const FootballEventValue& EventPredicate::value() const {
  if (!operand_) {
    throw std::logic_error("Null filter does not have a value");
  }
  return *operand_;
}

EventQueryResult::EventQueryResult(std::vector<FootballEventColumn> columns,
                                   std::vector<std::vector<FootballEventCell>> rows)
    : columns_(std::move(columns)), rows_(std::move(rows)) {
  if (std::any_of(rows_.begin(), rows_.end(), [&](const auto& row) {
        return row.size() != columns_.size();
      })) {
    throw std::invalid_argument("Query result row width does not match its columns");
  }
}

const std::vector<FootballEventColumn>& EventQueryResult::columns() const noexcept {
  return columns_;
}

std::size_t EventQueryResult::rowCount() const noexcept { return rows_.size(); }

std::size_t EventQueryResult::columnCount() const noexcept { return columns_.size(); }

const FootballEventCell& EventQueryResult::cell(std::size_t row,
                                                std::size_t column) const {
  if (row >= rowCount() || column >= columnCount()) {
    throw std::out_of_range("EventQueryResult cell index is out of range");
  }
  return rows_[row][column];
}

std::vector<std::size_t> selectRows(const FootballEventTable& table,
                                    const RowSelection& selection) {
  std::vector<std::size_t> rows;
  const auto capacity = selection.limit
                            ? std::min(table.rowCount(), *selection.limit)
                            : table.rowCount();
  rows.reserve(capacity);
  if (selection.limit && *selection.limit == 0) {
    return rows;
  }

  for (std::size_t row = 0; row < table.rowCount(); ++row) {
    const bool selected = std::all_of(
        selection.filters.begin(), selection.filters.end(),
        [&](const EventPredicate& filter) {
          return matches(table.cell(filter.column(), row), filter);
        });
    if (!selected) {
      continue;
    }
    rows.push_back(row);
    if (selection.limit && rows.size() == *selection.limit) {
      break;
    }
  }
  return rows;
}

EventQueryResult executeQuery(const FootballEventTable& table, const EventQuery& query) {
  if (query.projection.empty()) {
    throw std::invalid_argument("A query projection must contain at least one column");
  }
  for (std::size_t index = 0; index < query.projection.size(); ++index) {
    if (std::find(query.projection.begin(), query.projection.begin() +
                                                static_cast<std::ptrdiff_t>(index),
                  query.projection[index]) !=
        query.projection.begin() + static_cast<std::ptrdiff_t>(index)) {
      throw std::invalid_argument("Duplicate projection column '" +
                                  std::string(columnName(query.projection[index])) + "'");
    }
  }

  std::vector<std::vector<FootballEventCell>> rows;
  const auto selected_rows = selectRows(table, {query.filters, query.limit});
  rows.reserve(selected_rows.size());
  for (const auto row : selected_rows) {
    std::vector<FootballEventCell> projected_row;
    projected_row.reserve(query.projection.size());
    for (const auto column : query.projection) {
      projected_row.push_back(table.cell(column, row));
    }
    rows.push_back(std::move(projected_row));
  }
  return EventQueryResult(query.projection, std::move(rows));
}

}  // namespace emberdb
