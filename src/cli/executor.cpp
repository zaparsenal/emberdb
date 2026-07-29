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
    const ReviewProvenance provenance{
        options.actor, options.source, options.reason,
        std::chrono::time_point_cast<std::chrono::seconds>(
            std::chrono::system_clock::now())};
    store.accept(options.candidate_id, {options.canonical_match_id},
                 provenance);
    saveMatchReviewStore(store, options.review, loaded_revision);
    printCandidateAccepted(output, options.candidate_id,
                           options.canonical_match_id);
    return;
  }
  const ReviewProvenance provenance{
      options.actor, options.source, options.reason,
      std::chrono::time_point_cast<std::chrono::seconds>(
          std::chrono::system_clock::now())};
  store.reject(options.candidate_id, provenance);
  saveMatchReviewStore(store, options.review, loaded_revision);
  printCandidateRejected(output, options.candidate_id, options.reason);
}

}  // namespace

void executeCommand(const Options& options, std::ostream& output) {
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
