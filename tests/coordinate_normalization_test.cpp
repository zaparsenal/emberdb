#include "emberdb/common/coordinate_normalization.h"

#include <limits>
#include <optional>
#include <stdexcept>

#include <gtest/gtest.h>

namespace {

constexpr emberdb::PitchDimensions kStatsBombPitch{120.0, 80.0};

TEST(CoordinateNormalizationTest, MapsCornersAndCenterToCanonicalPitch) {
  EXPECT_EQ(emberdb::normalizeCoordinate(
                {0.0, 0.0}, kStatsBombPitch,
                emberdb::AttackingDirection::LeftToRight),
            (emberdb::Coordinate{0.0, 0.0}));
  EXPECT_EQ(emberdb::normalizeCoordinate(
                {120.0, 80.0}, kStatsBombPitch,
                emberdb::AttackingDirection::LeftToRight),
            (emberdb::Coordinate{100.0, 100.0}));
  EXPECT_EQ(emberdb::normalizeCoordinate(
                {60.0, 40.0}, kStatsBombPitch,
                emberdb::AttackingDirection::LeftToRight),
            (emberdb::Coordinate{50.0, 50.0}));
}

TEST(CoordinateNormalizationTest, FlipsRightToLeftAttacksAlongTheLengthAxis) {
  EXPECT_EQ(emberdb::normalizeCoordinate(
                {0.0, 20.0}, kStatsBombPitch,
                emberdb::AttackingDirection::RightToLeft),
            (emberdb::Coordinate{100.0, 25.0}));
  EXPECT_EQ(emberdb::normalizeCoordinate(
                {120.0, 20.0}, kStatsBombPitch,
                emberdb::AttackingDirection::RightToLeft),
            (emberdb::Coordinate{0.0, 25.0}));
}

TEST(CoordinateNormalizationTest, PreservesMissingCoordinates) {
  const std::optional<emberdb::Coordinate> missing;
  EXPECT_FALSE(emberdb::normalizeCoordinate(
      missing, kStatsBombPitch, emberdb::AttackingDirection::LeftToRight));
}

TEST(CoordinateNormalizationTest, RejectsCoordinatesOutsideSourceBounds) {
  EXPECT_THROW(static_cast<void>(emberdb::normalizeCoordinate(
                   {-0.1, 40.0}, kStatsBombPitch,
                   emberdb::AttackingDirection::LeftToRight)),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(emberdb::normalizeCoordinate(
                   {120.1, 40.0}, kStatsBombPitch,
                   emberdb::AttackingDirection::LeftToRight)),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(emberdb::normalizeCoordinate(
                   {60.0, 80.1}, kStatsBombPitch,
                   emberdb::AttackingDirection::LeftToRight)),
               std::invalid_argument);
}

TEST(CoordinateNormalizationTest, RejectsNonFiniteCoordinatesAndDimensions) {
  EXPECT_THROW(static_cast<void>(emberdb::normalizeCoordinate(
                   {std::numeric_limits<double>::quiet_NaN(), 40.0},
                   kStatsBombPitch, emberdb::AttackingDirection::LeftToRight)),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(emberdb::normalizeCoordinate(
                   {60.0, std::numeric_limits<double>::infinity()},
                   kStatsBombPitch, emberdb::AttackingDirection::LeftToRight)),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(emberdb::normalizeCoordinate(
                   {0.0, 0.0}, {0.0, 80.0},
                   emberdb::AttackingDirection::LeftToRight)),
               std::invalid_argument);
}

TEST(CoordinateNormalizationTest, ValidatesCanonicalCoordinates) {
  EXPECT_NO_THROW(emberdb::validateCanonicalCoordinate({0.0, 100.0}));
  EXPECT_NO_THROW(emberdb::validateCanonicalCoordinate({100.0, 0.0}));
  EXPECT_THROW(emberdb::validateCanonicalCoordinate({100.1, 50.0}),
               std::invalid_argument);
  EXPECT_THROW(emberdb::validateCanonicalCoordinate({50.0, -0.1}),
               std::invalid_argument);
}

}  // namespace
