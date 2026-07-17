#pragma once

#include <cstddef>
#include <vector>

#include "emberdb/common/football_event_column.h"

namespace emberdb {

class FootballEventTable;

class EqualityPredicate {
 public:
  EqualityPredicate(FootballEventColumn column, FootballEventValue value);

  [[nodiscard]] FootballEventColumn column() const noexcept;
  [[nodiscard]] const FootballEventValue& value() const noexcept;

 private:
  FootballEventColumn column_;
  FootballEventValue value_;
};

struct EventQuery {
  std::vector<EqualityPredicate> filters;
  std::vector<FootballEventColumn> projection;
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

[[nodiscard]] EventQueryResult executeQuery(const FootballEventTable& table,
                                            const EventQuery& query);

}  // namespace emberdb
