#include "emberdb/persistence/match_review_file.h"

#include <cstdint>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace emberdb {
namespace {

using Json = nlohmann::json;
constexpr std::string_view kFormatName = "emberdb-match-review";

class TemporaryFile {
 public:
  explicit TemporaryFile(std::filesystem::path path) : path_(std::move(path)) {}
  ~TemporaryFile() {
    if (!committed_) {
      std::error_code error;
      std::filesystem::remove(path_, error);
    }
  }
  void commit() noexcept { committed_ = true; }

 private:
  std::filesystem::path path_;
  bool committed_{};
};

[[noreturn]] void invalidFile(const std::filesystem::path& path,
                              const std::string& detail) {
  throw std::runtime_error("Invalid match review file '" + path.string() +
                           "': " + detail);
}

Json optionalString(const std::optional<std::string>& value) {
  return value ? Json(*value) : Json(nullptr);
}

Json providerMatchJson(const ProviderMatchReference& reference) {
  return {{"provider", reference.provider}, {"id", reference.id}};
}

Json providerTeamJson(const ProviderTeamReference& reference) {
  return {{"provider", reference.provider},
          {"id", reference.id},
          {"match_id", optionalString(reference.match_id)}};
}

Json providerPlayerJson(const ProviderPlayerReference& reference) {
  return {{"provider", reference.provider},
          {"id", reference.id},
          {"match_id", optionalString(reference.match_id)}};
}

std::string_view evidenceStatusName(ReconciliationStatus status) {
  switch (status) {
    case ReconciliationStatus::Missing:
      return "missing";
    case ReconciliationStatus::Agreeing:
      return "agreeing";
    case ReconciliationStatus::Conflicting:
      return "conflicting";
    case ReconciliationStatus::Uncertain:
      return "uncertain";
  }
  return "unknown";
}

Json evidenceJson(const MatchFieldEvidence& evidence) {
  return {{"status", evidenceStatusName(evidence.status)},
          {"left_source", evidence.left_source},
          {"right_source", evidence.right_source},
          {"left_value", optionalString(evidence.left_value)},
          {"right_value", optionalString(evidence.right_value)},
          {"canonical_value", optionalString(evidence.canonical_value)}};
}

Json catalogJson(const CanonicalIdentityCatalog& catalog) {
  Json result = {{"teams", Json::array()},
                 {"players", Json::array()},
                 {"matches", Json::array()},
                 {"team_mappings", Json::array()},
                 {"player_mappings", Json::array()},
                 {"match_mappings", Json::array()}};
  for (const auto& [id, team] : catalog.teams()) {
    result["teams"].push_back({{"id", id.value}, {"name", team.name}});
  }
  for (const auto& [id, player] : catalog.players()) {
    result["players"].push_back({{"id", id.value}, {"name", player.name}});
  }
  for (const auto& [id, match] : catalog.matches()) {
    result["matches"].push_back(
        {{"id", id.value},
         {"competition", match.competition},
         {"season", match.season},
         {"kickoff_seconds",
          match.kickoff ? Json(match.kickoff->time_since_epoch().count())
                        : Json(nullptr)},
         {"home_team_id", match.home_team_id.value},
         {"away_team_id", match.away_team_id.value},
         {"home_score", match.home_score ? Json(*match.home_score) : Json(nullptr)},
         {"away_score", match.away_score ? Json(*match.away_score) : Json(nullptr)}});
  }
  for (const auto& [reference, id] : catalog.teamMappings()) {
    auto mapping = providerTeamJson(reference);
    mapping["canonical_id"] = id.value;
    result["team_mappings"].push_back(std::move(mapping));
  }
  for (const auto& [reference, id] : catalog.playerMappings()) {
    auto mapping = providerPlayerJson(reference);
    mapping["canonical_id"] = id.value;
    result["player_mappings"].push_back(std::move(mapping));
  }
  for (const auto& [reference, id] : catalog.matchMappings()) {
    auto mapping = providerMatchJson(reference);
    mapping["canonical_id"] = id.value;
    result["match_mappings"].push_back(std::move(mapping));
  }
  return result;
}

Json candidateJson(const MatchCandidateRecord& record) {
  const auto& reconciliation = record.reconciliation;
  return {{"id", record.id},
          {"status", matchCandidateStatusName(record.status)},
          {"accepted_match_id",
           record.accepted_match_id ? Json(record.accepted_match_id->value)
                                    : Json(nullptr)},
          {"rejection_reason", optionalString(record.rejection_reason)},
          {"left_match", providerMatchJson(reconciliation.left_match)},
          {"right_match", providerMatchJson(reconciliation.right_match)},
          {"confidence", reconciliation.confidence},
          {"is_candidate", reconciliation.is_candidate},
          {"evidence",
           {{"competition", evidenceJson(reconciliation.competition)},
            {"season", evidenceJson(reconciliation.season)},
            {"kickoff", evidenceJson(reconciliation.kickoff)},
            {"home_team", evidenceJson(reconciliation.home_team)},
            {"away_team", evidenceJson(reconciliation.away_team)},
            {"score", evidenceJson(reconciliation.score)}}}};
}

std::optional<std::string> readOptionalString(const Json& object,
                                              std::string_view field) {
  const auto& value = object.at(field);
  return value.is_null() ? std::nullopt
                         : std::optional<std::string>(value.get<std::string>());
}

template <typename Value>
std::optional<Value> readOptionalNumber(const Json& object,
                                        std::string_view field) {
  const auto& value = object.at(field);
  return value.is_null() ? std::nullopt
                         : std::optional<Value>(value.get<Value>());
}

ProviderMatchReference readProviderMatch(const Json& value) {
  return {value.at("provider").get<std::string>(), value.at("id").get<std::string>()};
}

ProviderTeamReference readProviderTeam(const Json& value) {
  return {value.at("provider").get<std::string>(), value.at("id").get<std::string>(),
          readOptionalString(value, "match_id")};
}

ProviderPlayerReference readProviderPlayer(const Json& value) {
  return {value.at("provider").get<std::string>(), value.at("id").get<std::string>(),
          readOptionalString(value, "match_id")};
}

ReconciliationStatus readEvidenceStatus(const std::string& value) {
  if (value == "missing") return ReconciliationStatus::Missing;
  if (value == "agreeing") return ReconciliationStatus::Agreeing;
  if (value == "conflicting") return ReconciliationStatus::Conflicting;
  if (value == "uncertain") return ReconciliationStatus::Uncertain;
  throw std::invalid_argument("unknown evidence status '" + value + "'");
}

MatchCandidateStatus readCandidateStatus(const std::string& value) {
  if (value == "unresolved") return MatchCandidateStatus::Unresolved;
  if (value == "accepted") return MatchCandidateStatus::Accepted;
  if (value == "rejected") return MatchCandidateStatus::Rejected;
  throw std::invalid_argument("unknown candidate status '" + value + "'");
}

MatchFieldEvidence readEvidence(const Json& value) {
  return {readEvidenceStatus(value.at("status").get<std::string>()),
          value.at("left_source").get<std::string>(),
          value.at("right_source").get<std::string>(),
          readOptionalString(value, "left_value"),
          readOptionalString(value, "right_value"),
          readOptionalString(value, "canonical_value")};
}

CanonicalIdentityCatalog readCatalog(const Json& value) {
  CanonicalIdentityCatalog catalog;
  for (const auto& team : value.at("teams")) {
    catalog.addTeam({{team.at("id").get<Identifier>()},
                     team.at("name").get<std::string>()});
  }
  for (const auto& player : value.at("players")) {
    catalog.addPlayer({{player.at("id").get<Identifier>()},
                       player.at("name").get<std::string>()});
  }
  for (const auto& match : value.at("matches")) {
    const auto kickoff_count =
        readOptionalNumber<std::int64_t>(match, "kickoff_seconds");
    const auto kickoff = kickoff_count
                             ? std::optional<std::chrono::sys_seconds>{
                                   std::chrono::sys_seconds{
                                       std::chrono::seconds{*kickoff_count}}}
                             : std::nullopt;
    catalog.addMatch(
        {{match.at("id").get<Identifier>()},
         match.at("competition").get<std::string>(),
         match.at("season").get<std::string>(),
         kickoff,
         {match.at("home_team_id").get<Identifier>()},
         {match.at("away_team_id").get<Identifier>()},
         readOptionalNumber<std::int32_t>(match, "home_score"),
         readOptionalNumber<std::int32_t>(match, "away_score")});
  }
  for (const auto& mapping : value.at("team_mappings")) {
    catalog.mapTeam(readProviderTeam(mapping),
                    {mapping.at("canonical_id").get<Identifier>()});
  }
  for (const auto& mapping : value.at("player_mappings")) {
    catalog.mapPlayer(readProviderPlayer(mapping),
                      {mapping.at("canonical_id").get<Identifier>()});
  }
  for (const auto& mapping : value.at("match_mappings")) {
    catalog.mapMatch(readProviderMatch(mapping),
                     {mapping.at("canonical_id").get<Identifier>()});
  }
  return catalog;
}

MatchCandidateRecord readCandidate(const Json& value) {
  const auto& evidence = value.at("evidence");
  MatchReconciliation reconciliation;
  reconciliation.left_match = readProviderMatch(value.at("left_match"));
  reconciliation.right_match = readProviderMatch(value.at("right_match"));
  reconciliation.competition = readEvidence(evidence.at("competition"));
  reconciliation.season = readEvidence(evidence.at("season"));
  reconciliation.kickoff = readEvidence(evidence.at("kickoff"));
  reconciliation.home_team = readEvidence(evidence.at("home_team"));
  reconciliation.away_team = readEvidence(evidence.at("away_team"));
  reconciliation.score = readEvidence(evidence.at("score"));
  reconciliation.confidence = value.at("confidence").get<double>();
  reconciliation.is_candidate = value.at("is_candidate").get<bool>();
  const auto accepted = value.at("accepted_match_id").is_null()
                            ? std::optional<CanonicalMatchId>{}
                            : std::optional<CanonicalMatchId>{{
                                  value.at("accepted_match_id").get<Identifier>()}};
  return {value.at("id").get<std::uint64_t>(), std::move(reconciliation),
          readCandidateStatus(value.at("status").get<std::string>()), accepted,
          readOptionalString(value, "rejection_reason")};
}

Json storeJson(const MatchReviewStore& store) {
  Json candidates = Json::array();
  for (const auto* candidate : store.candidates()) {
    candidates.push_back(candidateJson(*candidate));
  }
  return {{"format", kFormatName},
          {"version", kMatchReviewFileFormatVersion},
          {"catalog", catalogJson(store.catalog())},
          {"candidates", std::move(candidates)}};
}

}  // namespace

void saveMatchReviewStore(const MatchReviewStore& store,
                          const std::filesystem::path& path) {
  auto temporary_path = path;
  temporary_path += ".tmp";
  std::error_code error;
  if (std::filesystem::exists(temporary_path, error)) {
    throw std::runtime_error("Temporary match review output already exists '" +
                             temporary_path.string() + "'");
  }
  if (error) {
    throw std::runtime_error("Unable to inspect temporary match review path '" +
                             temporary_path.string() + "': " + error.message());
  }
  TemporaryFile temporary(temporary_path);
  std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("Unable to create match review file '" + path.string() +
                             "'");
  }
  output << storeJson(store).dump(2) << '\n';
  output.close();
  if (!output) {
    throw std::runtime_error("Unable to write complete match review file '" +
                             path.string() + "'");
  }
  std::filesystem::rename(temporary_path, path, error);
  if (error) {
    throw std::runtime_error("Unable to finalize match review file '" + path.string() +
                             "': " + error.message());
  }
  temporary.commit();
}

MatchReviewStore loadMatchReviewStore(const std::filesystem::path& path) {
  try {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      throw std::runtime_error("Unable to read match review file '" + path.string() +
                               "'");
    }
    const auto document = Json::parse(input);
    if (document.at("format").get<std::string>() != kFormatName) {
      invalidFile(path, "format marker is not recognized");
    }
    const auto version = document.at("version").get<std::uint32_t>();
    if (version != kMatchReviewFileFormatVersion) {
      invalidFile(path, "unsupported format version " + std::to_string(version));
    }
    auto catalog = readCatalog(document.at("catalog"));
    std::vector<MatchCandidateRecord> candidates;
    for (const auto& candidate : document.at("candidates")) {
      candidates.push_back(readCandidate(candidate));
    }
    return MatchReviewStore::restore(std::move(catalog), std::move(candidates));
  } catch (const nlohmann::json::exception& error) {
    invalidFile(path, error.what());
  } catch (const std::invalid_argument& error) {
    invalidFile(path, error.what());
  }
}

}  // namespace emberdb
