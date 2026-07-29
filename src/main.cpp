#include <algorithm>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "emberdb/ingestion/metrica_event_adapter.h"
#include "emberdb/ingestion/statsbomb_event_adapter.h"
#include "emberdb/ingestion/statsbomb_metadata_adapter.h"
#include "emberdb/ingestion/wyscout_event_adapter.h"
#include "emberdb/ingestion/wyscout_metadata_adapter.h"
#include "emberdb/persistence/match_review_file.h"
#include "emberdb/query/aggregation_query.h"
#include "emberdb/query/event_query.h"
#include "emberdb/reconciliation/match_reconciliation.h"
#include "emberdb/storage/football_event_file.h"
#include "emberdb/storage/football_event_table.h"

namespace {

enum class Command {
  Import,
  Query,
  ReconcileGenerate,
  ReconcileList,
  ReconcileInspect,
  ReconcileAccept,
  ReconcileReject
};

struct Options {
  Command command{Command::Import};
  std::string provider;
  emberdb::Identifier match_id{};
  bool has_match_id{};
  std::filesystem::path input;
  std::filesystem::path output;
  std::filesystem::path database;
  std::size_t limit{};
  bool has_limit{};
  std::vector<std::string> filters;
  std::string projection;
  std::string group_by;
  std::vector<std::string> aggregates;
  std::optional<emberdb::AttackingDirection> home_first_half_direction;
  std::filesystem::path review;
  std::string left_provider;
  std::filesystem::path left_input;
  std::string right_provider;
  std::filesystem::path right_input;
  std::uint64_t candidate_id{};
  bool has_candidate_id{};
  emberdb::Identifier canonical_match_id{};
  bool has_canonical_match_id{};
  std::optional<emberdb::MatchCandidateStatus> candidate_status;
  std::string reason;
};

void usage(std::ostream& output) {
  output << "Usage: emberdb_cli import --provider PROVIDER --match-id ID --input PATH "
            "[--home-first-half-direction left-to-right|right-to-left] "
            "[--output DATABASE] [--limit N]\n"
            "       emberdb_cli query (--database DATABASE | --provider PROVIDER "
            "--match-id ID --input PATH) "
            "(--project COLUMN[,COLUMN...] | --aggregate FUNCTION(COLUMN|*)) "
            "[--aggregate FUNCTION(COLUMN|*)]... [--group-by COLUMN[,COLUMN...]] "
            "[--filter COLUMN=VALUE]...\n"
            "       emberdb_cli reconcile generate --review PATH "
            "--left-provider PROVIDER --left-input PATH "
            "--right-provider PROVIDER --right-input PATH\n"
            "       emberdb_cli reconcile list --review PATH "
            "[--status unresolved|accepted|rejected]\n"
            "       emberdb_cli reconcile inspect --review PATH --candidate-id ID\n"
            "       emberdb_cli reconcile accept --review PATH --candidate-id ID "
            "--canonical-match-id ID\n"
            "       emberdb_cli reconcile reject --review PATH --candidate-id ID "
            "--reason TEXT\n";
}

template <typename Integer>
Integer parseInteger(std::string_view text, std::string_view option) {
  Integer value{};
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    throw std::runtime_error("Invalid value for " + std::string(option) + ": '" +
                             std::string(text) + "'");
  }
  return value;
}

Options parseOptions(int argc, char** argv) {
  if (argc < 2) {
    throw std::runtime_error("Expected the 'import', 'query', or 'reconcile' command");
  }
  Options options;
  const std::string_view command(argv[1]);
  int first_option = 2;
  if (command == "import") {
    options.command = Command::Import;
  } else if (command == "query") {
    options.command = Command::Query;
  } else if (command == "reconcile") {
    if (argc < 3) {
      throw std::runtime_error("Expected a reconcile action");
    }
    const std::string_view action(argv[2]);
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
      throw std::runtime_error("Unknown reconcile action '" + std::string(action) +
                               "'");
    }
    first_option = 3;
  } else {
    throw std::runtime_error("Expected the 'import', 'query', or 'reconcile' command");
  }
  for (int index = first_option; index < argc; ++index) {
    const std::string_view option(argv[index]);
    if (index + 1 >= argc) {
      throw std::runtime_error("Missing value for " + std::string(option));
    }
    const std::string_view value(argv[++index]);
    if (option == "--provider") {
      options.provider = value;
    } else if (option == "--match-id") {
      options.match_id = parseInteger<emberdb::Identifier>(value, option);
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
        options.home_first_half_direction = emberdb::AttackingDirection::LeftToRight;
      } else if (value == "right-to-left") {
        options.home_first_half_direction = emberdb::AttackingDirection::RightToLeft;
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
      options.canonical_match_id =
          parseInteger<emberdb::Identifier>(value, option);
      options.has_canonical_match_id = true;
    } else if (option == "--status") {
      if (value == "unresolved") {
        options.candidate_status = emberdb::MatchCandidateStatus::Unresolved;
      } else if (value == "accepted") {
        options.candidate_status = emberdb::MatchCandidateStatus::Accepted;
      } else if (value == "rejected") {
        options.candidate_status = emberdb::MatchCandidateStatus::Rejected;
      } else {
        throw std::runtime_error(
            "--status must be unresolved, accepted, or rejected");
      }
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
        !options.provider.empty() || options.has_match_id || !options.input.empty() ||
        !options.output.empty() || !options.database.empty() || options.has_limit ||
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
               !options.right_provider.empty() || !options.right_input.empty()) {
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
    if (options.command == Command::ReconcileReject && options.reason.empty()) {
      throw std::runtime_error("--reason is required for reconcile reject");
    } else if (options.command != Command::ReconcileReject &&
               !options.reason.empty()) {
      throw std::runtime_error("--reason is only valid for reconcile reject");
    }
    if (options.command != Command::ReconcileList && options.candidate_status) {
      throw std::runtime_error("--status is only valid for reconcile list");
    }
    return options;
  }
  if (!options.review.empty() || !options.left_provider.empty() ||
      !options.left_input.empty() || !options.right_provider.empty() ||
      !options.right_input.empty() || options.has_candidate_id ||
      options.has_canonical_match_id || options.candidate_status ||
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
      throw std::runtime_error("--provider, --match-id, and --input are required for import");
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

std::vector<emberdb::ProviderMatchMetadata> loadProviderMatches(
    const std::string& provider, const std::filesystem::path& input) {
  if (provider == "statsbomb") {
    return emberdb::StatsBombMetadataAdapter{}.loadMatches(input).matches;
  }
  if (provider == "wyscout") {
    return emberdb::WyscoutMetadataAdapter{}.loadMatches(input).matches;
  }
  throw std::runtime_error("Unsupported metadata provider '" + provider +
                           "'; expected statsbomb or wyscout");
}

emberdb::FootballEventTable importTable(const Options& options) {
  std::unique_ptr<emberdb::EventProviderAdapter> adapter;
  if (options.provider == "statsbomb") {
    adapter = std::make_unique<emberdb::StatsBombEventAdapter>();
  } else if (options.provider == "metrica") {
    adapter = std::make_unique<emberdb::MetricaEventAdapter>();
  } else if (options.provider == "wyscout") {
    adapter = std::make_unique<emberdb::WyscoutEventAdapter>();
  } else {
    throw std::runtime_error("Unsupported provider '" + options.provider + "'");
  }
  const auto events = adapter->loadEvents(
      options.input, {options.match_id, options.home_first_half_direction});
  emberdb::FootballEventTable table;
  for (const auto& event : events) {
    table.append(event);
  }
  return table;
}

emberdb::FootballEventColumn parseColumn(std::string_view name) {
  const auto column = emberdb::columnFromName(name);
  if (!column) {
    throw std::runtime_error("Unknown column '" + std::string(name) + "'");
  }
  return *column;
}

std::vector<emberdb::FootballEventColumn> parseProjection(std::string_view text) {
  std::vector<emberdb::FootballEventColumn> columns;
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

emberdb::FootballEventValue parseFilterValue(emberdb::FootballEventColumn column,
                                              std::string_view text) {
  switch (emberdb::columnValueType(column)) {
    case emberdb::FootballEventValueType::Identifier:
      return parseInteger<emberdb::Identifier>(text, "filter");
    case emberdb::FootballEventValueType::Integer:
      return parseInteger<std::int32_t>(text, "filter");
    case emberdb::FootballEventValueType::Timestamp:
      return std::chrono::milliseconds{parseInteger<std::int64_t>(text, "filter")};
    case emberdb::FootballEventValueType::Number: {
      double value{};
      std::istringstream input{std::string(text)};
      input >> std::noskipws >> value;
      if (!input || !input.eof()) {
        throw std::runtime_error("Invalid numeric filter value '" + std::string(text) + "'");
      }
      return value;
    }
    case emberdb::FootballEventValueType::Text:
      return std::string(text);
  }
  throw std::runtime_error("Unsupported filter value type");
}

emberdb::EqualityPredicate parseFilter(std::string_view text) {
  const auto separator = text.find('=');
  if (separator == std::string_view::npos || separator == 0) {
    throw std::runtime_error("Filter must have the form COLUMN=VALUE");
  }
  const auto column = parseColumn(text.substr(0, separator));
  return {column, parseFilterValue(column, text.substr(separator + 1))};
}

emberdb::EventQuery makeQuery(const Options& options) {
  emberdb::EventQuery query;
  query.projection = parseProjection(options.projection);
  query.filters.reserve(options.filters.size());
  for (const auto& filter : options.filters) {
    query.filters.push_back(parseFilter(filter));
  }
  return query;
}

emberdb::AggregateFunction parseAggregateFunction(std::string_view name) {
  if (name == "count") {
    return emberdb::AggregateFunction::Count;
  }
  if (name == "sum") {
    return emberdb::AggregateFunction::Sum;
  }
  if (name == "avg") {
    return emberdb::AggregateFunction::Average;
  }
  if (name == "min") {
    return emberdb::AggregateFunction::Minimum;
  }
  if (name == "max") {
    return emberdb::AggregateFunction::Maximum;
  }
  throw std::runtime_error("Unknown aggregate function '" + std::string(name) + "'");
}

emberdb::AggregateExpression parseAggregate(std::string_view text) {
  const auto open = text.find('(');
  if (open == std::string_view::npos || open == 0 || text.back() != ')' ||
      text.find(')', open) != text.size() - 1) {
    throw std::runtime_error("Aggregate must have the form FUNCTION(COLUMN) or count(*)");
  }
  const auto function = parseAggregateFunction(text.substr(0, open));
  const auto input = text.substr(open + 1, text.size() - open - 2);
  if (input == "*") {
    if (function != emberdb::AggregateFunction::Count) {
      throw std::runtime_error("Only count(*) accepts '*'");
    }
    return {function};
  }
  if (input.empty()) {
    throw std::runtime_error("Aggregate input column cannot be empty");
  }
  return {function, parseColumn(input)};
}

emberdb::AggregationQuery makeAggregationQuery(const Options& options) {
  emberdb::AggregationQuery query;
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

std::string optionalText(const std::optional<std::string>& value) {
  return value.value_or("NULL");
}

std::string coordinateText(const std::optional<emberdb::Coordinate>& value) {
  if (!value) {
    return "NULL";
  }
  return "(" + std::to_string(value->x) + ", " + std::to_string(value->y) + ")";
}

void printPreview(const emberdb::FootballEventTable& table, std::size_t limit) {
  if (limit == 0) {
    return;
  }
  std::cout << "\nPreview\n";
  for (std::size_t index = 0; index < std::min(limit, table.rowCount()); ++index) {
    const auto event = table.row(index);
    std::cout << index << ": id=" << event.provider_event_id << " type=" << event.event_type
              << " team=" << optionalText(event.team_name)
              << " player=" << optionalText(event.player_name)
              << " start=" << coordinateText(event.start_location)
              << " end=" << coordinateText(event.end_location)
              << " source_start=" << coordinateText(event.source_start_location)
              << " source_end=" << coordinateText(event.source_end_location) << '\n';
  }
}

template <typename Cell>
std::string queryValueText(const Cell& cell) {
  if (!cell) {
    return "NULL";
  }
  return std::visit(
      [](const auto& value) {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::string>) {
          return value;
        } else if constexpr (std::is_same_v<Value, std::chrono::milliseconds>) {
          return std::to_string(value.count());
        } else {
          std::ostringstream output;
          output << value;
          return output.str();
        }
      },
      *cell);
}

void printAggregationResult(const emberdb::AggregationResult& result) {
  std::cout << "Result rows: " << result.rowCount() << '\n';
  for (std::size_t column = 0; column < result.columnCount(); ++column) {
    if (column != 0) {
      std::cout << '\t';
    }
    std::cout << result.columnNames()[column];
  }
  std::cout << '\n';
  for (std::size_t row = 0; row < result.rowCount(); ++row) {
    for (std::size_t column = 0; column < result.columnCount(); ++column) {
      if (column != 0) {
        std::cout << '\t';
      }
      std::cout << queryValueText(result.cell(row, column));
    }
    std::cout << '\n';
  }
}

void printQueryResult(const emberdb::EventQueryResult& result) {
  std::cout << "Matched " << result.rowCount()
            << (result.rowCount() == 1 ? " event\n" : " events\n");
  for (std::size_t column = 0; column < result.columnCount(); ++column) {
    if (column != 0) {
      std::cout << '\t';
    }
    std::cout << emberdb::columnName(result.columns()[column]);
  }
  std::cout << '\n';
  for (std::size_t row = 0; row < result.rowCount(); ++row) {
    for (std::size_t column = 0; column < result.columnCount(); ++column) {
      if (column != 0) {
        std::cout << '\t';
      }
      std::cout << queryValueText(result.cell(row, column));
    }
    std::cout << '\n';
  }
}

std::string providerMatchText(const emberdb::ProviderMatchReference& reference) {
  return reference.provider + ":" + reference.id;
}

std::string_view evidenceStatusText(emberdb::ReconciliationStatus status) {
  switch (status) {
    case emberdb::ReconciliationStatus::Missing:
      return "missing";
    case emberdb::ReconciliationStatus::Agreeing:
      return "agreeing";
    case emberdb::ReconciliationStatus::Conflicting:
      return "conflicting";
    case emberdb::ReconciliationStatus::Uncertain:
      return "uncertain";
  }
  return "unknown";
}

void printCandidateSummary(const emberdb::MatchCandidateRecord& candidate) {
  std::cout << candidate.id << '\t'
            << emberdb::matchCandidateStatusName(candidate.status) << '\t'
            << candidate.reconciliation.confidence << '\t'
            << providerMatchText(candidate.reconciliation.left_match) << '\t'
            << providerMatchText(candidate.reconciliation.right_match) << '\n';
}

void printEvidence(std::string_view name,
                   const emberdb::MatchFieldEvidence& evidence) {
  std::cout << name << '\t' << evidenceStatusText(evidence.status) << '\t'
            << optionalText(evidence.left_value) << '\t'
            << optionalText(evidence.right_value) << '\t'
            << optionalText(evidence.canonical_value) << '\n';
}

void runReconciliationCommand(const Options& options) {
  auto store = emberdb::loadMatchReviewStore(options.review);
  if (options.command == Command::ReconcileGenerate) {
    const auto left =
        loadProviderMatches(options.left_provider, options.left_input);
    const auto right =
        loadProviderMatches(options.right_provider, options.right_input);
    const auto generated =
        emberdb::findMatchCandidates(left, right, store.catalog());
    const auto existing_count = store.candidates().size();
    const auto ids = store.addCandidates(generated);
    const auto added_count = store.candidates().size() - existing_count;
    emberdb::saveMatchReviewStore(store, options.review);
    std::cout << "Generated " << generated.size() << " qualified "
              << (generated.size() == 1 ? "comparison" : "comparisons") << '\n'
              << "Added " << added_count << " new "
              << (added_count == 1 ? "candidate" : "candidates") << '\n';
    if (!ids.empty()) {
      std::cout << "Candidate IDs:";
      for (const auto id : ids) {
        std::cout << ' ' << id;
      }
      std::cout << '\n';
    }
    return;
  }
  if (options.command == Command::ReconcileList) {
    const auto candidates = store.candidates(options.candidate_status);
    std::cout << "Candidates: " << candidates.size() << '\n'
              << "id\tstatus\tconfidence\tleft_match\tright_match\n";
    for (const auto* candidate : candidates) {
      printCandidateSummary(*candidate);
    }
    return;
  }

  const auto* candidate = store.candidate(options.candidate_id);
  if (candidate == nullptr) {
    throw std::runtime_error("Unknown match candidate " +
                             std::to_string(options.candidate_id));
  }
  if (options.command == Command::ReconcileInspect) {
    printCandidateSummary(*candidate);
    if (candidate->accepted_match_id) {
      std::cout << "Canonical match: " << candidate->accepted_match_id->value << '\n';
    }
    if (candidate->rejection_reason) {
      std::cout << "Rejection reason: " << *candidate->rejection_reason << '\n';
    }
    std::cout << "field\tstatus\tleft_value\tright_value\tcanonical_value\n";
    const auto& result = candidate->reconciliation;
    printEvidence("competition", result.competition);
    printEvidence("season", result.season);
    printEvidence("kickoff", result.kickoff);
    printEvidence("home_team", result.home_team);
    printEvidence("away_team", result.away_team);
    printEvidence("score", result.score);
    return;
  }
  if (options.command == Command::ReconcileAccept) {
    store.accept(options.candidate_id, {options.canonical_match_id});
    emberdb::saveMatchReviewStore(store, options.review);
    std::cout << "Accepted candidate " << options.candidate_id
              << " as canonical match " << options.canonical_match_id << '\n';
    return;
  }
  store.reject(options.candidate_id, options.reason);
  emberdb::saveMatchReviewStore(store, options.review);
  std::cout << "Rejected candidate " << options.candidate_id << ": "
            << options.reason << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto options = parseOptions(argc, argv);
    if (options.command != Command::Import && options.command != Command::Query) {
      runReconciliationCommand(options);
      return 0;
    }
    const auto table = !options.database.empty()
                           ? emberdb::loadFootballEventTable(options.database)
                           : importTable(options);
    if (!table.validate()) {
      throw std::runtime_error("Internal error: column lengths are inconsistent");
    }

    if (options.command == Command::Query) {
      if (!options.aggregates.empty()) {
        printAggregationResult(
            emberdb::executeAggregationQuery(table, makeAggregationQuery(options)));
      } else {
        printQueryResult(emberdb::executeQuery(table, makeQuery(options)));
      }
    } else {
      if (!options.output.empty()) {
        emberdb::saveFootballEventTable(table, options.output);
      }
      std::cout << "Imported " << table.rowCount() << " events\n"
                << "Provider: "
                << (table.rowCount() == 0 ? options.provider : table.row(0).provider) << '\n'
                << "Match ID: " << options.match_id << '\n'
                << "Columns: " << emberdb::FootballEventTable::kColumnCount << '\n'
                << "Events with player data: " << table.playerDataCount() << '\n'
                << "Events with start locations: " << table.startLocationCount() << '\n'
                << "Events with end locations: " << table.endLocationCount() << '\n';
      if (!options.output.empty()) {
        std::cout << "Saved database: " << options.output.string() << '\n'
                  << "Database size: " << std::filesystem::file_size(options.output)
                  << " bytes\n";
      }
      printPreview(table, options.limit);
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "emberdb: " << error.what() << '\n';
    usage(std::cerr);
    return 1;
  }
}
