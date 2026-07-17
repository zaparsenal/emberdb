#pragma once

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace emberdb::metadata_json {

using Json = nlohmann::json;

inline std::runtime_error recordError(std::string_view provider,
                                      const std::filesystem::path& path,
                                      std::size_t index,
                                      const std::string& message) {
  return std::runtime_error(std::string(provider) + " metadata file '" +
                            path.string() + "' record at index " +
                            std::to_string(index) + ": " + message);
}

inline Json readArray(std::string_view provider,
                      const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Unable to read " + std::string(provider) +
                             " metadata file '" + path.string() + "'");
  }
  Json document;
  try {
    input >> document;
  } catch (const Json::parse_error& error) {
    throw std::runtime_error("Invalid JSON in " + std::string(provider) +
                             " metadata file '" + path.string() + "': " +
                             error.what());
  }
  if (!document.is_array()) {
    throw std::runtime_error("Invalid " + std::string(provider) +
                             " metadata file '" + path.string() +
                             "': top-level JSON must be an array");
  }
  return document;
}

template <typename T>
T required(const Json& object, std::string_view key, std::string_view provider,
           const std::filesystem::path& path, std::size_t index) {
  const auto value = object.find(key);
  if (value == object.end() || value->is_null()) {
    throw recordError(provider, path, index,
                      "missing required field '" + std::string(key) + "'");
  }
  try {
    return value->get<T>();
  } catch (const Json::exception& error) {
    throw recordError(provider, path, index,
                      "invalid field '" + std::string(key) + "': " + error.what());
  }
}

template <typename T>
std::optional<T> optional(const Json& object, std::string_view key,
                          std::string_view provider,
                          const std::filesystem::path& path, std::size_t index) {
  const auto value = object.find(key);
  if (value == object.end() || value->is_null()) {
    return std::nullopt;
  }
  try {
    return value->get<T>();
  } catch (const Json::exception& error) {
    throw recordError(provider, path, index,
                      "invalid optional field '" + std::string(key) + "': " +
                          error.what());
  }
}

inline std::chrono::sys_seconds dateTime(
    int year, unsigned month, unsigned day, int hour, int minute, int second,
    std::string_view provider, const std::filesystem::path& path, std::size_t index,
    std::string_view field) {
  const std::chrono::year_month_day date{std::chrono::year{year},
                                         std::chrono::month{month},
                                         std::chrono::day{day}};
  if (!date.ok() || hour < 0 || hour >= 24 || minute < 0 || minute >= 60 ||
      second < 0 || second >= 60) {
    throw recordError(provider, path, index,
                      "invalid date/time field '" + std::string(field) + "'");
  }
  return std::chrono::sys_days{date} + std::chrono::hours{hour} +
         std::chrono::minutes{minute} + std::chrono::seconds{second};
}

inline std::chrono::sys_seconds parseDateAndTime(
    const std::string& date, const std::string& time, std::string_view provider,
    const std::filesystem::path& path, std::size_t index) {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  char first_dash = '\0';
  char second_dash = '\0';
  std::istringstream date_input(date);
  date_input >> year >> first_dash >> month >> second_dash >> day;
  int hour = 0;
  int minute = 0;
  double seconds = 0.0;
  char first_colon = '\0';
  char second_colon = '\0';
  std::istringstream time_input(time);
  time_input >> hour >> first_colon >> minute >> second_colon >> seconds;
  if (!date_input || !date_input.eof() || first_dash != '-' || second_dash != '-' ||
      !time_input || !time_input.eof() || first_colon != ':' ||
      second_colon != ':' || !std::isfinite(seconds) || seconds < 0.0 ||
      seconds >= 60.0) {
    throw recordError(provider, path, index, "invalid date or time field");
  }
  return dateTime(year, month, day, hour, minute,
                  static_cast<int>(std::floor(seconds)), provider, path, index,
                  "date/time");
}

}  // namespace emberdb::metadata_json
