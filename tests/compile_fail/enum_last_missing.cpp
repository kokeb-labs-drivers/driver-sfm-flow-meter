/**
 * @file enum_last_missing.cpp
 * @brief Must not compile. EXPECT: EnumLast
 */
#include "sfm_flow_meter.hpp"

int main() {
  return sfm::detail::IsEnumerator(sfm::LogLevel::Info) ? 1 : 0; // LogLevel has no EnumLast
}
