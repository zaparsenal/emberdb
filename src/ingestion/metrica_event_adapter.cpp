#include "emberdb/ingestion/metrica_event_adapter.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "emberdb/common/coordinate_normalization.h"

namespace emberdb {
namespace {

constexpr PitchDimensions kMetricaPitch{1.0, 1.0};

std::runtime_error rowError(std::size_t row, const std::string& message) {
  return std::runtime_error("Metrica event at CSV row " + std::to_string(row) + ": " +
                            message);
}

std::vector<std::string> parseCsvRow(const std::string& line, std::size_t row) {
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false;
  for (std::size_t index = 0; index < line.size(); ++index) {
    const char value = line[index];
    if (quoted) {
      if (value == '"') {
        if (index + 1 < line.size() && line[index + 1] == '"') {
          field.push_back('"');
          ++index;
        } else {
          quoted = false;
        }
      } else {
        field.push_back(value);
      }
    } else if (value == ',') {
      fields.push_back(std::move(field));
      field.clear();
    } else if (value == '"' && field.empty()) {
      quoted = true;
    } else {
      field.push_back(value);
    }
  }
  if (quoted) {
    throw rowError(row, "unterminated quoted CSV field");
  }
  if (!field.empty() && field.back() == '\r') {
    field.pop_back();
  }
  fields.push_back(std::move(field));
  return fields;
}

using Header = std::unordered_map<std::string, std::size_t>;

Header parseHeader(const std::string& line) {
  const auto fields = parseCsvRow(line, 1);
  Header header;
  for (std::size_t index = 0; index < fields.size(); ++index) {
    if (fields[index].empty() || !header.emplace(fields[index], index).second) {
      throw rowError(1, "header names must be non-empty and unique");
    }
  }
  for (const auto* required : {"Team", "Type", "Subtype", "Period", "Start Frame",
                               "Start Time [s]", "From", "Start X", "Start Y", "End X",
                               "End Y"}) {
    if (!header.contains(required)) {
      throw rowError(1, "missing required column '" + std::string(required) + "'");
    }
  }
  return header;
}

const std::string& field(const std::vector<std::string>& fields, const Header& header,
                         std::string_view name, std::size_t row) {
  const auto index = header.at(std::string(name));
  if (index >= fields.size()) {
    throw rowError(row, "missing field for column '" + std::string(name) + "'");
  }
  return fields[index];
}

template <typename Integer>
Integer parseInteger(std::string_view text, std::string_view name, std::size_t row) {
  Integer value{};
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    throw rowError(row, "invalid integer in column '" + std::string(name) + "'");
  }
  return value;
}

double parseNumber(std::string_view text, std::string_view name, std::size_t row) {
  double value{};
  std::istringstream input{std::string(text)};
  input >> std::noskipws >> value;
  if (!input || !input.eof() || !std::isfinite(value)) {
    throw rowError(row, "invalid finite number in column '" + std::string(name) + "'");
  }
  return value;
}

bool isMissingCoordinate(std::string_view value) {
  return value.empty() || value == "NaN" || value == "nan";
}

std::optional<Coordinate> coordinate(const std::vector<std::string>& fields,
                                     const Header& header, std::string_view x_name,
                                     std::string_view y_name, std::size_t row) {
  const auto& x_text = field(fields, header, x_name, row);
  const auto& y_text = field(fields, header, y_name, row);
  const bool x_missing = isMissingCoordinate(x_text);
  const bool y_missing = isMissingCoordinate(y_text);
  if (x_missing != y_missing) {
    throw rowError(row, "coordinate columns '" + std::string(x_name) + "' and '" +
                            std::string(y_name) + "' must both be present or missing");
  }
  if (x_missing) {
    return std::nullopt;
  }
  return Coordinate{parseNumber(x_text, x_name, row), parseNumber(y_text, y_name, row)};
}

AttackingDirection opposite(AttackingDirection direction) {
  return direction == AttackingDirection::LeftToRight
             ? AttackingDirection::RightToLeft
             : AttackingDirection::LeftToRight;
}

AttackingDirection eventDirection(std::string_view team, std::int32_t period,
                                   const ImportContext& context, std::size_t row) {
  if (!context.home_team_first_half_direction) {
    throw rowError(row, "Metrica import requires the home team's first-half direction");
  }
  if (period != 1 && period != 2) {
    throw rowError(row, "Period must be 1 or 2");
  }
  AttackingDirection direction = *context.home_team_first_half_direction;
  if (team == "Away") {
    direction = opposite(direction);
  } else if (team != "Home") {
    throw rowError(row, "Team must be 'Home' or 'Away'");
  }
  return period == 2 ? opposite(direction) : direction;
}

std::string canonicalEventType(std::string value) {
  bool capitalize = true;
  for (char& character : value) {
    if (character == ' ' || character == '-') {
      capitalize = true;
    } else if (capitalize) {
      character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
      capitalize = false;
    } else {
      character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
  }
  return value;
}

std::optional<Coordinate> normalizeMetricaCoordinate(
    const std::optional<Coordinate>& source, AttackingDirection direction,
    std::string_view name, std::size_t row) {
  if (!source) {
    return std::nullopt;
  }
  const bool in_bounds = source->x >= 0.0 && source->x <= 1.0 && source->y >= 0.0 &&
                         source->y <= 1.0;
  if (!in_bounds) {
    return std::nullopt;
  }
  try {
    return normalizeCoordinate(*source, kMetricaPitch, direction);
  } catch (const std::invalid_argument& error) {
    throw rowError(row, "invalid coordinate columns for '" + std::string(name) +
                            "': " + error.what());
  }
}

FootballEvent normalize(const std::vector<std::string>& fields, const Header& header,
                        const ImportContext& context, std::size_t row,
                        std::size_t event_index) {
  const auto& team = field(fields, header, "Team", row);
  const auto period = parseInteger<std::int32_t>(field(fields, header, "Period", row),
                                                 "Period", row);
  const auto direction = eventDirection(team, period, context, row);
  const double seconds =
      parseNumber(field(fields, header, "Start Time [s]", row), "Start Time [s]", row);
  if (seconds < 0.0) {
    throw rowError(row, "Start Time [s] must not be negative");
  }
  const auto whole_seconds = static_cast<std::int64_t>(std::floor(seconds));
  const auto start_frame = field(fields, header, "Start Frame", row);
  const auto& type = field(fields, header, "Type", row);
  if (type.empty()) {
    throw rowError(row, "Type must not be empty");
  }

  FootballEvent event;
  event.provider_event_id = "metrica:" + std::to_string(context.match_id) + ":" +
                            start_frame + ":" + std::to_string(event_index);
  event.match_id = context.match_id;
  event.period = period;
  event.time = MatchTime{
      std::chrono::milliseconds{static_cast<std::int64_t>(std::llround(seconds * 1000.0))},
      static_cast<std::int32_t>(whole_seconds / 60),
      static_cast<std::int32_t>(whole_seconds % 60)};
  event.team_name = team;
  const auto& player = field(fields, header, "From", row);
  if (!player.empty()) {
    event.player_name = player;
  }
  event.event_type = canonicalEventType(type);
  const auto& subtype = field(fields, header, "Subtype", row);
  if (!subtype.empty()) {
    event.outcome = subtype;
  }
  event.source_start_location =
      coordinate(fields, header, "Start X", "Start Y", row);
  event.source_end_location = coordinate(fields, header, "End X", "End Y", row);
  event.start_location = normalizeMetricaCoordinate(
      event.source_start_location, direction, "start", row);
  event.end_location = normalizeMetricaCoordinate(
      event.source_end_location, direction, "end", row);
  event.provider = "Metrica";
  return event;
}

}  // namespace

std::vector<FootballEvent> MetricaEventAdapter::loadEvents(
    const std::filesystem::path& path, const ImportContext& context) const {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Unable to read Metrica event file '" + path.string() + "'");
  }
  std::string line;
  if (!std::getline(input, line)) {
    throw std::runtime_error("Invalid Metrica event file '" + path.string() +
                             "': missing CSV header");
  }
  const auto header = parseHeader(line);
  std::vector<FootballEvent> events;
  std::size_t row = 1;
  while (std::getline(input, line)) {
    ++row;
    if (line.empty() || line == "\r") {
      continue;
    }
    const auto fields = parseCsvRow(line, row);
    if (fields.size() != header.size()) {
      throw rowError(row, "expected " + std::to_string(header.size()) +
                              " fields but found " + std::to_string(fields.size()));
    }
    events.push_back(normalize(fields, header, context, row, events.size()));
  }
  if (!input.eof()) {
    throw std::runtime_error("Unable to finish reading Metrica event file '" +
                             path.string() + "'");
  }
  return events;
}

}  // namespace emberdb
