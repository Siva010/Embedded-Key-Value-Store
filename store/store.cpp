#include "ekv/store.hpp"

#include "ekv/os_sync.hpp"
#include "ekv/record.hpp"

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

Store::~Store() { close(); }

Store::Store(Store&& other) noexcept {
  std::unique_lock lock(other.mu_);
  open_ = other.open_;
  options_ = other.options_;
  path_ = std::move(other.path_);
  index_ = std::move(other.index_);
  log_ = std::move(other.log_);
  other.open_ = false;
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
  other.open_ = false;
  return *this;
}

void Store::ensure_open_unlocked() const {
  if (!open_) {
    throw Error(ErrorCode::NotOpen, "store is not open");
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
  const RecordLocator loc = log_.append_put(key, value);
  index_.put(std::string(key), loc);
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
  if (!index_.contains(key)) {
    return false;
  }
  log_.append_delete(key);
  return index_.erase(key);
}

CompactStats Store::compact() {
  std::unique_lock lock(mu_);
  ensure_open_unlocked();

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
    // Explicit full durable point before install regardless of SyncMode.
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

}  // namespace ekv
