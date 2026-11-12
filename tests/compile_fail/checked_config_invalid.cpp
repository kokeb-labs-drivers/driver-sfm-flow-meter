/**
 * @file checked_config_invalid.cpp
 * @brief Must not compile. EXPECT: ConfigIsInvalid|constant expression
 */
#include "sfm_flow_meter.hpp"

int main() {
  sfm::Config c{};
  c.timing = sfm::TimingPolicy::Datasheet; // Auto needs the conservative waits
  const sfm::CheckedConfig checked{c};
  return static_cast<int>(checked.Get().averaging);
}
