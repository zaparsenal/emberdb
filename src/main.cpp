#include <algorithm>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "emberdb/ingestion/statsbomb_event_adapter.h"
#include "emberdb/storage/football_event_table.h"

namespace {

struct Options {
  std::string provider;
  emberdb::Identifier match_id{};
  std::filesystem::path input;
  std::size_t limit{};
};

void usage(std::ostream& output) {
  output << "Usage: emberdb_cli import --provider statsbomb --match-id ID --input PATH "
            "[--limit N]\n";
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
  if (argc < 2 || std::string_view(argv[1]) != "import") {
    throw std::runtime_error("Expected the 'import' command");
  }
  Options options;
  bool has_match_id = false;
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
      has_match_id = true;
    } else if (option == "--input") {
      options.input = value;
    } else if (option == "--limit") {
      options.limit = parseInteger<std::size_t>(value, option);
    } else {
      throw std::runtime_error("Unknown option '" + std::string(option) + "'");
    }
  }
  if (options.provider.empty() || !has_match_id || options.input.empty()) {
    throw std::runtime_error("--provider, --match-id, and --input are required");
  }
  return options;
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
              << " end=" << coordinateText(event.end_location) << '\n';
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto options = parseOptions(argc, argv);
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
    if (!table.validate()) {
      throw std::runtime_error("Internal error: column lengths are inconsistent");
    }

    std::cout << "Imported " << table.rowCount() << " events\n"
              << "Provider: StatsBomb\n"
              << "Match ID: " << options.match_id << '\n'
              << "Columns: " << emberdb::FootballEventTable::kColumnCount << '\n'
              << "Events with player data: " << table.playerDataCount() << '\n'
              << "Events with start locations: " << table.startLocationCount() << '\n'
              << "Events with end locations: " << table.endLocationCount() << '\n';
    printPreview(table, options.limit);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "emberdb: " << error.what() << '\n';
    usage(std::cerr);
    return 1;
  }
}
