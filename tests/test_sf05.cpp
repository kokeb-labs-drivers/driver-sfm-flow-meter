/**
 * @file test_sf05.cpp
 * @brief The SF05 family (SFM3000, SFM3200, SFM3300, SFM3400, SFM4200) against
 * the chip model.
 */
#include "rig.hpp"

namespace {

using rig::Rig;
using sfm::Error;
using sfm::Gas;
using sfm::State;
using sfm::Variant;

TEST_CASE("sf05 init: reset, serial number, no identifier to check") {
  Rig<> r(Variant::SFM3200);
  REQUIRE(r.Init());
  CHECK_EQ(r.meter.GetProductInfo().serial, uint32_t{0x0A1B2C3D});
  CHECK_EQ(r.meter.GetProductInfo().product_id, uint32_t{0});
  CHECK(r.meter.GetProductInfo().variant == Variant::SFM3200);
  CHECK_EQ(r.chip.resets, uint32_t{1});
  const int reset_at = rig::FindWrite(r, 0x20, 0x00);
  const int high_at = rig::FindWrite(r, 0x31, 0xAE, static_cast<std::size_t>(reset_at));
  CHECK(reset_at >= 0);
  CHECK(high_at > reset_at); // the serial number: two pointers, one word each (SFM4200 5.2)
  CHECK(rig::FindWrite(r, 0x31, 0xAF, static_cast<std::size_t>(high_at)) > high_at);
  CHECK_EQ(r.chip.empty_writes, uint32_t{0});
  CHECK(rig::ReadsAreWholeWords(r));
  // The SFM4200 has no reset command: /RES or the supply only.
  Rig<> r4(Variant::SFM4200);
  REQUIRE(r4.Init());
  CHECK_EQ(r4.chip.resets, uint32_t{0});
  CHECK(rig::FindWrite(r4, 0x20, 0x00) < 0);
  // A sensor still powering up is waited for; a missing one is NoDevice.
  Rig<> late(Variant::SFM3400_D);
  late.chip.BrownOut();
  REQUIRE(late.Init());
  Rig<> gone(Variant::SFM3400_D);
  gone.chip.dead = true;
  CHECK(gone.meter.EnsureInitialized().error == Error::NoDevice);
  Rig<> bad(Variant::SFM3400_D);
  bad.chip.serial = 0;
  CHECK(bad.meter.EnsureInitialized().error == Error::InvalidReply);
  Rig<> crc(Variant::SFM3400_D);
  crc.chip.flip_read_byte = 2;
  CHECK(crc.meter.EnsureInitialized().error == Error::Crc);
  sfm::Config c{};
  c.family = sfm::Family::Sf05; // no Auto without an identifier
  Rig<> au(*expect::Find(Variant::SFM3400_D), c);
  CHECK(au.meter.EnsureInitialized().error == Error::InvalidArgument);
}

TEST_CASE("sf05 measurement: scale check against the air calibration, "
          "conversion, restart after "
          "each read") {
  Rig<> r(Variant::SFM3000_200_C);
  REQUIRE(r.Init());
  REQUIRE(r.Start(Gas::O2)); // oxygen: the registers hold the air calibration,
                             // 142.8 is applied by the driver
  r.chip.flow_ml = 100000;
  r.chip.now_us += 1000U;
  auto s = r.meter.ReadSample();
  REQUIRE(s.ok());
  // The model sends the air raw value; the driver converts it with the oxygen
  // scale 142.8: 98039 ml/min.
  CHECK(s.value.flow_ml_per_min > 98000 && s.value.flow_ml_per_min < 98080);
  CHECK(s.value.gas == Gas::O2);
  CHECK((static_cast<uint32_t>(s.value.flow_raw) & 3U) == 0U);
  CHECK(!s.value.has_status && !s.value.has_temperature);
  // The write of 0x1000 follows every read.
  const std::size_t before = r.chip.logged;
  r.chip.now_us += 1000U;
  REQUIRE(r.meter.ReadSample().ok());
  CHECK_EQ(r.chip.logged, before + 2U);
  CHECK(r.chip.log[before].write == false);
  CHECK(r.chip.log[before + 1U].write && r.chip.log[before + 1U].data[0] == 0x10);
  // The driver never reads fewer than a whole word, never one byte.
  CHECK(rig::ReadsAreWholeWords(r));
  CHECK(r.Finish(r.meter.Stop()));
  CHECK(!r.chip.Measuring());
  r.chip.scale_override = 150;
  CHECK(r.meter.StartMeasurement(Gas::Air).error == Error::DeviceFault);
}

TEST_CASE("sf05 measurement without the restart: the rule is configurable") {
  sfm::Config c = sfm::PresetFor(Variant::SFM3400_D);
  c.restart_before_read = false;
  Rig<> r(*expect::Find(Variant::SFM3400_D), c);
  r.chip.strict_restart = true; // a sensor that wants the restart before each read
  REQUIRE(r.Init());
  REQUIRE(r.Start(Gas::Air));
  r.chip.now_us += 1000U;
  CHECK(r.meter.ReadSample().ok());
  r.chip.now_us += 1000U;
  CHECK(r.meter.ReadSample().error == Error::NoData);
  CHECK_EQ(r.meter.GetStats().consecutive_errors, uint32_t{0}); // NoData is not a fault
  CHECK(r.meter.GetState() == State::Measuring);
  Rig<> w(Variant::SFM3400_D);
  w.chip.strict_restart = true;
  REQUIRE(w.Init());
  REQUIRE(w.Start(Gas::Air));
  for (int i = 0; i < 4; ++i) {
    w.chip.now_us += 1000U;
    CHECK(w.meter.ReadSample().ok());
  }
}

TEST_CASE("sf05 oxygen on the air-calibrated parts is refused: the datasheet "
          "gives a correction, "
          "not a scale") {
  // SFM4200 4.4 corrects oxygen by dQ = 1.3e-2 Q + 1.2e-4 Q^2; the SFM3200
  // oxygen note is not read. A flow in oxygen is therefore not offered on them
  // (nitrogen is, at the air scale) (the SFM3000 states its own scale).
  for (const Variant v : {Variant::SFM3200, Variant::SFM3200_AW, Variant::SFM3300_D,
                          Variant::SFM3400_D, Variant::SFM4200}) {
    Rig<> r(v);
    REQUIRE(r.Init());
    CHECK(r.meter.StartMeasurement(Gas::O2).error == Error::InvalidArgument);
    CHECK(r.meter.GetState() == State::Idle);
  }
  // Nitrogen at the air scale on every SF05 part: the media tables and scale
  // rows list it.
  for (const Variant v :
       {Variant::SFM3000_200_C, Variant::SFM3200, Variant::SFM3200_AW, Variant::SFM3300_D,
        Variant::SFM3300_AW, Variant::SFM3400_D, Variant::SFM3400_AW, Variant::SFM4200}) {
    Rig<> n(v);
    REQUIRE(n.Init());
    CHECK(n.Start(Gas::N2));
  }
  Rig<> r3(Variant::SFM3000_200_C);
  REQUIRE(r3.Init());
  CHECK(r3.Start(Gas::O2));
}

TEST_CASE("sf05 errors in a row: a power cycle, or an unknown mode, at the limit") {
  sfm::Config c = sfm::PresetFor(Variant::SFM3400_D);
  c.consecutive_error_limit = 3;
  Rig<> r(*expect::Find(Variant::SFM3400_D), c);
  REQUIRE(r.Init());
  REQUIRE(r.Start(Gas::Air));
  r.chip.flip_read_byte = 2;
  r.chip.now_us += 1000U;
  CHECK(r.meter.ReadSample().error == Error::Crc);
  r.chip.now_us += 1000U;
  CHECK(r.meter.ReadSample().error == Error::Crc);
  r.chip.flip_read_byte = -1;
  r.chip.now_us += 1000U;
  CHECK(r.meter.ReadSample().ok()); // a good sample clears the count
  CHECK_EQ(r.meter.GetStats().consecutive_errors, uint32_t{0});
  r.chip.flip_read_byte = 2;
  for (int i = 0; i < 2; ++i) {
    r.chip.now_us += 1000U;
    CHECK(r.meter.ReadSample().error == Error::Crc);
  }
  r.chip.now_us += 1000U;
  CHECK(r.meter.ReadSample().error == Error::StateUnknown);
  CHECK(r.meter.GetState() == State::StateUnknown);
  CHECK_EQ(r.chip.power_cycles, uint32_t{0}); // the driver never cycles the supply inside a read
  r.chip.flip_read_byte = -1;
  CHECK(r.Finish(r.meter.HardReset())); // the caller decides
  CHECK_EQ(r.chip.power_cycles, uint32_t{1});
  CHECK_EQ(r.meter.GetStats().power_cycles, uint32_t{1});
  CHECK(r.Init()); // recovery
  CHECK(r.Start(Gas::Air));
  // Without a power-cycle hook the mode is still unknown at the limit.
  Rig<model::kAllHooks & ~model::kHookPower> n(*expect::Find(Variant::SFM3400_D), c);
  REQUIRE(n.Init());
  REQUIRE(n.Start(Gas::Air));
  n.chip.now_us += 1000U;
  CHECK(n.meter.ReadSample().ok()); // one good sample: later NACKs are probed
  n.chip.dead = true;
  n.chip.now_us += 1000000U; // silent for longer than the datasheet bound:
                             // NoData becomes NoDevice
  CHECK(n.meter.ReadSample().error == Error::NoDevice);
  CHECK(n.meter.ReadSample().error == Error::NoDevice);
  CHECK(n.meter.ReadSample().error == Error::StateUnknown);
  CHECK_EQ(n.chip.power_cycles, uint32_t{0});
}

TEST_CASE("sf05 invalid but CRC-valid replies") {
  {
    Rig<> r(Variant::SFM3200);
    REQUIRE(r.Init());
    REQUIRE(r.Start(Gas::Air));
    r.chip.now_us += 1000U;
    r.chip.or_mask = 0x0001; // a set bit below the 14-bit result
    CHECK(r.meter.ReadSample().error == Error::InvalidReply);
    r.chip.or_mask = 0;
    r.chip.stuck = model::Stuck::Zeros; // CRC-8 with initial value 0 of zeros
                                        // is zero: a valid frame
    CHECK(r.meter.ReadSample().error ==
          Error::InvalidReply); // and zero is not a flow here (offset 32768)
    r.chip.stuck = model::Stuck::Ones;
    CHECK(r.meter.ReadSample().error == Error::Crc);
  }
  {
    // The SFM4200 has offset 0: a zero flow is a legitimate CRC-valid frame
    // (documented limit).
    Rig<> r(Variant::SFM4200);
    REQUIRE(r.Init());
    REQUIRE(r.Start(Gas::Air));
    r.chip.stuck = model::Stuck::Zeros;
    const auto s = r.meter.ReadSample();
    REQUIRE(s.ok());
    CHECK_EQ(s.value.flow_ml_per_min, 0);
  }
}

TEST_CASE("sf05 temperature: pointer sequence and restore, only on the parts "
          "that offer it") {
  Rig<> r(Variant::SFM3300_AW);
  REQUIRE(r.Init());
  CHECK(r.meter.ReadTemperature().error == Error::Busy); // not measuring
  REQUIRE(r.Start(Gas::Air));
  r.chip.temp_mdegc = 30000;
  const std::size_t before = r.chip.logged;
  const auto t = r.meter.ReadTemperature();
  REQUIRE(t.ok());
  CHECK_EQ(t.value.us, uint32_t{5000}); // the temperature is updated at 250 Hz
  CHECK(r.Await());
  CHECK(r.meter.GetTemperature() > 29900 && r.meter.GetTemperature() < 30100);
  CHECK_EQ(r.chip.logged, before + 3U); // 0x1001, read, 0x1000
  CHECK(r.chip.log[before].data[1] == 0x01 && r.chip.log[before + 2U].data[1] == 0x00);
  r.chip.now_us += 200000U;
  CHECK(r.meter.ReadSample().ok()); // the pointer is back at the flow
  r.chip.flip_read_byte = 2;
  CHECK(r.Finish(r.meter.ReadTemperature()) == false);
  CHECK(r.meter.GetState() == State::Measuring); // the pointer was restored
  r.chip.flip_read_byte = -1;
  r.chip.now_us += 200000U;
  r.chip.fail_write_in = 1; // the restore fails
  const auto w = r.meter.ReadTemperature();
  CHECK(w.ok());
  CHECK(r.meter.AwaitPending().error == Error::Bus);
  CHECK(r.meter.GetState() == State::StateUnknown);
  Rig<> plain(Variant::SFM3300_D);
  REQUIRE(plain.Init());
  REQUIRE(plain.Start(Gas::Air));
  CHECK(plain.meter.ReadTemperature().error == Error::Unsupported);
}

TEST_CASE("sf05 reset and the calls the family does not offer") {
  Rig<> r(Variant::SFM3200);
  REQUIRE(r.Init());
  CHECK(r.meter.Sleep().error == Error::Unsupported);
  CHECK(r.meter.SetAveraging(4).error == Error::Unsupported);
  CHECK(r.meter.UpdateConcentration(100).error == Error::Busy);
  REQUIRE(r.Start(Gas::Air));
  CHECK(r.meter.UpdateConcentration(100).error == Error::Unsupported);
  CHECK(r.meter.WakeUp().error == Error::Busy);
  CHECK(r.Finish(r.meter.SoftReset()));
  CHECK(r.meter.GetState() == State::Uninitialized);
  CHECK(r.Init());
  CHECK(r.Finish(r.meter.HardReset())); // the power-cycle hook
  CHECK_EQ(r.chip.power_cycles, uint32_t{1});
  CHECK(r.Init());
  Rig<model::kAllHooks & ~model::kHookPower> nopower(Variant::SFM3200);
  REQUIRE(nopower.Init());
  CHECK(nopower.meter.HardReset().error == Error::Unsupported);
  // SFM4200: soft reset does not exist; /RES does.
  Rig<> p(Variant::SFM4200);
  REQUIRE(p.Init());
  CHECK(p.meter.SoftReset().error == Error::Unsupported);
  CHECK(p.Finish(p.meter.HardReset()));
  CHECK_EQ(p.chip.power_cycles, uint32_t{0});
  CHECK(p.meter.GetState() == State::Uninitialized);
  Rig<model::kAllHooks & ~model::kHookResetPin> nopin(Variant::SFM4200);
  REQUIRE(nopin.Init());
  CHECK(nopin.meter.HardReset().error == Error::Unsupported);
}

TEST_CASE("sf05 serial number: the SFM3000 lists one pointer only, so a "
          "refused 0x31AF is not fatal") {
  // K-SF05-SERIAL: the application note shows 0x31AE; the SFM4200 datasheet
  // adds 0x31AF.
  Rig<> legacy(Variant::SFM3000_200_C);
  legacy.chip.no_serial_low = true;
  REQUIRE(legacy.Init());
  CHECK_EQ(legacy.meter.GetProductInfo().serial, uint32_t{0x0A1B0000}); // the high word only
  CHECK_EQ(legacy.meter.GetStats().bus_errors, uint32_t{0});
  // Both pointers answered: the whole serial number.
  Rig<> both(Variant::SFM3000_200_C);
  REQUIRE(both.Init());
  CHECK_EQ(both.meter.GetProductInfo().serial, uint32_t{0x0A1B2C3D});
  // A part whose datasheet gives both pointers still treats a refusal as a
  // missing sensor.
  Rig<> strict(Variant::SFM4200);
  strict.chip.no_serial_low = true;
  CHECK(strict.meter.EnsureInitialized().error == Error::NoDevice);
  // The knob: never send 0x31AF.
  sfm::Config c = sfm::PresetFor(Variant::SFM3200);
  c.sf05_serial_low_word = false;
  Rig<> one(*expect::Find(Variant::SFM3200), c);
  REQUIRE(one.Init());
  CHECK(rig::FindWrite(one, 0x31, 0xAF) < 0);
  CHECK_EQ(one.meter.GetProductInfo().serial, uint32_t{0x0A1B0000});
}

TEST_CASE("sf05 the getters do not keep a value from an earlier call once a "
          "later one began") {
  Rig<> r(Variant::SFM3200_AW);
  REQUIRE(r.Init());
  REQUIRE(r.Start(Gas::Air));
  REQUIRE(r.Finish(r.meter.ReadTemperature()));
  CHECK(r.meter.GetTemperature() != 0);
  r.chip.fail_write_in = 0;
  CHECK(!r.meter.ReadTemperature().ok() || !r.Await());
  CHECK_EQ(r.meter.GetTemperature(), 0);
  REQUIRE(r.meter.Stop().ok());
  REQUIRE(r.Await());
  REQUIRE(r.Finish(r.meter.ReadScaling(Gas::Air)));
  CHECK(r.meter.GetScaling().matches_table);
  r.chip.fail_read_in = 0;
  CHECK(!r.meter.ReadScaling(Gas::Air).ok() || !r.Await());
  CHECK(!r.meter.GetScaling().matches_table);
}

TEST_CASE("sf05 a temperature read restarts the measurement, so it also "
          "restarts the silence clock") {
  // Found by the fuzzer: the restore after the temperature pointer starts the
  // sensor again, the next result is a first-result time away, and a NACK then
  // is not a reset.
  Rig<> r(Variant::SFM3200_AW);
  REQUIRE(r.Init());
  REQUIRE(r.Start(Gas::Air));
  r.chip.now_us += 200000U; // the caller was away for longer than the whole bound
  REQUIRE(r.Finish(r.meter.ReadTemperature()));
  const auto s = r.meter.ReadSample(); // the sensor has not produced its first result yet
  CHECK(s.error == Error::NoData);
  CHECK(r.meter.GetState() == State::Measuring);
  CHECK_EQ(r.meter.GetStats().device_resets, uint32_t{0});
}

TEST_CASE("sf05 a temperature read that fails its checks still restarted the "
          "measurement") {
  // Found by the fuzzer: the decoder rejected the temperature after the restore
  // write had been sent.
  Rig<> r(Variant::SFM3200_AW);
  REQUIRE(r.Init());
  REQUIRE(r.Start(Gas::Air));
  r.chip.now_us += 200000U;
  r.chip.flip_read_byte = 2; // the CRC of the temperature word
  CHECK(!r.Finish(r.meter.ReadTemperature()));
  r.chip.flip_read_byte = -1;
  CHECK(r.meter.ReadSample().error == Error::NoData);
  CHECK(r.meter.GetState() == State::Measuring);
  CHECK_EQ(r.meter.GetStats().device_resets, uint32_t{0});
}

TEST_CASE("sf05 a frame that arrived counts even when the restart write after "
          "it fails") {
  // Found by the fuzzer: the read delivered a result, the restart write that
  // follows it was refused, and the read reported only the bus error. The next
  // NACKed read, a millisecond later, was then reported as a reset.
  Rig<> r(Variant::SFM3200_AW);
  REQUIRE(r.Init());
  REQUIRE(r.Start(Gas::Air));
  r.chip.strict_restart = true; // a read without the restart finds nothing
  r.chip.now_us += 200000U;     // longer than the whole bound
  r.chip.fail_write_in = 0;     // the restart after the read
  CHECK(r.meter.ReadSample().error == Error::Bus);
  r.chip.fail_write_in = -1;
  r.chip.now_us += 1000U;
  CHECK(r.meter.ReadSample().error == Error::NoData);
  CHECK(r.meter.GetState() == State::Measuring);
  CHECK_EQ(r.meter.GetStats().device_resets, uint32_t{0});
}

TEST_CASE("sf05 a stuck bus is not a temperature, whichever way it is stuck") {
  for (const Variant v : {Variant::SFM3000_200_C, Variant::SFM3200_AW, Variant::SFM3300_AW,
                          Variant::SFM3400_AW, Variant::SFM4200}) {
    for (const model::Stuck stuck : {model::Stuck::Zeros, model::Stuck::Ones}) {
      Rig<> r(v);
      REQUIRE(r.Init());
      REQUIRE(r.Start(Gas::Air));
      r.chip.stuck = stuck;
      CHECK(!r.Finish(r.meter.ReadTemperature()));
      CHECK_EQ(r.meter.GetTemperature(), 0);
      r.chip.stuck = model::Stuck::None;
      REQUIRE(r.Finish(r.meter.ReadTemperature())); // the next read is fine again
      CHECK(r.meter.GetTemperature() != 0);
    }
  }
}

TEST_CASE("sf05 the low serial word is not read after its pointer was refused") {
  Rig<> r(Variant::SFM3000_200_C);
  r.chip.no_serial_low = true; // the model leaves its pointer on the high word:
                               // an extra read answers
  REQUIRE(r.Init());
  CHECK_EQ(r.meter.GetProductInfo().serial, uint32_t{0x0A1B0000});
}

} // namespace
