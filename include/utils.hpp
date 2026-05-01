#ifndef STIR_WHIR_GR_UTILS_HPP_
#define STIR_WHIR_GR_UTILS_HPP_

#include <cstdint>
#include <string_view>

namespace stir_whir_gr {

[[noreturn]] void throw_unimplemented(std::string_view feature);

bool is_power_of(std::uint64_t value, std::uint64_t base);

}  // namespace stir_whir_gr

#endif  // STIR_WHIR_GR_UTILS_HPP_
