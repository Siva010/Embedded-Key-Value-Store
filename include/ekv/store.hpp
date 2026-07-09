#pragma once

#include "ekv/append_log.hpp"
#include "ekv/error.hpp"
#include "ekv/hash_index.hpp"
#include "ekv/version.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ekv {

// Embedded key-value store facade.
//
// Phase 2: put/Delete append binary records to a log under `path`; the hash
// index maps keys to value offsets. open() replays the log to rebuild the
// index. get() reads the value from the log. Old versions remain in the file
// until Phase 4 compaction.
class Store {
 public:
  Store() = default;
  ~Store();

  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;
  Store(Store&&) noexcept;
  Store& operator=(Store&&) noexcept;

  // Open the store at `path` (created if missing). Replays `ekv.log` if present.
  void open(const std::filesystem::path& path);

  // Flush and release resources. Safe to call multiple times.
  void close() noexcept;

  [[nodiscard]] bool is_open() const noexcept { return open_; }

  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return path_;
  }

  // Insert or overwrite. Empty keys are rejected.
  void put(std::string_view key, std::string_view value);

  // Missing key → std::nullopt (not an exception).
  [[nodiscard]] std::optional<std::string> get(std::string_view key) const;

  // Erase key if present. Returns true when a mapping was removed.
  // Appends a delete tombstone when the key existed.
  bool Delete(std::string_view key);

  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] bool empty() const;

 private:
  void ensure_open() const;
  void rebuild_index_from_log();

  bool open_ = false;
  std::filesystem::path path_;
  HashIndex index_;
  AppendLog log_;
};

}  // namespace ekv
