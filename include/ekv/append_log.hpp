#pragma once

#include "ekv/error.hpp"
#include "ekv/record.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>

namespace ekv {

// Append-only durable log (single file under the store data directory).
//
// Writers append Put/Delete records with trailing CRC32 and fsync after each
// append. Readers rebuild an index by replaying complete, CRC-valid records.
class AppendLog {
 public:
  // Called for each complete CRC-valid record during replay (file order).
  using ReplayFn = std::function<void(
      std::uint8_t type, std::string_view key, RecordLocator locator)>;

  AppendLog() = default;
  ~AppendLog();

  AppendLog(const AppendLog&) = delete;
  AppendLog& operator=(const AppendLog&) = delete;
  AppendLog(AppendLog&&) noexcept;
  AppendLog& operator=(AppendLog&&) noexcept;

  // Open or create `path`. Trims a torn/bad tail; fails on mid-file corruption.
  void open(const std::filesystem::path& path);

  void close() noexcept;

  [[nodiscard]] bool is_open() const noexcept { return open_; }

  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return path_;
  }

  // Bytes discarded from a torn or CRC-bad tail on the last open (0 if clean).
  [[nodiscard]] std::uint64_t recovered_tail_bytes() const noexcept {
    return recovered_tail_bytes_;
  }

  // Logical end offset (header + complete records); equals file size after trim.
  [[nodiscard]] std::uint64_t size_bytes() const noexcept { return write_pos_; }

  RecordLocator append_put(std::string_view key, std::string_view value);
  void append_delete(std::string_view key);

  [[nodiscard]] std::string read_value(RecordLocator locator) const;

  void replay(const ReplayFn& on_record) const;

  // flush + OS durable sync (FlushFileBuffers / fsync).
  void sync();

 private:
  void ensure_open() const;
  std::uint64_t append_record(std::uint8_t type, std::string_view key,
                              std::string_view value);

  // Walk records. On success sets *out_append_pos to the first invalid offset.
  // Throws Corruption if a bad CRC is found before the recoverable tail region.
  void scan_records(const ReplayFn* on_record,
                    std::uint64_t* out_append_pos) const;

  bool open_ = false;
  std::filesystem::path path_;
  mutable std::fstream file_;
  std::uint64_t write_pos_ = 0;
  std::uint64_t recovered_tail_bytes_ = 0;
};

}  // namespace ekv
