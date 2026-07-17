#include "emberdb/storage/football_event_file.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace emberdb {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic{'E', 'M', 'B', 'E', 'R', 'D', 'B', 0};
constexpr std::uint16_t kFlags = 0;
constexpr std::size_t kFixedHeaderSize = 24;
constexpr std::size_t kDirectoryEntrySize = 44;
constexpr std::size_t kHeaderSize =
    kFixedHeaderSize + FootballEventTable::kColumnCount * kDirectoryEntrySize;

constexpr std::array<FootballEventColumn, FootballEventTable::kColumnCount> kColumns{
    FootballEventColumn::ProviderEventId, FootballEventColumn::MatchId,
    FootballEventColumn::Period,          FootballEventColumn::Timestamp,
    FootballEventColumn::Minute,          FootballEventColumn::Second,
    FootballEventColumn::PossessionId,    FootballEventColumn::TeamId,
    FootballEventColumn::TeamName,        FootballEventColumn::PlayerId,
    FootballEventColumn::PlayerName,      FootballEventColumn::EventType,
    FootballEventColumn::Outcome,         FootballEventColumn::StartX,
    FootballEventColumn::StartY,          FootballEventColumn::EndX,
    FootballEventColumn::EndY,            FootballEventColumn::Provider,
    FootballEventColumn::SourceStartX,    FootballEventColumn::SourceStartY,
    FootballEventColumn::SourceEndX,      FootballEventColumn::SourceEndY,
};

enum class PhysicalType : std::uint8_t {
  Int64 = 1,
  Int32 = 2,
  Milliseconds = 3,
  Double = 4,
  String = 5,
};

struct DirectoryEntry {
  std::uint16_t column_id{};
  PhysicalType physical_type{};
  bool nullable{};
  std::uint64_t bitmap_offset{};
  std::uint64_t bitmap_size{};
  std::uint32_t bitmap_checksum{};
  std::uint64_t data_offset{};
  std::uint64_t data_size{};
  std::uint32_t data_checksum{};
};

[[noreturn]] void invalidFile(const std::filesystem::path& path,
                              const std::string& message) {
  throw std::runtime_error("Invalid EmberDB file '" + path.string() + "': " + message);
}

template <typename Unsigned>
void appendLittleEndian(std::vector<std::uint8_t>& output, Unsigned value) {
  static_assert(std::is_unsigned_v<Unsigned>);
  for (std::size_t index = 0; index < sizeof(Unsigned); ++index) {
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
    value >>= 8U;
  }
}

void appendInt64(std::vector<std::uint8_t>& output, std::int64_t value) {
  appendLittleEndian(output, std::bit_cast<std::uint64_t>(value));
}

void appendInt32(std::vector<std::uint8_t>& output, std::int32_t value) {
  appendLittleEndian(output, std::bit_cast<std::uint32_t>(value));
}

std::uint64_t toUint64(std::size_t value) {
  if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
    if (value > std::numeric_limits<std::uint64_t>::max()) {
      throw std::overflow_error("EmberDB file size exceeds the format limit");
    }
  }
  return static_cast<std::uint64_t>(value);
}

std::size_t toSize(const std::filesystem::path& path, std::uint64_t value,
                   const std::string& field) {
  if (value > std::numeric_limits<std::size_t>::max()) {
    invalidFile(path, field + " exceeds this platform's addressable size");
  }
  return static_cast<std::size_t>(value);
}

std::streamsize toStreamSize(std::size_t value, const std::string& context) {
  if (value > static_cast<std::size_t>(
                  std::numeric_limits<std::streamsize>::max())) {
    throw std::overflow_error(context + " exceeds the stream size limit");
  }
  return static_cast<std::streamsize>(value);
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes) noexcept {
  std::uint32_t crc = 0xffffffffU;
  for (const auto byte : bytes) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      const auto mask = static_cast<std::uint32_t>(
          -static_cast<std::int32_t>(crc & 1U));
      crc = (crc >> 1U) ^ (0xedb88320U & mask);
    }
  }
  return ~crc;
}

PhysicalType physicalType(FootballEventColumn column) {
  switch (columnValueType(column)) {
    case FootballEventValueType::Identifier:
      return PhysicalType::Int64;
    case FootballEventValueType::Integer:
      return PhysicalType::Int32;
    case FootballEventValueType::Timestamp:
      return PhysicalType::Milliseconds;
    case FootballEventValueType::Number:
      return PhysicalType::Double;
    case FootballEventValueType::Text:
      return PhysicalType::String;
  }
  throw std::logic_error("Unknown football event value type");
}

std::size_t fixedWidth(PhysicalType type) noexcept {
  switch (type) {
    case PhysicalType::Int64:
    case PhysicalType::Milliseconds:
    case PhysicalType::Double:
      return 8;
    case PhysicalType::Int32:
      return 4;
    case PhysicalType::String:
      return 0;
  }
  return 0;
}

std::vector<std::uint8_t> makeBitmap(const FootballEventTable& table,
                                     FootballEventColumn column) {
  if (!columnIsNullable(column)) {
    return {};
  }
  std::vector<std::uint8_t> bitmap((table.rowCount() + 7U) / 8U, 0);
  for (std::size_t row = 0; row < table.rowCount(); ++row) {
    if (table.cell(column, row)) {
      bitmap[row / 8U] |= static_cast<std::uint8_t>(1U << (row % 8U));
    }
  }
  return bitmap;
}

void appendCell(std::vector<std::uint8_t>& data, PhysicalType type,
                const FootballEventValue& value) {
  switch (type) {
    case PhysicalType::Int64:
      appendInt64(data, std::get<Identifier>(value));
      return;
    case PhysicalType::Int32:
      appendInt32(data, std::get<std::int32_t>(value));
      return;
    case PhysicalType::Milliseconds:
      appendInt64(data, std::get<std::chrono::milliseconds>(value).count());
      return;
    case PhysicalType::Double:
      appendLittleEndian(data, std::bit_cast<std::uint64_t>(std::get<double>(value)));
      return;
    case PhysicalType::String: {
      const auto& text = std::get<std::string>(value);
      appendLittleEndian(data, toUint64(text.size()));
      data.insert(data.end(), text.begin(), text.end());
      return;
    }
  }
  throw std::logic_error("Unknown EmberDB physical type");
}

std::vector<std::uint8_t> makeColumnData(const FootballEventTable& table,
                                         FootballEventColumn column) {
  std::vector<std::uint8_t> data;
  const auto width = fixedWidth(physicalType(column));
  if (width != 0 && table.rowCount() <=
                        std::numeric_limits<std::size_t>::max() / width) {
    data.reserve(table.rowCount() * width);
  }
  for (std::size_t row = 0; row < table.rowCount(); ++row) {
    const auto cell = table.cell(column, row);
    if (!cell) {
      if (!columnIsNullable(column)) {
        throw std::logic_error("Non-nullable column contains a null value");
      }
      continue;
    }
    appendCell(data, physicalType(column), *cell);
  }
  return data;
}

std::vector<std::uint8_t> encodeHeader(
    std::size_t row_count,
    const std::array<DirectoryEntry, FootballEventTable::kColumnCount>& entries) {
  std::vector<std::uint8_t> header;
  header.reserve(kHeaderSize);
  header.insert(header.end(), kMagic.begin(), kMagic.end());
  appendLittleEndian(header, kFootballEventFileFormatVersion);
  appendLittleEndian(header, static_cast<std::uint16_t>(kHeaderSize));
  appendLittleEndian(header,
                     static_cast<std::uint16_t>(FootballEventTable::kColumnCount));
  appendLittleEndian(header, kFlags);
  appendLittleEndian(header, toUint64(row_count));
  for (const auto& entry : entries) {
    appendLittleEndian(header, entry.column_id);
    header.push_back(static_cast<std::uint8_t>(entry.physical_type));
    header.push_back(static_cast<std::uint8_t>(entry.nullable));
    appendLittleEndian(header, entry.bitmap_offset);
    appendLittleEndian(header, entry.bitmap_size);
    appendLittleEndian(header, entry.bitmap_checksum);
    appendLittleEndian(header, entry.data_offset);
    appendLittleEndian(header, entry.data_size);
    appendLittleEndian(header, entry.data_checksum);
  }
  if (header.size() != kHeaderSize) {
    throw std::logic_error("EmberDB header size is inconsistent");
  }
  return header;
}

class TemporaryFile {
 public:
  explicit TemporaryFile(std::filesystem::path path) : path_(std::move(path)) {}
  ~TemporaryFile() {
    if (!committed_) {
      std::error_code error;
      std::filesystem::remove(path_, error);
    }
  }

  void commit() noexcept { committed_ = true; }

 private:
  std::filesystem::path path_;
  bool committed_{};
};

class Cursor {
 public:
  Cursor(const std::filesystem::path& path, std::span<const std::uint8_t> bytes)
      : path_(path), bytes_(bytes) {}

  template <typename Unsigned>
  Unsigned read(const std::string& field) {
    static_assert(std::is_unsigned_v<Unsigned>);
    if (bytes_.size() - position_ < sizeof(Unsigned)) {
      invalidFile(path_, "truncated " + field);
    }
    Unsigned value{};
    for (std::size_t index = 0; index < sizeof(Unsigned); ++index) {
      value |= static_cast<Unsigned>(bytes_[position_++]) << (index * 8U);
    }
    return value;
  }

  std::span<const std::uint8_t> readBytes(std::size_t size,
                                          const std::string& field) {
    if (bytes_.size() - position_ < size) {
      invalidFile(path_, "truncated " + field);
    }
    const auto result = bytes_.subspan(position_, size);
    position_ += size;
    return result;
  }

  [[nodiscard]] bool exhausted() const noexcept {
    return position_ == bytes_.size();
  }

 private:
  const std::filesystem::path& path_;
  std::span<const std::uint8_t> bytes_;
  std::size_t position_{};
};

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    throw std::runtime_error("Unable to read EmberDB file '" + path.string() + "'");
  }
  const auto end = input.tellg();
  if (end < 0) {
    throw std::runtime_error("Unable to determine EmberDB file size for '" +
                             path.string() + "'");
  }
  const auto unsigned_size = static_cast<std::uint64_t>(end);
  const auto size = toSize(path, unsigned_size, "file size");
  std::vector<std::uint8_t> bytes(size);
  input.seekg(0);
  if (size != 0) {
    input.read(reinterpret_cast<char*>(bytes.data()),
               toStreamSize(size, "EmberDB file"));
  }
  if (!input) {
    throw std::runtime_error("Unable to read complete EmberDB file '" +
                             path.string() + "'");
  }
  return bytes;
}

std::span<const std::uint8_t> segment(
    const std::filesystem::path& path, std::span<const std::uint8_t> file,
    std::uint64_t offset, std::uint64_t size, const std::string& name) {
  if (offset > file.size() || size > file.size() - static_cast<std::size_t>(offset)) {
    invalidFile(path, name + " is outside the file bounds");
  }
  return file.subspan(static_cast<std::size_t>(offset),
                      static_cast<std::size_t>(size));
}

std::uint64_t bitmapSize(std::uint64_t row_count) {
  if (row_count > std::numeric_limits<std::uint64_t>::max() - 7U) {
    throw std::overflow_error("EmberDB row count exceeds the bitmap format limit");
  }
  return (row_count + 7U) / 8U;
}

std::size_t presentCount(std::span<const std::uint8_t> bitmap,
                         std::size_t row_count) {
  std::size_t count{};
  for (std::size_t row = 0; row < row_count; ++row) {
    if ((bitmap[row / 8U] & static_cast<std::uint8_t>(1U << (row % 8U))) != 0) {
      ++count;
    }
  }
  return count;
}

bool isPresent(std::span<const std::uint8_t> bitmap, bool nullable,
               std::size_t row) {
  return !nullable ||
         (bitmap[row / 8U] & static_cast<std::uint8_t>(1U << (row % 8U))) != 0;
}

std::vector<FootballEventCell> decodeColumn(
    const std::filesystem::path& path, std::span<const std::uint8_t> file,
    const DirectoryEntry& entry, FootballEventColumn column, std::size_t row_count) {
  const auto name = std::string(columnName(column));
  const auto bitmap = segment(path, file, entry.bitmap_offset, entry.bitmap_size,
                              name + " null bitmap");
  const auto data =
      segment(path, file, entry.data_offset, entry.data_size, name + " data");
  const auto present = entry.nullable ? presentCount(bitmap, row_count) : row_count;
  const auto width = fixedWidth(entry.physical_type);
  if (width != 0 &&
      (present > std::numeric_limits<std::size_t>::max() / width ||
       present * width != data.size())) {
    invalidFile(path, name + " data size does not match its row count");
  }

  Cursor cursor(path, data);
  std::vector<FootballEventCell> values;
  values.reserve(row_count);
  for (std::size_t row = 0; row < row_count; ++row) {
    if (!isPresent(bitmap, entry.nullable, row)) {
      values.push_back(std::nullopt);
      continue;
    }
    switch (entry.physical_type) {
      case PhysicalType::Int64:
        values.emplace_back(FootballEventValue{std::bit_cast<std::int64_t>(
            cursor.read<std::uint64_t>(name + " value"))});
        break;
      case PhysicalType::Int32:
        values.emplace_back(FootballEventValue{std::bit_cast<std::int32_t>(
            cursor.read<std::uint32_t>(name + " value"))});
        break;
      case PhysicalType::Milliseconds:
        values.emplace_back(FootballEventValue{std::chrono::milliseconds{
            std::bit_cast<std::int64_t>(
                cursor.read<std::uint64_t>(name + " value"))}});
        break;
      case PhysicalType::Double:
        values.emplace_back(FootballEventValue{std::bit_cast<double>(
            cursor.read<std::uint64_t>(name + " value"))});
        break;
      case PhysicalType::String: {
        const auto length = cursor.read<std::uint64_t>(name + " string length");
        const auto text_bytes =
            cursor.readBytes(toSize(path, length, name + " string length"),
                             name + " string data");
        values.emplace_back(FootballEventValue{std::string(
            reinterpret_cast<const char*>(text_bytes.data()), text_bytes.size())});
        break;
      }
    }
  }
  if (!cursor.exhausted()) {
    invalidFile(path, name + " contains trailing encoded values");
  }
  return values;
}

template <typename T>
T requiredValue(const std::filesystem::path& path,
                const std::vector<FootballEventCell>& column, std::size_t row,
                FootballEventColumn column_id) {
  if (!column[row] || !std::holds_alternative<T>(*column[row])) {
    invalidFile(path, "invalid value in non-nullable column '" +
                          std::string(columnName(column_id)) + "' at row " +
                          std::to_string(row));
  }
  return std::get<T>(*column[row]);
}

template <typename T>
std::optional<T> optionalValue(const std::filesystem::path& path,
                               const std::vector<FootballEventCell>& column,
                               std::size_t row, FootballEventColumn column_id) {
  if (!column[row]) {
    return std::nullopt;
  }
  if (!std::holds_alternative<T>(*column[row])) {
    invalidFile(path, "invalid value in nullable column '" +
                          std::string(columnName(column_id)) + "' at row " +
                          std::to_string(row));
  }
  return std::get<T>(*column[row]);
}

}  // namespace

void saveFootballEventTable(const FootballEventTable& table,
                            const std::filesystem::path& path) {
  if (!table.validate()) {
    throw std::invalid_argument("Cannot save an inconsistent FootballEventTable");
  }
  std::error_code error;
  if (std::filesystem::exists(path, error)) {
    throw std::runtime_error("Refusing to overwrite existing EmberDB file '" +
                             path.string() + "'");
  }
  if (error) {
    throw std::runtime_error("Unable to inspect EmberDB output path '" + path.string() +
                             "': " + error.message());
  }
  auto temporary_path = path;
  temporary_path += ".tmp";
  if (std::filesystem::exists(temporary_path, error)) {
    throw std::runtime_error("Temporary EmberDB output already exists '" +
                             temporary_path.string() + "'");
  }
  if (error) {
    throw std::runtime_error("Unable to inspect temporary EmberDB output path '" +
                             temporary_path.string() + "': " + error.message());
  }

  std::vector<std::uint8_t> file(kHeaderSize, 0);
  std::array<DirectoryEntry, FootballEventTable::kColumnCount> entries;
  for (std::size_t index = 0; index < kColumns.size(); ++index) {
    const auto column = kColumns[index];
    auto bitmap = makeBitmap(table, column);
    auto data = makeColumnData(table, column);
    auto& entry = entries[index];
    entry.column_id = static_cast<std::uint16_t>(column);
    entry.physical_type = physicalType(column);
    entry.nullable = columnIsNullable(column);
    if (!bitmap.empty()) {
      entry.bitmap_offset = toUint64(file.size());
      entry.bitmap_size = toUint64(bitmap.size());
      entry.bitmap_checksum = crc32(bitmap);
      file.insert(file.end(), bitmap.begin(), bitmap.end());
    }
    entry.data_offset = toUint64(file.size());
    entry.data_size = toUint64(data.size());
    entry.data_checksum = crc32(data);
    file.insert(file.end(), data.begin(), data.end());
  }
  const auto header = encodeHeader(table.rowCount(), entries);
  std::copy(header.begin(), header.end(), file.begin());

  TemporaryFile temporary(temporary_path);
  std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("Unable to create EmberDB file '" + path.string() + "'");
  }
  output.write(reinterpret_cast<const char*>(file.data()),
               toStreamSize(file.size(), "EmberDB output"));
  output.close();
  if (!output) {
    throw std::runtime_error("Unable to write complete EmberDB file '" + path.string() +
                             "'");
  }
  std::filesystem::rename(temporary_path, path, error);
  if (error) {
    throw std::runtime_error("Unable to finalize EmberDB file '" + path.string() +
                             "': " + error.message());
  }
  temporary.commit();
}

FootballEventTable loadFootballEventTable(const std::filesystem::path& path) {
  const auto file = readFile(path);
  if (file.size() < kHeaderSize) {
    invalidFile(path, "file is smaller than the version 2 header");
  }
  Cursor header(path, file);
  if (!std::ranges::equal(header.readBytes(kMagic.size(), "magic"), kMagic)) {
    invalidFile(path, "magic bytes do not identify an EmberDB file");
  }
  const auto version = header.read<std::uint16_t>("format version");
  if (version != kFootballEventFileFormatVersion) {
    invalidFile(path, "unsupported format version " + std::to_string(version));
  }
  if (header.read<std::uint16_t>("header size") != kHeaderSize) {
    invalidFile(path, "unexpected header size");
  }
  if (header.read<std::uint16_t>("column count") !=
      FootballEventTable::kColumnCount) {
    invalidFile(path, "schema column count does not match FootballEventTable");
  }
  if (header.read<std::uint16_t>("flags") != kFlags) {
    invalidFile(path, "unsupported header flags");
  }
  const auto stored_row_count = header.read<std::uint64_t>("row count");
  const auto row_count = toSize(path, stored_row_count, "row count");

  std::array<DirectoryEntry, FootballEventTable::kColumnCount> entries;
  for (std::size_t index = 0; index < entries.size(); ++index) {
    auto& entry = entries[index];
    entry.column_id = header.read<std::uint16_t>("column id");
    entry.physical_type =
        static_cast<PhysicalType>(header.read<std::uint8_t>("physical type"));
    const auto nullable = header.read<std::uint8_t>("nullable flag");
    if (nullable > 1U) {
      invalidFile(path, "nullable flag must be zero or one");
    }
    entry.nullable = nullable == 1U;
    entry.bitmap_offset = header.read<std::uint64_t>("bitmap offset");
    entry.bitmap_size = header.read<std::uint64_t>("bitmap size");
    entry.bitmap_checksum = header.read<std::uint32_t>("bitmap checksum");
    entry.data_offset = header.read<std::uint64_t>("data offset");
    entry.data_size = header.read<std::uint64_t>("data size");
    entry.data_checksum = header.read<std::uint32_t>("data checksum");

    const auto expected_column = kColumns[index];
    if (entry.column_id != static_cast<std::uint16_t>(expected_column) ||
        entry.physical_type != physicalType(expected_column) ||
        entry.nullable != columnIsNullable(expected_column)) {
      invalidFile(path, "stored schema does not match column '" +
                            std::string(columnName(expected_column)) + "'");
    }
    const auto expected_bitmap_size =
        entry.nullable ? bitmapSize(stored_row_count) : 0U;
    if (entry.bitmap_size != expected_bitmap_size ||
        (!entry.nullable &&
         (entry.bitmap_offset != 0 || entry.bitmap_checksum != 0))) {
      invalidFile(path, "invalid null bitmap metadata for column '" +
                            std::string(columnName(expected_column)) + "'");
    }
  }

  std::uint64_t expected_offset = kHeaderSize;
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const auto& entry = entries[index];
    const auto name = std::string(columnName(kColumns[index]));
    if (entry.bitmap_size != 0) {
      if (entry.bitmap_offset != expected_offset) {
        invalidFile(path, "non-canonical null bitmap offset for column '" + name + "'");
      }
      expected_offset += entry.bitmap_size;
    } else if (entry.bitmap_offset != 0) {
      invalidFile(path, "empty null bitmap has a nonzero offset for column '" + name +
                            "'");
    }
    if (entry.data_offset != expected_offset) {
      invalidFile(path, "non-canonical data offset for column '" + name + "'");
    }
    if (entry.data_size > std::numeric_limits<std::uint64_t>::max() - expected_offset) {
      invalidFile(path, "data range overflows for column '" + name + "'");
    }
    expected_offset += entry.data_size;
    const auto bitmap = segment(path, file, entry.bitmap_offset, entry.bitmap_size,
                                name + " null bitmap");
    const auto data = segment(path, file, entry.data_offset, entry.data_size, name + " data");
    if (crc32(bitmap) != entry.bitmap_checksum ||
        crc32(data) != entry.data_checksum) {
      invalidFile(path, "checksum mismatch for column '" + name + "'");
    }
    if (entry.nullable && stored_row_count % 8U != 0 && !bitmap.empty()) {
      const auto used_bits = static_cast<unsigned>(stored_row_count % 8U);
      const auto unused_mask = static_cast<std::uint8_t>(~((1U << used_bits) - 1U));
      if ((bitmap.back() & unused_mask) != 0) {
        invalidFile(path, "unused null bitmap bits are set for column '" + name + "'");
      }
    }
  }
  if (expected_offset != file.size()) {
    invalidFile(path, "file has missing or trailing column data");
  }

  std::array<std::vector<FootballEventCell>, FootballEventTable::kColumnCount> columns;
  for (std::size_t index = 0; index < columns.size(); ++index) {
    columns[index] = decodeColumn(path, file, entries[index], kColumns[index], row_count);
  }

  FootballEventTable table;
  for (std::size_t row = 0; row < row_count; ++row) {
    const auto start_x = optionalValue<double>(path, columns[13], row,
                                               FootballEventColumn::StartX);
    const auto start_y = optionalValue<double>(path, columns[14], row,
                                               FootballEventColumn::StartY);
    const auto end_x = optionalValue<double>(path, columns[15], row,
                                             FootballEventColumn::EndX);
    const auto end_y = optionalValue<double>(path, columns[16], row,
                                             FootballEventColumn::EndY);
    const auto source_start_x = optionalValue<double>(
        path, columns[18], row, FootballEventColumn::SourceStartX);
    const auto source_start_y = optionalValue<double>(
        path, columns[19], row, FootballEventColumn::SourceStartY);
    const auto source_end_x = optionalValue<double>(
        path, columns[20], row, FootballEventColumn::SourceEndX);
    const auto source_end_y = optionalValue<double>(
        path, columns[21], row, FootballEventColumn::SourceEndY);
    if (start_x.has_value() != start_y.has_value() ||
        end_x.has_value() != end_y.has_value() ||
        source_start_x.has_value() != source_start_y.has_value() ||
        source_end_x.has_value() != source_end_y.has_value()) {
      invalidFile(path, "coordinate column nullability is inconsistent at row " +
                            std::to_string(row));
    }
    try {
      table.append(FootballEvent{
          requiredValue<std::string>(path, columns[0], row,
                                     FootballEventColumn::ProviderEventId),
          requiredValue<Identifier>(path, columns[1], row,
                                    FootballEventColumn::MatchId),
          requiredValue<std::int32_t>(path, columns[2], row,
                                      FootballEventColumn::Period),
          {requiredValue<std::chrono::milliseconds>(
               path, columns[3], row, FootballEventColumn::Timestamp),
           requiredValue<std::int32_t>(path, columns[4], row,
                                       FootballEventColumn::Minute),
           requiredValue<std::int32_t>(path, columns[5], row,
                                       FootballEventColumn::Second)},
          optionalValue<Identifier>(path, columns[6], row,
                                    FootballEventColumn::PossessionId),
          optionalValue<Identifier>(path, columns[7], row,
                                    FootballEventColumn::TeamId),
          optionalValue<std::string>(path, columns[8], row,
                                     FootballEventColumn::TeamName),
          optionalValue<Identifier>(path, columns[9], row,
                                    FootballEventColumn::PlayerId),
          optionalValue<std::string>(path, columns[10], row,
                                     FootballEventColumn::PlayerName),
          requiredValue<std::string>(path, columns[11], row,
                                     FootballEventColumn::EventType),
          optionalValue<std::string>(path, columns[12], row,
                                     FootballEventColumn::Outcome),
          start_x ? std::optional<Coordinate>{{*start_x, *start_y}} : std::nullopt,
          end_x ? std::optional<Coordinate>{{*end_x, *end_y}} : std::nullopt,
          requiredValue<std::string>(path, columns[17], row,
                                     FootballEventColumn::Provider),
          source_start_x ? std::optional<Coordinate>{{*source_start_x, *source_start_y}}
                         : std::nullopt,
          source_end_x ? std::optional<Coordinate>{{*source_end_x, *source_end_y}}
                       : std::nullopt});
    } catch (const std::invalid_argument& error) {
      invalidFile(path, "invalid coordinate at row " + std::to_string(row) + ": " +
                            error.what());
    }
  }
  if (!table.validate()) {
    invalidFile(path, "loaded column lengths are inconsistent");
  }
  return table;
}

}  // namespace emberdb
