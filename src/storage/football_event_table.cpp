#include "emberdb/storage/football_event_table.h"

#include <algorithm>
#include <stdexcept>

namespace emberdb {

void FootballEventTable::append(const FootballEvent& event) {
  provider_event_ids_.push_back(event.provider_event_id);
  match_ids_.push_back(event.match_id);
  periods_.push_back(event.period);
  timestamps_.push_back(event.time.timestamp);
  minutes_.push_back(event.time.minute);
  seconds_.push_back(event.time.second);
  possession_ids_.push_back(event.possession_id);
  team_ids_.push_back(event.team_id);
  team_names_.push_back(event.team_name);
  player_ids_.push_back(event.player_id);
  player_names_.push_back(event.player_name);
  event_types_.push_back(event.event_type);
  outcomes_.push_back(event.outcome);
  start_x_values_.push_back(event.start_location ? std::optional{event.start_location->x}
                                                 : std::nullopt);
  start_y_values_.push_back(event.start_location ? std::optional{event.start_location->y}
                                                 : std::nullopt);
  end_x_values_.push_back(event.end_location ? std::optional{event.end_location->x}
                                             : std::nullopt);
  end_y_values_.push_back(event.end_location ? std::optional{event.end_location->y}
                                             : std::nullopt);
  providers_.push_back(event.provider);
}

std::size_t FootballEventTable::rowCount() const noexcept { return provider_event_ids_.size(); }

bool FootballEventTable::validate() const noexcept {
  const auto expected = rowCount();
  return match_ids_.size() == expected && periods_.size() == expected &&
         timestamps_.size() == expected && minutes_.size() == expected &&
         seconds_.size() == expected && possession_ids_.size() == expected &&
         team_ids_.size() == expected && team_names_.size() == expected &&
         player_ids_.size() == expected && player_names_.size() == expected &&
         event_types_.size() == expected && outcomes_.size() == expected &&
         start_x_values_.size() == expected && start_y_values_.size() == expected &&
         end_x_values_.size() == expected && end_y_values_.size() == expected &&
         providers_.size() == expected;
}

FootballEvent FootballEventTable::row(std::size_t index) const {
  if (index >= rowCount()) {
    throw std::out_of_range("FootballEventTable row index " + std::to_string(index) +
                            " is out of range");
  }
  const auto coordinate = [](const std::optional<double>& x,
                             const std::optional<double>& y) -> std::optional<Coordinate> {
    if (x && y) {
      return Coordinate{*x, *y};
    }
    return std::nullopt;
  };
  return FootballEvent{provider_event_ids_[index],
                       match_ids_[index],
                       periods_[index],
                       MatchTime{timestamps_[index], minutes_[index], seconds_[index]},
                       possession_ids_[index],
                       team_ids_[index],
                       team_names_[index],
                       player_ids_[index],
                       player_names_[index],
                       event_types_[index],
                       outcomes_[index],
                       coordinate(start_x_values_[index], start_y_values_[index]),
                       coordinate(end_x_values_[index], end_y_values_[index]),
                       providers_[index]};
}

std::size_t FootballEventTable::playerDataCount() const noexcept {
  return static_cast<std::size_t>(std::count_if(
      player_ids_.begin(), player_ids_.end(), [](const auto& value) { return value.has_value(); }));
}

std::size_t FootballEventTable::startLocationCount() const noexcept {
  return static_cast<std::size_t>(std::count_if(start_x_values_.begin(), start_x_values_.end(),
                                                [](const auto& value) { return value.has_value(); }));
}

std::size_t FootballEventTable::endLocationCount() const noexcept {
  return static_cast<std::size_t>(std::count_if(end_x_values_.begin(), end_x_values_.end(),
                                                [](const auto& value) { return value.has_value(); }));
}

}  // namespace emberdb
