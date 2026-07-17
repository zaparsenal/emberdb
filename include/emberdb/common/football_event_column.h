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
  ProviderEventId,
  MatchId,
  Period,
  Timestamp,
  Minute,
  Second,
  PossessionId,
  TeamId,
  TeamName,
  PlayerId,
  PlayerName,
  EventType,
  Outcome,
  StartX,
  StartY,
  EndX,
  EndY,
  Provider,
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
[[nodiscard]] FootballEventValueType valueType(const FootballEventValue& value) noexcept;
[[nodiscard]] std::optional<FootballEventColumn> columnFromName(std::string_view name) noexcept;

}  // namespace emberdb
