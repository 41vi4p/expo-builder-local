#pragma once
// A deliberately tiny, hand-rolled test harness - consistent with this project's
// existing "no vendored dependencies" stance (json.* is hand-written for the same
// reason, see its own header comment). Pulling in a real framework (Catch2/doctest)
// would be reasonable too, but for the handful of pure-logic units currently worth
// unit-testing (json.cpp, detect.cpp), this is enough: EBL_TEST registers a function
// at static-init time, EBL_CHECK/EBL_CHECK_EQ record failures without aborting the
// run, test_main.cpp runs everything registered and exits non-zero if anything failed.
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace ebl::test {

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

struct Registrar {
  Registrar(std::string name, std::function<void()> fn) { registry().push_back({std::move(name), std::move(fn)}); }
};

inline int g_failures = 0;

}  // namespace ebl::test

#define EBL_TEST(name)                                                 \
  static void name();                                                 \
  static ::ebl::test::Registrar registrar_##name(#name, name);         \
  static void name()

#define EBL_CHECK(cond)                                                                            \
  do {                                                                                              \
    if (!(cond)) {                                                                                  \
      std::cerr << "  FAIL: " << #cond << " (" << __FILE__ << ":" << __LINE__ << ")\n";             \
      ++::ebl::test::g_failures;                                                                    \
    }                                                                                                \
  } while (0)

#define EBL_CHECK_EQ(a, b)                                                                          \
  do {                                                                                               \
    auto ebl_check_eq_a_ = (a);                                                                      \
    auto ebl_check_eq_b_ = (b);                                                                      \
    if (!(ebl_check_eq_a_ == ebl_check_eq_b_)) {                                                     \
      std::cerr << "  FAIL: " << #a << " == " << #b << " (" << __FILE__ << ":" << __LINE__ << ")"    \
                << " - got \"" << ebl_check_eq_a_ << "\" vs \"" << ebl_check_eq_b_ << "\"\n";         \
      ++::ebl::test::g_failures;                                                                     \
    }                                                                                                 \
  } while (0)
