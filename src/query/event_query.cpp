#include "emberdb/query/event_query.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

#include "emberdb/storage/football_event_table.h"

namespace emberdb {

EqualityPredicate::EqualityPredicate(FootballEventColumn column, FootballEventValue value)
    : column_(column), value_(std::move(value)) {
  if (columnValueType(column_) != valueType(value_)) {
    throw std::invalid_argument("Filter value has the wrong type for column '" +
                                std::string(columnName(column_)) + "'");
  }
}

FootballEventColumn EqualityPredicate::column() const noexcept { return column_; }

const FootballEventValue& EqualityPredicate::value() const noexcept { return value_; }

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
  rows.reserve(table.rowCount());
  for (std::size_t row = 0; row < table.rowCount(); ++row) {
    const bool matches = std::all_of(
        query.filters.begin(), query.filters.end(), [&](const EqualityPredicate& filter) {
          const auto value = table.cell(filter.column(), row);
          return value && *value == filter.value();
        });
    if (!matches) {
      continue;
    }

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
