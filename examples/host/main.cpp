/**
 * @file main.cpp
 * @brief Host example: the driver against a simulated sensor, three scenarios, no hardware.
 *
 * The simulated sensor and its port are the chip model of the host tests (tests/model_sfm.hpp).
 * Each scenario shows one way to use the driver: a measurement with a mixture, the non-blocking
 * wait handling, and an SF05-family part with the error limit.
 */
#include <cstdio>

#include "model_sfm.hpp"
#include "sfm_flow_meter.hpp"

namespace {

using Port = model::Port<>;
using Meter = sfm::SfmFlowMeter<Port>;

/// Print one error and return false.
bool Report(const char* what, sfm::Error e) {
  std::printf("  %s failed: %s\n", what, sfm::ToString(e));
  return false;
}

/// Scenario 1: SFM4300-20-B, air and oxygen mixture, blocking calls through DelayUs().
bool MixtureScenario() {
  std::printf("scenario 1: SFM4300-20-B, 30 percent oxygen\n");
  model::Chip chip(*expect::Find(sfm::Variant::SFM4300_20_B));
  Port port(chip);
  Meter meter(port, sfm::PresetFor(sfm::Variant::SFM4300_20_B));
  if (const auto r = meter.EnsureInitialized(); !r) {
    return Report("EnsureInitialized", r.error);
  }
  std::printf("  serial number %08X\n", static_cast<unsigned>(meter.GetProductInfo().serial));
  if (const auto w = meter.StartMeasurement(sfm::Gas::AirO2, 300); !w) {
    return Report("StartMeasurement", w.error);
  }
  if (const auto r = meter.AwaitPending(); !r) {
    return Report("AwaitPending", r.error);
  }
  chip.flow_ml = 12500;
  const auto s = meter.ReadSample();
  if (!s) {
    return Report("ReadSample", s.error);
  }
  std::printf(
      "  flow %d ml/min, %d mdegC, oxygen %u permille\n", static_cast<int>(s.value.flow_ml_per_min),
      static_cast<int>(s.value.temperature_mdegc), static_cast<unsigned>(s.value.o2_permille));
  return true;
}

/// Scenario 2: SFM3003-300-CE, the application owns the waits (a scheduler would run other tasks).
bool NonBlockingScenario() {
  std::printf("scenario 2: SFM3003-300-CE, the application waits\n");
  model::Chip chip(*expect::Find(sfm::Variant::SFM3003_300_CE));
  Port port(chip);
  Meter meter(port, sfm::PresetFor(sfm::Variant::SFM3003_300_CE));
  sfm::Result<sfm::Wait> w = meter.BeginInitialize();
  while (w && (w.value.us != 0U)) {
    std::printf("  wait %u us\n", static_cast<unsigned>(w.value.us));
    chip.now_us += w.value.us; // here a task would sleep
    w = meter.Poll();
  }
  if (!w) {
    return Report("initialise", w.error);
  }
  w = meter.StartMeasurement(sfm::Gas::Air);
  while (w && (w.value.us != 0U)) {
    chip.now_us += w.value.us;
    w = meter.Poll();
  }
  if (!w) {
    return Report("start", w.error);
  }
  chip.flow_ml = -40000;
  const auto s = meter.ReadSample();
  std::printf("  flow %d ml/min\n", s ? static_cast<int>(s.value.flow_ml_per_min) : 0);
  return s.ok();
}

/// Scenario 3: SFM3400-AW, a legacy part: temperature by pointer, errors in a row.
bool LegacyScenario() {
  std::printf("scenario 3: SFM3400-AW, errors in a row\n");
  model::Chip chip(*expect::Find(sfm::Variant::SFM3400_AW));
  Port port(chip);
  Meter meter(port, sfm::PresetFor(sfm::Variant::SFM3400_AW));
  if (!meter.EnsureInitialized() || !meter.StartMeasurement(sfm::Gas::Air) ||
      !meter.AwaitPending()) {
    return Report("start", sfm::Error::Bus);
  }
  chip.now_us += 1000U;
  chip.flow_ml = 8000;
  const auto s = meter.ReadSample();
  if (!s) {
    return Report("ReadSample", s.error);
  }
  std::printf("  flow %d ml/min\n", static_cast<int>(s.value.flow_ml_per_min));
  if (!meter.ReadTemperature() || !meter.AwaitPending()) {
    return Report("ReadTemperature", sfm::Error::Bus);
  }
  std::printf("  temperature %d mdegC\n", static_cast<int>(meter.GetTemperature()));
  chip.now_us +=
      200000U; // the flow result needs its first-sample time after the pointer moved back
  chip.flip_read_byte = 2; // a noisy bus
  for (int i = 0; i < 5; ++i) {
    chip.now_us += 1000U;
    const auto bad = meter.ReadSample();
    std::printf("  read %d: %s\n", i, sfm::ToString(bad.error));
  }
  std::printf("  state after five errors: %s\n",
              meter.GetState() == sfm::State::StateUnknown ? "StateUnknown" : "other");
  return meter.GetState() == sfm::State::StateUnknown;
}

} // namespace

int main() {
  const bool ok = MixtureScenario() && NonBlockingScenario() && LegacyScenario();
  std::printf(ok ? "all scenarios passed\n" : "a scenario failed\n");
  return ok ? 0 : 1;
}
