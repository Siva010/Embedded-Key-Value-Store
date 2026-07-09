// Phase 6: Options / SyncMode wiring.
#include "ekv/options.hpp"
#include "ekv/store.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static int failures = 0;

#define EXPECT(cond)                                                         \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "EXPECT failed: " << #cond << " (" << __FILE__ << ":"    \
                << __LINE__ << ")\n";                                        \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

static fs::path unique_temp_dir(const char* name) {
  const auto dir = fs::temp_directory_path() / (std::string("ekv_") + name);
  fs::remove_all(dir);
  return dir;
}

static void test_default_options_are_full_sync() {
  const fs::path dir = unique_temp_dir("opt_default");
  ekv::Store db;
  db.open(dir);
  EXPECT(db.options().sync_mode == ekv::SyncMode::Full);
  db.put("a", "1");
  db.close();

  db.open(dir);
  EXPECT(*db.get("a") == "1");
  db.close();
  fs::remove_all(dir);
}

static void test_flush_mode_persists_after_close() {
  const fs::path dir = unique_temp_dir("opt_flush");
  ekv::Options opt;
  opt.sync_mode = ekv::SyncMode::Flush;

  {
    ekv::Store db;
    db.open(dir, opt);
    EXPECT(db.options().sync_mode == ekv::SyncMode::Flush);
    db.put("k", "v");
    db.close();
  }

  ekv::Store db;
  db.open(dir, opt);
  EXPECT(*db.get("k") == "v");
  db.close();
  fs::remove_all(dir);
}

static void test_none_mode_still_reopens_after_clean_close() {
  // SyncMode::None skips per-append durability, but close/flush of the stream
  // and process exit typically still leave a readable file after clean close.
  const fs::path dir = unique_temp_dir("opt_none");
  ekv::Options opt;
  opt.sync_mode = ekv::SyncMode::None;

  {
    ekv::Store db;
    db.open(dir, opt);
    EXPECT(db.options().sync_mode == ekv::SyncMode::None);
    for (int i = 0; i < 100; ++i) {
      db.put("k" + std::to_string(i), "v");
    }
    db.close();
  }

  ekv::Store db;
  db.open(dir);
  EXPECT(db.size() == 100);
  EXPECT(*db.get("k0") == "v");
  db.close();
  fs::remove_all(dir);
}

static void test_to_string_sync_mode() {
  EXPECT(ekv::to_string(ekv::SyncMode::Full) == "full");
  EXPECT(ekv::to_string(ekv::SyncMode::Flush) == "flush");
  EXPECT(ekv::to_string(ekv::SyncMode::None) == "none");
}

int main() {
  test_default_options_are_full_sync();
  test_flush_mode_persists_after_close();
  test_none_mode_still_reopens_after_clean_close();
  test_to_string_sync_mode();

  if (failures != 0) {
    std::cerr << failures << " expectation(s) failed\n";
    return 1;
  }
  std::cout << "options_test: ok\n";
  return 0;
}
