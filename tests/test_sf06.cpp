/**
 * @file test_sf06.cpp
 * @brief The SF06 family (SFM4300, SFM3003, SFM3013, SFM3019, SFM3119) against
 * the chip model.
 */
#include "rig.hpp"

namespace {

using rig::Rig;
using sfm::Error;
using sfm::Gas;
using sfm::State;
using sfm::Variant;

constexpr Variant kV = Variant::SFM3003_300_CL;

TEST_CASE("sf06 init: the sequence on the bus, the product information, the "
          "averaging") {
  Rig<> r(kV);
  REQUIRE(r.Init());
  CHECK(r.meter.GetState() == State::Idle);
  const sfm::ProductInfo& info = r.meter.GetProductInfo();
  CHECK_EQ(info.product_id, uint32_t{0x04020100});
  CHECK_EQ(info.serial, uint32_t{0x0A1B2C3D});
  CHECK(info.variant == kV);
  CHECK(!info.prototype && !info.preliminary);
  // Stop, identifier command, 18-byte read, averaging write: in this order.
  const int stop = rig::FindWrite(r, 0x3F, 0xF9);
  const int id = rig::FindWrite(r, 0xE1, 0x02);
  const int avg = rig::FindWrite(r, 0x36, 0x6A);
  CHECK((stop >= 0) && (id > stop) && (avg > id));
  CHECK_EQ(r.chip.Averaging(), uint16_t{0});
  CHECK(rig::ReadsAreWholeWords(r));
  CHECK_EQ(r.port.bus_inits, uint32_t{1});
  CHECK(r.meter.EnsureInitialized().ok()); // already initialised: no traffic
  CHECK_EQ(r.port.bus_inits, uint32_t{1});
  CHECK_EQ(r.meter.GetAddress(), uint8_t{0x28});
}

TEST_CASE("sf06 init: configured averaging is written; the default is the "
          "family's reset value") {
  sfm::Config c = sfm::PresetFor(kV);
  c.averaging = 16;
  Rig<> r(*expect::Find(kV), c);
  REQUIRE(r.Init());
  CHECK_EQ(r.chip.Averaging(), uint16_t{16});
}

TEST_CASE("sf06 init: Auto identifies the part by identifier and address, "
          "inside the family only") {
  sfm::Config c{};
  c.address = 0x2D;
  Rig<> r(*expect::Find(Variant::SFM3003_300_CE), c);
  REQUIRE(r.Init());
  CHECK(r.meter.GetProductInfo().variant == Variant::SFM3003_300_CE);
  CHECK(r.meter.GetPart()->variant == Variant::SFM3003_300_CE);
  // The scaling of the identified part is used: 120 counts per slm for the CE.
  REQUIRE(r.Start(Gas::Air));
  r.chip.flow_ml = 100000;
  const auto s = r.meter.ReadSample();
  REQUIRE(s.ok());
  CHECK_EQ(s.value.flow_ml_per_min, 100000);

  // An SFM3505 identifier does not select an SF06 row.
  sfm::Config c2{};
  c2.address = 0x2E;
  Rig<> r2(*expect::Find(Variant::SFM3505_300), c2);
  const auto init = r2.meter.EnsureInitialized();
  CHECK(init.error == Error::DeviceIdMismatch);
  CHECK(r2.meter.GetState() == State::Uninitialized);
}

TEST_CASE("sf06 init: every way the identification can fail") {
  {
    Rig<> r(kV);
    r.chip.id_override = 0x04020700; // the CE's identifier at the CL's configuration
    CHECK(r.meter.EnsureInitialized().error == Error::DeviceIdMismatch);
  }
  {
    Rig<> r(kV);
    r.chip.id_override = 0x04020180; // revision byte 0x80: a prototype
    CHECK(r.meter.EnsureInitialized().error == Error::DeviceIdMismatch);
  }
  {
    sfm::Config c = sfm::PresetFor(kV);
    c.accept_prototype = true;
    Rig<> r(*expect::Find(kV), c);
    r.chip.id_override = 0x04020180;
    REQUIRE(r.Init());
    CHECK(r.meter.GetProductInfo().prototype);
    CHECK_EQ(r.meter.GetProductInfo().revision, uint8_t{0x80});
  }
  {
    Rig<> r(kV);
    r.chip.serial = 0; // all-zero serial number: a stuck line that has a valid CRC
    CHECK(r.meter.EnsureInitialized().error == Error::InvalidReply);
  }
  {
    Rig<> r(kV);
    r.chip.flip_read_byte = 4;
    CHECK(r.meter.EnsureInitialized().error == Error::Crc);
    CHECK_EQ(r.meter.GetStats().crc_errors, uint32_t{1});
    CHECK(r.meter.GetState() == State::Uninitialized);
  }
  {
    Rig<> r(kV);
    r.chip.dead = true;
    CHECK(r.meter.EnsureInitialized().error == Error::NoDevice);
  }
  {
    Rig<> r(kV);
    r.port.fail_bus_init = true;
    CHECK(r.meter.EnsureInitialized().error == Error::Bus);
    CHECK_EQ(r.chip.writes, uint32_t{0}); // nothing on the bus before it is up
  }
  {
    sfm::Config c = sfm::PresetFor(kV);
    c.averaging = 500;
    Rig<> r(*expect::Find(kV), c);
    CHECK(r.meter.EnsureInitialized().error == Error::InvalidArgument);
    CHECK_EQ(r.chip.writes, uint32_t{0});
  }
  {
    using OnlySf05 = Rig<model::kAllHooks, sfm::kFamilySf05>;
    OnlySf05 r(kV);
    CHECK(r.meter.EnsureInitialized().error == Error::Unsupported);
  }
  {
    Rig<> r(kV); // the sensor answers late: the poll waits for it
    r.chip.BrownOut();
    REQUIRE(r.Init());
  }
}

TEST_CASE("sf06 measurement: gases, mixtures, status check, stop") {
  Rig<> r(Variant::SFM4300_20_B);
  REQUIRE(r.Init());
  REQUIRE(r.Start(Gas::AirO2, 300));
  CHECK_EQ(r.chip.Permille(), uint16_t{300});
  r.chip.flow_ml = 12345;
  r.chip.temp_mdegc = 23500;
  auto s = r.meter.ReadSample();
  REQUIRE(s.ok());
  CHECK(!s.value.pure_gas);
  CHECK_EQ(s.value.o2_permille, uint16_t{300});
  CHECK(s.value.has_status && s.value.has_temperature && !s.value.has_flow2);
  CHECK_EQ(s.value.temperature_mdegc, 23500);
  CHECK((s.value.status >> 12U) == 6U);
  CHECK_EQ(s.value.status & 0x3FFU, 300U);
  // The concentration is updated without stopping.
  const auto u = r.meter.UpdateConcentration(500);
  CHECK(r.Finish(u));
  CHECK_EQ(r.chip.Permille(), uint16_t{500});
  s = r.meter.ReadSample();
  REQUIRE(s.ok());
  CHECK_EQ(s.value.o2_permille, uint16_t{500});
  CHECK(r.meter.UpdateConcentration(1001).error == Error::InvalidArgument);
  // The sensor reports another command than the one we started.
  r.chip.bad_status = true;
  CHECK(r.meter.ReadSample().error == Error::StatusMismatch);
  r.chip.bad_status = false;
  // A pure gas rejects a concentration update.
  CHECK(r.Finish(r.meter.Stop()));
  REQUIRE(r.Start(Gas::O2));
  CHECK(r.meter.UpdateConcentration(100).error == Error::InvalidArgument);
  s = r.meter.ReadSample();
  REQUIRE(s.ok());
  CHECK(s.value.pure_gas);
  CHECK_EQ(s.value.o2_permille, uint16_t{0});
  CHECK_EQ(s.value.status & 0x3FFU, 0x3FFU);
  // A gas the part does not calibrate, and a concentration on a pure gas.
  CHECK(r.Finish(r.meter.Stop()));
  CHECK(r.meter.StartMeasurement(Gas::Ar).error == Error::InvalidArgument);
  CHECK(r.meter.StartMeasurement(Gas::O2, 5).error == Error::InvalidArgument);
  CHECK(r.meter.StartMeasurement(Gas::AirO2, 1001).error == Error::InvalidArgument);
}

TEST_CASE("sf06 scaling check: strict, lenient, off") {
  {
    Rig<> r(kV);
    REQUIRE(r.Init());
    r.chip.scale_override = 171;
    const auto w = r.meter.StartMeasurement(Gas::Air);
    CHECK(w.error == Error::DeviceFault);
    CHECK(r.meter.GetState() == State::Idle); // nothing was started
    CHECK(!r.meter.GetScaling().matches_table);
    CHECK_EQ(r.meter.GetScaling().scale, 171);
    CHECK_EQ(r.chip.Measuring(), false);
  }
  {
    sfm::Config c = sfm::PresetFor(kV);
    c.scaling_check = sfm::ScalingCheck::Lenient;
    Rig<> r(*expect::Find(kV), c);
    REQUIRE(r.Init());
    r.chip.scale_override = 171; // within 1 %
    CHECK(r.Start(Gas::Air));
    CHECK(r.Finish(r.meter.Stop()));
    r.chip.scale_override = 180; // 5.9 % off
    CHECK(r.meter.StartMeasurement(Gas::Air).error == Error::DeviceFault);
    r.chip.scale_override = 0;
    r.chip.unit_override = 0x0145; // another unit is always a fault
    CHECK(r.meter.StartMeasurement(Gas::Air).error == Error::DeviceFault);
  }
  {
    sfm::Config c = sfm::PresetFor(kV);
    c.scaling_check = sfm::ScalingCheck::Off;
    Rig<> r(*expect::Find(kV), c);
    REQUIRE(r.Init());
    r.chip.scale_override = 500;
    r.chip.unit_override = 0x0148;
    CHECK(r.Start(Gas::Air));
    CHECK(r.meter.GetScaling().matches_table);
  }
  {
    Rig<> r(kV);
    REQUIRE(r.Init());
    r.chip.flip_read_byte = 2;
    CHECK(r.meter.ReadScaling(Gas::Air).error == Error::Crc);
    r.chip.flip_read_byte = -1;
    CHECK(r.Finish(r.meter.ReadScaling(Gas::Air)));
    CHECK(r.meter.GetScaling().matches_table);
    CHECK_EQ(r.meter.GetScaling().offset, -24576);
    CHECK_EQ(r.meter.GetScaling().unit, uint16_t{0x0148});
    CHECK(r.meter.ReadScaling(Gas::Ar).error == Error::InvalidArgument);
  }
}

TEST_CASE("sf06 reads: a flipped bit anywhere in the frame is a CRC error, so "
          "is a stuck line") {
  Rig<> r(kV);
  REQUIRE(r.Init());
  REQUIRE(r.Start(Gas::Air));
  uint32_t crc_errors = 0;
  for (int byte = 0; byte < 9; ++byte) {
    r.chip.flip_read_byte = byte;
    const auto s = r.meter.ReadSample();
    CHECK(s.error == Error::Crc);
    crc_errors += 1U;
    CHECK_EQ(r.meter.GetStats().crc_errors, crc_errors);
    CHECK_EQ(s.value.flow_raw, 0);
  }
  r.chip.flip_read_byte = -1;
  r.chip.stuck = model::Stuck::Zeros; // CRC-8 with initial value 0xFF of zeros is not zero
  CHECK(r.meter.ReadSample().error == Error::Crc);
  r.chip.stuck = model::Stuck::Ones;
  CHECK(r.meter.ReadSample().error == Error::Crc);
  r.chip.stuck = model::Stuck::None;
  CHECK(r.meter.ReadSample().ok());
  CHECK(rig::ReadsAreWholeWords(r));
}

TEST_CASE("sf06 reads: range policy, freshness, device loss") {
  {
    Rig<> r(kV);
    REQUIRE(r.Init());
    REQUIRE(r.Start(Gas::Air));
    r.chip.flow_ml = 300000 + 6000; // the span plus 20 per mille of 330 slm is 306.6 slm
    CHECK(r.meter.ReadSample().ok());
    r.chip.flow_ml = 300000 + 7000;
    CHECK(r.meter.ReadSample().error == Error::OutOfRange);
    r.chip.flow_ml = -30000 - 6700;
    CHECK(r.meter.ReadSample().error == Error::OutOfRange);
  }
  {
    sfm::Config c = sfm::PresetFor(kV);
    c.range_policy = sfm::RangePolicy::Flag;
    Rig<> r(*expect::Find(kV), c);
    REQUIRE(r.Init());
    REQUIRE(r.Start(Gas::Air));
    r.chip.flow_ml = 310000;
    const auto s = r.meter.ReadSample();
    REQUIRE(s.ok());
    CHECK(s.value.out_of_range);
    CHECK(s.value.flow_ml_per_min > 300000);
    r.chip.flow_ml = 1000;
    CHECK(!r.meter.ReadSample().value.out_of_range);
  }
  {
    sfm::Config c = sfm::PresetFor(kV);
    c.require_settled = true;
    c.reject_smoothed = true;
    c.max_age_us = 5000;
    Rig<> r(*expect::Find(kV), c);
    REQUIRE(r.Init());
    REQUIRE(r.Start(Gas::Air)); // 12 ms to the first sample, the warm-up is 30 ms
    CHECK(r.meter.ReadSample().error == Error::Stale);
    r.chip.now_us += 40000U;
    CHECK(r.meter.ReadSample().error == Error::Stale); // the previous good read is 40 ms ago
    r.chip.now_us += 1000U;
    const auto s = r.meter.ReadSample();
    REQUIRE(s.ok());
    CHECK(s.value.settled);
    r.chip.smoothing = true;
    r.chip.now_us += 1000U;
    CHECK(r.meter.ReadSample().error == Error::Stale);
    r.chip.smoothing = false;
    r.chip.now_us += 1000U;
    CHECK(r.meter.ReadSample().ok());
  }
  {
    Rig<> r(kV);
    REQUIRE(r.Init());
    REQUIRE(r.Start(Gas::Air));
    CHECK(r.meter.ReadSample().ok());
    r.chip.BrownOut(); // the sensor reset under the driver: it answers its
                       // address, not the read
    r.chip.now_us += 100000U;
    CHECK(r.meter.ReadSample().error == Error::DeviceReset);
    CHECK(r.meter.GetState() == State::Idle);
    CHECK_EQ(r.meter.GetStats().device_resets, uint32_t{1});
    CHECK(r.meter.ReadSample().error == Error::Busy); // idle: not reading
  }
  {
    Rig<> r(kV);
    REQUIRE(r.Init());
    REQUIRE(r.Start(Gas::Air));
    CHECK(r.meter.ReadSample().ok());
    r.chip.dead = true;
    CHECK(r.meter.ReadSample().error == Error::NoData); // not silent for long enough yet
    r.chip.now_us += 100000U;
    CHECK(r.meter.ReadSample().error == Error::NoDevice); // past the bound: asked, no answer
  }
  {
    Rig<> r(kV);
    REQUIRE(r.Init());
    REQUIRE(r.Start(Gas::Air));
    r.chip.fail_read_in = 0;
    CHECK(r.meter.ReadSample().error == Error::NoData); // nothing before the first sample: no fault
    CHECK(r.meter.GetState() == State::Measuring);
    CHECK_EQ(r.meter.GetStats().no_data, uint32_t{1});
  }
}

TEST_CASE("sf06 sleep and wake, averaging, reset") {
  Rig<> r(kV);
  REQUIRE(r.Init());
  CHECK(r.Finish(r.meter.SetAveraging(8)));
  CHECK_EQ(r.chip.Averaging(), uint16_t{8});
  CHECK(r.meter.SetAveraging(129).error == Error::InvalidArgument);
  CHECK(r.Finish(r.meter.SetAveraging(sfm::kAveragingDefault)));
  CHECK_EQ(r.chip.Averaging(), uint16_t{0});
  CHECK(r.Finish(r.meter.SetAveraging(8)));
  REQUIRE(r.Start(Gas::Air));
  CHECK(r.meter.Sleep().error == Error::Busy); // measuring
  CHECK(r.meter.SetAveraging(4).error == Error::Busy);
  CHECK(r.Finish(r.meter.Stop()));
  CHECK(r.Finish(r.meter.Sleep()));
  CHECK(r.meter.GetState() == State::Asleep);
  CHECK(r.chip.Asleep());
  CHECK(r.meter.StartMeasurement(Gas::Air).error == Error::Busy);
  CHECK(r.meter.Stop().error == Error::Busy);
  CHECK(r.Finish(r.meter.WakeUp()));
  CHECK(r.meter.GetState() == State::Idle);
  CHECK(!r.chip.Asleep());
  // The averaging is written again after the wake (the documents do not say
  // sleep keeps it).
  CHECK(rig::FindWrite(r, 0x36, 0x6A, static_cast<std::size_t>(rig::FindWrite(r, 0x36, 0x77))) > 0);
  CHECK(r.meter.WakeUp().error == Error::Busy); // not asleep
  CHECK(r.Start(Gas::Air));
}

TEST_CASE("sf06 reset: the general call needs permission and the hook, and "
          "leaves the driver "
          "uninitialised") {
  {
    Rig<> r(kV);
    REQUIRE(r.Init());
    CHECK(r.meter.SoftReset().error == Error::Unsupported); // allow_general_call_reset is false
    CHECK_EQ(r.chip.resets, uint32_t{0});
  }
  {
    sfm::Config c = sfm::PresetFor(kV);
    c.allow_general_call_reset = true;
    Rig<> r(*expect::Find(kV), c);
    REQUIRE(r.Init());
    REQUIRE(r.Start(Gas::Air));
    CHECK(r.Finish(r.meter.SoftReset()));
    CHECK_EQ(r.chip.resets, uint32_t{1});
    CHECK(r.meter.GetState() == State::Uninitialized);
    CHECK(r.meter.ReadSample().error == Error::NotInitialized);
    CHECK(r.Init());
    CHECK(r.Start(Gas::Air));
    CHECK(r.meter.HardReset().error == Error::Unsupported); // no reset pin, no power-cycle feature
  }
  {
    sfm::Config c = sfm::PresetFor(kV);
    c.allow_general_call_reset = true;
    Rig<model::kAllHooks & ~model::kHookGeneralCall> r(*expect::Find(kV), c);
    REQUIRE(r.Init());
    CHECK(r.meter.SoftReset().error == Error::Unsupported);
  }
  {
    sfm::Config c = sfm::PresetFor(kV);
    c.allow_general_call_reset = true;
    Rig<> r(*expect::Find(kV), c);
    REQUIRE(r.Init());
    r.chip.dead = true;
    CHECK(r.meter.SoftReset().error == Error::Bus);
    CHECK(r.meter.GetState() == State::StateUnknown);
    CHECK(r.meter.ReadSample().error == Error::StateUnknown);
    r.chip.dead = false;
    CHECK(r.Init()); // initialisation recovers
    CHECK(r.meter.GetState() == State::Idle);
  }
}

TEST_CASE("sf06 failed commands leave the mode unknown") {
  Rig<> r(kV);
  REQUIRE(r.Init());
  r.chip.fail_write_in = 1; // the start command (after the scale-read write)
  CHECK(r.meter.StartMeasurement(Gas::Air).error == Error::Bus);
  CHECK(r.meter.GetState() == State::StateUnknown);
  CHECK(r.meter.StartMeasurement(Gas::Air).error == Error::StateUnknown);
  CHECK(r.Init());
  REQUIRE(r.Start(Gas::Air));
  r.chip.fail_write_in = 0;
  CHECK(r.meter.Stop().error == Error::Bus);
  CHECK(r.meter.GetState() == State::StateUnknown);
  CHECK(r.Init());
  r.chip.fail_write_in = 0;
  CHECK(r.meter.Sleep().error == Error::Bus);
  CHECK(r.meter.GetState() == State::StateUnknown);
  // A failed averaging write may have been applied: the cached averaging is
  // unknown, so the mode is.
  CHECK(r.Init());
  r.chip.fail_write_in = 0;
  CHECK(r.meter.SetAveraging(4).error == Error::Bus);
  CHECK(r.meter.GetState() == State::StateUnknown);
  CHECK_EQ(r.meter.GetStats().bus_errors >= 4U, true);
}

TEST_CASE("sf06 the application owns the waits: Poll, partial waits, Busy "
          "while pending") {
  Rig<> r(kV);
  REQUIRE(r.Init());
  sfm::Result<sfm::Wait> w = r.meter.StartMeasurement(Gas::Air);
  REQUIRE(w.ok());
  CHECK_EQ(w.value.us, uint32_t{12000}); // the first sample of the SFM3003
  CHECK(r.meter.HasPending());
  CHECK(r.meter.ReadSample().error == Error::Busy);
  CHECK(r.meter.Stop().error == Error::Busy);
  CHECK(r.meter.StartMeasurement(Gas::O2).error == Error::Busy);
  CHECK(r.meter.BeginInitialize().error == Error::Busy);
  CHECK(r.meter.SoftReset().error == Error::Busy);
  CHECK(r.meter.HardReset().error == Error::Busy);
  w = r.meter.Poll(5000U);
  REQUIRE(w.ok());
  CHECK_EQ(w.value.us, uint32_t{7000}); // the remainder
  r.chip.now_us += 12000U;
  w = r.meter.Poll(); // with a clock the elapsed time is measured
  REQUIRE(w.ok());
  CHECK_EQ(w.value.us, uint32_t{0});
  CHECK(!r.meter.HasPending());
  CHECK(r.meter.Poll().value.us == 0U); // nothing pending
  CHECK(r.meter.GetState() == State::Measuring);
  CHECK(r.meter.ReadSample().ok());
  // Without a clock Poll() takes the whole wait as elapsed.
  Rig<model::kAllHooks & ~model::kHookClock> nc(kV);
  REQUIRE(nc.Init());
  w = nc.meter.StartMeasurement(Gas::Air);
  REQUIRE(w.ok());
  nc.chip.now_us += 12000U;
  CHECK(nc.meter.Poll().ok());
  CHECK(nc.meter.GetState() == State::Measuring);
  CHECK(nc.meter.ReadSample().ok());
}

} // namespace

namespace {

TEST_CASE("sf06 raw reply: the CRC-checked words without decoding, and the "
          "same through GetLastReply()") {
  Rig<> r(kV);
  CHECK(r.meter.ReadRawReply().error == Error::NotInitialized); // before initialisation
  REQUIRE(r.Init());
  CHECK(r.meter.ReadRawReply().error == Error::Busy); // idle: no measurement to read
  CHECK_EQ(r.meter.GetLastReply().count, uint8_t{0});
  REQUIRE(r.Start(Gas::Air));
  r.chip.flow_ml = 50000;
  const auto raw = r.meter.ReadRawReply();
  REQUIRE(raw.ok());
  CHECK_EQ(raw.value.count, uint8_t{3}); // flow, temperature, status
  CHECK_EQ(r.meter.GetLastReply().count, uint8_t{3});
  CHECK_EQ(r.meter.GetLastReply().words[0], raw.value.words[0]);
  // The raw flow word is the scaled value: 50 slm at 170 counts per slm, offset
  // -24576, as 16-bit.
  const auto sample = r.meter.ReadSample();
  REQUIRE(sample.ok());
  CHECK_EQ(static_cast<int32_t>(static_cast<int16_t>(raw.value.words[0])), sample.value.flow_raw);
}

TEST_CASE("sf06 raw reply: a bad word is Crc and counted, no data is NoData, "
          "nothing is decoded") {
  Rig<> r(kV);
  REQUIRE(r.Init());
  REQUIRE(r.Start(Gas::Air));
  r.chip.flip_read_byte = 1; // a data byte of the first word
  const uint32_t before = r.meter.GetStats().crc_errors;
  CHECK(r.meter.ReadRawReply().error == Error::Crc);
  CHECK_EQ(r.meter.GetStats().crc_errors, before + 1U);
  r.chip.flip_read_byte = -1;
  r.chip.fail_read_in = 0; // the address is not acknowledged on the next read
  CHECK(r.meter.ReadRawReply().error == Error::NoData);
}

TEST_CASE("sf06 no new sample yet: a healthy sensor read faster than its "
          "period stays Measuring") {
  for (const uint16_t n : {uint16_t{1}, uint16_t{128}}) {
    sfm::Config c = sfm::PresetFor(kV);
    c.averaging = n;
    Rig<> r(*expect::Find(kV), c);
    r.chip.no_new_sample = true; // SFM4300 3.2, 3.3: a result every N x 0.5 ms, a NACK in between
    REQUIRE(r.Init());
    REQUIRE(r.Start(Gas::Air));
    uint32_t good = 0;
    for (int i = 0; i < 2000; ++i) {
      r.chip.now_us += 100U;
      const auto s = r.meter.ReadSample();
      if (s.ok()) {
        ++good;
      } else {
        CHECK(s.error == Error::NoData);
      }
      CHECK(r.meter.GetState() == State::Measuring);
    }
    CHECK(good > 0U);
    CHECK_EQ(r.meter.GetStats().device_resets, uint32_t{0});
    CHECK_EQ(r.chip.empty_writes, uint32_t{0});
  }
}

TEST_CASE("sf06 a sensor silent for the whole bound is asked with a real "
          "command, never an empty write") {
  Rig<> r(kV);
  REQUIRE(r.Init());
  REQUIRE(r.Start(Gas::Air));
  CHECK(r.meter.ReadSample().ok());
  r.chip.BrownOut(); // the sensor reset under the driver: it answers its
                     // address, not the read
  r.chip.now_us += 3000U;
  CHECK(r.meter.ReadSample().error == Error::NoData); // inside the bound: nothing is decided
  CHECK(r.meter.GetState() == State::Measuring);
  CHECK_EQ(r.meter.GetStats().device_resets, uint32_t{0});
  const auto before = r.chip.logged;
  r.chip.now_us += 100000U;
  CHECK(r.meter.ReadSample().error == Error::DeviceReset);
  CHECK(r.meter.GetState() == State::Idle);
  CHECK(rig::FindWrite(r, 0x3F, 0xF9, before) >= 0); // the stop command
  CHECK_EQ(r.chip.empty_writes, uint32_t{0});
  CHECK_EQ(r.meter.GetStats().bus_errors, uint32_t{0}); // an answered ask is not a bus error
}

TEST_CASE("sf06 without a clock a silent sensor is never reported from a count "
          "of reads") {
  // A NACKed read lasts 10 to 40 us; N = 128 gives a result every 64 ms. A
  // count of reads proves nothing about time, so the driver reports NoData and
  // the caller's timer owns the bound.
  for (const uint16_t n : {uint16_t{1}, uint16_t{128}}) {
    sfm::Config c = sfm::PresetFor(kV);
    c.averaging = n;
    Rig<model::kAllHooks & ~model::kHookClock> r(*expect::Find(kV), c);
    r.chip.no_new_sample = true;
    REQUIRE(r.Init());
    REQUIRE(r.Start(Gas::Air));
    uint32_t good = 0;
    for (uint32_t i = 0; i < 20000U; ++i) {
      r.chip.now_us += 3U; // far faster than a period, as a tight loop is
      const auto s = r.meter.ReadSample();
      good += s.ok() ? 1U : 0U;
      CHECK((s.ok() || (s.error == Error::NoData)));
      CHECK(r.meter.GetState() == State::Measuring);
    }
    CHECK(good > 0U);
    CHECK_EQ(r.meter.GetStats().device_resets, uint32_t{0});
    CHECK_EQ(r.chip.empty_writes, uint32_t{0});
  }
  // A dead sensor without a clock: still NoData, and the caller's own `Stop()`
  // is the real command that finds out (it fails, as a bus fault does).
  Rig<model::kAllHooks & ~model::kHookClock> d(*expect::Find(kV), sfm::PresetFor(kV));
  REQUIRE(d.Init());
  REQUIRE(d.Start(Gas::Air));
  d.chip.dead = true;
  CHECK(d.meter.ReadSample().error == Error::NoData);
  CHECK(!d.meter.Stop().ok());
}

TEST_CASE("sf06 frames the sensor delivered keep the silence clock running: "
          "rejected ones and raw "
          "reads") {
  const int32_t beyond = 309000; // the span 300 slm plus 20 per mille is 306 slm
  {
    // (a) out-of-range frames for far longer than the bound, then NACKs between
    // the samples
    sfm::Config c = sfm::PresetFor(kV);
    c.averaging = 1;
    Rig<> r(*expect::Find(kV), c);
    r.chip.no_new_sample = true;
    REQUIRE(r.Init());
    REQUIRE(r.Start(Gas::Air));
    r.chip.flow_ml = beyond;
    for (int i = 0; i < 2000; ++i) {
      r.chip.now_us += 100U;
      const auto s = r.meter.ReadSample();
      CHECK((s.error == Error::OutOfRange) || (s.error == Error::NoData));
      CHECK(r.meter.GetState() == State::Measuring);
    }
    CHECK_EQ(r.meter.GetStats().device_resets, uint32_t{0});
    CHECK_EQ(r.chip.empty_writes, uint32_t{0});
  }
  {
    // (b) a burst of frames with a bad CRC
    sfm::Config c = sfm::PresetFor(kV);
    c.averaging = 1;
    Rig<> r(*expect::Find(kV), c);
    r.chip.no_new_sample = true;
    REQUIRE(r.Init());
    REQUIRE(r.Start(Gas::Air));
    r.chip.flip_read_byte = 2;
    for (int i = 0; i < 2000; ++i) {
      r.chip.now_us += 100U;
      const auto s = r.meter.ReadSample();
      CHECK((s.error == Error::Crc) || (s.error == Error::NoData));
      CHECK(r.meter.GetState() == State::Measuring);
    }
    CHECK_EQ(r.meter.GetStats().device_resets, uint32_t{0});
  }
  {
    // (c) raw reads for 50 ms, then one sample read that meets a NACK between
    // two samples
    sfm::Config c = sfm::PresetFor(kV);
    c.averaging = 1;
    Rig<> r(*expect::Find(kV), c);
    REQUIRE(r.Init());
    REQUIRE(r.Start(Gas::Air));
    for (int i = 0; i < 100; ++i) {
      r.chip.now_us += 500U;
      CHECK(r.meter.ReadRawReply().ok());
    }
    r.chip.fail_read_in = 0;
    CHECK(r.meter.ReadSample().error == Error::NoData);
    CHECK(r.meter.GetState() == State::Measuring);
    CHECK_EQ(r.meter.GetStats().device_resets, uint32_t{0});
  }
}

TEST_CASE("sf06 a reported reset waits for the stop command's power-down time "
          "and ends the run") {
  Rig<> r(kV);
  REQUIRE(r.Init());
  REQUIRE(r.Start(Gas::Air));
  CHECK(r.meter.ReadSample().ok());
  r.chip.BrownOut();
  r.chip.now_us += 100000U;
  CHECK(r.meter.ReadSample().error == Error::DeviceReset);
  CHECK(r.meter.GetState() == State::Idle);
  CHECK(r.meter.StartMeasurement(Gas::Air).error == Error::Busy); // inside the 0.5 ms
  r.chip.now_us += 600U;
  CHECK(r.meter.StartMeasurement(Gas::Air).ok());
}

TEST_CASE("sf06 a sleeping sensor is not soft reset, and the calls never send "
          "an empty write") {
  sfm::Config c = sfm::PresetFor(kV);
  c.allow_general_call_reset = true;
  Rig<> r(*expect::Find(kV), c);
  REQUIRE(r.Init());
  CHECK(r.Finish(r.meter.Sleep()));
  const auto writes = r.chip.writes;
  CHECK(r.meter.SoftReset().error == Error::Busy);
  CHECK_EQ(r.chip.writes, writes); // nothing was sent
  CHECK_EQ(r.chip.forbidden, uint32_t{0});
  CHECK(r.Finish(r.meter.WakeUp()));
  CHECK(r.Finish(r.meter.SoftReset()));
  CHECK_EQ(r.chip.empty_writes, uint32_t{0});
  CHECK_EQ(r.chip.forbidden, uint32_t{0});
}

TEST_CASE("sf06 the getters do not keep a value from an earlier call once a "
          "later one began") {
  Rig<> r(kV);
  REQUIRE(r.Init());
  REQUIRE(r.Start(Gas::Air));
  CHECK(r.meter.ReadRawReply().ok());
  CHECK_EQ(r.meter.GetLastReply().count, uint8_t{3});
  r.chip.fail_read_in = 0;
  CHECK(!r.meter.ReadRawReply().ok());
  CHECK_EQ(r.meter.GetLastReply().count, uint8_t{0}); // not the words of the earlier read
}

TEST_CASE("sf06 the driver starts against a sleeping sensor, and the polls "
          "cost no bus error") {
  for (const Variant v : {Variant::SFM4300_20_B, Variant::SFM3304_250_D}) {
    Rig<> r(v);
    REQUIRE(r.Init());
    CHECK(r.Finish(r.meter.Sleep()));
    CHECK(r.meter.GetState() == State::Asleep);
    REQUIRE(r.meter.Deinitialize().ok());
    CHECK(r.meter.EnsureInitialized().ok()); // the poll wakes it: the stop command reaches it
    CHECK(r.meter.GetState() == State::Idle);
    CHECK_EQ(r.chip.empty_writes, uint32_t{0});
    CHECK_EQ(r.meter.GetStats().bus_errors, uint32_t{0});
  }
}

TEST_CASE("sf06 expected NACKs of the wake, reset and start-up polls are not "
          "bus errors") {
  sfm::Config c = sfm::PresetFor(kV);
  c.allow_general_call_reset = true;
  Rig<> r(*expect::Find(kV), c);
  r.chip.BrownOut();
  REQUIRE(r.Init()); // polls through the boot time
  CHECK(r.Finish(r.meter.Sleep()));
  CHECK(r.Finish(r.meter.WakeUp()));
  CHECK(r.Finish(r.meter.SoftReset()));
  CHECK_EQ(r.meter.GetStats().bus_errors, uint32_t{0});
  CHECK_EQ(r.chip.empty_writes, uint32_t{0});
  Rig<> h(Variant::SFM3505_300); // a power-cycle part: the poll after the cycle
                                 // meets NACKs too
  REQUIRE(h.Init());
  CHECK(h.Finish(h.meter.HardReset()));
  CHECK_EQ(h.meter.GetStats().bus_errors, uint32_t{0});
}

TEST_CASE("sf06 the poll can use the identifier command when stop is not taken "
          "while idle") {
  sfm::Config c = sfm::PresetFor(kV);
  c.poll_with_identifier = true;
  Rig<> r(*expect::Find(kV), c);
  REQUIRE(r.Init());
  CHECK(rig::FindWrite(r, 0x3F, 0xF9) < 0); // no stop command was sent
  CHECK(rig::FindWrite(r, 0xE1, 0x02) >= 0);
  CHECK_EQ(r.chip.empty_writes, uint32_t{0});
  CHECK(r.Finish(r.meter.Sleep()));
  CHECK(r.Finish(r.meter.WakeUp()));
  CHECK(rig::FindWrite(r, 0x3F, 0xF9) < 0);
}

TEST_CASE("sf06 the datasheet timing still waits for a wake that takes the "
          "typical time") {
  sfm::Config c = sfm::PresetFor(Variant::SFM4300_20_B);
  c.timing = sfm::TimingPolicy::Datasheet;
  Rig<> r(*expect::Find(Variant::SFM4300_20_B), c);
  REQUIRE(r.Init());
  CHECK(r.Finish(r.meter.Sleep()));
  CHECK(r.Finish(r.meter.WakeUp())); // one quarter more than the typical 16 ms
                                     // before giving up
}

} // namespace
