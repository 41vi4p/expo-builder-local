#include <iostream>

#include "test_framework.hpp"

int main() {
  int total = 0;
  for (auto& tc : ebl::test::registry()) {
    ++total;
    std::cout << "RUN  " << tc.name << "\n";
    tc.fn();
  }
  std::cout << "\n" << total << " test(s) run, " << ebl::test::g_failures << " failure(s)\n";
  return ebl::test::g_failures == 0 ? 0 : 1;
}
