/**
 * @file test_errors.cpp
 * @brief Every `Error` has a name and is returned by some call; `Result`
 * carries value and error.
 */
#include <array>
#include <cstring>

#include "rig.hpp"

namespace {

using rig::Rig;
using sfm::Error;
using sfm::Gas;
using sfm::Variant;

constexpr std::array<Error, 18> kAll{
    Error::Ok,          Error::NotInitialized, Error::InvalidArgument,
    Error::Bus,         Error::Timeout,        Error::DeviceIdMismatch,
    Error::Crc,         Error::Busy,           Error::DeviceFault,
    Error::Unsupported, Error::NoData,         Error::NoDevice,
    Error::OutOfRange,  Error::StatusMismatch, Error::Stale,
    Error::DeviceReset, Error::StateUnknown,   Error::InvalidReply};

struct Seen {
  uint32_t bits{0};
  void Note(Error e) noexcept {
    bits |= (uint32_t{1} << static_cast<uint32_t>(sfm::detail::ToUnderlying(e) & 0x1FU));
  }
  [[nodiscard]] bool Has(Error e) const noexcept {
    return (bits & (uint32_t{1} << static_cast<uint32_t>(sfm::detail::ToUnderlying(e) & 0x1FU))) !=
           0U;
  }
};

TEST_CASE("errors: every error has a distinct name; a value that is no "
          "enumerator is Unknown") {
  for (const Error a : kAll) {
    CHECK(std::strcmp(sfm::ToString(a), "Unknown") != 0);
    for (const Error b : kAll) {
      CHECK((a == b) || (std::strcmp(sfm::ToString(a), sfm::ToString(b)) != 0));
    }
  }
  CHECK(std::strcmp(sfm::ToString(ut::OutOfRange<Error>(uint8_t{99})), "Unknown") == 0);
}

TEST_CASE("errors: Result carries a value or an error, never both") {
  const auto ok = sfm::Result<int>::Success(5);
  CHECK(ok.ok() && static_cast<bool>(ok) && (ok.value == 5));
  const auto bad = sfm::Result<int>::Failure(Error::Crc);
  CHECK(!bad.ok() && !static_cast<bool>(bad) && (bad.value == 0) && (bad.error == Error::Crc));
  CHECK(sfm::Result<void>::Success().ok());
  CHECK(sfm::Result<void>::Failure(Error::Bus).error == Error::Bus);
}

TEST_CASE("errors: every error is returned by some call") {
  Seen seen;
  seen.Note(Error::Ok);
  {
    Rig<> r(Variant::SFM3003_300_CL);
    seen.Note(r.meter.ReadSample().error); // NotInitialized
    r.port.fail_bus_init = true;
    seen.Note(r.meter.EnsureInitialized().error); // Bus
    r.port.fail_bus_init = false;
    r.chip.dead = true;
    seen.Note(r.meter.EnsureInitialized().error); // NoDevice
    r.chip.dead = false;
    r.chip.id_override = 0x04020700;
    seen.Note(r.meter.EnsureInitialized().error); // DeviceIdMismatch
    r.chip.id_override = 0;
    r.chip.flip_read_byte = 3;
    seen.Note(r.meter.EnsureInitialized().error); // Crc
    r.chip.flip_read_byte = -1;
    r.chip.serial = 0;
    seen.Note(r.meter.EnsureInitialized().error); // InvalidReply
    r.chip.serial = 1;
    (void)r.Init();
    seen.Note(r.meter.StartMeasurement(Gas::Ar).error); // InvalidArgument
    seen.Note(r.meter.HardReset().error);               // Unsupported
    seen.Note(r.meter.ReadSample().error);              // Busy
    r.chip.scale_override = 3;
    seen.Note(r.meter.StartMeasurement(Gas::Air).error); // DeviceFault
    r.chip.scale_override = 0;
    (void)r.Start(Gas::Air);
    r.chip.fail_read_in = 0;
    seen.Note(r.meter.ReadSample().error); // NoData
    r.chip.flow_ml = 310000;
    seen.Note(r.meter.ReadSample().error); // OutOfRange
    r.chip.flow_ml = 0;
    r.chip.bad_status = true;
    seen.Note(r.meter.ReadSample().error); // StatusMismatch
    r.chip.bad_status = false;
    r.chip.fail_write_in = 0;
    seen.Note(r.meter.Stop().error);       // Bus
    seen.Note(r.meter.ReadSample().error); // StateUnknown
  }
  {
    sfm::Config c = sfm::PresetFor(Variant::SFM3003_300_CL);
    c.require_settled = true;
    Rig<> r(*expect::Find(Variant::SFM3003_300_CL), c);
    (void)r.Init();
    (void)r.Start(Gas::Air);
    seen.Note(r.meter.ReadSample().error); // Stale
    (void)r.meter.ReadSample();
    r.chip.BrownOut();
    r.chip.now_us += 100000U;
    seen.Note(r.meter.ReadSample().error); // DeviceReset
  }
  {
    Rig<> r(Variant::SFM4100_AIR);
    (void)r.Init();
    (void)r.Start(Gas::Air);
    r.chip.fail_read_in = 0;
    seen.Note(r.meter.ReadSample().error); // Timeout
  }
  for (const Error e : kAll) {
    if (!seen.Has(e)) {
      CHECK_EQ(static_cast<int>(e),
               -1); // names the missing error in the report
    }
  }
}

} // namespace
