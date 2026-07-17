#include "emberdb/ingestion/wyscout_event_adapter.h"

#include <chrono>
#include <cmath>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "emberdb/common/coordinate_normalization.h"

namespace emberdb {
namespace {

using Json = nlohmann::json;
constexpr PitchDimensions kWyscoutPitch{100.0, 100.0};
constexpr AttackingDirection kWyscoutDirection = AttackingDirection::LeftToRight;

std::runtime_error eventError(std::size_t index, const std::string& message) {
  return std::runtime_error("Wyscout event at index " + std::to_string(index) + ": " +
                            message);
}

template <typename T>
T required(const Json& event, std::string_view key, std::size_t index) {
  const auto value = event.find(key);
  if (value == event.end() || value->is_null()) {
    throw eventError(index, "missing required field '" + std::string(key) + "'");
  }
  try {
    return value->get<T>();
  } catch (const Json::exception& error) {
    throw eventError(index, "invalid field '" + std::string(key) + "': " + error.what());
  }
}

struct Period {
  std::int32_t number{};
  std::int32_t minute_offset{};
};

Period parsePeriod(std::string_view value, std::size_t index) {
  if (value == "1H") {
    return {1, 0};
  }
  if (value == "2H") {
    return {2, 45};
  }
  if (value == "E1") {
    return {3, 90};
  }
  if (value == "E2") {
    return {4, 105};
  }
  if (value == "P") {
    return {5, 120};
  }
  throw eventError(index, "unsupported matchPeriod '" + std::string(value) + "'");
}

std::optional<Coordinate> position(const Json& positions, std::size_t position_index,
                                   std::size_t event_index) {
  if (position_index >= positions.size()) {
    return std::nullopt;
  }
  const auto& value = positions[position_index];
  if (!value.is_object()) {
    throw eventError(event_index, "positions entries must be objects");
  }
  const auto coordinate = Coordinate{required<double>(value, "x", event_index),
                                     required<double>(value, "y", event_index)};
  try {
    static_cast<void>(normalizeCoordinate(coordinate, kWyscoutPitch,
                                          kWyscoutDirection));
  } catch (const std::invalid_argument& error) {
    throw eventError(event_index, "invalid positions coordinate: " +
                                      std::string(error.what()));
  }
  return coordinate;
}

std::optional<std::string> outcome(const Json& event, std::size_t index) {
  const auto tags = event.find("tags");
  if (tags == event.end() || tags->is_null()) {
    return std::nullopt;
  }
  if (!tags->is_array()) {
    throw eventError(index, "field 'tags' must be an array");
  }
  for (const auto& tag : *tags) {
    if (!tag.is_object()) {
      throw eventError(index, "tags entries must be objects");
    }
    if (required<std::int32_t>(tag, "id", index) == 1801) {
      return "Accurate";
    }
  }
  return std::nullopt;
}

FootballEvent normalize(const Json& source, const ImportContext& context,
                        std::size_t index) {
  if (!source.is_object()) {
    throw eventError(index, "expected an object");
  }
  const auto period = parsePeriod(required<std::string>(source, "matchPeriod", index),
                                  index);
  const double period_seconds = required<double>(source, "eventSec", index);
  if (!std::isfinite(period_seconds) || period_seconds < 0.0) {
    throw eventError(index, "eventSec must be a finite non-negative number");
  }
  const auto whole_seconds = static_cast<std::int64_t>(std::floor(period_seconds));
  const auto positions = source.find("positions");
  if (positions == source.end() || positions->is_null() || !positions->is_array()) {
    throw eventError(index, "field 'positions' must be an array");
  }
  if (positions->size() > 2) {
    throw eventError(index, "field 'positions' must contain at most two coordinates");
  }

  FootballEvent event;
  event.provider_event_id =
      std::to_string(required<Identifier>(source, "id", index));
  event.match_id = context.match_id;
  event.period = period.number;
  event.time = MatchTime{
      std::chrono::milliseconds{
          static_cast<std::int64_t>(std::llround(period_seconds * 1000.0))},
      period.minute_offset + static_cast<std::int32_t>(whole_seconds / 60),
      static_cast<std::int32_t>(whole_seconds % 60)};
  event.team_id = required<Identifier>(source, "teamId", index);
  const auto player_id = required<Identifier>(source, "playerId", index);
  if (player_id != 0) {
    event.player_id = player_id;
  }
  event.event_type = required<std::string>(source, "eventName", index);
  if (event.event_type.empty()) {
    throw eventError(index, "eventName must not be empty");
  }
  event.outcome = outcome(source, index);
  event.source_start_location = position(*positions, 0, index);
  event.source_end_location = position(*positions, 1, index);
  event.start_location = event.source_start_location;
  event.end_location = event.source_end_location;
  event.provider = "Wyscout";
  return event;
}

}  // namespace

std::vector<FootballEvent> WyscoutEventAdapter::loadEvents(
    const std::filesystem::path& path, const ImportContext& context) const {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Unable to read Wyscout event file '" + path.string() + "'");
  }
  Json document;
  try {
    input >> document;
  } catch (const Json::parse_error& error) {
    throw std::runtime_error("Invalid JSON in Wyscout event file '" + path.string() +
                             "': " + error.what());
  }
  if (!document.is_array()) {
    throw std::runtime_error("Invalid Wyscout event file '" + path.string() +
                             "': top-level JSON must be an array");
  }

  std::vector<FootballEvent> events;
  for (std::size_t index = 0; index < document.size(); ++index) {
    if (!document[index].is_object()) {
      throw eventError(index, "expected an object");
    }
    const auto match_id = required<Identifier>(document[index], "matchId", index);
    if (match_id == context.match_id) {
      events.push_back(normalize(document[index], context, index));
    }
  }
  if (events.empty()) {
    throw std::runtime_error("Wyscout event file '" + path.string() +
                             "' contains no events for match ID " +
                             std::to_string(context.match_id));
  }
  return events;
}

}  // namespace emberdb
