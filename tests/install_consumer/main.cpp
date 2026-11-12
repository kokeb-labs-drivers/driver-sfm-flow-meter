/**
 * @file main.cpp
 * @brief Builds the driver from the installed package only: headers, version header and .ipp.
 */
#include <cstddef>
#include <cstdint>

#include "sfm_flow_meter.hpp"

namespace {

// A port that acknowledges everything and reads zeros: enough to instantiate the whole driver
// template.
class NullPort : public sfm::I2cInterface<NullPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    (void)addr;
    (void)data;
    (void)len;
    return true;
  }
  bool Read(uint8_t addr, uint8_t* data, std::size_t len) noexcept {
    (void)addr;
    for (std::size_t i = 0; i < len; ++i) {
      data[i] = 0;
    }
    return true;
  }
  void DelayUs(uint32_t us) noexcept {
    (void)us;
  }
};

} // namespace

int main() {
  NullPort port;
  sfm::SfmFlowMeter<NullPort> meter(port);
  // Zeros fail the CRC, so initialisation reports an error; the point is that everything links.
  return meter.EnsureInitialized().ok() ? 1 : 0;
}
