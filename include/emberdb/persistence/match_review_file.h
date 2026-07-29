#pragma once

#include <cstdint>
#include <filesystem>

#include "emberdb/reconciliation/match_review.h"

namespace emberdb {

inline constexpr std::uint32_t kMatchReviewFileFormatVersion = 4;

void createMatchReviewStore(const MatchReviewStore& store,
                            const std::filesystem::path& path);

void saveMatchReviewStore(const MatchReviewStore& store,
                          const std::filesystem::path& path,
                          std::uint64_t expected_revision);

[[nodiscard]] MatchReviewStore loadMatchReviewStore(
    const std::filesystem::path& path);

}  // namespace emberdb
