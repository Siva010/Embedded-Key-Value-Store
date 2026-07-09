#pragma once

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
// Phase 1: all data lives in an in-memory HashIndex. `open(path)` records a
// data directory for future durable log files; process exit still loses data.
// Phase 2 will append records under that path and rebuild the index on open.
class Store {
 public:
  Store() = default;
  ~Store();

  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;
  Store(Store&&) noexcept;
  Store& operator=(Store&&) noexcept;

  // Prepare the store for use. `path` is the on-disk data directory
  // (created if missing). Throws Error if already open or path is empty.
  void open(const std::filesystem::path& path);

  // Release in-memory state. Safe to call multiple times.
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
  // Named Delete (capital D) to avoid the C++ keyword `delete`.
  bool Delete(std::string_view key);

  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] bool empty() const;

 private:
  void ensure_open() const;

  bool open_ = false;
  std::filesystem::path path_;
  HashIndex index_;
};

}  // namespace ekv
