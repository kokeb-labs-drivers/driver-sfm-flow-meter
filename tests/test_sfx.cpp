/**
 * @file test_sfx.cpp
 * @brief The SFM6000D (preliminary protocol): every fact the model relies on is
 * tagged [P] in the docs.
 */
#include "rig.hpp"

namespace {

using rig::Rig;
using sfm::Error;
using sfm::Gas;
using sfm::State;
using sfm::Variant;

TEST_CASE("sfm6000d: identification by the full 32-bit product number, "
          "controllers refused") {
  Rig<> r(Variant::SFM6000D_20);
  REQUIRE(r.Init());
  CHECK(r.meter.GetProductInfo().preliminary);
  CHECK(r.meter.GetCapabilities().preliminary);
  CHECK_EQ(r.meter.GetProductInfo().product_id, uint32_t{0x06021284});
  Rig<> c(Variant::SFM6000D_20);
  c.chip.id_override = 0x06021384; // an SFC controller product
  CHECK(c.meter.EnsureInitialized().error == Error::DeviceIdMismatch);
  Rig<> rev(Variant::SFM6000D_20);
  rev.chip.id_override = 0x06021285; // the revision byte is part of the match
  CHECK(rev.meter.EnsureInitialized().error == Error::DeviceIdMismatch);
  Rig<> other(Variant::SFM6000D_20);
  other.chip.id_override = 0x06021184; // the -50 at a -20 configuration
  CHECK(other.meter.EnsureInitialized().error == Error::DeviceIdMismatch);
  sfm::Config a{};
  a.family = sfm::Family::Sfx6000;
  a.address = 0x24;
  Rig<> au(*expect::Find(Variant::SFM6000D_5), a);
  REQUIRE(au.Init());
  CHECK(au.meter.GetProductInfo().variant == Variant::SFM6000D_5);
}

TEST_CASE("sfm6000d: gases with the two scale columns, the scale read through "
          "the pointer") {
  Rig<> r(Variant::SFM6000D_50);
  REQUIRE(r.Init());
  REQUIRE(r.Start(Gas::O2));
  CHECK(r.meter.GetScaling().matches_table);
  CHECK_EQ(r.meter.GetScaling().scale, 1024);
  CHECK(rig::FindWrite(r, 0xE1, 0x51) >= 0);
  r.chip.flow_ml = 30000;
  auto s = r.meter.ReadSample();
  REQUIRE(s.ok());
  CHECK_EQ(s.value.flow_ml_per_min, 30000);
  CHECK(s.value.has_status && !s.value.has_temperature);
  CHECK(r.Finish(r.meter.Stop()));
  REQUIRE(r.Start(Gas::CO2)); // the second column: 2560 counts per slm
  CHECK_EQ(r.meter.GetScaling().scale, 2560);
  r.chip.flow_ml = 10000;
  s = r.meter.ReadSample();
  REQUIRE(s.ok());
  CHECK_EQ(s.value.flow_ml_per_min, 10000);
  r.chip.bad_status = true;
  CHECK(r.meter.ReadSample().error == Error::StatusMismatch);
  r.chip.bad_status = false;
  CHECK(r.Finish(r.meter.Stop()));
  CHECK(r.meter.StartMeasurement(Gas::HeOx).error == Error::InvalidArgument);
}

TEST_CASE("sfm6000d: mixture 0, concentration update, temperature by pointer") {
  Rig<> r(Variant::SFM6000D_5);
  REQUIRE(r.Init());
  REQUIRE(r.Start(Gas::AirO2, 400));
  CHECK((r.chip.Permille()) == 400U);
  r.chip.flow_ml = 2500;
  auto s = r.meter.ReadSample();
  REQUIRE(s.ok());
  CHECK_EQ(s.value.o2_permille, uint16_t{400});
  CHECK((s.value.status >> 12U) == 10U);
  CHECK(r.Finish(r.meter.UpdateConcentration(600)));
  CHECK_EQ(r.chip.Permille(), uint16_t{600});
  r.chip.temp_mdegc = 31000;
  CHECK(r.Finish(r.meter.ReadTemperature()));
  CHECK(r.meter.GetTemperature() > 30900 && r.meter.GetTemperature() < 31100);
  s = r.meter.ReadSample(); // the pointer is back at the result
  CHECK(s.ok());
  CHECK(r.Finish(r.meter.Stop()));
  CHECK(r.meter.ReadTemperature().error == Error::Busy);
}

TEST_CASE("sfm6000d: a scale the document does not state is a fault, not a guess") {
  Rig<> r(Variant::SFM6000D_50);
  REQUIRE(r.Init());
  r.chip.scale_override = 1000;
  CHECK(r.meter.StartMeasurement(Gas::O2).error == Error::DeviceFault);
  CHECK(r.meter.GetState() == State::Idle);
}

TEST_CASE("sfm6000d: temperature near zero is data, and the plausible band has "
          "its edges") {
  Rig<> r(Variant::SFM6000D_5);
  REQUIRE(r.Init());
  REQUIRE(r.Start(Gas::AirO2, 400));
  // -5 mdegC is the raw word 0xFFFF (scale 200): a real reading here, not a
  // stuck line.
  r.chip.temp_mdegc = -5;
  REQUIRE(r.Finish(r.meter.ReadTemperature()));
  CHECK_EQ(r.meter.GetTemperature(), -5);
  r.chip.temp_mdegc = 0;
  REQUIRE(r.Finish(r.meter.ReadTemperature()));
  CHECK_EQ(r.meter.GetTemperature(), 0);
  r.chip.temp_mdegc = 150000; // the top of the band
  REQUIRE(r.Finish(r.meter.ReadTemperature()));
  CHECK_EQ(r.meter.GetTemperature(), 150000);
  r.chip.temp_mdegc = 150500; // above it
  CHECK(!r.Finish(r.meter.ReadTemperature()));
  CHECK_EQ(r.meter.GetTemperature(), 0);
  r.chip.temp_mdegc = -60000; // the bottom of the band
  REQUIRE(r.Finish(r.meter.ReadTemperature()));
  CHECK_EQ(r.meter.GetTemperature(), -60000);
  r.chip.temp_mdegc = -60500; // below it
  CHECK(!r.Finish(r.meter.ReadTemperature()));
  CHECK_EQ(r.meter.GetTemperature(), 0);
}

} // namespace
