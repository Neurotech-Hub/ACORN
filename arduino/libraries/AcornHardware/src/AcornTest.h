#pragma once

// Minimal serial reporting for the isolated hardware test sketches.
// Output format: "[PASS] name", "[FAIL] name (detail)", "[INFO] ...".

#include <Arduino.h>

namespace acorn {
namespace test {

inline uint16_t& passes() {
  static uint16_t n = 0;
  return n;
}
inline uint16_t& failures() {
  static uint16_t n = 0;
  return n;
}

inline void begin(const char* name) {
  Serial.begin(115200);
  const uint32_t start = millis();
  while (!Serial && millis() - start < 3000) delay(10);
  delay(200);
  Serial.println();
  Serial.printf("==== ACORN %s ====\n", name);
}

inline bool check(const char* name, bool ok, const char* detail = nullptr) {
  if (ok) passes()++;
  else failures()++;
  if (detail && *detail) Serial.printf("[%s] %s (%s)\n", ok ? "PASS" : "FAIL", name, detail);
  else Serial.printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
  return ok;
}

template <typename... Args>
inline void info(const char* fmt, Args... args) {
  Serial.print("[INFO] ");
  Serial.printf(fmt, args...);
  Serial.println();
}

inline void info(const char* msg) {
  Serial.print("[INFO] ");
  Serial.println(msg);
}

inline void summary() {
  Serial.printf("==== RESULT: %s (%u passed, %u failed) ====\n", failures() ? "FAIL" : "PASS",
                passes(), failures());
}

// Read a trimmed line from Serial without blocking; returns true when complete.
inline bool readLine(String& out) {
  static String buf;
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      out = buf;
      out.trim();
      buf = "";
      return true;
    }
    if (buf.length() < 96) buf += c;
  }
  return false;
}

}  // namespace test
}  // namespace acorn
