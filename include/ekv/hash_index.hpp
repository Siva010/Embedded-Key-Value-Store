#pragma once

#include "ekv/record.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ekv {

// Heterogeneous lookup: hash/eq accept string_view without allocating a key.
struct StringHash {
  using is_transparent = void;

  [[nodiscard]] std::size_t operator()(std::string_view s) const noexcept {
    return std::hash<std::string_view>{}(s);
  }
};

// In-memory key → durable location map.
//
// Phase 1 stored full values here. Phase 2+ stores RecordLocator (value offset
// + size in the append-only log). Keys remain in RAM for O(1) point lookups.
class HashIndex {
 public:
  using Map =
      std::unordered_map<std::string, RecordLocator, StringHash, std::equal_to<>>;

  void put(std::string key, RecordLocator locator) {
    map_.insert_or_assign(std::move(key), locator);
  }

  [[nodiscard]] std::optional<RecordLocator> get(std::string_view key) const {
    const auto it = map_.find(key);
    if (it == map_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  // true if a previous mapping existed and was removed.
  // find+erase: key-based erase is not heterogeneous on all C++20 libraries.
  bool erase(std::string_view key) {
    const auto it = map_.find(key);
    if (it == map_.end()) {
      return false;
    }
    map_.erase(it);
    return true;
  }

  [[nodiscard]] bool contains(std::string_view key) const {
    return map_.find(key) != map_.end();
  }

  [[nodiscard]] std::size_t size() const noexcept { return map_.size(); }

  [[nodiscard]] bool empty() const noexcept { return map_.empty(); }

  void clear() noexcept { map_.clear(); }

  [[nodiscard]] Map::const_iterator begin() const noexcept {
    return map_.begin();
  }
  [[nodiscard]] Map::const_iterator end() const noexcept { return map_.end(); }

 private:
  Map map_;
};

}  // namespace ekv
