#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "emberdb/reconciliation/match_review.h"

namespace emberdb {

inline constexpr std::uint32_t kCatalogManifestVersion = 1;

struct CatalogManifestProvenance {
  std::string author;
  std::string source;
  std::string reason;
  std::vector<std::string> issues;
};

struct CatalogManifestMapping {
  std::string provider;
  std::string provider_id;
  std::optional<std::string> provider_match_id;
  CatalogManifestProvenance provenance;
  std::vector<std::string> issues;
  std::size_t sequence{};
};

struct CatalogManifestEntry {
  CatalogEntityType entity_type{CatalogEntityType::Team};
  Identifier canonical_id{};
  std::string name;
  Identifier competition_id{};
  Identifier season_id{};
  Identifier home_team_id{};
  Identifier away_team_id{};
  std::optional<std::int64_t> kickoff_seconds;
  std::optional<std::int32_t> home_score;
  std::optional<std::int32_t> away_score;
  CatalogManifestProvenance provenance;
  std::vector<CatalogManifestMapping> mappings;
  std::vector<std::string> issues;
  std::size_t sequence{};
};

struct CatalogManifest {
  std::uint32_t version{kCatalogManifestVersion};
  std::vector<CatalogManifestEntry> entries;
};

enum class CatalogManifestAction { Create, Unchanged, Conflict, Invalid };

struct CatalogManifestResult {
  CatalogEntityType entity_type{CatalogEntityType::Team};
  Identifier canonical_id{};
  std::string canonical_name;
  std::optional<std::string> provider;
  std::optional<std::string> provider_id;
  std::optional<std::string> provider_match_id;
  CatalogManifestAction action{CatalogManifestAction::Invalid};
  std::string reason;
  std::size_t sequence{};
};

struct CatalogManifestSummary {
  std::size_t create{};
  std::size_t unchanged{};
  std::size_t conflicts{};
  std::size_t invalid{};
};

struct CatalogManifestReport {
  std::uint32_t manifest_version{kCatalogManifestVersion};
  std::uint64_t base_revision{};
  std::uint64_t planned_revision{};
  CatalogManifestSummary summary;
  std::vector<CatalogManifestResult> results;

  [[nodiscard]] bool importable() const noexcept;
  [[nodiscard]] bool hasChanges() const noexcept;
};

struct CatalogManifestImportPlan {
  CatalogManifestReport report;
  MatchReviewStore resulting_store;
};

[[nodiscard]] CatalogManifest loadCatalogManifest(
    const std::filesystem::path& path);

[[nodiscard]] CatalogManifestImportPlan planCatalogManifestImport(
    const CatalogManifest& manifest, const MatchReviewStore& current_store,
    std::chrono::sys_seconds recorded_at);

[[nodiscard]] std::string_view catalogManifestActionName(
    CatalogManifestAction action) noexcept;

}  // namespace emberdb
