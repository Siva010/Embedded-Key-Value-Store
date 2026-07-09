#include "ekv/store.hpp"

#include "ekv/os_sync.hpp"
#include "ekv/record.hpp"

#include <filesystem>
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

Store::Store(Store&& other) noexcept
    : open_(other.open_),
      path_(std::move(other.path_)),
      index_(std::move(other.index_)),
      log_(std::move(other.log_)) {
  other.open_ = false;
}

Store& Store::operator=(Store&& other) noexcept {
  if (this != &other) {
    close();
    open_ = other.open_;
    path_ = std::move(other.path_);
    index_ = std::move(other.index_);
    log_ = std::move(other.log_);
    other.open_ = false;
  }
  return *this;
}

void Store::ensure_open() const {
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

  // Crash during compact after new file was fully written but before replace:
  // keep the live log, drop the temp.
  if (fs::exists(compactp, ec) && fs::exists(logp, ec)) {
    fs::remove(compactp, ec);
  }

  // Crash after removing/renaming away the live log but before installing
  // the compact file as ekv.log — promote the compact file.
  if (fs::exists(compactp, ec) && !fs::exists(logp, ec)) {
    fs::rename(compactp, logp, ec);
    if (ec) {
      throw Error(ErrorCode::IoError,
                  "failed to finish interrupted compaction rename: " +
                      ec.message());
    }
    sync_directory(dir);
  }

  // Leftover backup from replace path.
  if (fs::exists(oldp, ec)) {
    fs::remove(oldp, ec);
  }
}

void Store::open(const fs::path& path) {
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

  try {
    cleanup_compaction_artifacts(path_);
    log_.open(log_path_for(path_));
    rebuild_index_from_log();
  } catch (...) {
    log_.close();
    path_.clear();
    index_.clear();
    throw;
  }

  open_ = true;
}

void Store::close() noexcept {
  if (!open_) {
    return;
  }
  log_.close();
  index_.clear();
  path_.clear();
  open_ = false;
}

void Store::put(std::string_view key, std::string_view value) {
  ensure_open();
  if (key.empty()) {
    throw Error(ErrorCode::InvalidArgument, "key must not be empty");
  }
  const RecordLocator loc = log_.append_put(key, value);
  index_.put(std::string(key), loc);
}

std::optional<std::string> Store::get(std::string_view key) const {
  ensure_open();
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
  ensure_open();
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
  ensure_open();

  CompactStats stats;
  stats.bytes_before = log_.size_bytes();
  stats.live_keys = index_.size();

  const fs::path logp = log_path_for(path_);
  const fs::path compactp = compact_path_for(path_);
  const fs::path oldp = old_path_for(path_);

  std::error_code ec;
  fs::remove(compactp, ec);
  fs::remove(oldp, ec);

  // Snapshot live keys so we can close/reopen the primary log safely.
  // Values are read from the still-open log before we cut over.
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
    fresh.open(compactp);
    for (const auto& row : live) {
      (void)fresh.append_put(row.key, row.value);
    }
    // Final durable point before install (each put already synced; belt+suspenders).
    fresh.sync();
    stats.bytes_after = fresh.size_bytes();
    fresh.close();
  }

  // Install: release handle on the live log, then replace the path.
  log_.close();

  try {
    // Keep a backup until the new file is installed (helps odd failure modes).
    if (fs::exists(logp, ec)) {
      fs::rename(logp, oldp, ec);
      if (ec) {
        // Windows may fail rename if something still holds the file; try remove.
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
    // Best-effort: try to reopen whatever log remains so the Store is usable.
    try {
      if (!fs::exists(logp, ec) && fs::exists(oldp, ec)) {
        fs::rename(oldp, logp, ec);
      }
      if (fs::exists(logp, ec)) {
        log_.open(logp);
        rebuild_index_from_log();
      }
    } catch (...) {
      // Leave closed; rethrow original.
    }
    open_ = log_.is_open();
    throw;
  }

  log_.open(logp);
  rebuild_index_from_log();
  stats.live_keys = index_.size();
  stats.bytes_after = log_.size_bytes();
  return stats;
}

std::size_t Store::size() const {
  ensure_open();
  return index_.size();
}

bool Store::empty() const {
  ensure_open();
  return index_.empty();
}

std::uint64_t Store::log_size_bytes() const {
  ensure_open();
  return log_.size_bytes();
}

}  // namespace ekv
