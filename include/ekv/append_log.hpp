#pragma once

#include "ekv/error.hpp"
#include "ekv/options.hpp"
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
// Writers append Put/Delete records with trailing CRC32. Durability after each
// append is controlled by SyncMode (Full = flush+fsync by default).
class AppendLog {
 public:
  using ReplayFn = std::function<void(
      std::uint8_t type, std::string_view key, RecordLocator locator)>;

  AppendLog() = default;
  ~AppendLog();

  AppendLog(const AppendLog&) = delete;
  AppendLog& operator=(const AppendLog&) = delete;
  AppendLog(AppendLog&&) noexcept;
  AppendLog& operator=(AppendLog&&) noexcept;

  // Open or create `path`. Trims a torn/bad tail; fails on mid-file corruption.
  void open(const std::filesystem::path& path,
            SyncMode sync_mode = SyncMode::Full);

  void close() noexcept;

  [[nodiscard]] bool is_open() const noexcept { return open_; }

  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return path_;
  }

  [[nodiscard]] SyncMode sync_mode() const noexcept { return sync_mode_; }

  [[nodiscard]] std::uint64_t recovered_tail_bytes() const noexcept {
    return recovered_tail_bytes_;
  }

  [[nodiscard]] std::uint64_t size_bytes() const noexcept { return write_pos_; }

  RecordLocator append_put(std::string_view key, std::string_view value);
  void append_delete(std::string_view key);

  [[nodiscard]] std::string read_value(RecordLocator locator) const;

  void replay(const ReplayFn& on_record) const;

  // Always flush + OS durable sync (ignores SyncMode::None/Flush).
  void sync();

 private:
  void ensure_open() const;
  void apply_append_durability();
  std::uint64_t append_record(std::uint8_t type, std::string_view key,
                              std::string_view value);
  void scan_records(const ReplayFn* on_record,
                    std::uint64_t* out_append_pos) const;

  bool open_ = false;
  SyncMode sync_mode_ = SyncMode::Full;
  std::filesystem::path path_;
  mutable std::fstream file_;
  std::uint64_t write_pos_ = 0;
  std::uint64_t recovered_tail_bytes_ = 0;
};

}  // namespace ekv
