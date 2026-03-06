#ifndef SWGR_BENCH_COMMON_HPP_
#define SWGR_BENCH_COMMON_HPP_

#include <iostream>

namespace swgr::bench {

inline void PrintScaffoldBanner(const char* target) {
  std::cout << "[scaffold] " << target
            << " is wired into CMake, implementation pending.\n";
}

}  // namespace swgr::bench

#endif  // SWGR_BENCH_COMMON_HPP_
