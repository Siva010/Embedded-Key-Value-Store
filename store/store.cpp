#include "ekv/store.hpp"

#include "ekv/record.hpp"

#include <filesystem>
#include <string>
#include <utility>

namespace ekv {
namespace fs = std::filesystem;

namespace {

fs::path log_path_for(const fs::path& dir) { return dir / "ekv.log"; }

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

std::size_t Store::size() const {
  ensure_open();
  return index_.size();
}

bool Store::empty() const {
  ensure_open();
  return index_.empty();
}

}  // namespace ekv
