#include "stir/parameters.hpp"

namespace swgr::stir {

bool validate(const StirParameters& params) {
  return params.virtual_fold_factor == 9 && params.shift_power == 3 &&
         params.stop_degree > 0;
}

}  // namespace swgr::stir
