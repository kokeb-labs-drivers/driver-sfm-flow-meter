/**
 * @file port_without_delay.cpp
 * @brief Must not compile. EXPECT: must implement
 */
#include "sfm_flow_meter.hpp"

class Port : public sfm::I2cInterface<Port> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Write(uint8_t, const uint8_t*, std::size_t) noexcept {
    return true;
  }
  bool Read(uint8_t, uint8_t*, std::size_t) noexcept {
    return true;
  }
};

int main() {
  Port port;
  sfm::SfmFlowMeter<Port> meter(port);
  return meter.EnsureInitialized().ok() ? 0 : 1; // needs DelayUs() to wait
}
