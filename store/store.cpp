#include "ekv/store.hpp"

#include "ekv/os_sync.hpp"
#include "ekv/record.hpp"

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace ekv {
namespace fs = std::filesystem;

namespace {

fs::path log_path_for(const fs::path& dir) { return dir / "ekv.log"; }

fs::path compact_path_for(const fs::path& dir) {
  return dir / kCompactTempName;
}

fs::path old_path_for(const fs::path& dir) { return dir / kCompactOldName; }

}  // namespace

std::uint64_t Store::live_record_bytes(std::size_t key_len,
                                       std::uint32_t value_len) noexcept {
  return record_total_size(static_cast<std::uint32_t>(key_len), value_len);
}

Store::~Store() { close(); }

Store::Store(Store&& other) noexcept {
  std::unique_lock lock(other.mu_);
  open_ = other.open_;
  options_ = other.options_;
  path_ = std::move(other.path_);
  index_ = std::move(other.index_);
  log_ = std::move(other.log_);
  live_bytes_ = other.live_bytes_;
  other.open_ = false;
  other.live_bytes_ = 0;
}

Store& Store::operator=(Store&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  std::unique_lock lk_this(mu_, std::defer_lock);
  std::unique_lock lk_other(other.mu_, std::defer_lock);
  std::lock(lk_this, lk_other);

  close_unlocked();
  open_ = other.open_;
  options_ = other.options_;
  path_ = std::move(other.path_);
  index_ = std::move(other.index_);
  log_ = std::move(other.log_);
  live_bytes_ = other.live_bytes_;
  other.open_ = false;
  other.live_bytes_ = 0;
  return *this;
}

void Store::ensure_open_unlocked() const {
  if (!open_) {
    throw Error(ErrorCode::NotOpen, "store is not open");
  }
}

void Store::recompute_live_bytes() {
  live_bytes_ = 0;
  for (const auto& entry : index_) {
    live_bytes_ +=
        live_record_bytes(entry.first.size(), entry.second.value_size);
  }
}

void Store::rebuild_index_from_log() {
  index_.clear();
  log_.replay([this](std::uint8_t type, std::string_view key,
                     RecordLocator locator) {
    if (type == kRecordPut) {
      index_.put(std::string(key), locator);
    } else if (type == kRecordDelete) {
      index_.erase(key);
    }
  });
  recompute_live_bytes();
}

void Store::cleanup_compaction_artifacts(const fs::path& dir) {
  const auto logp = log_path_for(dir);
  const auto compactp = compact_path_for(dir);
  const auto oldp = old_path_for(dir);

  std::error_code ec;

  if (fs::exists(compactp, ec) && fs::exists(logp, ec)) {
    fs::remove(compactp, ec);
  }

  if (fs::exists(compactp, ec) && !fs::exists(logp, ec)) {
    fs::rename(compactp, logp, ec);
    if (ec) {
      throw Error(ErrorCode::IoError,
                  "failed to finish interrupted compaction rename: " +
                      ec.message());
    }
    sync_directory(dir);
  }

  if (fs::exists(oldp, ec)) {
    fs::remove(oldp, ec);
  }
}

void Store::open(const fs::path& path) { open(path, Options{}); }

void Store::open(const fs::path& path, Options options) {
  std::unique_lock lock(mu_);
  open_unlocked(path, options);
}

void Store::open_unlocked(const fs::path& path, Options options) {
  if (open_) {
    throw Error(ErrorCode::AlreadyOpen, "store is already open");
  }
  if (path.empty()) {
    throw Error(ErrorCode::InvalidArgument, "data path must not be empty");
  }
  if (options.auto_compact_ratio < 0.0) {
    throw Error(ErrorCode::InvalidArgument,
                "auto_compact_ratio must be >= 0");
  }

  std::error_code ec;
  fs::create_directories(path, ec);
  if (ec) {
    throw Error(ErrorCode::IoError,
                "failed to create data directory: " + ec.message());
  }

  path_ = fs::absolute(path, ec);
  if (ec) {
    path_ = path;
  }
  options_ = options;

  try {
    cleanup_compaction_artifacts(path_);
    log_.open(log_path_for(path_), options_.sync_mode);
    rebuild_index_from_log();
  } catch (...) {
    log_.close();
    path_.clear();
    index_.clear();
    live_bytes_ = 0;
    options_ = Options{};
    throw;
  }

  open_ = true;
}

void Store::close_unlocked() noexcept {
  if (!open_) {
    return;
  }
  log_.close();
  index_.clear();
  path_.clear();
  options_ = Options{};
  live_bytes_ = 0;
  open_ = false;
}

void Store::close() noexcept {
  std::unique_lock lock(mu_);
  close_unlocked();
}

bool Store::is_open() const noexcept {
  std::shared_lock lock(mu_);
  return open_;
}

std::filesystem::path Store::path() const {
  std::shared_lock lock(mu_);
  return path_;
}

Options Store::options() const {
  std::shared_lock lock(mu_);
  return options_;
}

void Store::put(std::string_view key, std::string_view value) {
  std::unique_lock lock(mu_);
  ensure_open_unlocked();
  if (key.empty()) {
    throw Error(ErrorCode::InvalidArgument, "key must not be empty");
  }

  if (const auto old = index_.get(key)) {
    const auto sub =
        live_record_bytes(key.size(), old->value_size);
    live_bytes_ = (live_bytes_ > sub) ? (live_bytes_ - sub) : 0;
  }

  const RecordLocator loc = log_.append_put(key, value);
  index_.put(std::string(key), loc);
  live_bytes_ +=
      live_record_bytes(key.size(), static_cast<std::uint32_t>(value.size()));

  maybe_auto_compact_unlocked();
}

std::optional<std::string> Store::get(std::string_view key) const {
  std::shared_lock lock(mu_);
  ensure_open_unlocked();
  if (key.empty()) {
    throw Error(ErrorCode::InvalidArgument, "key must not be empty");
  }
  const auto loc = index_.get(key);
  if (!loc.has_value()) {
    return std::nullopt;
  }
  return log_.read_value(*loc);
}

bool Store::Delete(std::string_view key) {
  std::unique_lock lock(mu_);
  ensure_open_unlocked();
  if (key.empty()) {
    throw Error(ErrorCode::InvalidArgument, "key must not be empty");
  }
  const auto old = index_.get(key);
  if (!old.has_value()) {
    return false;
  }

  const auto sub = live_record_bytes(key.size(), old->value_size);
  live_bytes_ = (live_bytes_ > sub) ? (live_bytes_ - sub) : 0;

  log_.append_delete(key);
  const bool erased = index_.erase(key);
  maybe_auto_compact_unlocked();
  return erased;
}

void Store::maybe_auto_compact_unlocked() {
  if (options_.auto_compact_ratio <= 0.0) {
    return;
  }
  const std::uint64_t log_bytes = log_.size_bytes();
  if (log_bytes < options_.auto_compact_min_bytes) {
    return;
  }
  const std::uint64_t denom = (std::max)(live_bytes_, std::uint64_t{1});
  const double amp =
      static_cast<double>(log_bytes) / static_cast<double>(denom);
  if (amp >= options_.auto_compact_ratio) {
    (void)compact_unlocked();
  }
}

CompactStats Store::compact() {
  std::unique_lock lock(mu_);
  ensure_open_unlocked();
  return compact_unlocked();
}

CompactStats Store::compact_unlocked() {
  CompactStats stats;
  stats.bytes_before = log_.size_bytes();
  stats.live_keys = index_.size();

  const fs::path logp = log_path_for(path_);
  const fs::path compactp = compact_path_for(path_);
  const fs::path oldp = old_path_for(path_);

  std::error_code ec;
  fs::remove(compactp, ec);
  fs::remove(oldp, ec);

  struct Live {
    std::string key;
    std::string value;
  };
  std::vector<Live> live;
  live.reserve(index_.size());
  for (const auto& entry : index_) {
    Live row;
    row.key = entry.first;
    row.value = log_.read_value(entry.second);
    live.push_back(std::move(row));
  }

  {
    AppendLog fresh;
    fresh.open(compactp, options_.sync_mode);
    for (const auto& row : live) {
      (void)fresh.append_put(row.key, row.value);
    }
    fresh.sync();
    stats.bytes_after = fresh.size_bytes();
    fresh.close();
  }

  log_.close();

  try {
    if (fs::exists(logp, ec)) {
      fs::rename(logp, oldp, ec);
      if (ec) {
        fs::remove(logp, ec);
        if (ec) {
          throw Error(ErrorCode::IoError,
                      "failed to move aside old log: " + ec.message());
        }
      }
    }

    replace_file(compactp, logp);

    if (fs::exists(oldp, ec)) {
      fs::remove(oldp, ec);
    }
    sync_directory(path_);
  } catch (...) {
    try {
      if (!fs::exists(logp, ec) && fs::exists(oldp, ec)) {
        fs::rename(oldp, logp, ec);
      }
      if (fs::exists(logp, ec)) {
        log_.open(logp, options_.sync_mode);
        rebuild_index_from_log();
      }
    } catch (...) {
    }
    open_ = log_.is_open();
    throw;
  }

  log_.open(logp, options_.sync_mode);
  rebuild_index_from_log();
  stats.live_keys = index_.size();
  stats.bytes_after = log_.size_bytes();
  return stats;
}

std::size_t Store::size() const {
  std::shared_lock lock(mu_);
  ensure_open_unlocked();
  return index_.size();
}

bool Store::empty() const {
  std::shared_lock lock(mu_);
  ensure_open_unlocked();
  return index_.empty();
}

std::uint64_t Store::log_size_bytes() const {
  std::shared_lock lock(mu_);
  ensure_open_unlocked();
  return log_.size_bytes();
}

StoreStats Store::stats() const {
  std::shared_lock lock(mu_);
  ensure_open_unlocked();
  StoreStats s;
  s.live_keys = index_.size();
  s.log_bytes = log_.size_bytes();
  s.live_bytes = live_bytes_;
  const std::uint64_t denom = (std::max)(live_bytes_, std::uint64_t{1});
  s.space_amp =
      static_cast<double>(s.log_bytes) / static_cast<double>(denom);
  return s;
}

}  // namespace ekv
