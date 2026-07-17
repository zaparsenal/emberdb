#pragma once

#include <filesystem>

#include "emberdb/ingestion/provider_metadata.h"

namespace emberdb {

class StatsBombMetadataAdapter {
 public:
  [[nodiscard]] ProviderMetadata loadMatches(
      const std::filesystem::path& path) const;
  [[nodiscard]] ProviderMetadata loadLineups(
      const std::filesystem::path& path) const;
};

}  // namespace emberdb
