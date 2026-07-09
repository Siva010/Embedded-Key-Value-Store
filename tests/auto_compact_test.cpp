// Auto-compaction + stats + transparent index lookups.
#include "ekv/hash_index.hpp"
#include "ekv/options.hpp"
#include "ekv/record.hpp"
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

static void test_transparent_index_lookup() {
  ekv::HashIndex idx;
  idx.put("hello", ekv::RecordLocator{10, 3});
  // string_view lookup must not require constructing a temporary key beyond
  // the heterogeneous find path.
  const std::string_view sv = "hello";
  EXPECT(idx.contains(sv));
  EXPECT(idx.get(sv)->value_offset == 10);
  EXPECT(idx.erase(sv));
  EXPECT(!idx.contains(sv));
}

static void test_stats_space_amp_grows_with_overwrites() {
  const fs::path dir = unique_temp_dir("stats_amp");
  ekv::Options opt;
  opt.sync_mode = ekv::SyncMode::None;
  opt.auto_compact_ratio = 0.0;  // off

  ekv::Store db;
  db.open(dir, opt);
  db.put("k", "v0");
  const auto s0 = db.stats();
  EXPECT(s0.live_keys == 1);
  EXPECT(s0.log_bytes > 0);
  EXPECT(s0.live_bytes > 0);
  EXPECT(s0.space_amp >= 1.0);

  for (int i = 0; i < 40; ++i) {
    db.put("k", "v" + std::to_string(i));
  }
  const auto s1 = db.stats();
  EXPECT(s1.live_keys == 1);
  EXPECT(s1.log_bytes > s0.log_bytes);
  EXPECT(s1.space_amp > s0.space_amp);

  db.close();
  fs::remove_all(dir);
}

static void test_auto_compact_reclaims_space() {
  const fs::path dir = unique_temp_dir("auto_compact");
  ekv::Options opt;
  opt.sync_mode = ekv::SyncMode::None;
  opt.auto_compact_ratio = 3.0;
  opt.auto_compact_min_bytes = 512;  // low threshold for the test

  ekv::Store db;
  db.open(dir, opt);

  db.put("keep", "yes");
  // Generate garbage: many overwrites of the same key.
  for (int i = 0; i < 200; ++i) {
    db.put("hot", std::string(32, static_cast<char>('a' + (i % 26))));
  }

  const auto st = db.stats();
  EXPECT(st.live_keys == 2);
  // After enough garbage, auto-compact should have fired at least once.
  EXPECT(st.space_amp < 3.0 || st.log_bytes < opt.auto_compact_min_bytes * 2);
  EXPECT(*db.get("keep") == "yes");
  EXPECT(db.get("hot").has_value());

  // Force another wave and ensure store stays healthy.
  for (int i = 0; i < 50; ++i) {
    db.put("hot", "x");
  }
  EXPECT(*db.get("hot") == "x");

  db.close();
  db.open(dir, opt);
  EXPECT(*db.get("keep") == "yes");
  EXPECT(*db.get("hot") == "x");
  db.close();
  fs::remove_all(dir);
}

static void test_auto_compact_disabled_by_default() {
  const fs::path dir = unique_temp_dir("auto_off");
  ekv::Options opt;
  opt.sync_mode = ekv::SyncMode::None;

  ekv::Store db;
  db.open(dir, opt);
  EXPECT(db.options().auto_compact_ratio == 0.0);

  for (int i = 0; i < 100; ++i) {
    db.put("k", std::to_string(i));
  }
  const auto st = db.stats();
  // Without auto-compact, overwrites accumulate.
  EXPECT(st.space_amp > 2.0);
  db.close();
  fs::remove_all(dir);
}

int main() {
  test_transparent_index_lookup();
  test_stats_space_amp_grows_with_overwrites();
  test_auto_compact_reclaims_space();
  test_auto_compact_disabled_by_default();

  if (failures != 0) {
    std::cerr << failures << " expectation(s) failed\n";
    return 1;
  }
  std::cout << "auto_compact_test: ok\n";
  return 0;
}
