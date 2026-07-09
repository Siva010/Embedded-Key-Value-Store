# ARM / QEMU notes (Phase 6)

The engine is portable C++20 + STL with thin OS shims (`fsync` /
`FlushFileBuffers`, positioned reads). It builds on x86_64 host toolchains and
can be cross-compiled for aarch64.

## Native Linux (including WSL2)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/bench/ekv_bench --ops 20000 --sync flush
```

## Cross-compile to aarch64 (from Debian/Ubuntu x86_64)

```bash
sudo apt-get install -y g++-aarch64-linux-gnu qemu-user-static

cmake -S . -B build-arm64 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DEKV_BUILD_TESTS=ON

cmake --build build-arm64 -j
```

Run under QEMU user-mode (binaries are aarch64 ELF):

```bash
qemu-aarch64-static ./build-arm64/tests/ekv_smoke
qemu-aarch64-static ./build-arm64/bench/ekv_bench --ops 2000 --sync none
```

If dynamic linker paths fail under qemu-user, either:

- install `libc6-arm64-cross` and set `QEMU_LD_PREFIX`, or
- prefer static linking for smoke checks:
  `cmake ... -DCMAKE_EXE_LINKER_FLAGS="-static"` (where supported).

## Windows host

- Day-to-day: MinGW (WinLibs) or MSVC, as used in development.
- ARM64 Windows: use an ARM64 native toolchain, or build Linux aarch64 artifacts
  inside WSL2 and run with `qemu-user-static` there.
- Full system QEMU (virtual machine) is optional; user-mode is enough to validate
  instruction set + ABI for unit tests and microbenchmarks.

## What we validate on ARM

| Check | Notes |
|-------|--------|
| Smoke / store / recovery tests | Same logic as host |
| Endianness | On-disk format is **little-endian** (explicit); aarch64 LE is fine |
| `fsync` / positional read | Linux paths in `os_sync.cpp` |
| Performance | Expect lower absolute numbers under qemu-user than bare metal |

## Non-goals

- Official CI matrix for every board (RPi, etc.) — document and toolchain only.
- Android NDK / bare-metal freestanding builds.
