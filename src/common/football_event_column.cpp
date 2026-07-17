#include "emberdb/common/football_event_column.h"

#include <array>
#include <type_traits>
#include <utility>

namespace emberdb {
namespace {

using ColumnName = std::pair<FootballEventColumn, std::string_view>;

constexpr std::array<ColumnName, 18> kColumnNames{{
    {FootballEventColumn::ProviderEventId, "provider_event_id"},
    {FootballEventColumn::MatchId, "match_id"},
    {FootballEventColumn::Period, "period"},
    {FootballEventColumn::Timestamp, "timestamp"},
    {FootballEventColumn::Minute, "minute"},
    {FootballEventColumn::Second, "second"},
    {FootballEventColumn::PossessionId, "possession_id"},
    {FootballEventColumn::TeamId, "team_id"},
    {FootballEventColumn::TeamName, "team_name"},
    {FootballEventColumn::PlayerId, "player_id"},
    {FootballEventColumn::PlayerName, "player_name"},
    {FootballEventColumn::EventType, "event_type"},
    {FootballEventColumn::Outcome, "outcome"},
    {FootballEventColumn::StartX, "start_x"},
    {FootballEventColumn::StartY, "start_y"},
    {FootballEventColumn::EndX, "end_x"},
    {FootballEventColumn::EndY, "end_y"},
    {FootballEventColumn::Provider, "provider"},
}};

}  // namespace

std::string_view columnName(FootballEventColumn column) noexcept {
  for (const auto& [candidate, name] : kColumnNames) {
    if (candidate == column) {
      return name;
    }
  }
  return "unknown";
}

FootballEventValueType columnValueType(FootballEventColumn column) noexcept {
  switch (column) {
    case FootballEventColumn::MatchId:
    case FootballEventColumn::PossessionId:
    case FootballEventColumn::TeamId:
    case FootballEventColumn::PlayerId:
      return FootballEventValueType::Identifier;
    case FootballEventColumn::Period:
    case FootballEventColumn::Minute:
    case FootballEventColumn::Second:
      return FootballEventValueType::Integer;
    case FootballEventColumn::Timestamp:
      return FootballEventValueType::Timestamp;
    case FootballEventColumn::StartX:
    case FootballEventColumn::StartY:
    case FootballEventColumn::EndX:
    case FootballEventColumn::EndY:
      return FootballEventValueType::Number;
    case FootballEventColumn::ProviderEventId:
    case FootballEventColumn::TeamName:
    case FootballEventColumn::PlayerName:
    case FootballEventColumn::EventType:
    case FootballEventColumn::Outcome:
    case FootballEventColumn::Provider:
      return FootballEventValueType::Text;
  }
  return FootballEventValueType::Text;
}

FootballEventValueType valueType(const FootballEventValue& value) noexcept {
  return std::visit(
      [](const auto& typed_value) {
        using Value = std::decay_t<decltype(typed_value)>;
        if constexpr (std::is_same_v<Value, Identifier>) {
          return FootballEventValueType::Identifier;
        } else if constexpr (std::is_same_v<Value, std::int32_t>) {
          return FootballEventValueType::Integer;
        } else if constexpr (std::is_same_v<Value, std::chrono::milliseconds>) {
          return FootballEventValueType::Timestamp;
        } else if constexpr (std::is_same_v<Value, double>) {
          return FootballEventValueType::Number;
        } else {
          return FootballEventValueType::Text;
        }
      },
      value);
}

bool columnIsNullable(FootballEventColumn column) noexcept {
  switch (column) {
    case FootballEventColumn::PossessionId:
    case FootballEventColumn::TeamId:
    case FootballEventColumn::TeamName:
    case FootballEventColumn::PlayerId:
    case FootballEventColumn::PlayerName:
    case FootballEventColumn::Outcome:
    case FootballEventColumn::StartX:
    case FootballEventColumn::StartY:
    case FootballEventColumn::EndX:
    case FootballEventColumn::EndY:
      return true;
    case FootballEventColumn::ProviderEventId:
    case FootballEventColumn::MatchId:
    case FootballEventColumn::Period:
    case FootballEventColumn::Timestamp:
    case FootballEventColumn::Minute:
    case FootballEventColumn::Second:
    case FootballEventColumn::EventType:
    case FootballEventColumn::Provider:
      return false;
  }
  return false;
}

std::optional<FootballEventColumn> columnFromName(std::string_view name) noexcept {
  for (const auto& [column, candidate] : kColumnNames) {
    if (candidate == name) {
      return column;
    }
  }
  return std::nullopt;
}

}  // namespace emberdb
