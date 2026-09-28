#pragma once

// Minimal host test harness: TEST(name) { CHECK(...); CHECK_EQ(a, b); }

#include <stdio.h>

#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace check {

struct Test {
  const char* name;
  std::function<void()> fn;
};

inline std::vector<Test>& registry() {
  static std::vector<Test> tests;
  return tests;
}

inline int& failures() {
  static int n = 0;
  return n;
}

inline const char*& current() {
  static const char* name = "";
  return name;
}

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline void fail(const char* file, int line, const std::string& what) {
  failures()++;
  printf("  FAIL %s (%s:%d): %s\n", current(), file, line, what.c_str());
}

template <typename A, typename B>
void checkEq(const A& a, const B& b, const char* as, const char* bs, const char* file, int line) {
  if (a == b) return;
  std::ostringstream os;
  os << as << " == " << bs << "  (got " << a << " vs " << b << ")";
  fail(file, line, os.str());
}

inline int runAll() {
  int failedTests = 0;
  for (const Test& t : registry()) {
    current() = t.name;
    const int before = failures();
    t.fn();
    const bool ok = failures() == before;
    if (!ok) failedTests++;
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", t.name);
  }
  printf("==== %zu tests, %d failed ====\n", registry().size(), failedTests);
  return failedTests ? 1 : 0;
}

}  // namespace check

#define CHECK_CAT2(a, b) a##b
#define CHECK_CAT(a, b) CHECK_CAT2(a, b)
#define TEST(name)                                                              \
  static void CHECK_CAT(test_, name)();                                         \
  static check::Registrar CHECK_CAT(reg_, name)(#name, CHECK_CAT(test_, name)); \
  static void CHECK_CAT(test_, name)()

#define CHECK(cond) \
  do {              \
    if (!(cond)) check::fail(__FILE__, __LINE__, #cond); \
  } while (0)

#define CHECK_EQ(a, b) check::checkEq((a), (b), #a, #b, __FILE__, __LINE__)
