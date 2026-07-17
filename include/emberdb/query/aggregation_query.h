#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "emberdb/query/event_query.h"

namespace emberdb {

enum class AggregateFunction { Count, Sum, Average, Minimum, Maximum };

class AggregateExpression {
 public:
  AggregateExpression(AggregateFunction function,
                      std::optional<FootballEventColumn> column = std::nullopt);

  [[nodiscard]] AggregateFunction function() const noexcept;
  [[nodiscard]] std::optional<FootballEventColumn> column() const noexcept;
  [[nodiscard]] std::string name() const;

 private:
  AggregateFunction function_;
  std::optional<FootballEventColumn> column_;
};

struct AggregationQuery {
  std::vector<EqualityPredicate> filters;
  std::vector<FootballEventColumn> group_by;
  std::vector<AggregateExpression> aggregates;
};

using AggregationValue =
    std::variant<std::int64_t, std::int32_t, std::chrono::milliseconds, double,
                 std::string, std::uint64_t>;
using AggregationCell = std::optional<AggregationValue>;

class AggregationResult {
 public:
  AggregationResult(std::vector<std::string> column_names,
                    std::vector<std::vector<AggregationCell>> rows);

  [[nodiscard]] const std::vector<std::string>& columnNames() const noexcept;
  [[nodiscard]] std::size_t rowCount() const noexcept;
  [[nodiscard]] std::size_t columnCount() const noexcept;
  [[nodiscard]] const AggregationCell& cell(std::size_t row,
                                             std::size_t column) const;

 private:
  std::vector<std::string> column_names_;
  std::vector<std::vector<AggregationCell>> rows_;
};

[[nodiscard]] std::string_view aggregateFunctionName(AggregateFunction function) noexcept;
[[nodiscard]] AggregationResult executeAggregationQuery(
    const FootballEventTable& table, const AggregationQuery& query);

}  // namespace emberdb
