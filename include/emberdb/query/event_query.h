#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "emberdb/common/football_event_column.h"

namespace emberdb {

class FootballEventTable;

enum class FilterOperator {
  Equal,
  NotEqual,
  Less,
  LessOrEqual,
  Greater,
  GreaterOrEqual,
  IsNull,
  IsNotNull,
};

class EventPredicate {
 public:
  EventPredicate(FootballEventColumn column, FootballEventValue value);
  EventPredicate(FootballEventColumn column, FilterOperator operation,
                 FootballEventValue value);
  EventPredicate(FootballEventColumn column, FilterOperator operation);

  [[nodiscard]] FootballEventColumn column() const noexcept;
  [[nodiscard]] FilterOperator operation() const noexcept;
  [[nodiscard]] const std::optional<FootballEventValue>& operand() const noexcept;
  [[nodiscard]] const FootballEventValue& value() const;

 private:
  FootballEventColumn column_;
  FilterOperator operation_;
  std::optional<FootballEventValue> operand_;
};

// Retains source compatibility for callers that only construct equality filters.
using EqualityPredicate = EventPredicate;

struct RowSelection {
  std::vector<EventPredicate> filters;
  std::optional<std::size_t> limit{std::nullopt};
};

struct EventQuery {
  std::vector<EventPredicate> filters;
  std::vector<FootballEventColumn> projection;
  std::optional<std::size_t> limit{std::nullopt};
};

class EventQueryResult {
 public:
  EventQueryResult(std::vector<FootballEventColumn> columns,
                   std::vector<std::vector<FootballEventCell>> rows);

  [[nodiscard]] const std::vector<FootballEventColumn>& columns() const noexcept;
  [[nodiscard]] std::size_t rowCount() const noexcept;
  [[nodiscard]] std::size_t columnCount() const noexcept;
  [[nodiscard]] const FootballEventCell& cell(std::size_t row,
                                               std::size_t column) const;

 private:
  std::vector<FootballEventColumn> columns_;
  std::vector<std::vector<FootballEventCell>> rows_;
};

[[nodiscard]] std::vector<std::size_t> selectRows(
    const FootballEventTable& table, const RowSelection& selection);
[[nodiscard]] EventQueryResult executeQuery(const FootballEventTable& table,
                                            const EventQuery& query);

}  // namespace emberdb
