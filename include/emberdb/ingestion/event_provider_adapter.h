#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include "emberdb/common/coordinate_normalization.h"
#include "emberdb/common/football_event.h"

namespace emberdb {

struct ImportContext {
  ImportContext(
      Identifier imported_match_id,
      std::optional<AttackingDirection> imported_home_team_first_half_direction =
          std::nullopt)
      : match_id(imported_match_id),
        home_team_first_half_direction(imported_home_team_first_half_direction) {}

  Identifier match_id{};
  std::optional<AttackingDirection> home_team_first_half_direction;
};

class EventProviderAdapter {
 public:
  virtual ~EventProviderAdapter() = default;

  [[nodiscard]] virtual std::vector<FootballEvent> loadEvents(
      const std::filesystem::path& path, const ImportContext& context) const = 0;
};

}  // namespace emberdb
