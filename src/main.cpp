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

#include "emberdb/ingestion/statsbomb_event_adapter.h"
#include "emberdb/query/aggregation_query.h"
#include "emberdb/query/event_query.h"
#include "emberdb/storage/football_event_file.h"
#include "emberdb/storage/football_event_table.h"

namespace {

enum class Command { Import, Query };

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
};

void usage(std::ostream& output) {
  output << "Usage: emberdb_cli import --provider statsbomb --match-id ID --input PATH "
            "[--output DATABASE] [--limit N]\n"
            "       emberdb_cli query (--database DATABASE | --provider statsbomb "
            "--match-id ID --input PATH) "
            "(--project COLUMN[,COLUMN...] | --aggregate FUNCTION(COLUMN|*)) "
            "[--aggregate FUNCTION(COLUMN|*)]... [--group-by COLUMN[,COLUMN...]] "
            "[--filter COLUMN=VALUE]...\n";
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
    throw std::runtime_error("Expected the 'import' or 'query' command");
  }
  Options options;
  const std::string_view command(argv[1]);
  if (command == "import") {
    options.command = Command::Import;
  } else if (command == "query") {
    options.command = Command::Query;
  } else {
    throw std::runtime_error("Expected the 'import' or 'query' command");
  }
  for (int index = 2; index < argc; ++index) {
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
    } else {
      throw std::runtime_error("Unknown option '" + std::string(option) + "'");
    }
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
  return options;
}

emberdb::FootballEventTable importTable(const Options& options) {
  std::unique_ptr<emberdb::EventProviderAdapter> adapter;
  if (options.provider == "statsbomb") {
    adapter = std::make_unique<emberdb::StatsBombEventAdapter>();
  } else {
    throw std::runtime_error("Unsupported provider '" + options.provider + "'");
  }
  const auto events = adapter->loadEvents(options.input, {options.match_id});
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

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto options = parseOptions(argc, argv);
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
                << "Provider: StatsBomb\n"
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
