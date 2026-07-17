#include "emberdb/ingestion/statsbomb_event_adapter.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "emberdb/common/coordinate_normalization.h"

namespace emberdb {
namespace {

using Json = nlohmann::json;

constexpr PitchDimensions kStatsBombPitch{120.0, 80.0};
constexpr AttackingDirection kStatsBombDirection = AttackingDirection::LeftToRight;

std::runtime_error eventError(std::size_t index, const std::string& message) {
  return std::runtime_error("StatsBomb event at index " + std::to_string(index) +
                            ": " + message);
}

template <typename T>
T required(const Json& event, std::string_view key, std::size_t index) {
  const auto it = event.find(key);
  if (it == event.end() || it->is_null()) {
    throw eventError(index, "missing required field '" + std::string(key) + "'");
  }
  try {
    return it->get<T>();
  } catch (const Json::exception& error) {
    throw eventError(index, "invalid field '" + std::string(key) + "': " + error.what());
  }
}

template <typename T>
std::optional<T> optionalScalar(const Json& object, std::string_view key,
                                std::size_t index) {
  const auto it = object.find(key);
  if (it == object.end() || it->is_null()) {
    return std::nullopt;
  }
  try {
    return it->get<T>();
  } catch (const Json::exception& error) {
    throw eventError(index, "invalid optional field '" + std::string(key) +
                                "': " + error.what());
  }
}

template <typename T>
std::optional<T> optionalNested(const Json& event, std::string_view object_key,
                                std::string_view value_key, std::size_t index) {
  const auto object = event.find(object_key);
  if (object == event.end() || object->is_null()) {
    return std::nullopt;
  }
  if (!object->is_object()) {
    throw eventError(index, "field '" + std::string(object_key) + "' must be an object");
  }
  return optionalScalar<T>(*object, value_key, index);
}

std::optional<Coordinate> optionalCoordinate(const Json& object, std::string_view key,
                                             std::size_t index) {
  const auto it = object.find(key);
  if (it == object.end() || it->is_null()) {
    return std::nullopt;
  }
  if (!it->is_array() || it->size() < 2 || !(*it)[0].is_number() ||
      !(*it)[1].is_number()) {
    throw eventError(index, "field '" + std::string(key) +
                                "' must be a numeric coordinate pair");
  }
  return Coordinate{(*it)[0].get<double>(), (*it)[1].get<double>()};
}

std::chrono::milliseconds parseTimestamp(const std::string& value, std::size_t index) {
  int hours = 0;
  int minutes = 0;
  double seconds = 0.0;
  char first_colon = '\0';
  char second_colon = '\0';
  std::istringstream input(value);
  input >> hours >> first_colon >> minutes >> second_colon >> seconds;
  if (!input || first_colon != ':' || second_colon != ':' || hours < 0 ||
      minutes < 0 || minutes >= 60 || seconds < 0.0 || seconds >= 60.0) {
    throw eventError(index, "invalid timestamp '" + value + "'");
  }
  char trailing = '\0';
  if (input >> trailing) {
    throw eventError(index, "invalid timestamp '" + value + "'");
  }
  const auto total_seconds = static_cast<double>(hours * 3600 + minutes * 60) + seconds;
  return std::chrono::milliseconds{static_cast<std::int64_t>(total_seconds * 1000.0 + 0.5)};
}

std::optional<std::string> outcome(const Json& event, std::size_t index) {
  for (const auto* detail_key : {"pass", "shot", "duel", "dribble", "goalkeeper"}) {
    const auto detail = event.find(detail_key);
    if (detail == event.end() || detail->is_null()) {
      continue;
    }
    if (!detail->is_object()) {
      throw eventError(index, "field '" + std::string(detail_key) + "' must be an object");
    }
    if (auto value = optionalNested<std::string>(*detail, "outcome", "name", index)) {
      return value;
    }
  }
  return std::nullopt;
}

std::optional<Coordinate> endLocation(const Json& event, std::size_t index) {
  for (const auto* detail_key : {"pass", "carry"}) {
    const auto detail = event.find(detail_key);
    if (detail == event.end() || detail->is_null()) {
      continue;
    }
    if (!detail->is_object()) {
      throw eventError(index, "field '" + std::string(detail_key) + "' must be an object");
    }
    if (auto coordinate = optionalCoordinate(*detail, "end_location", index)) {
      return coordinate;
    }
  }
  return std::nullopt;
}

std::optional<Coordinate> normalizeStatsBombCoordinate(
    const std::optional<Coordinate>& source, std::string_view field,
    std::size_t index) {
  try {
    return normalizeCoordinate(source, kStatsBombPitch, kStatsBombDirection);
  } catch (const std::invalid_argument& error) {
    throw eventError(index, "invalid coordinate field '" + std::string(field) +
                                "': " + error.what());
  }
}

FootballEvent normalize(const Json& event, const ImportContext& context, std::size_t index) {
  if (!event.is_object()) {
    throw eventError(index, "expected an object");
  }

  const auto timestamp_text = required<std::string>(event, "timestamp", index);
  FootballEvent normalized;
  normalized.provider_event_id = required<std::string>(event, "id", index);
  normalized.match_id = context.match_id;
  normalized.period = required<std::int32_t>(event, "period", index);
  normalized.time = MatchTime{parseTimestamp(timestamp_text, index),
                              required<std::int32_t>(event, "minute", index),
                              required<std::int32_t>(event, "second", index)};
  if (normalized.period <= 0 || normalized.time.minute < 0 || normalized.time.second < 0 ||
      normalized.time.second >= 60) {
    throw eventError(index, "period, minute, or second is outside its valid range");
  }
  normalized.possession_id = optionalScalar<Identifier>(event, "possession", index);
  normalized.team_id = optionalNested<Identifier>(event, "team", "id", index);
  normalized.team_name = optionalNested<std::string>(event, "team", "name", index);
  normalized.player_id = optionalNested<Identifier>(event, "player", "id", index);
  normalized.player_name = optionalNested<std::string>(event, "player", "name", index);
  normalized.event_type = required<std::string>(required<Json>(event, "type", index),
                                                "name", index);
  normalized.outcome = outcome(event, index);
  normalized.source_start_location = optionalCoordinate(event, "location", index);
  normalized.source_end_location = endLocation(event, index);
  normalized.start_location = normalizeStatsBombCoordinate(
      normalized.source_start_location, "location", index);
  normalized.end_location = normalizeStatsBombCoordinate(
      normalized.source_end_location, "end_location", index);
  normalized.provider = "StatsBomb";
  return normalized;
}

}  // namespace

std::vector<FootballEvent> StatsBombEventAdapter::loadEvents(
    const std::filesystem::path& path, const ImportContext& context) const {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Unable to read StatsBomb event file '" + path.string() + "'");
  }

  Json document;
  try {
    input >> document;
  } catch (const Json::parse_error& error) {
    throw std::runtime_error("Invalid JSON in StatsBomb event file '" + path.string() +
                             "': " + error.what());
  }
  if (!document.is_array()) {
    throw std::runtime_error("Invalid StatsBomb event file '" + path.string() +
                             "': top-level JSON must be an array");
  }

  std::vector<FootballEvent> events;
  events.reserve(document.size());
  for (std::size_t index = 0; index < document.size(); ++index) {
    events.push_back(normalize(document[index], context, index));
  }
  return events;
}

}  // namespace emberdb
