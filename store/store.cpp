#include "ekv/store.hpp"

#include <filesystem>
#include <string>
#include <utility>

namespace ekv {
namespace fs = std::filesystem;

Store::~Store() { close(); }

Store::Store(Store&& other) noexcept
    : open_(other.open_),
      path_(std::move(other.path_)),
      index_(std::move(other.index_)) {
  other.open_ = false;
}

Store& Store::operator=(Store&& other) noexcept {
  if (this != &other) {
    close();
    open_ = other.open_;
    path_ = std::move(other.path_);
    index_ = std::move(other.index_);
    other.open_ = false;
  }
  return *this;
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

  index_.clear();
  open_ = true;
}

void Store::close() noexcept {
  if (!open_) {
    return;
  }
  index_.clear();
  path_.clear();
  open_ = false;
}

void Store::ensure_open() const {
  if (!open_) {
    throw Error(ErrorCode::NotOpen, "store is not open");
  }
}

void Store::put(std::string_view key, std::string_view value) {
  ensure_open();
  if (key.empty()) {
    throw Error(ErrorCode::InvalidArgument, "key must not be empty");
  }
  index_.put(std::string(key), std::string(value));
}

std::optional<std::string> Store::get(std::string_view key) const {
  ensure_open();
  if (key.empty()) {
    throw Error(ErrorCode::InvalidArgument, "key must not be empty");
  }
  return index_.get(key);
}

bool Store::Delete(std::string_view key) {
  ensure_open();
  if (key.empty()) {
    throw Error(ErrorCode::InvalidArgument, "key must not be empty");
  }
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
