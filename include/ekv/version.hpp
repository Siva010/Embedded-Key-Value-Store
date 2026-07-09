#pragma once

// Public version macros for the embedded key-value store.
// Keep in sync with project(VERSION ...) in the root CMakeLists.txt.

#define EKV_VERSION_MAJOR 0
#define EKV_VERSION_MINOR 1
#define EKV_VERSION_PATCH 0

#define EKV_VERSION_STRING "0.1.0"

namespace ekv {

struct Version {
  int major = EKV_VERSION_MAJOR;
  int minor = EKV_VERSION_MINOR;
  int patch = EKV_VERSION_PATCH;
};

constexpr Version version() noexcept {
  return {};
}

}  // namespace ekv
