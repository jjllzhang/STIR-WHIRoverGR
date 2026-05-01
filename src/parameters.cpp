#include "parameters.hpp"

namespace stir_whir_gr {

std::string to_string(SecurityMode mode) {
  switch (mode) {
    case SecurityMode::ConjectureCapacity:
      return "ConjectureCapacity";
    case SecurityMode::Conservative:
      return "Conservative";
  }
  return "UnknownSecurityMode";
}

std::string to_string(HashProfile profile) {
  switch (profile) {
    case HashProfile::STIR_NATIVE:
      return "STIR_NATIVE";
    case HashProfile::WHIR_NATIVE:
      return "WHIR_NATIVE";
  }
  return "UnknownHashProfile";
}

}  // namespace stir_whir_gr
