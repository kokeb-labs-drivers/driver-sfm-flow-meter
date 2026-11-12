/**
 * @file not_a_port.cpp
 * @brief Must not compile. EXPECT: constraints not satisfied
 */
#include "sfm_flow_meter.hpp"

class NotAPort {};

int main() {
  NotAPort port;
  sfm::SfmFlowMeter<NotAPort> meter(port);
  return static_cast<int>(meter.GetState());
}
