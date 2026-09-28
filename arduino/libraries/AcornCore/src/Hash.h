#pragma once

#include <stdint.h>

#include <string>

namespace acorn {

// FNV-1a 32-bit over the bytes of `s`.
inline uint32_t fnv1a(const std::string& s) {
  uint32_t h = 0x811c9dc5u;
  for (unsigned char c : s) {
    h ^= c;
    h *= 0x01000193u;
  }
  return h;
}

// mulberry32 PRNG. next() returns a double in [0, 1), bit-identical to the
// JavaScript reference used by the dev mock (docs/scheduling.md).
class Mulberry32 {
 public:
  explicit Mulberry32(uint32_t seed) : a_(seed) {}
  double next() {
    a_ += 0x6d2b79f5u;
    uint32_t t = a_;
    t = (t ^ (t >> 15)) * (1u | t);
    t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
  }

 private:
  uint32_t a_;
};

}  // namespace acorn
