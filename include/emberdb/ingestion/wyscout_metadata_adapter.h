#pragma once

#include <filesystem>

#include "emberdb/ingestion/provider_metadata.h"

namespace emberdb {

class WyscoutMetadataAdapter {
 public:
  [[nodiscard]] ProviderMetadata loadCompetitions(
      const std::filesystem::path& path) const;
  [[nodiscard]] ProviderMetadata loadTeams(
      const std::filesystem::path& path) const;
  [[nodiscard]] ProviderMetadata loadPlayers(
      const std::filesystem::path& path) const;
  [[nodiscard]] ProviderMetadata loadMatches(
      const std::filesystem::path& path) const;
};

}  // namespace emberdb
