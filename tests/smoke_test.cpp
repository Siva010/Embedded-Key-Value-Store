// Minimal smoke test: validates toolchain + library link before real API lands.
#include <cstdint>

namespace ekv {
void placeholder();
}

int main() {
  ekv::placeholder();
  static_assert(sizeof(std::uint64_t) == 8, "need 64-bit integers");
  return 0;
}
