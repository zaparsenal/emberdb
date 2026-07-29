#pragma once

#include <cstdint>
#include <filesystem>

#include "emberdb/reconciliation/match_review.h"

namespace emberdb {

inline constexpr std::uint32_t kMatchReviewFileFormatVersion = 2;

void saveMatchReviewStore(const MatchReviewStore& store,
                          const std::filesystem::path& path);

[[nodiscard]] MatchReviewStore loadMatchReviewStore(
    const std::filesystem::path& path);

}  // namespace emberdb
