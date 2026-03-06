#ifndef TESTS_TEST_COMMON_HPP_
#define TESTS_TEST_COMMON_HPP_

#include <iostream>
#include <string>

extern int g_failures;

inline void Check(bool condition, const std::string& message, const char* file,
                  int line) {
  if (condition) {
    return;
  }

  std::cerr << file << ":" << line << " FAIL: " << message << "\n";
  ++g_failures;
}

#define CHECK(cond) Check((cond), #cond, __FILE__, __LINE__)
#define CHECK_MSG(cond, msg) Check((cond), (msg), __FILE__, __LINE__)
#define CHECK_EQ(a, b)                                                         \
  Check(((a) == (b)), std::string(#a) + " == " + std::string(#b), __FILE__,   \
        __LINE__)

namespace testutil {

inline void PrintTestStart(const char* name) {
  std::cout << "\n[ RUN      ] " << name << "\n";
}

inline void PrintTestResult(const char* name, int new_failures) {
  if (new_failures == 0) {
    std::cout << "[       OK ] " << name << "\n";
    return;
  }
  std::cout << "[  FAILED  ] " << name << " (" << new_failures
            << " failure(s))\n";
}

inline void PrintInfo(const std::string& message) {
  std::cout << "  " << message << "\n";
}

}  // namespace testutil

#define RUN_TEST(fn)                                                           \
  do {                                                                         \
    testutil::PrintTestStart(#fn);                                             \
    const int before = g_failures;                                             \
    fn();                                                                      \
    const int after = g_failures;                                              \
    testutil::PrintTestResult(#fn, after - before);                            \
  } while (0)

#endif  // TESTS_TEST_COMMON_HPP_
