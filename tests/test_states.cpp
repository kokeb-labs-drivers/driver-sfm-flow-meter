/**
 * @file test_states.cpp
 * @brief The state machine: every call in every state, with the result written
 * by hand; and a bus-fault sweep that fails each transfer of a long sequence in
 * turn.
 */
#include <array>
#include <cstdio>
#include <string_view>

#include "rig.hpp"

namespace {

using rig::Rig;
using sfm::Error;
using sfm::Gas;
using sfm::State;
using sfm::Variant;

enum class Where : uint8_t { Uninit, Idle, Measuring, Asleep, Unknown, Pending };
enum class Call : uint8_t {
  Begin,
  Start,
  Stop,
  Averaging,
  Scaling,
  Sleep,
  Wake,
  Soft,
  Concentration,
  Sample,
  Temperature
};

constexpr std::size_t kWheres = 6;
constexpr std::size_t kCalls = 11;

// Rows: calls; columns: Uninit, Idle, Measuring, Asleep, Unknown, Pending. "Ok"
// is Error::Ok.
constexpr std::array<std::array<Error, kWheres>, kCalls> kExpected{{
    {Error::Ok, Error::Ok, Error::Ok, Error::Ok, Error::Ok, Error::Busy}, // Begin
    {Error::NotInitialized, Error::Ok, Error::Busy, Error::Busy, Error::StateUnknown,
     Error::Busy}, // Start
    {Error::NotInitialized, Error::Ok, Error::Ok, Error::Busy, Error::StateUnknown,
     Error::Busy}, // Stop
    {Error::NotInitialized, Error::Ok, Error::Busy, Error::Busy, Error::StateUnknown,
     Error::Busy}, // Averaging
    {Error::NotInitialized, Error::Ok, Error::Busy, Error::Busy, Error::StateUnknown,
     Error::Busy}, // Scaling
    {Error::NotInitialized, Error::Ok, Error::Busy, Error::Busy, Error::StateUnknown,
     Error::Busy}, // Sleep
    {Error::NotInitialized, Error::Busy, Error::Busy, Error::Ok, Error::StateUnknown,
     Error::Busy},                                                          // Wake
    {Error::Ok, Error::Ok, Error::Ok, Error::Busy, Error::Ok, Error::Busy}, // Soft reset
    {Error::NotInitialized, Error::Busy, Error::Ok, Error::Busy, Error::StateUnknown,
     Error::Busy}, // Concentration
    {Error::NotInitialized, Error::Busy, Error::Ok, Error::Busy, Error::StateUnknown,
     Error::Busy}, // Sample
    {Error::NotInitialized, Error::Busy, Error::Unsupported, Error::Busy, Error::StateUnknown,
     Error::Busy}, // Temperature
}};

sfm::Config AllowReset() {
  sfm::Config c = sfm::PresetFor(Variant::SFM4300_20_B);
  c.allow_general_call_reset = true;
  return c;
}

void Drive(Rig<>& r, Where w) {
  switch (w) {
  case Where::Uninit:
    break;
  case Where::Idle:
    (void)r.Init();
    break;
  case Where::Measuring:
    (void)r.Init();
    (void)r.Start(Gas::AirO2, 210);
    break;
  case Where::Asleep:
    (void)r.Init();
    (void)r.Finish(r.meter.Sleep());
    break;
  case Where::Unknown:
    (void)r.Init();
    r.chip.fail_write_in = 0;
    (void)r.meter.SetAveraging(4); // fails but keeps Idle ...
    (void)r.Start(Gas::Air);
    r.chip.fail_write_in = 0;
    (void)r.meter.Stop(); // ... a failed stop does not
    break;
  case Where::Pending:
    (void)r.Init();
    (void)r.meter.StartMeasurement(Gas::Air);
    break;
  default:
    break;
  }
}

Error Do(Rig<>& r, Call c) {
  switch (c) {
  case Call::Begin:
    return r.meter.BeginInitialize().error;
  case Call::Start:
    return r.meter.StartMeasurement(Gas::Air).error;
  case Call::Stop:
    return r.meter.Stop().error;
  case Call::Averaging:
    return r.meter.SetAveraging(4).error;
  case Call::Scaling:
    return r.meter.ReadScaling(Gas::Air).error;
  case Call::Sleep:
    return r.meter.Sleep().error;
  case Call::Wake:
    return r.meter.WakeUp().error;
  case Call::Soft:
    return r.meter.SoftReset().error;
  case Call::Concentration:
    return r.meter.UpdateConcentration(400).error;
  case Call::Sample:
    return r.meter.ReadSample().error;
  case Call::Temperature:
    return r.meter.ReadTemperature().error;
  default:
    break;
  }
  return Error::Ok;
}

TEST_CASE("states: every call in every state") {
  for (std::size_t c = 0; c < kCalls; ++c) {
    for (std::size_t w = 0; w < kWheres; ++w) {
      Rig<> r(*expect::Find(Variant::SFM4300_20_B), AllowReset());
      Drive(r, static_cast<Where>(w));
      const Error got = Do(r, static_cast<Call>(c));
      if (got != kExpected[c][w]) {
        std::printf("  call %zu in state %zu: got %s, expected %s\n", c, w, sfm::ToString(got),
                    sfm::ToString(kExpected[c][w]));
      }
      CHECK(got == kExpected[c][w]);
    }
  }
}

TEST_CASE("states: Unknown is reached by a failed stop, and only "
          "initialisation leaves it") {
  Rig<> r(Variant::SFM4300_20_B);
  Drive(r, Where::Unknown);
  CHECK(r.meter.GetState() == State::StateUnknown);
  CHECK(r.Init());
  CHECK(r.meter.GetState() == State::Idle);
  CHECK(r.meter.Deinitialize().ok());
  CHECK(r.meter.GetState() == State::Uninitialized);
  CHECK(r.meter.ReadSample().error == Error::NotInitialized);
}

TEST_CASE("states: deinitialise cancels a pending operation") {
  Rig<> r(Variant::SFM4300_20_B);
  Drive(r, Where::Pending);
  CHECK(r.meter.HasPending());
  CHECK(r.meter.Deinitialize().ok());
  CHECK(!r.meter.HasPending());
  CHECK(r.Init());
}

/// A sequence of calls on one part with a fault injected at transfer `k`; the
/// driver must survive, name its errors, and recover with an initialisation
/// once the fault is gone.
void FaultSweep(Variant v, bool temperature, bool sleep, bool fail_reads) {
  for (int k = 0; k < 40; ++k) {
    Rig<> r(v);
    if (fail_reads) {
      r.chip.fail_read_in = k;
    } else {
      r.chip.fail_write_in = k;
    }
    (void)r.Init();
    const sfm::Gas gas = expect::Find(v)->gases[0].gas;
    (void)r.Start(gas);
    for (int i = 0; i < 2; ++i) {
      r.chip.now_us += 1000U;
      const auto s = r.meter.ReadSample();
      CHECK(std::string_view{sfm::ToString(s.error)} != "Unknown");
    }
    if (temperature) {
      (void)r.Finish(r.meter.ReadTemperature());
    }
    (void)r.Finish(r.meter.Stop());
    if (sleep) {
      (void)r.Finish(r.meter.Sleep());
      (void)r.Finish(r.meter.WakeUp());
    }
    CHECK(!r.meter.HasPending());
    r.chip.fail_read_in = -1;
    r.chip.fail_write_in = -1;
    r.chip.now_us += 1000000U;
    (void)r.meter.Deinitialize();
    CHECK(r.Init());
    CHECK(r.meter.GetState() == State::Idle);
    CHECK(r.Start(gas));
    r.chip.now_us += 1000U;
    CHECK(r.meter.ReadSample().ok());
    CHECK(rig::ReadsAreWholeWords(r));
  }
}

TEST_CASE("bus faults: a failing transfer anywhere in a sequence never wedges "
          "the driver") {
  for (const bool reads : {false, true}) {
    FaultSweep(Variant::SFM3003_300_CL, false, true, reads);
    FaultSweep(Variant::SFM3304_250_D, false, true, reads);
    FaultSweep(Variant::SFM3505_300, false, false, reads);
    FaultSweep(Variant::SFM3300_AW, true, false, reads);
    FaultSweep(Variant::SFM4100_AIR, false, false, reads);
    FaultSweep(Variant::SFM6000D_20, true, false, reads);
  }
}

} // namespace
