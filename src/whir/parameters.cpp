#include "whir/parameters.hpp"

namespace swgr::whir {

bool validate(const WhirParameters& params) { return params.folding_factor > 0; }

}  // namespace swgr::whir
