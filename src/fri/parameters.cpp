#include "fri/parameters.hpp"

#include "utils.hpp"

namespace swgr::fri {

bool validate(const FriParameters& params) {
  if ((params.fold_factor != 3 && params.fold_factor != 9) ||
      params.stop_degree == 0) {
    return false;
  }

  for (const auto query_count : params.query_repetitions) {
    if (query_count == 0) {
      return false;
    }
  }
  return true;
}

bool validate(const FriParameters& params, const FriInstance& instance) {
  if (!validate(params)) {
    return false;
  }
  if (instance.domain.size() == 0) {
    return false;
  }
  if (!swgr::is_power_of(instance.domain.size(), 3)) {
    return false;
  }
  if (instance.claimed_degree >= instance.domain.size()) {
    return false;
  }

  std::uint64_t current_domain_size = instance.domain.size();
  std::uint64_t current_degree_bound = instance.claimed_degree;
  while (current_degree_bound > params.stop_degree) {
    if (current_domain_size % params.fold_factor != 0) {
      return false;
    }
    current_domain_size /= params.fold_factor;
    current_degree_bound /= params.fold_factor;
  }
  return true;
}

}  // namespace swgr::fri
