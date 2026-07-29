#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

#include "cli/command.h"
#include "emberdb/query/aggregation_query.h"
#include "emberdb/query/event_query.h"
#include "emberdb/reconciliation/catalog_validation.h"
#include "emberdb/reconciliation/match_review.h"
#include "emberdb/storage/football_event_table.h"

namespace emberdb::cli {

void printUsage(std::ostream& output);
void printImportResult(std::ostream& output, const FootballEventTable& table,
                       const Options& options,
                       std::optional<std::uintmax_t> database_size);
void printQueryResult(std::ostream& output, const EventQueryResult& result);
void printAggregationResult(std::ostream& output,
                            const AggregationResult& result);
void printCandidateGeneration(std::ostream& output, std::size_t generated_count,
                              std::size_t added_count,
                              const std::vector<std::uint64_t>& ids);
void printCandidateList(
    std::ostream& output,
    const std::vector<const MatchCandidateRecord*>& candidates);
void printCandidateInspection(std::ostream& output,
                              const MatchCandidateRecord& candidate);
void printCandidateAccepted(std::ostream& output, std::uint64_t candidate_id,
                            Identifier canonical_match_id);
void printCandidateRejected(std::ostream& output, std::uint64_t candidate_id,
                            const std::string& reason);
void printEntityCandidateGeneration(
    std::ostream& output, std::size_t generated_count,
    std::size_t added_count, const std::vector<std::uint64_t>& ids);
void printEntityCandidateList(
    std::ostream& output,
    const std::vector<const EntityCandidateRecord*>& candidates);
void printEntityCandidateInspection(
    std::ostream& output, const EntityCandidateRecord& candidate);
void printEntityCandidateAccepted(
    std::ostream& output, const EntityCandidateRecord& candidate);
void printEntityCandidateRejected(std::ostream& output,
                                  std::uint64_t candidate_id,
                                  const std::string& reason);
void printCatalogCreated(std::ostream& output,
                         const std::filesystem::path& path);
void printCatalogSummary(std::ostream& output,
                         const MatchReviewStore& store);
void printCatalogHistory(
    std::ostream& output,
    const std::vector<CatalogChangeRecord>& catalog_changes);
void printCatalogValidation(std::ostream& output,
                            const CatalogValidationReport& report,
                            std::uint64_t review_revision);
void printCatalogMutation(std::ostream& output, std::uint64_t revision,
                          const CatalogChangeRecord* change);

}  // namespace emberdb::cli
