// Smoke test: public API basics (in-memory Phase 1).
#include "ekv/store.hpp"
#include "ekv/version.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " << #cond << " (" << __FILE__ << ":"     \
                << __LINE__ << ")\n";                                        \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

int main() {
  const auto ver = ekv::version();
  CHECK(ver.major == EKV_VERSION_MAJOR);
  CHECK(std::string(EKV_VERSION_STRING) == "0.1.0");

  const fs::path dir = fs::temp_directory_path() / "ekv_smoke_phase1";
  fs::remove_all(dir);

  ekv::Store store;
  CHECK(!store.is_open());

  store.open(dir);
  CHECK(store.is_open());
  CHECK(store.empty());

  store.put("alpha", "one");
  store.put("beta", "two");
  CHECK(store.size() == 2);

  auto a = store.get("alpha");
  CHECK(a.has_value());
  CHECK(*a == "one");

  store.put("alpha", "updated");
  CHECK(*store.get("alpha") == "updated");

  CHECK(store.Delete("beta"));
  CHECK(!store.get("beta").has_value());
  CHECK(!store.Delete("beta"));
  CHECK(store.size() == 1);

  // Empty key rejected.
  bool threw = false;
  try {
    store.put("", "x");
  } catch (const ekv::Error& e) {
    threw = e.code() == ekv::ErrorCode::InvalidArgument;
  }
  CHECK(threw);

  // Double open rejected.
  threw = false;
  try {
    store.open(dir);
  } catch (const ekv::Error& e) {
    threw = e.code() == ekv::ErrorCode::AlreadyOpen;
  }
  CHECK(threw);

  store.close();
  CHECK(!store.is_open());

  threw = false;
  try {
    (void)store.get("alpha");
  } catch (const ekv::Error& e) {
    threw = e.code() == ekv::ErrorCode::NotOpen;
  }
  CHECK(threw);

  // Phase 1: data is not durable across open.
  store.open(dir);
  CHECK(store.empty());
  store.close();

  fs::remove_all(dir);

  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "ekv_smoke: ok\n";
  return 0;
}
