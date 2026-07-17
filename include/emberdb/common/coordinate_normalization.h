#pragma once

#include <optional>

#include "emberdb/common/football_event.h"

namespace emberdb {

inline constexpr double kCanonicalPitchLength = 100.0;
inline constexpr double kCanonicalPitchWidth = 100.0;

struct PitchDimensions {
  double length{};
  double width{};

  bool operator==(const PitchDimensions&) const = default;
};

enum class AttackingDirection { LeftToRight, RightToLeft };

void validateCanonicalCoordinate(Coordinate coordinate);

[[nodiscard]] Coordinate normalizeCoordinate(
    Coordinate source, PitchDimensions source_pitch,
    AttackingDirection attacking_direction);

[[nodiscard]] std::optional<Coordinate> normalizeCoordinate(
    const std::optional<Coordinate>& source, PitchDimensions source_pitch,
    AttackingDirection attacking_direction);

}  // namespace emberdb
