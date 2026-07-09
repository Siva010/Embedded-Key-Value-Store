#pragma once

#include "ekv/error.hpp"
#include "ekv/record.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace ekv {

// Append-only durable log (single file under the store data directory).
//
// Writers append Put/Delete records. Readers rebuild an index by replaying
// from the file header, and fetch values by absolute file offset.
class AppendLog {
 public:
  // Called for each complete record during replay (in file order).
  // For Put: locator points at the value payload. For Delete: size is 0.
  using ReplayFn = std::function<void(
      std::uint8_t type, std::string_view key, RecordLocator locator)>;

  AppendLog() = default;
  ~AppendLog();

  AppendLog(const AppendLog&) = delete;
  AppendLog& operator=(const AppendLog&) = delete;
  AppendLog(AppendLog&&) noexcept;
  AppendLog& operator=(AppendLog&&) noexcept;

  // Open or create `path`. Replays nothing itself — caller owns index rebuild.
  void open(const std::filesystem::path& path);

  void close() noexcept;

  [[nodiscard]] bool is_open() const noexcept { return open_; }

  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return path_;
  }

  // Append a Put record. Returns locator for the written value bytes.
  RecordLocator append_put(std::string_view key, std::string_view value);

  // Append a Delete (tombstone) record.
  void append_delete(std::string_view key);

  // Read value bytes at an absolute file offset.
  [[nodiscard]] std::string read_value(RecordLocator locator) const;

  // Scan all complete records after the file header. Stops cleanly at a
  // truncated tail (incomplete final record is ignored — Phase 3 + CRC).
  void replay(const ReplayFn& on_record) const;

  // Force buffered data to the OS (not necessarily to stable media; Phase 3).
  void sync();

 private:
  void ensure_open() const;
  std::uint64_t append_record(std::uint8_t type, std::string_view key,
                              std::string_view value);
  // Scan complete records; return file offset where the next append should go.
  // Truncated tails are skipped (not deleted until Phase 3 recovery policy).
  [[nodiscard]] std::uint64_t find_next_append_offset() const;

  bool open_ = false;
  std::filesystem::path path_;
  // mutable: allows const read/replay while a single writer holds the log.
  mutable std::fstream file_;
  std::uint64_t write_pos_ = 0;
};

}  // namespace ekv
