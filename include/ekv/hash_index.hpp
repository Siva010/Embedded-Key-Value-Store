#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ekv {

// In-memory key → value map (Phase 1).
//
// Phase 2 will change the mapped type from the full value to a durable
// location (file offset + length). The public methods intentionally take
// ownership of keys/values on put so callers can move large strings in.
class HashIndex {
 public:
  using Map = std::unordered_map<std::string, std::string>;

  void put(std::string key, std::string value) {
    map_.insert_or_assign(std::move(key), std::move(value));
  }

  // Returns a copy so the index remains free to relocate entries later.
  // Phase 2 may switch to reading bytes from the log instead of copying.
  [[nodiscard]] std::optional<std::string> get(std::string_view key) const {
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

  [[nodiscard]] Map::const_iterator begin() const noexcept { return map_.begin(); }
  [[nodiscard]] Map::const_iterator end() const noexcept { return map_.end(); }

 private:
  Map map_;
};

}  // namespace ekv
