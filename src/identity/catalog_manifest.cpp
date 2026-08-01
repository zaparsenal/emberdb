#include "emberdb/identity/catalog_manifest.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

#include <nlohmann/json.hpp>

namespace emberdb {
namespace {

using Json = nlohmann::json;
constexpr std::string_view kManifestFormat = "emberdb-canonical-manifest";

bool blank(std::string_view value) {
  return std::ranges::all_of(value, [](char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
  });
}

std::string joinedIssues(const std::vector<std::string>& issues) {
  std::ostringstream output;
  for (std::size_t index = 0; index < issues.size(); ++index) {
    if (index != 0) {
      output << "; ";
    }
    output << issues[index];
  }
  return output.str();
}

void appendIssues(std::vector<std::string>& destination,
                  const std::vector<std::string>& source) {
  destination.insert(destination.end(), source.begin(), source.end());
}

void rejectUnknownFields(const Json& object,
                         std::initializer_list<std::string_view> allowed,
                         std::vector<std::string>& issues) {
  if (!object.is_object()) {
    return;
  }
  for (const auto& [field, unused] : object.items()) {
    static_cast<void>(unused);
    if (std::ranges::find(allowed, field) == allowed.end()) {
      issues.push_back("unsupported field '" + field + "'");
    }
  }
}

std::string readRequiredString(const Json& object, std::string_view field,
                               std::vector<std::string>& issues) {
  if (!object.contains(field)) {
    issues.push_back("missing required field '" + std::string(field) + "'");
    return {};
  }
  const auto& value = object.at(field);
  if (!value.is_string()) {
    issues.push_back("field '" + std::string(field) + "' must be a string");
    return {};
  }
  auto result = value.get<std::string>();
  if (result.empty() || blank(result)) {
    issues.push_back("field '" + std::string(field) + "' must not be blank");
  }
  return result;
}

template <typename Integer>
Integer readRequiredInteger(const Json& object, std::string_view field,
                            std::vector<std::string>& issues,
                            bool require_positive = false) {
  if (!object.contains(field)) {
    issues.push_back("missing required field '" + std::string(field) + "'");
    return {};
  }
  const auto& value = object.at(field);
  if (!value.is_number_integer()) {
    issues.push_back("field '" + std::string(field) + "' must be an integer");
    return {};
  }
  try {
    const auto result = value.get<Integer>();
    if (require_positive && result <= 0) {
      issues.push_back("field '" + std::string(field) +
                       "' must be a positive integer");
    }
    return result;
  } catch (const nlohmann::json::exception&) {
    issues.push_back("field '" + std::string(field) +
                     "' is outside the supported integer range");
    return {};
  }
}

template <typename Integer>
std::optional<Integer> readOptionalInteger(
    const Json& object, std::string_view field,
    std::vector<std::string>& issues) {
  if (!object.contains(field)) {
    return std::nullopt;
  }
  const auto& value = object.at(field);
  if (!value.is_number_integer()) {
    issues.push_back("field '" + std::string(field) + "' must be an integer");
    return std::nullopt;
  }
  try {
    return value.get<Integer>();
  } catch (const nlohmann::json::exception&) {
    issues.push_back("field '" + std::string(field) +
                     "' is outside the supported integer range");
    return std::nullopt;
  }
}

std::optional<std::string> readOptionalString(
    const Json& object, std::string_view field,
    std::vector<std::string>& issues) {
  if (!object.contains(field)) {
    return std::nullopt;
  }
  const auto& value = object.at(field);
  if (!value.is_string()) {
    issues.push_back("field '" + std::string(field) + "' must be a string");
    return std::nullopt;
  }
  auto result = value.get<std::string>();
  if (result.empty() || blank(result)) {
    issues.push_back("field '" + std::string(field) + "' must not be blank");
  }
  return result;
}

CatalogManifestProvenance readProvenance(const Json& parent) {
  CatalogManifestProvenance result;
  if (!parent.contains("provenance")) {
    result.issues.push_back("missing required provenance");
    return result;
  }
  const auto& value = parent.at("provenance");
  if (!value.is_object()) {
    result.issues.push_back("provenance must be an object");
    return result;
  }
  rejectUnknownFields(value, {"author", "source", "reason"}, result.issues);
  result.author = readRequiredString(value, "author", result.issues);
  result.source = readRequiredString(value, "source", result.issues);
  result.reason = readRequiredString(value, "reason", result.issues);
  return result;
}

CatalogManifestMapping readMapping(const Json& value,
                                   CatalogEntityType entity_type,
                                   std::size_t sequence) {
  CatalogManifestMapping result;
  result.sequence = sequence;
  if (!value.is_object()) {
    result.issues.push_back("provider mapping must be an object");
    return result;
  }
  rejectUnknownFields(
      value,
      {"provider", "provider_id", "provider_match_id", "provenance"},
      result.issues);
  result.provider =
      readRequiredString(value, "provider", result.issues);
  result.provider_id =
      readRequiredString(value, "provider_id", result.issues);
  result.provider_match_id =
      readOptionalString(value, "provider_match_id", result.issues);
  result.provenance = readProvenance(value);
  appendIssues(result.issues, result.provenance.issues);
  if (result.provider_match_id &&
      entity_type != CatalogEntityType::Team &&
      entity_type != CatalogEntityType::Player) {
    result.issues.push_back(
        "provider_match_id is only valid for team or player mappings");
  }
  return result;
}

void readMappings(const Json& object, CatalogManifestEntry& entry,
                  std::size_t& sequence) {
  if (!object.contains("mappings")) {
    return;
  }
  const auto& mappings = object.at("mappings");
  if (!mappings.is_array()) {
    entry.issues.push_back("field 'mappings' must be an array");
    return;
  }
  for (const auto& mapping : mappings) {
    entry.mappings.push_back(
        readMapping(mapping, entry.entity_type, sequence++));
  }
}

CatalogManifestEntry readNamedEntry(const Json& value,
                                    CatalogEntityType entity_type,
                                    std::size_t sequence) {
  CatalogManifestEntry result;
  result.entity_type = entity_type;
  result.sequence = sequence;
  if (!value.is_object()) {
    result.issues.push_back("canonical entry must be an object");
    return result;
  }
  rejectUnknownFields(value, {"id", "name", "provenance", "mappings"},
                      result.issues);
  result.canonical_id =
      readRequiredInteger<Identifier>(value, "id", result.issues, true);
  result.name = readRequiredString(value, "name", result.issues);
  result.provenance = readProvenance(value);
  appendIssues(result.issues, result.provenance.issues);
  return result;
}

CatalogManifestEntry readSeasonEntry(const Json& value,
                                     std::size_t sequence) {
  CatalogManifestEntry result;
  result.entity_type = CatalogEntityType::Season;
  result.sequence = sequence;
  if (!value.is_object()) {
    result.issues.push_back("canonical entry must be an object");
    return result;
  }
  rejectUnknownFields(
      value, {"id", "competition_id", "name", "provenance", "mappings"},
      result.issues);
  result.canonical_id =
      readRequiredInteger<Identifier>(value, "id", result.issues, true);
  result.competition_id = readRequiredInteger<Identifier>(
      value, "competition_id", result.issues, true);
  result.name = readRequiredString(value, "name", result.issues);
  result.provenance = readProvenance(value);
  appendIssues(result.issues, result.provenance.issues);
  return result;
}

CatalogManifestEntry readMatchEntry(const Json& value,
                                    std::size_t sequence) {
  CatalogManifestEntry result;
  result.entity_type = CatalogEntityType::Match;
  result.sequence = sequence;
  if (!value.is_object()) {
    result.issues.push_back("canonical entry must be an object");
    return result;
  }
  rejectUnknownFields(
      value,
      {"id", "season_id", "home_team_id", "away_team_id",
       "kickoff_seconds", "home_score", "away_score", "provenance",
       "mappings"},
      result.issues);
  result.canonical_id =
      readRequiredInteger<Identifier>(value, "id", result.issues, true);
  result.season_id = readRequiredInteger<Identifier>(
      value, "season_id", result.issues, true);
  result.home_team_id = readRequiredInteger<Identifier>(
      value, "home_team_id", result.issues, true);
  result.away_team_id = readRequiredInteger<Identifier>(
      value, "away_team_id", result.issues, true);
  result.kickoff_seconds =
      readOptionalInteger<std::int64_t>(value, "kickoff_seconds",
                                        result.issues);
  result.home_score =
      readOptionalInteger<std::int32_t>(value, "home_score", result.issues);
  result.away_score =
      readOptionalInteger<std::int32_t>(value, "away_score", result.issues);
  if (result.home_score.has_value() != result.away_score.has_value()) {
    result.issues.push_back(
        "home_score and away_score must be specified together");
  }
  result.provenance = readProvenance(value);
  appendIssues(result.issues, result.provenance.issues);
  return result;
}

void readEntries(const Json& document, std::string_view field,
                 CatalogEntityType entity_type,
                 std::vector<CatalogManifestEntry>& entries,
                 std::size_t& sequence) {
  if (!document.contains(field)) {
    return;
  }
  const auto& values = document.at(field);
  if (!values.is_array()) {
    throw std::runtime_error("catalog manifest field '" +
                             std::string(field) + "' must be an array");
  }
  for (const auto& value : values) {
    CatalogManifestEntry entry;
    if (entity_type == CatalogEntityType::Season) {
      entry = readSeasonEntry(value, sequence++);
    } else if (entity_type == CatalogEntityType::Match) {
      entry = readMatchEntry(value, sequence++);
    } else {
      entry = readNamedEntry(value, entity_type, sequence++);
    }
    readMappings(value, entry, sequence);
    entries.push_back(std::move(entry));
  }
}

ReviewProvenance reviewProvenance(
    const CatalogManifestProvenance& provenance,
    std::chrono::sys_seconds recorded_at) {
  return {provenance.author, provenance.source, provenance.reason,
          recorded_at};
}

int entityRank(CatalogEntityType entity_type) {
  switch (entity_type) {
    case CatalogEntityType::Competition:
      return 0;
    case CatalogEntityType::Season:
      return 1;
    case CatalogEntityType::Team:
      return 2;
    case CatalogEntityType::Player:
      return 3;
    case CatalogEntityType::Match:
      return 4;
  }
  return 5;
}

struct MappingKey {
  CatalogEntityType entity_type;
  std::string provider;
  std::string provider_id;
  std::optional<std::string> provider_match_id;

  bool operator<(const MappingKey& other) const {
    return std::tie(entity_type, provider, provider_id, provider_match_id) <
           std::tie(other.entity_type, other.provider, other.provider_id,
                    other.provider_match_id);
  }
};

struct MappingLocation {
  std::size_t entry_index{};
  std::size_t mapping_index{};
};

std::string entryName(const CatalogManifestEntry& entry,
                      const CanonicalIdentityCatalog& catalog) {
  if (entry.entity_type != CatalogEntityType::Match) {
    return entry.name;
  }
  const auto* season = catalog.season({entry.season_id});
  if (season == nullptr) {
    return "season " + std::to_string(entry.season_id);
  }
  const auto* competition = catalog.competition(season->competition_id);
  if (competition == nullptr) {
    return "season " + std::to_string(entry.season_id);
  }
  return competition->name + " " + season->name;
}

CatalogManifestResult entityResult(const CatalogManifestEntry& entry,
                                   std::string name,
                                   CatalogManifestAction action,
                                   std::string reason = {}) {
  return {entry.entity_type,
          entry.canonical_id,
          std::move(name),
          std::nullopt,
          std::nullopt,
          std::nullopt,
          action,
          std::move(reason),
          entry.sequence};
}

CatalogManifestResult mappingResult(const CatalogManifestEntry& entry,
                                    const CatalogManifestMapping& mapping,
                                    std::string name,
                                    CatalogManifestAction action,
                                    std::string reason = {}) {
  return {entry.entity_type,
          entry.canonical_id,
          std::move(name),
          mapping.provider,
          mapping.provider_id,
          mapping.provider_match_id,
          action,
          std::move(reason),
          mapping.sequence};
}

bool sameMatch(const CanonicalMatch& existing,
               const CanonicalMatch& expected) {
  return existing.season_id == expected.season_id &&
         existing.legacy_ancestry == expected.legacy_ancestry &&
         existing.kickoff == expected.kickoff &&
         existing.home_team_id == expected.home_team_id &&
         existing.away_team_id == expected.away_team_id &&
         existing.home_score == expected.home_score &&
         existing.away_score == expected.away_score;
}

CanonicalMatch manifestMatch(const CatalogManifestEntry& entry,
                             const CanonicalIdentityCatalog& catalog) {
  const auto* season = catalog.season({entry.season_id});
  if (season == nullptr) {
    throw std::invalid_argument(
        "match references unknown canonical season " +
        std::to_string(entry.season_id));
  }
  if (season->status != CanonicalEntityStatus::Active) {
    throw std::invalid_argument(
        "match references inactive canonical season " +
        std::to_string(entry.season_id));
  }
  const auto* competition = catalog.competition(season->competition_id);
  if (competition == nullptr) {
    throw std::invalid_argument(
        "match season references an unknown competition");
  }
  if (competition->status != CanonicalEntityStatus::Active) {
    throw std::invalid_argument(
        "match season references an inactive competition");
  }
  const auto* home_team = catalog.team({entry.home_team_id});
  if (home_team == nullptr) {
    throw std::invalid_argument(
        "match references unknown home team " +
        std::to_string(entry.home_team_id));
  }
  if (home_team->status != CanonicalEntityStatus::Active) {
    throw std::invalid_argument(
        "match references inactive home team " +
        std::to_string(entry.home_team_id));
  }
  const auto* away_team = catalog.team({entry.away_team_id});
  if (away_team == nullptr) {
    throw std::invalid_argument(
        "match references unknown away team " +
        std::to_string(entry.away_team_id));
  }
  if (away_team->status != CanonicalEntityStatus::Active) {
    throw std::invalid_argument(
        "match references inactive away team " +
        std::to_string(entry.away_team_id));
  }
  if (entry.home_team_id == entry.away_team_id) {
    throw std::invalid_argument("canonical match teams must be different");
  }
  if (entry.home_score.has_value() != entry.away_score.has_value() ||
      (entry.home_score &&
       (*entry.home_score < 0 || *entry.away_score < 0))) {
    throw std::invalid_argument(
        "canonical match scores must be non-negative and both present or "
        "missing");
  }
  return {{entry.canonical_id},
          {entry.season_id},
          entry.kickoff_seconds
              ? std::optional<std::chrono::sys_seconds>{
                    std::chrono::sys_seconds{
                        std::chrono::seconds{*entry.kickoff_seconds}}}
              : std::nullopt,
          {entry.home_team_id},
          {entry.away_team_id},
          entry.home_score,
          entry.away_score};
}

std::optional<Identifier> existingEntityId(
    const CanonicalIdentityCatalog& catalog, CatalogEntityType entity_type,
    Identifier canonical_id) {
  switch (entity_type) {
    case CatalogEntityType::Competition:
      return catalog.competition({canonical_id}) ? std::optional{canonical_id}
                                                 : std::nullopt;
    case CatalogEntityType::Season:
      return catalog.season({canonical_id}) ? std::optional{canonical_id}
                                            : std::nullopt;
    case CatalogEntityType::Team:
      return catalog.team({canonical_id}) ? std::optional{canonical_id}
                                          : std::nullopt;
    case CatalogEntityType::Player:
      return catalog.player({canonical_id}) ? std::optional{canonical_id}
                                            : std::nullopt;
    case CatalogEntityType::Match:
      return catalog.match({canonical_id}) ? std::optional{canonical_id}
                                           : std::nullopt;
  }
  return std::nullopt;
}

std::optional<Identifier> resolveMapping(
    const CanonicalIdentityCatalog& catalog, const MappingKey& key) {
  switch (key.entity_type) {
    case CatalogEntityType::Competition:
      if (const auto id =
              catalog.resolveCompetition({key.provider, key.provider_id})) {
        return id->value;
      }
      break;
    case CatalogEntityType::Season:
      if (const auto id =
              catalog.resolveSeason({key.provider, key.provider_id})) {
        return id->value;
      }
      break;
    case CatalogEntityType::Team:
      if (const auto id = catalog.resolveTeam(
              {key.provider, key.provider_id, key.provider_match_id})) {
        return id->value;
      }
      break;
    case CatalogEntityType::Player:
      if (const auto id = catalog.resolvePlayer(
              {key.provider, key.provider_id, key.provider_match_id})) {
        return id->value;
      }
      break;
    case CatalogEntityType::Match:
      if (const auto id =
              catalog.resolveMatch({key.provider, key.provider_id})) {
        return id->value;
      }
      break;
  }
  return std::nullopt;
}

void applyMapping(MatchReviewStore& store,
                  const CatalogManifestEntry& entry,
                  const CatalogManifestMapping& mapping,
                  std::chrono::sys_seconds recorded_at) {
  auto provenance = reviewProvenance(mapping.provenance, recorded_at);
  switch (entry.entity_type) {
    case CatalogEntityType::Competition:
      store.mapCompetition({mapping.provider, mapping.provider_id},
                           {entry.canonical_id}, std::move(provenance));
      break;
    case CatalogEntityType::Season:
      store.mapSeason({mapping.provider, mapping.provider_id},
                      {entry.canonical_id}, std::move(provenance));
      break;
    case CatalogEntityType::Team:
      store.mapTeam(
          {mapping.provider, mapping.provider_id, mapping.provider_match_id},
          {entry.canonical_id}, std::move(provenance));
      break;
    case CatalogEntityType::Player:
      store.mapPlayer(
          {mapping.provider, mapping.provider_id,
           mapping.provider_match_id},
          {entry.canonical_id}, std::move(provenance));
      break;
    case CatalogEntityType::Match:
      store.mapMatch({mapping.provider, mapping.provider_id},
                     {entry.canonical_id}, std::move(provenance));
      break;
  }
}

void addSummary(CatalogManifestSummary& summary,
                CatalogManifestAction action) {
  switch (action) {
    case CatalogManifestAction::Create:
      ++summary.create;
      break;
    case CatalogManifestAction::Unchanged:
      ++summary.unchanged;
      break;
    case CatalogManifestAction::Conflict:
      ++summary.conflicts;
      break;
    case CatalogManifestAction::Invalid:
      ++summary.invalid;
      break;
  }
}

}  // namespace

bool CatalogManifestReport::importable() const noexcept {
  return summary.conflicts == 0 && summary.invalid == 0;
}

bool CatalogManifestReport::hasChanges() const noexcept {
  return summary.create != 0;
}

CatalogManifest loadCatalogManifest(const std::filesystem::path& path) {
  try {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      throw std::runtime_error("Unable to read catalog manifest '" +
                               path.string() + "'");
    }
    const auto document = Json::parse(input);
    if (!document.is_object()) {
      throw std::runtime_error("catalog manifest root must be an object");
    }
    std::vector<std::string> root_issues;
    rejectUnknownFields(
        document,
        {"format", "version", "competitions", "seasons", "teams", "players",
         "matches"},
        root_issues);
    if (!root_issues.empty()) {
      throw std::runtime_error(joinedIssues(root_issues));
    }
    if (!document.contains("format") ||
        !document.at("format").is_string() ||
        document.at("format").get<std::string>() != kManifestFormat) {
      throw std::runtime_error(
          "catalog manifest format marker is not recognized");
    }
    if (!document.contains("version") ||
        !document.at("version").is_number_unsigned()) {
      throw std::runtime_error(
          "catalog manifest version must be an unsigned integer");
    }
    const auto version = document.at("version").get<std::uint32_t>();
    if (version != kCatalogManifestVersion) {
      throw std::runtime_error("unsupported catalog manifest version " +
                               std::to_string(version));
    }

    CatalogManifest manifest;
    manifest.version = version;
    std::size_t sequence{};
    readEntries(document, "competitions", CatalogEntityType::Competition,
                manifest.entries, sequence);
    readEntries(document, "seasons", CatalogEntityType::Season,
                manifest.entries, sequence);
    readEntries(document, "teams", CatalogEntityType::Team,
                manifest.entries, sequence);
    readEntries(document, "players", CatalogEntityType::Player,
                manifest.entries, sequence);
    readEntries(document, "matches", CatalogEntityType::Match,
                manifest.entries, sequence);
    return manifest;
  } catch (const nlohmann::json::exception& error) {
    throw std::runtime_error("Invalid catalog manifest '" + path.string() +
                             "': malformed JSON: " + error.what());
  } catch (const std::runtime_error& error) {
    const std::string message = error.what();
    if (message.starts_with("Unable to read catalog manifest '") ||
        message.starts_with("Invalid catalog manifest '")) {
      throw;
    }
    throw std::runtime_error("Invalid catalog manifest '" + path.string() +
                             "': " + message);
  }
}

CatalogManifestImportPlan planCatalogManifestImport(
    const CatalogManifest& manifest, const MatchReviewStore& current_store,
    std::chrono::sys_seconds recorded_at) {
  CatalogManifestImportPlan plan;
  plan.resulting_store = current_store;
  plan.report.manifest_version = manifest.version;
  plan.report.base_revision = current_store.revision();

  std::map<std::pair<CatalogEntityType, Identifier>, std::size_t>
      entity_counts;
  std::map<MappingKey, std::vector<MappingLocation>> mapping_locations;
  for (std::size_t entry_index = 0; entry_index < manifest.entries.size();
       ++entry_index) {
    const auto& entry = manifest.entries[entry_index];
    ++entity_counts[{entry.entity_type, entry.canonical_id}];
    for (std::size_t mapping_index = 0;
         mapping_index < entry.mappings.size(); ++mapping_index) {
      const auto& mapping = entry.mappings[mapping_index];
      mapping_locations[{entry.entity_type, mapping.provider,
                         mapping.provider_id, mapping.provider_match_id}]
          .push_back({entry_index, mapping_index});
    }
  }

  std::vector<std::optional<CatalogManifestAction>> entity_actions(
      manifest.entries.size());
  const std::array application_order{
      CatalogEntityType::Competition, CatalogEntityType::Team,
      CatalogEntityType::Player, CatalogEntityType::Season,
      CatalogEntityType::Match};

  for (const auto entity_type : application_order) {
    for (std::size_t entry_index = 0;
         entry_index < manifest.entries.size(); ++entry_index) {
      const auto& entry = manifest.entries[entry_index];
      if (entry.entity_type != entity_type) {
        continue;
      }
      auto name = entryName(entry, plan.resulting_store.catalog());
      if (!entry.issues.empty()) {
        entity_actions[entry_index] = CatalogManifestAction::Invalid;
        plan.report.results.push_back(entityResult(
            entry, std::move(name), CatalogManifestAction::Invalid,
            joinedIssues(entry.issues)));
        continue;
      }
      if (entity_counts.at({entry.entity_type, entry.canonical_id}) > 1) {
        entity_actions[entry_index] = CatalogManifestAction::Invalid;
        plan.report.results.push_back(entityResult(
            entry, std::move(name), CatalogManifestAction::Invalid,
            "duplicate canonical " +
                std::string(catalogEntityTypeName(entry.entity_type)) +
                " ID " + std::to_string(entry.canonical_id)));
        continue;
      }

      try {
        const auto& catalog = plan.resulting_store.catalog();
        if (entry.entity_type == CatalogEntityType::Season) {
          const auto* competition =
              catalog.competition({entry.competition_id});
          if (competition == nullptr) {
            throw std::invalid_argument(
                "season references unknown canonical competition " +
                std::to_string(entry.competition_id));
          }
          if (competition->status != CanonicalEntityStatus::Active) {
            throw std::invalid_argument(
                "season references inactive canonical competition " +
                std::to_string(entry.competition_id));
          }
        }
        const auto expected_match =
            entry.entity_type == CatalogEntityType::Match
                ? std::optional<CanonicalMatch>{
                      manifestMatch(entry, catalog)}
                : std::nullopt;
        if (existingEntityId(catalog, entry.entity_type,
                             entry.canonical_id)) {
          bool matches = false;
          std::string conflict;
          switch (entry.entity_type) {
            case CatalogEntityType::Competition: {
              const auto* existing =
                  catalog.competition({entry.canonical_id});
              matches =
                  existing->status == CanonicalEntityStatus::Active &&
                  existing->name == entry.name;
              conflict = "existing canonical competition metadata differs";
              break;
            }
            case CatalogEntityType::Season: {
              const auto* existing = catalog.season({entry.canonical_id});
              matches =
                  existing->status == CanonicalEntityStatus::Active &&
                  existing->competition_id.value == entry.competition_id &&
                  existing->name == entry.name;
              conflict = "existing canonical season metadata differs";
              break;
            }
            case CatalogEntityType::Team: {
              const auto* existing = catalog.team({entry.canonical_id});
              matches =
                  existing->status == CanonicalEntityStatus::Active &&
                  existing->name == entry.name;
              conflict = "existing canonical team metadata differs";
              break;
            }
            case CatalogEntityType::Player: {
              const auto* existing = catalog.player({entry.canonical_id});
              matches =
                  existing->status == CanonicalEntityStatus::Active &&
                  existing->name == entry.name;
              conflict = "existing canonical player metadata differs";
              break;
            }
            case CatalogEntityType::Match: {
              matches = sameMatch(*catalog.match({entry.canonical_id}),
                                  *expected_match);
              conflict = "existing canonical match metadata differs";
              break;
            }
          }
          entity_actions[entry_index] =
              matches ? CatalogManifestAction::Unchanged
                      : CatalogManifestAction::Conflict;
          plan.report.results.push_back(entityResult(
              entry, std::move(name), *entity_actions[entry_index],
              matches ? "canonical metadata already matches"
                      : std::move(conflict)));
          continue;
        }

        auto provenance =
            reviewProvenance(entry.provenance, recorded_at);
        switch (entry.entity_type) {
          case CatalogEntityType::Competition:
            plan.resulting_store.addCompetition(
                {{entry.canonical_id}, entry.name}, std::move(provenance));
            break;
          case CatalogEntityType::Season:
            plan.resulting_store.addSeason(
                {{entry.canonical_id}, {entry.competition_id}, entry.name},
                std::move(provenance));
            break;
          case CatalogEntityType::Team:
            plan.resulting_store.addTeam(
                {{entry.canonical_id}, entry.name}, std::move(provenance));
            break;
          case CatalogEntityType::Player:
            plan.resulting_store.addPlayer(
                {{entry.canonical_id}, entry.name}, std::move(provenance));
            break;
          case CatalogEntityType::Match:
            plan.resulting_store.addMatch(
                *expected_match, std::move(provenance));
            break;
        }
        entity_actions[entry_index] = CatalogManifestAction::Create;
        plan.report.results.push_back(entityResult(
            entry, std::move(name), CatalogManifestAction::Create));
      } catch (const std::invalid_argument& error) {
        entity_actions[entry_index] = CatalogManifestAction::Invalid;
        plan.report.results.push_back(entityResult(
            entry, std::move(name), CatalogManifestAction::Invalid,
            error.what()));
      }
    }
  }

  for (std::size_t entry_index = 0;
       entry_index < manifest.entries.size(); ++entry_index) {
    const auto& entry = manifest.entries[entry_index];
    const auto name = entryName(entry, plan.resulting_store.catalog());
    for (const auto& mapping : entry.mappings) {
      if (!mapping.issues.empty()) {
        plan.report.results.push_back(mappingResult(
            entry, mapping, name, CatalogManifestAction::Invalid,
            joinedIssues(mapping.issues)));
        continue;
      }
      const MappingKey key{entry.entity_type, mapping.provider,
                           mapping.provider_id,
                           mapping.provider_match_id};
      const auto& locations = mapping_locations.at(key);
      if (locations.size() > 1) {
        std::set<Identifier> targets;
        for (const auto& location : locations) {
          targets.insert(
              manifest.entries[location.entry_index].canonical_id);
        }
        const bool collision = targets.size() > 1;
        plan.report.results.push_back(mappingResult(
            entry, mapping, name,
            collision ? CatalogManifestAction::Conflict
                      : CatalogManifestAction::Invalid,
            collision
                ? "provider identity maps to multiple canonical " +
                      std::string(catalogEntityTypeName(entry.entity_type)) +
                      " entities in the manifest"
                : "duplicate provider mapping in the manifest"));
        continue;
      }
      const auto existing = resolveMapping(
          plan.resulting_store.catalog(), key);
      if (existing) {
        if (*existing == entry.canonical_id) {
          plan.report.results.push_back(mappingResult(
              entry, mapping, name, CatalogManifestAction::Unchanged,
              "provider mapping already matches"));
        } else {
          plan.report.results.push_back(mappingResult(
              entry, mapping, name, CatalogManifestAction::Conflict,
              "provider identity already maps to canonical " +
                  std::string(catalogEntityTypeName(entry.entity_type)) +
                  " " + std::to_string(*existing)));
        }
        continue;
      }
      if (!entity_actions[entry_index] ||
          (*entity_actions[entry_index] != CatalogManifestAction::Create &&
           *entity_actions[entry_index] !=
               CatalogManifestAction::Unchanged)) {
        plan.report.results.push_back(mappingResult(
            entry, mapping, name, CatalogManifestAction::Invalid,
            "provider mapping target canonical entry is not valid"));
        continue;
      }
      try {
        applyMapping(plan.resulting_store, entry, mapping, recorded_at);
        plan.report.results.push_back(mappingResult(
            entry, mapping, name, CatalogManifestAction::Create));
      } catch (const std::invalid_argument& error) {
        plan.report.results.push_back(mappingResult(
            entry, mapping, name, CatalogManifestAction::Invalid,
            error.what()));
      }
    }
  }

  std::ranges::sort(
      plan.report.results,
      [](const CatalogManifestResult& left,
         const CatalogManifestResult& right) {
        return std::tuple{entityRank(left.entity_type), left.canonical_id,
                          left.provider.has_value(), left.provider,
                          left.provider_id, left.provider_match_id,
                          left.sequence} <
               std::tuple{entityRank(right.entity_type), right.canonical_id,
                          right.provider.has_value(), right.provider,
                          right.provider_id, right.provider_match_id,
                          right.sequence};
      });
  for (const auto& result : plan.report.results) {
    addSummary(plan.report.summary, result.action);
  }
  if (plan.report.importable()) {
    plan.report.planned_revision = plan.resulting_store.revision();
  } else {
    plan.resulting_store = current_store;
    plan.report.planned_revision = current_store.revision();
  }
  return plan;
}

std::string_view catalogManifestActionName(
    CatalogManifestAction action) noexcept {
  switch (action) {
    case CatalogManifestAction::Create:
      return "create";
    case CatalogManifestAction::Unchanged:
      return "unchanged";
    case CatalogManifestAction::Conflict:
      return "conflict";
    case CatalogManifestAction::Invalid:
      return "invalid";
  }
  return "unknown";
}

}  // namespace emberdb
