#include "fri/parameters.hpp"

namespace swgr::fri {

bool validate(const FriParameters& params) {
  return (params.fold_factor == 3 || params.fold_factor == 9) &&
         params.stop_degree > 0;
}

}  // namespace swgr::fri
