#pragma once

#include <cstdint>
#include <filesystem>

#include "emberdb/storage/football_event_table.h"

namespace emberdb {

inline constexpr std::uint16_t kFootballEventFileFormatVersion = 2;

void saveFootballEventTable(const FootballEventTable& table,
                            const std::filesystem::path& path);

[[nodiscard]] FootballEventTable loadFootballEventTable(
    const std::filesystem::path& path);

}  // namespace emberdb
