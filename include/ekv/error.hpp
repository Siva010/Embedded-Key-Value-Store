#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace ekv {

// High-level error categories used by the public API.
// Keep this list small; map OS / I/O failures into these as persistence lands.
enum class ErrorCode {
  Ok = 0,
  NotOpen,
  AlreadyOpen,
  NotFound,
  InvalidArgument,
  IoError,
};

[[nodiscard]] constexpr std::string_view to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::Ok:
      return "ok";
    case ErrorCode::NotOpen:
      return "not open";
    case ErrorCode::AlreadyOpen:
      return "already open";
    case ErrorCode::NotFound:
      return "not found";
    case ErrorCode::InvalidArgument:
      return "invalid argument";
    case ErrorCode::IoError:
      return "I/O error";
  }
  return "unknown";
}

// Thrown by mutating / open paths that cannot return a soft failure.
// Lookup misses use std::optional instead of exceptions.
class Error : public std::runtime_error {
 public:
  Error(ErrorCode code, std::string message)
      : std::runtime_error(std::move(message)), code_(code) {}

  [[nodiscard]] ErrorCode code() const noexcept { return code_; }

 private:
  ErrorCode code_;
};

}  // namespace ekv
