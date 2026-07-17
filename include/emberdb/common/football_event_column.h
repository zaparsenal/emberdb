#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "emberdb/common/football_event.h"

namespace emberdb {

enum class FootballEventColumn {
  ProviderEventId = 0,
  MatchId = 1,
  Period = 2,
  Timestamp = 3,
  Minute = 4,
  Second = 5,
  PossessionId = 6,
  TeamId = 7,
  TeamName = 8,
  PlayerId = 9,
  PlayerName = 10,
  EventType = 11,
  Outcome = 12,
  StartX = 13,
  StartY = 14,
  EndX = 15,
  EndY = 16,
  Provider = 17,
};

enum class FootballEventValueType {
  Identifier,
  Integer,
  Timestamp,
  Number,
  Text,
};

using FootballEventValue =
    std::variant<Identifier, std::int32_t, std::chrono::milliseconds, double, std::string>;
using FootballEventCell = std::optional<FootballEventValue>;

[[nodiscard]] std::string_view columnName(FootballEventColumn column) noexcept;
[[nodiscard]] FootballEventValueType columnValueType(FootballEventColumn column) noexcept;
[[nodiscard]] bool columnIsNullable(FootballEventColumn column) noexcept;
[[nodiscard]] FootballEventValueType valueType(const FootballEventValue& value) noexcept;
[[nodiscard]] std::optional<FootballEventColumn> columnFromName(std::string_view name) noexcept;

}  // namespace emberdb
