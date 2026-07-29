#include "cli/command_parser.h"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "emberdb/common/football_event_column.h"

namespace emberdb::cli {
namespace {

template <typename Integer>
Integer parseInteger(std::string_view text, std::string_view option) {
  Integer value{};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    throw std::runtime_error("Invalid value for " + std::string(option) + ": '" +
                             std::string(text) + "'");
  }
  return value;
}

FootballEventColumn parseColumn(std::string_view name) {
  const auto column = columnFromName(name);
  if (!column) {
    throw std::runtime_error("Unknown column '" + std::string(name) + "'");
  }
  return *column;
}

std::vector<FootballEventColumn> parseProjection(std::string_view text) {
  std::vector<FootballEventColumn> columns;
  while (!text.empty()) {
    const auto separator = text.find(',');
    const auto name = text.substr(0, separator);
    if (name.empty()) {
      throw std::runtime_error("Projection contains an empty column name");
    }
    columns.push_back(parseColumn(name));
    if (separator == std::string_view::npos) {
      break;
    }
    text.remove_prefix(separator + 1);
    if (text.empty()) {
      throw std::runtime_error("Projection contains an empty column name");
    }
  }
  return columns;
}

FootballEventValue parseFilterValue(FootballEventColumn column,
                                    std::string_view text) {
  switch (columnValueType(column)) {
    case FootballEventValueType::Identifier:
      return parseInteger<Identifier>(text, "filter");
    case FootballEventValueType::Integer:
      return parseInteger<std::int32_t>(text, "filter");
    case FootballEventValueType::Timestamp:
      return std::chrono::milliseconds{
          parseInteger<std::int64_t>(text, "filter")};
    case FootballEventValueType::Number: {
      double value{};
      std::istringstream input{std::string(text)};
      input >> std::noskipws >> value;
      if (!input || !input.eof()) {
        throw std::runtime_error("Invalid numeric filter value '" +
                                 std::string(text) + "'");
      }
      return value;
    }
    case FootballEventValueType::Text:
      return std::string(text);
  }
  throw std::runtime_error("Unsupported filter value type");
}

EqualityPredicate parseFilter(std::string_view text) {
  const auto separator = text.find('=');
  if (separator == std::string_view::npos || separator == 0) {
    throw std::runtime_error("Filter must have the form COLUMN=VALUE");
  }
  const auto column = parseColumn(text.substr(0, separator));
  return {column, parseFilterValue(column, text.substr(separator + 1))};
}

AggregateFunction parseAggregateFunction(std::string_view name) {
  if (name == "count") {
    return AggregateFunction::Count;
  }
  if (name == "sum") {
    return AggregateFunction::Sum;
  }
  if (name == "avg") {
    return AggregateFunction::Average;
  }
  if (name == "min") {
    return AggregateFunction::Minimum;
  }
  if (name == "max") {
    return AggregateFunction::Maximum;
  }
  throw std::runtime_error("Unknown aggregate function '" + std::string(name) +
                           "'");
}

AggregateExpression parseAggregate(std::string_view text) {
  const auto open = text.find('(');
  if (open == std::string_view::npos || open == 0 || text.back() != ')' ||
      text.find(')', open) != text.size() - 1) {
    throw std::runtime_error(
        "Aggregate must have the form FUNCTION(COLUMN) or count(*)");
  }
  const auto function = parseAggregateFunction(text.substr(0, open));
  const auto input = text.substr(open + 1, text.size() - open - 2);
  if (input == "*") {
    if (function != AggregateFunction::Count) {
      throw std::runtime_error("Only count(*) accepts '*'");
    }
    return {function};
  }
  if (input.empty()) {
    throw std::runtime_error("Aggregate input column cannot be empty");
  }
  return {function, parseColumn(input)};
}

}  // namespace

Options parseOptions(std::span<const std::string_view> arguments) {
  if (arguments.size() < 2) {
    throw std::runtime_error(
        "Expected the 'import', 'query', or 'reconcile' command");
  }
  Options options;
  const auto command = arguments[1];
  std::size_t first_option = 2;
  if (command == "import") {
    options.command = Command::Import;
  } else if (command == "query") {
    options.command = Command::Query;
  } else if (command == "reconcile") {
    if (arguments.size() < 3) {
      throw std::runtime_error("Expected a reconcile action");
    }
    const auto action = arguments[2];
    if (action == "generate") {
      options.command = Command::ReconcileGenerate;
    } else if (action == "list") {
      options.command = Command::ReconcileList;
    } else if (action == "inspect") {
      options.command = Command::ReconcileInspect;
    } else if (action == "accept") {
      options.command = Command::ReconcileAccept;
    } else if (action == "reject") {
      options.command = Command::ReconcileReject;
    } else {
      throw std::runtime_error("Unknown reconcile action '" +
                               std::string(action) + "'");
    }
    first_option = 3;
  } else {
    throw std::runtime_error(
        "Expected the 'import', 'query', or 'reconcile' command");
  }

  for (std::size_t index = first_option; index < arguments.size(); ++index) {
    const auto option = arguments[index];
    if (index + 1 >= arguments.size()) {
      throw std::runtime_error("Missing value for " + std::string(option));
    }
    const auto value = arguments[++index];
    if (option == "--provider") {
      options.provider = value;
    } else if (option == "--match-id") {
      options.match_id = parseInteger<Identifier>(value, option);
      options.has_match_id = true;
    } else if (option == "--input") {
      options.input = value;
    } else if (option == "--output") {
      options.output = value;
    } else if (option == "--database") {
      options.database = value;
    } else if (option == "--limit") {
      options.limit = parseInteger<std::size_t>(value, option);
      options.has_limit = true;
    } else if (option == "--filter") {
      options.filters.emplace_back(value);
    } else if (option == "--project") {
      if (!options.projection.empty()) {
        throw std::runtime_error("--project may only be specified once");
      }
      options.projection = value;
    } else if (option == "--group-by") {
      if (!options.group_by.empty()) {
        throw std::runtime_error("--group-by may only be specified once");
      }
      options.group_by = value;
    } else if (option == "--aggregate") {
      options.aggregates.emplace_back(value);
    } else if (option == "--home-first-half-direction") {
      if (options.home_first_half_direction) {
        throw std::runtime_error(
            "--home-first-half-direction may only be specified once");
      }
      if (value == "left-to-right") {
        options.home_first_half_direction = AttackingDirection::LeftToRight;
      } else if (value == "right-to-left") {
        options.home_first_half_direction = AttackingDirection::RightToLeft;
      } else {
        throw std::runtime_error(
            "--home-first-half-direction must be left-to-right or right-to-left");
      }
    } else if (option == "--review") {
      options.review = value;
    } else if (option == "--left-provider") {
      options.left_provider = value;
    } else if (option == "--left-input") {
      options.left_input = value;
    } else if (option == "--right-provider") {
      options.right_provider = value;
    } else if (option == "--right-input") {
      options.right_input = value;
    } else if (option == "--candidate-id") {
      options.candidate_id = parseInteger<std::uint64_t>(value, option);
      options.has_candidate_id = true;
    } else if (option == "--canonical-match-id") {
      options.canonical_match_id = parseInteger<Identifier>(value, option);
      options.has_canonical_match_id = true;
    } else if (option == "--status") {
      if (value == "unresolved") {
        options.candidate_status = MatchCandidateStatus::Unresolved;
      } else if (value == "accepted") {
        options.candidate_status = MatchCandidateStatus::Accepted;
      } else if (value == "rejected") {
        options.candidate_status = MatchCandidateStatus::Rejected;
      } else {
        throw std::runtime_error(
            "--status must be unresolved, accepted, or rejected");
      }
    } else if (option == "--actor") {
      options.actor = value;
    } else if (option == "--source") {
      options.source = value;
    } else if (option == "--reason") {
      options.reason = value;
    } else {
      throw std::runtime_error("Unknown option '" + std::string(option) + "'");
    }
  }

  const bool is_reconciliation =
      options.command != Command::Import && options.command != Command::Query;
  if (is_reconciliation) {
    const bool has_event_option =
        !options.provider.empty() || options.has_match_id ||
        !options.input.empty() || !options.output.empty() ||
        !options.database.empty() || options.has_limit ||
        !options.filters.empty() || !options.projection.empty() ||
        !options.group_by.empty() || !options.aggregates.empty() ||
        options.home_first_half_direction.has_value();
    if (has_event_option) {
      throw std::runtime_error(
          "event import and query options are not valid for reconcile commands");
    }
    if (options.review.empty()) {
      throw std::runtime_error("--review is required for reconcile commands");
    }
    if (options.command == Command::ReconcileGenerate) {
      if (options.left_provider.empty() || options.left_input.empty() ||
          options.right_provider.empty() || options.right_input.empty()) {
        throw std::runtime_error(
            "reconcile generate requires both provider and input pairs");
      }
    } else if (!options.left_provider.empty() || !options.left_input.empty() ||
               !options.right_provider.empty() ||
               !options.right_input.empty()) {
      throw std::runtime_error(
          "provider and input pairs are only valid for reconcile generate");
    }
    if (options.command == Command::ReconcileInspect ||
        options.command == Command::ReconcileAccept ||
        options.command == Command::ReconcileReject) {
      if (!options.has_candidate_id || options.candidate_id == 0) {
        throw std::runtime_error("a positive --candidate-id is required");
      }
    } else if (options.has_candidate_id) {
      throw std::runtime_error(
          "--candidate-id is only valid for reconcile inspect, accept, or reject");
    }
    if (options.command == Command::ReconcileAccept &&
        (!options.has_canonical_match_id || options.canonical_match_id <= 0)) {
      throw std::runtime_error("a positive --canonical-match-id is required");
    } else if (options.command != Command::ReconcileAccept &&
               options.has_canonical_match_id) {
      throw std::runtime_error(
          "--canonical-match-id is only valid for reconcile accept");
    }
    const bool is_decision = options.command == Command::ReconcileAccept ||
                             options.command == Command::ReconcileReject;
    if (is_decision && (options.actor.empty() || options.source.empty() ||
                        options.reason.empty())) {
      throw std::runtime_error(
          "--actor, --source, and --reason are required for reconcile decisions");
    } else if (!is_decision &&
               (!options.actor.empty() || !options.source.empty() ||
                !options.reason.empty())) {
      throw std::runtime_error(
          "--actor, --source, and --reason are only valid for reconcile decisions");
    }
    if (options.command != Command::ReconcileList &&
        options.candidate_status) {
      throw std::runtime_error("--status is only valid for reconcile list");
    }
    return options;
  }

  if (!options.review.empty() || !options.left_provider.empty() ||
      !options.left_input.empty() || !options.right_provider.empty() ||
      !options.right_input.empty() || options.has_candidate_id ||
      options.has_canonical_match_id || options.candidate_status ||
      !options.actor.empty() || !options.source.empty() ||
      !options.reason.empty()) {
    throw std::runtime_error(
        "reconciliation options are only valid for reconcile commands");
  }
  const bool has_complete_raw_source =
      !options.provider.empty() && options.has_match_id && !options.input.empty();
  const bool has_any_raw_source =
      !options.provider.empty() || options.has_match_id || !options.input.empty();
  if (options.command == Command::Import) {
    if (!has_complete_raw_source) {
      throw std::runtime_error(
          "--provider, --match-id, and --input are required for import");
    }
    if (!options.database.empty()) {
      throw std::runtime_error("--database is only valid for query");
    }
    if (!options.filters.empty() || !options.projection.empty() ||
        !options.group_by.empty() || !options.aggregates.empty()) {
      throw std::runtime_error(
          "--filter, --project, --group-by, and --aggregate are only valid for query");
    }
  } else {
    if (!options.output.empty()) {
      throw std::runtime_error("--output is only valid for import");
    }
    if (!options.database.empty()) {
      if (has_any_raw_source) {
        throw std::runtime_error(
            "--database cannot be combined with --provider, --match-id, or --input");
      }
    } else if (!has_complete_raw_source) {
      throw std::runtime_error(
          "query requires --database or --provider, --match-id, and --input");
    }
    if (options.projection.empty() == options.aggregates.empty()) {
      throw std::runtime_error(
          "query requires exactly one of --project or --aggregate");
    }
    if (!options.projection.empty() && !options.group_by.empty()) {
      throw std::runtime_error("--group-by requires --aggregate");
    }
    if (options.has_limit) {
      throw std::runtime_error("--limit is only valid for import");
    }
  }
  if (options.home_first_half_direction && options.provider != "metrica") {
    throw std::runtime_error(
        "--home-first-half-direction is only valid with --provider metrica");
  }
  if (options.provider == "metrica" && has_any_raw_source &&
      !options.home_first_half_direction) {
    throw std::runtime_error(
        "--provider metrica requires --home-first-half-direction");
  }
  return options;
}

EventQuery makeEventQuery(const Options& options) {
  EventQuery query;
  query.projection = parseProjection(options.projection);
  query.filters.reserve(options.filters.size());
  for (const auto& filter : options.filters) {
    query.filters.push_back(parseFilter(filter));
  }
  return query;
}

AggregationQuery makeAggregationQuery(const Options& options) {
  AggregationQuery query;
  if (!options.group_by.empty()) {
    query.group_by = parseProjection(options.group_by);
  }
  query.filters.reserve(options.filters.size());
  for (const auto& filter : options.filters) {
    query.filters.push_back(parseFilter(filter));
  }
  query.aggregates.reserve(options.aggregates.size());
  for (const auto& aggregate : options.aggregates) {
    query.aggregates.push_back(parseAggregate(aggregate));
  }
  return query;
}

}  // namespace emberdb::cli
