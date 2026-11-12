/**
 * @file quickstart.cpp
 * @brief The Quickstart and README example: identify an SFM3003-300-CL, start air, read one sample.
 */
#include "my_port.hpp"

namespace app {
void Report(int32_t flow_ml_per_min, int32_t temperature_mdegc) noexcept;
}

// --8<-- [start:quickstart]
#include "sfm_flow_meter.hpp"

bool ReadOnce(MyPort& port) {
  sfm::SfmFlowMeter<MyPort> meter(
      port, sfm::PresetFor(sfm::Variant::SFM3003_300_CL)); // no bus traffic yet

  if (!meter.EnsureInitialized()) { // wakes the sensor, checks the product identifier
    return false;
  }
  if (!meter.StartMeasurement(sfm::Gas::Air)) { // reads and checks the scaling, then starts
    return false;
  }
  if (!meter.AwaitPending()) { // the first sample is ready after the start wait
    return false;
  }
  const sfm::Result<sfm::Sample> s = meter.ReadSample();
  if (!s) {
    return false; // s.error says why: NoData, Crc, DeviceReset, ...
  }
  app::Report(s.value.flow_ml_per_min, s.value.temperature_mdegc);
  return meter.Stop().ok() && meter.AwaitPending().ok();
}
// --8<-- [end:quickstart]
