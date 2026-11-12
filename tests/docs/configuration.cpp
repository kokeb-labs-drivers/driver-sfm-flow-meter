/**
 * @file configuration.cpp
 * @brief Configuration snippets of the Configuration page.
 */
#include "my_port.hpp"
#include "sfm_flow_meter_kconfig.hpp"
#include "sfm_flow_meter_parts.hpp"

// --8<-- [start:custom]
// A part from its preset, then the fields that differ: a fixed averaging of 16 samples and a
// staleness check.
sfm::Config MakeConfig() {
  sfm::Config c = sfm::PresetFor(sfm::Variant::SFM4300_20_B);
  c.averaging = 16;                        // N = 16 samples per result (0 is average-until-read)
  c.timing = sfm::TimingPolicy::Datasheet; // the part's own waits, not the conservative 20 ms
  c.require_settled = true; // a sample before the warm-up time is Stale (needs NowUs())
  c.max_age_us = 200'000;   // a read later than 200 ms after the previous good one is Stale
  return c;
}

// The same, proven valid at build time: an invalid value does not compile.
constexpr sfm::CheckedConfig kBoard{sfm::PresetFor(sfm::Variant::SFM3003_300_CL)};
// --8<-- [end:custom]

// --8<-- [start:kconfig]
// The menuconfig options on top of the part: the variant and the address name the part, so the
// application sets them.
sfm::Config FromKconfig() {
  sfm::Config c = sfm::KconfigDefaults();
  const sfm::Config part = sfm::PresetFor(sfm::Variant::SFM3003_300_CL);
  c.variant = part.variant;
  c.family = part.family;
  c.address = part.address;
  return c;
}
// --8<-- [end:kconfig]

bool ConfigurationCompiles(MyPort& port) {
  sfm::SfmFlowMeter<MyPort> a(port, MakeConfig());
  sfm::SfmFlowMeter<MyPort> b(port, kBoard.Get());
  sfm::SfmFlowMeter<MyPort> c(port, FromKconfig());
  return (sfm::Validate(MakeConfig()) == sfm::Error::Ok) &&
         (sfm::Validate(FromKconfig()) == sfm::Error::Ok) && (a.GetState() == b.GetState()) &&
         (b.GetState() == c.GetState());
}
