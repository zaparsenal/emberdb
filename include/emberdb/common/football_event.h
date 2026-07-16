#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace emberdb {

using Identifier = std::int64_t;

struct Coordinate {
  double x{};
  double y{};

  bool operator==(const Coordinate&) const = default;
};

// Provider-relative time within a match period. StatsBomb timestamps are
// converted to milliseconds while minute/second retain the provider values.
struct MatchTime {
  std::chrono::milliseconds timestamp{};
  std::int32_t minute{};
  std::int32_t second{};

  bool operator==(const MatchTime&) const = default;
};

// Provider-independent normalized event. Coordinates currently remain on the
// provider's scale; provider records which interpretation applies.
struct FootballEvent {
  std::string provider_event_id;
  Identifier match_id{};
  std::int32_t period{};
  MatchTime time;
  std::optional<Identifier> possession_id;
  std::optional<Identifier> team_id;
  std::optional<std::string> team_name;
  std::optional<Identifier> player_id;
  std::optional<std::string> player_name;
  std::string event_type;
  std::optional<std::string> outcome;
  std::optional<Coordinate> start_location;
  std::optional<Coordinate> end_location;
  std::string provider;
};

}  // namespace emberdb
