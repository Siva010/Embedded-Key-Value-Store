// Phase 5: single-writer / multi-reader stress.
#include "ekv/store.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

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

static void test_concurrent_readers_see_stable_keys() {
  const fs::path dir = unique_temp_dir("conc_readers");
  ekv::Store db;
  db.open(dir);

  constexpr int kKeys = 64;
  for (int i = 0; i < kKeys; ++i) {
    db.put("k" + std::to_string(i), "v" + std::to_string(i));
  }

  std::atomic<int> errors{0};
  std::atomic<bool> run{true};

  auto reader = [&] {
    while (run.load(std::memory_order_relaxed)) {
      for (int i = 0; i < kKeys; ++i) {
        const auto key = "k" + std::to_string(i);
        auto v = db.get(key);
        if (!v || *v != "v" + std::to_string(i)) {
          errors.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
  };

  constexpr int kReaders = 4;
  std::vector<std::thread> threads;
  threads.reserve(static_cast<std::size_t>(kReaders));
  for (int i = 0; i < kReaders; ++i) {
    threads.emplace_back(reader);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  run.store(false, std::memory_order_relaxed);
  for (auto& t : threads) {
    t.join();
  }

  EXPECT(errors.load() == 0);
  db.close();
  fs::remove_all(dir);
}

static void test_single_writer_many_readers() {
  const fs::path dir = unique_temp_dir("conc_swmr");
  ekv::Store db;
  db.open(dir);

  db.put("x", "0");

  std::atomic<int> write_errors{0};
  std::atomic<int> read_errors{0};
  std::atomic<bool> run{true};
  std::atomic<int> last_written{0};

  auto writer = [&] {
    try {
      for (int i = 1; i <= 200; ++i) {
        db.put("x", std::to_string(i));
        last_written.store(i, std::memory_order_release);
      }
    } catch (...) {
      write_errors.fetch_add(1, std::memory_order_relaxed);
    }
    run.store(false, std::memory_order_release);
  };

  auto reader = [&] {
    while (run.load(std::memory_order_acquire) ||
           last_written.load(std::memory_order_acquire) < 200) {
      auto v = db.get("x");
      if (!v) {
        read_errors.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
      // Value must be a non-negative integer string and never go backwards
      // past what could have been written (monotonic puts of "x").
      try {
        const int n = std::stoi(*v);
        if (n < 0 || n > 200) {
          read_errors.fetch_add(1, std::memory_order_relaxed);
        }
      } catch (...) {
        read_errors.fetch_add(1, std::memory_order_relaxed);
      }
      if (!run.load(std::memory_order_acquire) &&
          last_written.load(std::memory_order_acquire) >= 200) {
        break;
      }
    }
  };

  std::thread w(writer);
  std::vector<std::thread> readers;
  for (int i = 0; i < 6; ++i) {
    readers.emplace_back(reader);
  }
  w.join();
  for (auto& t : readers) {
    t.join();
  }

  EXPECT(write_errors.load() == 0);
  EXPECT(read_errors.load() == 0);
  auto final_v = db.get("x");
  EXPECT(final_v.has_value());
  EXPECT(*final_v == "200");

  db.close();

  // Durable after concurrent workload.
  db.open(dir);
  EXPECT(*db.get("x") == "200");
  db.close();
  fs::remove_all(dir);
}

static void test_writer_and_compact_with_readers() {
  const fs::path dir = unique_temp_dir("conc_compact");
  ekv::Store db;
  db.open(dir);

  for (int i = 0; i < 32; ++i) {
    db.put("k" + std::to_string(i), "init");
  }

  std::atomic<bool> run{true};
  std::atomic<int> errors{0};

  auto reader = [&] {
    while (run.load(std::memory_order_relaxed)) {
      for (int i = 0; i < 32; ++i) {
        auto v = db.get("k" + std::to_string(i));
        if (!v) {
          errors.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
  };

  std::vector<std::thread> readers;
  for (int i = 0; i < 3; ++i) {
    readers.emplace_back(reader);
  }

  try {
    for (int round = 0; round < 20; ++round) {
      for (int i = 0; i < 32; ++i) {
        db.put("k" + std::to_string(i), "r" + std::to_string(round));
      }
      if (round % 5 == 4) {
        (void)db.compact();
      }
    }
  } catch (...) {
    errors.fetch_add(1, std::memory_order_relaxed);
  }

  run.store(false, std::memory_order_relaxed);
  for (auto& t : readers) {
    t.join();
  }

  EXPECT(errors.load() == 0);
  for (int i = 0; i < 32; ++i) {
    EXPECT(*db.get("k" + std::to_string(i)) == "r19");
  }

  db.close();
  fs::remove_all(dir);
}

static void test_delete_under_concurrency() {
  const fs::path dir = unique_temp_dir("conc_delete");
  ekv::Store db;
  db.open(dir);

  std::atomic<bool> run{true};
  std::atomic<int> errors{0};

  auto writer = [&] {
    try {
      for (int i = 0; i < 100; ++i) {
        const auto k = "d" + std::to_string(i % 10);
        db.put(k, "v");
        if (i % 2 == 1) {
          (void)db.Delete(k);
        }
      }
    } catch (...) {
      errors.fetch_add(1, std::memory_order_relaxed);
    }
    run.store(false, std::memory_order_relaxed);
  };

  auto reader = [&] {
    while (run.load(std::memory_order_relaxed)) {
      for (int i = 0; i < 10; ++i) {
        (void)db.get("d" + std::to_string(i));  // optional presence
      }
    }
  };

  std::thread w(writer);
  std::vector<std::thread> readers;
  for (int i = 0; i < 4; ++i) {
    readers.emplace_back(reader);
  }
  w.join();
  for (auto& t : readers) {
    t.join();
  }

  EXPECT(errors.load() == 0);
  // Even keys 0,2,4,6,8 last op was put; odd last was delete (i=99 odd -> delete d9).
  // i=90..99: put d0, del d1, put d2, ... pattern by i%10 and i%2.
  db.close();
  fs::remove_all(dir);
}

int main() {
  test_concurrent_readers_see_stable_keys();
  test_single_writer_many_readers();
  test_writer_and_compact_with_readers();
  test_delete_under_concurrency();

  if (failures != 0) {
    std::cerr << failures << " expectation(s) failed\n";
    return 1;
  }
  std::cout << "concurrency_test: ok\n";
  return 0;
}
