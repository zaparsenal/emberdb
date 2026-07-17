#include "emberdb/common/coordinate_normalization.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace emberdb {

Coordinate normalizeCoordinate(Coordinate source, PitchDimensions source_pitch,
                               AttackingDirection attacking_direction) {
  if (!std::isfinite(source_pitch.length) || !std::isfinite(source_pitch.width) ||
      source_pitch.length <= 0.0 || source_pitch.width <= 0.0) {
    throw std::invalid_argument(
        "Source pitch dimensions must be finite and greater than zero");
  }
  if (!std::isfinite(source.x) || !std::isfinite(source.y)) {
    throw std::invalid_argument("Source coordinate values must be finite");
  }
  if (source.x < 0.0 || source.x > source_pitch.length || source.y < 0.0 ||
      source.y > source_pitch.width) {
    throw std::invalid_argument(
        "Source coordinate (" + std::to_string(source.x) + ", " +
        std::to_string(source.y) + ") is outside pitch bounds x=0.." +
        std::to_string(source_pitch.length) + ", y=0.." +
        std::to_string(source_pitch.width));
  }

  auto normalized_x = source.x / source_pitch.length * kCanonicalPitchLength;
  if (attacking_direction == AttackingDirection::RightToLeft) {
    normalized_x = kCanonicalPitchLength - normalized_x;
  }
  return Coordinate{normalized_x,
                    source.y / source_pitch.width * kCanonicalPitchWidth};
}

std::optional<Coordinate> normalizeCoordinate(
    const std::optional<Coordinate>& source, PitchDimensions source_pitch,
    AttackingDirection attacking_direction) {
  if (!source) {
    return std::nullopt;
  }
  return normalizeCoordinate(*source, source_pitch, attacking_direction);
}

}  // namespace emberdb
