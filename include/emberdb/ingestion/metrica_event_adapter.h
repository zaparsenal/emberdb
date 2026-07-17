#pragma once

#include "emberdb/ingestion/event_provider_adapter.h"

namespace emberdb {

class MetricaEventAdapter final : public EventProviderAdapter {
 public:
  [[nodiscard]] std::vector<FootballEvent> loadEvents(
      const std::filesystem::path& path, const ImportContext& context) const override;
};

}  // namespace emberdb
