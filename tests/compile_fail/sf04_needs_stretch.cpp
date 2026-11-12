/**
 * @file sf04_needs_stretch.cpp
 * @brief Must not compile. EXPECT: ReadStretch
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
  sfm::SfmFlowMeter<Port, sfm::kFamilySf04> meter(port);
  return static_cast<int>(meter.GetState());
}
