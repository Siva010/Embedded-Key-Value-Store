#pragma once

#include "ekv/append_log.hpp"
#include "ekv/compaction.hpp"
#include "ekv/error.hpp"
#include "ekv/hash_index.hpp"
#include "ekv/options.hpp"
#include "ekv/stats.hpp"
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
// Threading: single writer, multiple readers (shared_mutex).
// Options: SyncMode + optional auto-compaction by space amplification.
class Store {
 public:
  Store() = default;
  ~Store();

  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;
  Store(Store&&) noexcept;
  Store& operator=(Store&&) noexcept;

  void open(const std::filesystem::path& path);
  void open(const std::filesystem::path& path, Options options);

  void close() noexcept;

  [[nodiscard]] bool is_open() const noexcept;
  [[nodiscard]] std::filesystem::path path() const;
  [[nodiscard]] Options options() const;

  void put(std::string_view key, std::string_view value);
  [[nodiscard]] std::optional<std::string> get(std::string_view key) const;
  bool Delete(std::string_view key);

  // Manual compaction. Also used by auto-compact when thresholds hit.
  CompactStats compact();

  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] bool empty() const;
  [[nodiscard]] std::uint64_t log_size_bytes() const;
  [[nodiscard]] StoreStats stats() const;

 private:
  void open_unlocked(const std::filesystem::path& path, Options options);
  void ensure_open_unlocked() const;
  void rebuild_index_from_log();
  void recompute_live_bytes();
  void close_unlocked() noexcept;
  CompactStats compact_unlocked();
  void maybe_auto_compact_unlocked();
  static void cleanup_compaction_artifacts(const std::filesystem::path& dir);
  static std::uint64_t live_record_bytes(std::size_t key_len,
                                         std::uint32_t value_len) noexcept;

  mutable std::shared_mutex mu_;
  bool open_ = false;
  Options options_{};
  std::filesystem::path path_;
  HashIndex index_;
  AppendLog log_;
  std::uint64_t live_bytes_ = 0;
};

}  // namespace ekv
