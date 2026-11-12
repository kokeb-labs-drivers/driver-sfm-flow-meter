/**
 * @file api_tasks.cpp
 * @brief The API guide's code: one snippet per task, compiled against the current headers.
 */
#include "my_port.hpp"

namespace app {
void Report(int32_t flow_ml_per_min, int32_t temperature_mdegc) noexcept;
void Schedule(uint32_t wait_us) noexcept;
void Alarm(const char* what) noexcept;
} // namespace app

using Meter = sfm::SfmFlowMeter<MyPort>;

// --8<-- [start:lifecycle]
bool Bringup(Meter& meter) {
  const sfm::Result<void> init = meter.EnsureInitialized();
  if (!init) {
    return false; // NoDevice: nothing answers at the address; DeviceIdMismatch: wrong part
  }
  const sfm::ProductInfo& info = meter.GetProductInfo();
  return !info.prototype && (info.variant != sfm::Variant::Auto);
}
// --8<-- [end:lifecycle]

// --8<-- [start:polled]
// The calls return the wait the chip needs; the application owns the clock. A wait of 0 means
// "done".
sfm::Result<sfm::Wait> RunToEnd(Meter& meter, sfm::Result<sfm::Wait> step) {
  while (step && (step.value.us != 0U)) {
    app::Schedule(step.value.us); // sleep, or run other tasks, for at least this long
    step = meter.Poll();          // Poll(elapsed_us) when you keep your own clock
  }
  return step;
}

bool StartPolled(Meter& meter) {
  return RunToEnd(meter, meter.BeginInitialize()).ok() &&
         RunToEnd(meter, meter.StartMeasurement(sfm::Gas::Air)).ok();
}
// --8<-- [end:polled]

// --8<-- [start:mixture]
// An SFM4300-20 with 30 percent oxygen in air, then the mixer reports 45 percent.
bool Mixture(Meter& meter) {
  if (!meter.StartMeasurement(sfm::Gas::AirO2, 300) || !meter.AwaitPending()) {
    return false;
  }
  return meter.UpdateConcentration(450).ok() && meter.AwaitPending().ok();
}
// --8<-- [end:mixture]

// --8<-- [start:errors]
bool ReadAndHandle(Meter& meter) {
  const sfm::Result<sfm::Sample> s = meter.ReadSample();
  if (s) {
    app::Report(s.value.flow_ml_per_min, s.value.temperature_mdegc);
    return true;
  }
  switch (s.error) {
  case sfm::Error::NoData: // no new sample yet: ask again later; nothing is wrong
    break;
  case sfm::Error::DeviceReset: // the sensor reset under the driver: it is idle, start again
    (void)meter.StartMeasurement(sfm::Gas::Air);
    break;
  case sfm::Error::Crc:          // a word failed its CRC: the whole transfer was dropped
  case sfm::Error::InvalidReply: // a CRC-valid reply the sensor cannot send: a stuck line
    app::Alarm(sfm::ToString(s.error));
    break;
  case sfm::Error::StateUnknown: // a failed command left the mode unknown: initialise again
    (void)meter.EnsureInitialized();
    break;
  default:
    break;
  }
  return false;
}
// --8<-- [end:errors]

// --8<-- [start:sleep]
bool NapAndWake(Meter& meter) {
  if (!meter.Stop() || !meter.AwaitPending()) { // sleep is idle-only
    return false;
  }
  if (!meter.Sleep() || !meter.AwaitPending()) {
    return false;
  }
  // ... the sensor draws the lowest current here ...
  return meter.WakeUp().ok() &&
         meter.AwaitPending().ok(); // the wake polls the address until it answers
}
// --8<-- [end:sleep]

// --8<-- [start:scaling]
bool CheckScaling(Meter& meter) {
  if (!meter.ReadScaling(sfm::Gas::Air) || !meter.AwaitPending()) {
    return false; // DeviceFault: the sensor reports other scaling than the datasheet
  }
  const sfm::ScalingInfo& scaling = meter.GetScaling();
  return scaling.matches_table && (scaling.scale > 0);
}
// --8<-- [end:scaling]

// --8<-- [start:deinit]
bool Shutdown(Meter& meter) {
  if (meter.GetState() == sfm::State::Measuring) {
    if (!meter.Stop() || !meter.AwaitPending()) {
      return false;
    }
  }
  return meter.Deinitialize()
      .ok(); // no bus traffic; the next EnsureInitialized() starts from the beginning
}
// --8<-- [end:deinit]

bool ApiTasksCompile(MyPort& port) {
  Meter meter(port, sfm::PresetFor(sfm::Variant::SFM4300_20_B));
  return Bringup(meter) && StartPolled(meter) && Mixture(meter) && ReadAndHandle(meter) &&
         NapAndWake(meter) && CheckScaling(meter) && Shutdown(meter);
}
