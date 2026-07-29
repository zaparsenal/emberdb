#include "cli/executor.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "cli/command_parser.h"
#include "cli/renderer.h"
#include "emberdb/ingestion/event_provider_adapter.h"
#include "emberdb/ingestion/metrica_event_adapter.h"
#include "emberdb/ingestion/provider_metadata.h"
#include "emberdb/ingestion/statsbomb_event_adapter.h"
#include "emberdb/ingestion/statsbomb_metadata_adapter.h"
#include "emberdb/ingestion/wyscout_event_adapter.h"
#include "emberdb/ingestion/wyscout_metadata_adapter.h"
#include "emberdb/persistence/match_review_file.h"
#include "emberdb/query/aggregation_query.h"
#include "emberdb/query/event_query.h"
#include "emberdb/reconciliation/entity_reconciliation.h"
#include "emberdb/reconciliation/match_reconciliation.h"
#include "emberdb/storage/football_event_file.h"
#include "emberdb/storage/football_event_table.h"

namespace emberdb::cli {
namespace {

std::vector<ProviderMatchMetadata> loadProviderMatches(
    const std::string& provider, const std::filesystem::path& input) {
  if (provider == "statsbomb") {
    return StatsBombMetadataAdapter{}.loadMatches(input).matches;
  }
  if (provider == "wyscout") {
    return WyscoutMetadataAdapter{}.loadMatches(input).matches;
  }
  throw std::runtime_error("Unsupported metadata provider '" + provider +
                           "'; expected statsbomb or wyscout");
}

FootballEventTable importTable(const Options& options) {
  std::unique_ptr<EventProviderAdapter> adapter;
  if (options.provider == "statsbomb") {
    adapter = std::make_unique<StatsBombEventAdapter>();
  } else if (options.provider == "metrica") {
    adapter = std::make_unique<MetricaEventAdapter>();
  } else if (options.provider == "wyscout") {
    adapter = std::make_unique<WyscoutEventAdapter>();
  } else {
    throw std::runtime_error("Unsupported provider '" + options.provider + "'");
  }
  const auto events = adapter->loadEvents(
      options.input, {options.match_id, options.home_first_half_direction});
  FootballEventTable table;
  for (const auto& event : events) {
    table.append(event);
  }
  return table;
}

ReviewProvenance reviewProvenance(const Options& options) {
  return {options.actor, options.source, options.reason,
          std::chrono::time_point_cast<std::chrono::seconds>(
              std::chrono::system_clock::now())};
}

IdentityEntityType identityEntityType(CatalogEntityType entity_type) {
  switch (entity_type) {
    case CatalogEntityType::Competition:
      return IdentityEntityType::Competition;
    case CatalogEntityType::Season:
      return IdentityEntityType::Season;
    case CatalogEntityType::Team:
      return IdentityEntityType::Team;
    case CatalogEntityType::Player:
      return IdentityEntityType::Player;
    case CatalogEntityType::Match:
      throw std::runtime_error(
          "Internal error: matches are not catalog entity candidates");
  }
  throw std::runtime_error("Internal error: unknown catalog entity type");
}

ProviderMetadata loadProviderEntities(
    const std::string& provider, IdentityEntityType entity_type,
    const std::filesystem::path& input) {
  if (provider == "statsbomb") {
    if (entity_type == IdentityEntityType::Player) {
      return StatsBombMetadataAdapter{}.loadLineups(input);
    }
    return StatsBombMetadataAdapter{}.loadMatches(input);
  }
  if (provider == "wyscout") {
    const WyscoutMetadataAdapter adapter;
    switch (entity_type) {
      case IdentityEntityType::Competition:
        return adapter.loadCompetitions(input);
      case IdentityEntityType::Season:
        return adapter.loadMatches(input);
      case IdentityEntityType::Team:
        return adapter.loadTeams(input);
      case IdentityEntityType::Player:
        return adapter.loadPlayers(input);
    }
  }
  throw std::runtime_error("Unsupported metadata provider '" + provider +
                           "'; expected statsbomb or wyscout");
}

bool isEntityCandidateCommand(Command command) {
  return command == Command::EntityCandidateGenerate ||
         command == Command::EntityCandidateList ||
         command == Command::EntityCandidateInspect ||
         command == Command::EntityCandidateAccept ||
         command == Command::EntityCandidateReject;
}

bool isCatalogCommand(Command command) {
  return command == Command::CatalogInit ||
         command == Command::CatalogAdd ||
         command == Command::CatalogMap ||
         command == Command::CatalogRename ||
         command == Command::CatalogDeprecate ||
         command == Command::CatalogMerge ||
         command == Command::CatalogList ||
         command == Command::CatalogHistory ||
         isEntityCandidateCommand(command);
}

void runEntityCandidateCommand(const Options& options,
                               std::ostream& output) {
  auto store = loadMatchReviewStore(options.review);
  const auto loaded_revision = store.revision();
  if (options.command == Command::EntityCandidateGenerate) {
    const auto entity_type = identityEntityType(*options.catalog_entity);
    const auto metadata =
        loadProviderEntities(options.provider, entity_type, options.input);
    const auto generated = findEntityCandidates(
        metadata, entity_type, store.catalog(), options.input.string());
    const auto existing_count = store.entityCandidates().size();
    const auto ids = store.addEntityCandidates(generated);
    const auto added_count =
        store.entityCandidates().size() - existing_count;
    if (added_count != 0) {
      saveMatchReviewStore(store, options.review, loaded_revision);
    }
    printEntityCandidateGeneration(output, generated.size(), added_count, ids);
    return;
  }
  if (options.command == Command::EntityCandidateList) {
    const auto entity_type =
        options.catalog_entity
            ? std::optional<IdentityEntityType>{
                  identityEntityType(*options.catalog_entity)}
            : std::nullopt;
    printEntityCandidateList(
        output,
        store.entityCandidates(options.candidate_status, entity_type));
    return;
  }
  const auto* candidate = store.entityCandidate(options.candidate_id);
  if (candidate == nullptr) {
    throw std::runtime_error(
        "Unknown entity candidate " +
        std::to_string(options.candidate_id));
  }
  if (options.command == Command::EntityCandidateInspect) {
    printEntityCandidateInspection(output, *candidate);
    return;
  }
  const auto provenance = reviewProvenance(options);
  if (options.command == Command::EntityCandidateAccept) {
    store.acceptEntityCandidate(options.candidate_id, provenance);
    saveMatchReviewStore(store, options.review, loaded_revision);
    printEntityCandidateAccepted(output, *store.entityCandidate(
                                             options.candidate_id));
    return;
  }
  store.rejectEntityCandidate(options.candidate_id, provenance);
  saveMatchReviewStore(store, options.review, loaded_revision);
  printEntityCandidateRejected(output, options.candidate_id, options.reason);
}

void runCatalogCommand(const Options& options, std::ostream& output) {
  if (isEntityCandidateCommand(options.command)) {
    runEntityCandidateCommand(options, output);
    return;
  }
  if (options.command == Command::CatalogInit) {
    const MatchReviewStore store;
    createMatchReviewStore(store, options.review);
    printCatalogCreated(output, options.review);
    return;
  }
  auto store = loadMatchReviewStore(options.review);
  if (options.command == Command::CatalogList) {
    printCatalogSummary(output, store);
    return;
  }
  if (options.command == Command::CatalogHistory) {
    printCatalogHistory(output, store.catalogChanges());
    return;
  }

  const auto loaded_revision = store.revision();
  const auto previous_change_count = store.catalogChanges().size();
  auto provenance = reviewProvenance(options);
  if (options.command == Command::CatalogRename) {
    store.renameCatalogEntity(*options.catalog_entity, options.canonical_id,
                              options.name, std::move(provenance));
  } else if (options.command == Command::CatalogDeprecate) {
    store.deprecateCatalogEntity(*options.catalog_entity,
                                 options.canonical_id,
                                 std::move(provenance));
  } else if (options.command == Command::CatalogMerge) {
    store.mergeCatalogEntity(*options.catalog_entity, options.canonical_id,
                             options.target_canonical_id,
                             std::move(provenance));
  } else if (options.command == Command::CatalogAdd) {
    switch (*options.catalog_entity) {
      case CatalogEntityType::Competition:
        store.addCompetition({{options.canonical_id}, options.name},
                             std::move(provenance));
        break;
      case CatalogEntityType::Season:
        store.addSeason({{options.canonical_id}, {options.competition_id},
                         options.name},
                        std::move(provenance));
        break;
      case CatalogEntityType::Team:
        store.addTeam({{options.canonical_id}, options.name},
                      std::move(provenance));
        break;
      case CatalogEntityType::Player:
        store.addPlayer({{options.canonical_id}, options.name},
                        std::move(provenance));
        break;
      case CatalogEntityType::Match: {
        const auto kickoff =
            options.kickoff_seconds
                ? std::optional<std::chrono::sys_seconds>{
                      std::chrono::sys_seconds{
                          std::chrono::seconds{*options.kickoff_seconds}}}
                : std::nullopt;
        store.addMatch(
            {{options.canonical_id},
             options.competition,
             options.season,
             kickoff,
             {options.home_team_id},
             {options.away_team_id},
             options.home_score,
             options.away_score},
            std::move(provenance));
        break;
      }
    }
  } else {
    switch (*options.catalog_entity) {
      case CatalogEntityType::Competition:
        store.mapCompetition({options.provider, options.provider_id},
                             {options.canonical_id},
                             std::move(provenance));
        break;
      case CatalogEntityType::Season:
        store.mapSeason({options.provider, options.provider_id},
                        {options.canonical_id}, std::move(provenance));
        break;
      case CatalogEntityType::Team:
        store.mapTeam(
            {options.provider, options.provider_id, options.provider_match_id},
            {options.canonical_id}, std::move(provenance));
        break;
      case CatalogEntityType::Player:
        store.mapPlayer(
            {options.provider, options.provider_id, options.provider_match_id},
            {options.canonical_id}, std::move(provenance));
        break;
      case CatalogEntityType::Match:
        throw std::runtime_error(
            "Internal error: catalog map cannot map matches");
    }
  }

  const CatalogChangeRecord* change = nullptr;
  if (store.catalogChanges().size() != previous_change_count) {
    change = &store.catalogChanges().back();
    saveMatchReviewStore(store, options.review, loaded_revision);
  }
  printCatalogMutation(output, store.revision(), change);
}

void runReconciliationCommand(const Options& options, std::ostream& output) {
  auto store = loadMatchReviewStore(options.review);
  const auto loaded_revision = store.revision();
  if (options.command == Command::ReconcileGenerate) {
    const auto left =
        loadProviderMatches(options.left_provider, options.left_input);
    const auto right =
        loadProviderMatches(options.right_provider, options.right_input);
    const auto generated = findMatchCandidates(left, right, store.catalog());
    const auto existing_count = store.candidates().size();
    const auto ids = store.addCandidates(generated);
    const auto added_count = store.candidates().size() - existing_count;
    if (added_count != 0) {
      saveMatchReviewStore(store, options.review, loaded_revision);
    }
    printCandidateGeneration(output, generated.size(), added_count, ids);
    return;
  }
  if (options.command == Command::ReconcileList) {
    printCandidateList(output, store.candidates(options.candidate_status));
    return;
  }

  const auto* candidate = store.candidate(options.candidate_id);
  if (candidate == nullptr) {
    throw std::runtime_error("Unknown match candidate " +
                             std::to_string(options.candidate_id));
  }
  if (options.command == Command::ReconcileInspect) {
    printCandidateInspection(output, *candidate);
    return;
  }
  if (options.command == Command::ReconcileAccept) {
    const auto provenance = reviewProvenance(options);
    store.accept(options.candidate_id, {options.canonical_match_id},
                 provenance);
    saveMatchReviewStore(store, options.review, loaded_revision);
    printCandidateAccepted(output, options.candidate_id,
                           options.canonical_match_id);
    return;
  }
  const auto provenance = reviewProvenance(options);
  store.reject(options.candidate_id, provenance);
  saveMatchReviewStore(store, options.review, loaded_revision);
  printCandidateRejected(output, options.candidate_id, options.reason);
}

}  // namespace

void executeCommand(const Options& options, std::ostream& output) {
  if (isCatalogCommand(options.command)) {
    runCatalogCommand(options, output);
    return;
  }
  if (options.command != Command::Import && options.command != Command::Query) {
    runReconciliationCommand(options, output);
    return;
  }

  const auto table = !options.database.empty()
                         ? loadFootballEventTable(options.database)
                         : importTable(options);
  if (!table.validate()) {
    throw std::runtime_error("Internal error: column lengths are inconsistent");
  }

  if (options.command == Command::Query) {
    if (!options.aggregates.empty()) {
      printAggregationResult(
          output,
          executeAggregationQuery(table, makeAggregationQuery(options)));
    } else {
      printQueryResult(output, executeQuery(table, makeEventQuery(options)));
    }
    return;
  }

  std::optional<std::uintmax_t> database_size;
  if (!options.output.empty()) {
    saveFootballEventTable(table, options.output);
    database_size = std::filesystem::file_size(options.output);
  }
  printImportResult(output, table, options, database_size);
}

}  // namespace emberdb::cli
