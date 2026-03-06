#ifndef SWGR_UTILS_HPP_
#define SWGR_UTILS_HPP_

#include <cstdint>
#include <string_view>

namespace swgr {

[[noreturn]] void throw_unimplemented(std::string_view feature);

bool is_power_of(std::uint64_t value, std::uint64_t base);

}  // namespace swgr

#endif  // SWGR_UTILS_HPP_
