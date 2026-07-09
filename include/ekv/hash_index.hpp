#pragma once

#include "ekv/record.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ekv {

// In-memory key → durable location map.
//
// Phase 1 stored full values here. Phase 2 stores RecordLocator (value offset
// + size in the append-only log). Keys remain in RAM for O(1) point lookups.
class HashIndex {
 public:
  using Map = std::unordered_map<std::string, RecordLocator>;

  void put(std::string key, RecordLocator locator) {
    map_.insert_or_assign(std::move(key), locator);
  }

  [[nodiscard]] std::optional<RecordLocator> get(std::string_view key) const {
    const auto it = map_.find(std::string(key));
    if (it == map_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  // true if a previous mapping existed and was removed.
  bool erase(std::string_view key) {
    return map_.erase(std::string(key)) > 0;
  }

  [[nodiscard]] bool contains(std::string_view key) const {
    return map_.find(std::string(key)) != map_.end();
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
