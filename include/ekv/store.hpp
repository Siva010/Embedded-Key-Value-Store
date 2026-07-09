#pragma once

#include "ekv/append_log.hpp"
#include "ekv/compaction.hpp"
#include "ekv/error.hpp"
#include "ekv/hash_index.hpp"
#include "ekv/version.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>

namespace ekv {

// Embedded key-value store facade.
//
// Threading model (Phase 5): single writer, multiple readers.
// - Shared lock: get, size, empty, log_size_bytes, is_open, path
// - Unique lock: put, Delete, compact, open, close
// Concurrent get uses positioned file reads (not a shared stream cursor).
// Move operations are not safe concurrently with other methods on the same
// instance (lock the store externally if you must move).
class Store {
 public:
  Store() = default;
  ~Store();

  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;
  Store(Store&&) noexcept;
  Store& operator=(Store&&) noexcept;

  // Open the store at `path` (created if missing). Replays `ekv.log` if present.
  // Cleans up interrupted compaction artifacts (.compact / .old).
  void open(const std::filesystem::path& path);

  // Flush and release resources. Safe to call multiple times.
  void close() noexcept;

  [[nodiscard]] bool is_open() const noexcept;

  [[nodiscard]] std::filesystem::path path() const;

  // Insert or overwrite. Empty keys are rejected.
  void put(std::string_view key, std::string_view value);

  // Missing key → std::nullopt (not an exception).
  [[nodiscard]] std::optional<std::string> get(std::string_view key) const;

  // Erase key if present. Returns true when a mapping was removed.
  // Appends a delete tombstone when the key existed.
  bool Delete(std::string_view key);

  // Rewrite live keys only. Drops tombstones and superseded versions.
  // Sequence: write ekv.log.compact → durable sync → replace ekv.log → reopen.
  // Exclusive: blocks readers for the duration.
  CompactStats compact();

  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] bool empty() const;

  // Current append-log logical size in bytes (for tests / metrics).
  [[nodiscard]] std::uint64_t log_size_bytes() const;

 private:
  void ensure_open_unlocked() const;
  void rebuild_index_from_log();
  void close_unlocked() noexcept;
  static void cleanup_compaction_artifacts(const std::filesystem::path& dir);

  // mutable: const readers take shared locks.
  mutable std::shared_mutex mu_;
  bool open_ = false;
  std::filesystem::path path_;
  HashIndex index_;
  AppendLog log_;
};

}  // namespace ekv
