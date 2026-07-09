// Simple throughput / latency bench for put and get.
//
// Example:
//   ekv_bench --ops 10000 --value-size 64 --sync flush
//   ekv_bench --ops 2000 --sync full --dir ./bench_data

#include "ekv/options.hpp"
#include "ekv/store.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using clock_type = std::chrono::steady_clock;

struct Args {
  std::uint64_t ops = 5000;
  std::size_t value_size = 64;
  ekv::SyncMode sync = ekv::SyncMode::Flush;
  fs::path dir = fs::temp_directory_path() / "ekv_bench_data";
  bool keep = false;
};

static void usage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0
      << " [--ops N] [--value-size N] [--sync full|flush|none] [--dir PATH] "
         "[--keep]\n";
}

static bool parse_args(int argc, char** argv, Args& out) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view a = argv[i];
    auto need = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        std::cerr << "missing value for " << name << '\n';
        return nullptr;
      }
      return argv[++i];
    };

    if (a == "--ops") {
      const char* v = need("--ops");
      if (!v) {
        return false;
      }
      out.ops = static_cast<std::uint64_t>(std::strtoull(v, nullptr, 10));
    } else if (a == "--value-size") {
      const char* v = need("--value-size");
      if (!v) {
        return false;
      }
      out.value_size = static_cast<std::size_t>(std::strtoull(v, nullptr, 10));
    } else if (a == "--sync") {
      const char* v = need("--sync");
      if (!v) {
        return false;
      }
      const std::string_view s = v;
      if (s == "full") {
        out.sync = ekv::SyncMode::Full;
      } else if (s == "flush") {
        out.sync = ekv::SyncMode::Flush;
      } else if (s == "none") {
        out.sync = ekv::SyncMode::None;
      } else {
        std::cerr << "unknown --sync mode: " << v << '\n';
        return false;
      }
    } else if (a == "--dir") {
      const char* v = need("--dir");
      if (!v) {
        return false;
      }
      out.dir = v;
    } else if (a == "--keep") {
      out.keep = true;
    } else if (a == "-h" || a == "--help") {
      usage(argv[0]);
      std::exit(0);
    } else {
      std::cerr << "unknown arg: " << a << '\n';
      return false;
    }
  }
  if (out.ops == 0) {
    std::cerr << "--ops must be > 0\n";
    return false;
  }
  return true;
}

static double ms_since(clock_type::time_point start) {
  return std::chrono::duration<double, std::milli>(clock_type::now() - start)
      .count();
}

static void report(std::string_view name, std::uint64_t ops, double ms) {
  const double ops_per_s = (ms > 0.0) ? (static_cast<double>(ops) * 1000.0 / ms)
                                      : 0.0;
  const double us_per_op =
      (ops > 0) ? (ms * 1000.0 / static_cast<double>(ops)) : 0.0;
  std::cout << std::fixed << std::setprecision(2);
  std::cout << name << ": ops=" << ops << " wall_ms=" << ms
            << " throughput_ops_s=" << ops_per_s
            << " avg_latency_us=" << us_per_op << '\n';
}

int main(int argc, char** argv) {
  Args args;
  if (!parse_args(argc, argv, args)) {
    usage(argv[0]);
    return 2;
  }

  fs::remove_all(args.dir);

  const std::string value(args.value_size, 'x');
  std::vector<std::string> keys;
  keys.reserve(static_cast<std::size_t>(args.ops));
  for (std::uint64_t i = 0; i < args.ops; ++i) {
    keys.push_back("k" + std::to_string(i));
  }

  ekv::Options opt;
  opt.sync_mode = args.sync;

  std::cout << "ekv_bench config: ops=" << args.ops
            << " value_size=" << args.value_size
            << " sync=" << ekv::to_string(args.sync)
            << " dir=" << args.dir.string() << '\n';

  ekv::Store db;
  db.open(args.dir, opt);

  // Sequential puts.
  {
    const auto t0 = clock_type::now();
    for (std::uint64_t i = 0; i < args.ops; ++i) {
      db.put(keys[static_cast<std::size_t>(i)], value);
    }
    report("put_seq", args.ops, ms_since(t0));
  }

  // Sequential gets (verify + measure).
  {
    const auto t0 = clock_type::now();
    std::uint64_t ok = 0;
    for (std::uint64_t i = 0; i < args.ops; ++i) {
      auto v = db.get(keys[static_cast<std::size_t>(i)]);
      if (v && v->size() == args.value_size) {
        ++ok;
      }
    }
    report("get_seq", args.ops, ms_since(t0));
    if (ok != args.ops) {
      std::cerr << "get verification failed: ok=" << ok << " expected="
                << args.ops << '\n';
      return 1;
    }
  }

  // Overwrite same key (append amplification / sync cost).
  {
    const auto t0 = clock_type::now();
    for (std::uint64_t i = 0; i < args.ops; ++i) {
      db.put("hot", value);
    }
    report("put_overwrite", args.ops, ms_since(t0));
  }

  db.close();

  // Reopen + cold get of first key (replay cost is open, not per get).
  {
    const auto t0 = clock_type::now();
    db.open(args.dir, opt);
    const double open_ms = ms_since(t0);
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "open_replay: wall_ms=" << open_ms
              << " log_bytes=" << db.log_size_bytes()
              << " keys=" << db.size() << '\n';

    const auto t1 = clock_type::now();
    auto v = db.get(keys.front());
    report("get_after_open", 1, ms_since(t1));
    if (!v) {
      std::cerr << "missing key after reopen\n";
      return 1;
    }
    db.close();
  }

  if (!args.keep) {
    fs::remove_all(args.dir);
  }
  return 0;
}
