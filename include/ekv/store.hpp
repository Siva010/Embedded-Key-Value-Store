#pragma once

#include "ekv/append_log.hpp"
#include "ekv/compaction.hpp"
#include "ekv/error.hpp"
#include "ekv/hash_index.hpp"
#include "ekv/options.hpp"
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
// - Shared lock: get, size, empty, log_size_bytes, is_open, path, options
// - Unique lock: put, Delete, compact, open, close
// Concurrent get uses positioned file reads (not a shared stream cursor).
// Options (Phase 6) select per-append SyncMode at open time.
class Store {
 public:
  Store() = default;
  ~Store();

  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;
  Store(Store&&) noexcept;
  Store& operator=(Store&&) noexcept;

  // Open with default Options (SyncMode::Full).
  void open(const std::filesystem::path& path);

  // Open with explicit options (durability policy, etc.).
  void open(const std::filesystem::path& path, Options options);

  void close() noexcept;

  [[nodiscard]] bool is_open() const noexcept;

  [[nodiscard]] std::filesystem::path path() const;

  // Options used for the current open session (default if never opened).
  [[nodiscard]] Options options() const;

  void put(std::string_view key, std::string_view value);

  [[nodiscard]] std::optional<std::string> get(std::string_view key) const;

  bool Delete(std::string_view key);

  CompactStats compact();

  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] bool empty() const;

  [[nodiscard]] std::uint64_t log_size_bytes() const;

 private:
  void open_unlocked(const std::filesystem::path& path, Options options);
  void ensure_open_unlocked() const;
  void rebuild_index_from_log();
  void close_unlocked() noexcept;
  static void cleanup_compaction_artifacts(const std::filesystem::path& dir);

  mutable std::shared_mutex mu_;
  bool open_ = false;
  Options options_{};
  std::filesystem::path path_;
  HashIndex index_;
  AppendLog log_;
};

}  // namespace ekv
