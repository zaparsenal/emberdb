#pragma once

#include <span>
#include <string_view>

#include "cli/command.h"
#include "emberdb/query/aggregation_query.h"
#include "emberdb/query/event_query.h"

namespace emberdb::cli {

[[nodiscard]] Options parseOptions(
    std::span<const std::string_view> arguments);
[[nodiscard]] EventQuery makeEventQuery(const Options& options);
[[nodiscard]] AggregationQuery makeAggregationQuery(const Options& options);

}  // namespace emberdb::cli
