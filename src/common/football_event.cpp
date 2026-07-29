#include "emberdb/common/football_event.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>

#include "emberdb/common/coordinate_normalization.h"

namespace emberdb {
namespace {

bool isBlank(std::string_view value) {
  return value.empty() ||
         std::ranges::all_of(value, [](const char character) {
           return std::isspace(static_cast<unsigned char>(character)) != 0;
         });
}

void requireText(std::string_view value, std::string_view field) {
  if (isBlank(value)) {
    throw std::invalid_argument("FootballEvent field '" + std::string(field) +
                                "' must not be blank");
  }
}

void requireOptionalText(const std::optional<std::string>& value,
                         std::string_view field) {
  if (value) {
    requireText(*value, field);
  }
}

void requireOptionalPositive(const std::optional<Identifier>& value,
                             std::string_view field) {
  if (value && *value <= 0) {
    throw std::invalid_argument("FootballEvent field '" + std::string(field) +
                                "' must be positive when present");
  }
}

void validateSourceCoordinate(const std::optional<Coordinate>& coordinate,
                              std::string_view field) {
  if (coordinate &&
      (!std::isfinite(coordinate->x) || !std::isfinite(coordinate->y))) {
    throw std::invalid_argument("FootballEvent field '" + std::string(field) +
                                "' must contain finite coordinates");
  }
}

}  // namespace

void validateFootballEvent(const FootballEvent& event) {
  requireText(event.provider_event_id, "provider_event_id");
  if (event.match_id <= 0) {
    throw std::invalid_argument("FootballEvent field 'match_id' must be positive");
  }
  if (event.period <= 0) {
    throw std::invalid_argument("FootballEvent field 'period' must be positive");
  }
  if (event.time.timestamp < std::chrono::milliseconds::zero()) {
    throw std::invalid_argument(
        "FootballEvent field 'timestamp' must not be negative");
  }
  if (event.time.minute < 0) {
    throw std::invalid_argument("FootballEvent field 'minute' must not be negative");
  }
  if (event.time.second < 0 || event.time.second >= 60) {
    throw std::invalid_argument(
        "FootballEvent field 'second' must be between 0 and 59");
  }

  requireOptionalPositive(event.possession_id, "possession_id");
  requireOptionalPositive(event.team_id, "team_id");
  requireOptionalPositive(event.player_id, "player_id");
  requireOptionalText(event.team_name, "team_name");
  requireOptionalText(event.player_name, "player_name");
  requireText(event.event_type, "event_type");
  requireOptionalText(event.outcome, "outcome");
  requireText(event.provider, "provider");

  if (event.start_location) {
    validateCanonicalCoordinate(*event.start_location);
  }
  if (event.end_location) {
    validateCanonicalCoordinate(*event.end_location);
  }
  validateSourceCoordinate(event.source_start_location, "source_start_location");
  validateSourceCoordinate(event.source_end_location, "source_end_location");
}

}  // namespace emberdb
