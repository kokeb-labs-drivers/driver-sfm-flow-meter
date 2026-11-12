/**
 * @file test_sf04.cpp
 * @brief The SFM4100 (SF04 family): clock-stretching read, calibration field,
 * EEPROM scale.
 */
#include "rig.hpp"

namespace {

using rig::Rig;
using sfm::Error;
using sfm::Gas;
using sfm::State;
using sfm::Variant;

TEST_CASE("sfm4100 init: the calibration field selects the EEPROM page of the "
          "serial number") {
  Rig<> r(Variant::SFM4100_O2);
  r.chip.field = 3;
  REQUIRE(r.Init());
  CHECK_EQ(r.meter.GetProductInfo().serial, uint32_t{0x0A1B2C3D});
  CHECK(r.meter.GetProductInfo().variant == Variant::SFM4100_O2);
  CHECK_EQ(r.meter.GetAddress(), uint8_t{2});
  // EEPROM address 3 * 0x300 + 0x2F8 = 0xBF8, shifted left by 4 into 16 bits:
  // 0xBF80.
  bool found = false;
  for (std::size_t i = 0; i < r.chip.logged && i < r.chip.log.size(); ++i) {
    const model::Txn& t = r.chip.log[i];
    found = found ||
            (t.write && t.len == 3 && t.data[0] == 0xFA && t.data[1] == 0xBF && t.data[2] == 0x80);
  }
  CHECK(found);
  CHECK(rig::ReadsAreWholeWords(r));
}

TEST_CASE("sfm4100 measurement: a trigger and a stretched read for each sample") {
  Rig<> r(Variant::SFM4100_AIR);
  REQUIRE(r.Init());
  const auto w = r.meter.StartMeasurement(Gas::Air);
  REQUIRE(w.ok());
  CHECK_EQ(w.value.us, uint32_t{0}); // no continuous mode: nothing to wait for
  CHECK(r.meter.GetState() == State::Measuring);
  CHECK(r.meter.GetScaling().matches_table);
  CHECK_EQ(r.meter.GetScaling().unit, uint16_t{0x0145});
  r.chip.flow_ml = 12345;
  const std::size_t before = r.chip.logged;
  auto s = r.meter.ReadSample();
  REQUIRE(s.ok());
  CHECK_EQ(s.value.flow_ml_per_min, 12345);
  CHECK_EQ(r.chip.logged, before + 2U);
  CHECK(r.chip.log[before].write && r.chip.log[before].data[0] == 0xF1);
  CHECK(!r.chip.log[before + 1U].write && r.chip.log[before + 1U].stretch);
  r.chip.flow_ml = -300; // within the margin below zero
  s = r.meter.ReadSample();
  REQUIRE(s.ok());
  CHECK_EQ(s.value.flow_ml_per_min, -300);
  r.chip.flow_ml = -500;
  CHECK(r.meter.ReadSample().error == Error::OutOfRange);
  r.chip.flow_ml = 0;
  r.chip.stuck = model::Stuck::Zeros; // zero is a valid flow and a valid CRC:
                                      // documented limit
  CHECK(r.meter.ReadSample().ok());
  r.chip.stuck = model::Stuck::Ones;
  CHECK(r.meter.ReadSample().error == Error::Crc);
  r.chip.stuck = model::Stuck::None;
  r.chip.fail_read_in = 0;
  CHECK(r.meter.ReadSample().error == Error::Timeout); // the stretched read never completes
  const uint32_t before_stop = r.chip.writes;
  CHECK(r.Finish(r.meter.Stop()));
  CHECK_EQ(r.chip.writes, before_stop); // nothing to stop
  CHECK(r.meter.GetState() == State::Idle);
}

TEST_CASE("sfm4100 scaling check and reset") {
  Rig<> r(Variant::SFM4100_AR);
  REQUIRE(r.Init());
  r.chip.scale_override = 900;
  CHECK(r.meter.StartMeasurement(Gas::Ar).error == Error::DeviceFault);
  CHECK(r.meter.GetState() == State::Idle);
  r.chip.scale_override = 0;
  r.chip.unit_override = 0x0148;
  CHECK(r.meter.StartMeasurement(Gas::Ar).error == Error::DeviceFault);
  r.chip.unit_override = 0;
  CHECK(r.meter.StartMeasurement(Gas::Air).error == Error::InvalidArgument); // one gas per address
  REQUIRE(r.Start(Gas::Ar));
  CHECK(r.Finish(r.meter.SoftReset()));
  CHECK(r.meter.GetState() == State::Uninitialized);
  CHECK(r.Init());
  CHECK(r.Finish(r.meter.ReadScaling(Gas::Ar)));
  CHECK(r.meter.GetScaling().matches_table);
  CHECK(r.meter.HardReset().error == Error::Unsupported);
  CHECK(r.meter.Sleep().error == Error::Unsupported);
}

TEST_CASE("sfm4100 needs the port's stretching read") {
  Rig<model::kAllHooks & ~model::kHookStretch> r(Variant::SFM4100_AIR);
  CHECK(r.meter.EnsureInitialized().error == Error::Unsupported);
  CHECK_EQ(r.chip.writes, uint32_t{0});
  Rig<> wrong(Variant::SFM4100_AIR);
  wrong.chip.bus_address = 3; // another gas address answers
  CHECK(wrong.meter.EnsureInitialized().error == Error::NoDevice);
}

} // namespace
