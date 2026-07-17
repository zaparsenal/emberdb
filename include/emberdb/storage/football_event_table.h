#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "emberdb/common/football_event_column.h"
#include "emberdb/common/football_event.h"

namespace emberdb {

class FootballEventTable {
 public:
  static constexpr std::size_t kColumnCount = 18;

  void append(const FootballEvent& event);
  [[nodiscard]] std::size_t rowCount() const noexcept;
  [[nodiscard]] bool validate() const noexcept;
  [[nodiscard]] FootballEvent row(std::size_t index) const;
  [[nodiscard]] FootballEventCell cell(FootballEventColumn column,
                                       std::size_t index) const;

  [[nodiscard]] std::size_t playerDataCount() const noexcept;
  [[nodiscard]] std::size_t startLocationCount() const noexcept;
  [[nodiscard]] std::size_t endLocationCount() const noexcept;

 private:
  std::vector<std::string> provider_event_ids_;
  std::vector<Identifier> match_ids_;
  std::vector<std::int32_t> periods_;
  std::vector<std::chrono::milliseconds> timestamps_;
  std::vector<std::int32_t> minutes_;
  std::vector<std::int32_t> seconds_;
  std::vector<std::optional<Identifier>> possession_ids_;
  std::vector<std::optional<Identifier>> team_ids_;
  std::vector<std::optional<std::string>> team_names_;
  std::vector<std::optional<Identifier>> player_ids_;
  std::vector<std::optional<std::string>> player_names_;
  std::vector<std::string> event_types_;
  std::vector<std::optional<std::string>> outcomes_;
  std::vector<std::optional<double>> start_x_values_;
  std::vector<std::optional<double>> start_y_values_;
  std::vector<std::optional<double>> end_x_values_;
  std::vector<std::optional<double>> end_y_values_;
  std::vector<std::string> providers_;
};

}  // namespace emberdb
