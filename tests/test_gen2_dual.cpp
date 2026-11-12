/**
 * @file test_gen2_dual.cpp
 * @brief The SFM3304 (SF06 generation 2) and the SFM3505 (two flows) against
 * the chip model.
 */
#include "rig.hpp"

namespace {

using rig::Rig;
using sfm::Error;
using sfm::Gas;
using sfm::State;
using sfm::Variant;

TEST_CASE("sfm3304: default averaging 2, status carries the averaging and the "
          "filter flag") {
  Rig<> r(Variant::SFM3304_250_D);
  REQUIRE(r.Init());
  CHECK_EQ(r.chip.Averaging(), uint16_t{2});
  REQUIRE(r.Start(Gas::Air));
  r.chip.flow_ml = -120000;
  auto s = r.meter.ReadSample();
  REQUIRE(s.ok());
  CHECK_EQ(s.value.flow_ml_per_min, -120000);
  CHECK((s.value.status & 0x1FFU) == 2U);
  CHECK((s.value.status & 0x0400U) != 0U);
  CHECK((s.value.status & 0x0200U) == 0U);
  CHECK(s.value.has_temperature);
  r.chip.bad_status = true; // a command nibble the SFM3304 never sends
  CHECK(r.meter.ReadSample().error == Error::InvalidReply);
  r.chip.bad_status = false;
  r.chip.status_xor = 0x0001; // a different number of averages than configured
  CHECK(r.meter.ReadSample().error == Error::StatusMismatch);
  r.chip.status_xor = 0x0200; // a filter argument that was not sent
  CHECK(r.meter.ReadSample().error == Error::StatusMismatch);
  r.chip.status_xor = 0;
  CHECK(r.Finish(r.meter.Stop()));
  CHECK(r.Finish(r.meter.SetAveraging(0))); // average-until-read
  REQUIRE(r.Start(Gas::Air));
  s = r.meter.ReadSample();
  REQUIRE(s.ok());
  CHECK((s.value.status & 0x07FFU) == 0U);
}

TEST_CASE("sfm3304: the signal filter argument is sent and reported") {
  sfm::Config c = sfm::PresetFor(Variant::SFM3304_250_D);
  c.filter_enabled = true;
  c.filter = 1234;
  Rig<> r(*expect::Find(Variant::SFM3304_250_D), c);
  REQUIRE(r.Init());
  REQUIRE(r.Start(Gas::Air));
  CHECK_EQ(rig::CountWrites(r, 5),
           uint32_t{3}); // averaging, scale read, start with the filter argument
  const auto s = r.meter.ReadSample();
  REQUIRE(s.ok());
  CHECK((s.value.status & 0x0200U) != 0U);
}

TEST_CASE("sfm3304: wake by polling with the stop command, never an empty write") {
  Rig<> r(Variant::SFM3304_250_D);
  REQUIRE(r.Init());
  CHECK(r.Finish(r.meter.Sleep()));
  const auto before = r.chip.logged;
  CHECK(r.Finish(r.meter.WakeUp()));
  CHECK(rig::FindWrite(r, 0x3F, 0xF9, before) >=
        0); // the address and a byte, as the datasheet needs
  CHECK_EQ(r.chip.empty_writes, uint32_t{0});
  CHECK(r.meter.GetState() == State::Idle);
  CHECK_EQ(r.chip.Averaging(), uint16_t{2}); // written again
  // A sensor that never wakes: the poll ends with Timeout and the driver stays
  // asleep.
  CHECK(r.Finish(r.meter.Sleep()));
  r.chip.dead = true;
  CHECK(r.meter.WakeUp().ok());
  CHECK(r.meter.AwaitPending().error == Error::Timeout);
  CHECK(r.meter.GetState() == State::Asleep);
}

TEST_CASE("sfm3505: two 24-bit flows, no status, no temperature, no scale read") {
  Rig<> r(Variant::SFM3505_300);
  REQUIRE(r.Init());
  CHECK_EQ(r.meter.GetProductInfo().product_id, uint32_t{0x04060200});
  CHECK(rig::FindWrite(r, 0x36, 0x5B) >= 0); // the identifier command of this part
  CHECK(rig::FindWrite(r, 0x36, 0x4D) >= 0); // its averaging command
  CHECK_EQ(r.chip.Averaging(), uint16_t{2});
  const auto caps = r.meter.GetCapabilities();
  CHECK(caps.two_flows && !caps.status_word && !caps.scale_read && !caps.sleep);
  CHECK(r.meter.ReadScaling(Gas::Air).error == Error::Unsupported);
  CHECK(r.meter.Sleep().error == Error::Unsupported);
  const uint32_t writes = r.chip.writes;
  const auto w = r.meter.StartMeasurement(Gas::Air);
  REQUIRE(w.ok());
  CHECK_EQ(r.chip.writes, writes + 1U); // the start command only: no scale read
  CHECK_EQ(w.value.us, uint32_t{4000});
  CHECK(r.Await());
  r.chip.flow_ml = 250000;
  r.chip.flow2_ml = 55000;
  const auto s = r.meter.ReadSample();
  REQUIRE(s.ok());
  CHECK(s.value.has_flow2 && !s.value.has_status && !s.value.has_temperature);
  CHECK(s.value.flow_ml_per_min > 249990 && s.value.flow_ml_per_min < 250010);
  CHECK(s.value.flow2_ml_per_min > 54990 && s.value.flow2_ml_per_min < 55010);
  // The extremes of the 24-bit words, with the spans of the two channels.
  r.chip.flow_ml = -150000;
  r.chip.flow2_ml = -150000;
  CHECK(r.meter.ReadSample().ok());
  r.chip.flow2_ml = 400000; // the oxygen channel is out of the span
  CHECK(r.meter.ReadSample().error == Error::OutOfRange);
  r.chip.flow2_ml = 0;
  r.chip.flip_read_byte = 5;
  CHECK(r.meter.ReadSample().error == Error::Crc);
  CHECK(rig::ReadsAreWholeWords(r));
}

TEST_CASE("sfm3505: identification and reset by supply") {
  sfm::Config c = sfm::PresetFor(Variant::SFM3505_300);
  Rig<> r(*expect::Find(Variant::SFM3505_300_X), c);
  CHECK(r.meter.EnsureInitialized().error ==
        Error::DeviceIdMismatch); // the -X part answers 0x04060100
  Rig<> ok(Variant::SFM3505_300_X);
  REQUIRE(ok.Init());
  CHECK(ok.Finish(ok.meter.HardReset()));
  CHECK_EQ(ok.chip.power_cycles, uint32_t{1});
  CHECK(ok.meter.GetState() == State::Uninitialized);
  CHECK(ok.Init());
}

} // namespace
