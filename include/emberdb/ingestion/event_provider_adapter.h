#pragma once

#include <filesystem>
#include <vector>

#include "emberdb/common/football_event.h"

namespace emberdb {

struct ImportContext {
  Identifier match_id{};
};

class EventProviderAdapter {
 public:
  virtual ~EventProviderAdapter() = default;

  [[nodiscard]] virtual std::vector<FootballEvent> loadEvents(
      const std::filesystem::path& path, const ImportContext& context) const = 0;
};

}  // namespace emberdb
