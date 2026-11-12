/**
 * @file test_kconfig.cpp
 * @brief `KconfigDefaults()` without Kconfig equals `Config{}`; with CONFIG_
 * values it follows them.
 *
 * Built twice by tests/CMakeLists.txt: without any CONFIG_ macro, and with
 * KCONFIG_SET=1 plus the macros listed there.
 */
#include "sfm_flow_meter_kconfig.hpp"
#include "sfm_flow_meter_parts.hpp"
#include "test_harness.hpp"

namespace {

TEST_CASE("kconfig: the defaults are valid and follow the options") {
  constexpr sfm::Config kDefaults = sfm::KconfigDefaults();
#ifdef KCONFIG_SET
  sfm::Config named = kDefaults; // the datasheet timing needs a named part
  named.variant = sfm::Variant::SFM4300_20_B;
  CHECK(sfm::Validate(named) == sfm::Error::Ok);
  CHECK_EQ(kDefaults.averaging, uint16_t{4});
  CHECK(kDefaults.scaling_check == sfm::ScalingCheck::Lenient);
  CHECK(kDefaults.range_policy == sfm::RangePolicy::Flag);
  CHECK(kDefaults.timing == sfm::TimingPolicy::Datasheet);
  CHECK(kDefaults.allow_general_call_reset);
  CHECK(!kDefaults.restart_before_read);
  CHECK_EQ(kDefaults.consecutive_error_limit, uint8_t{9});
#else
  CHECK(sfm::Validate(kDefaults) == sfm::Error::Ok);
  constexpr sfm::Config kPlain{};
  CHECK_EQ(kDefaults.averaging, kPlain.averaging);
  CHECK(kDefaults.scaling_check == kPlain.scaling_check);
  CHECK(kDefaults.range_policy == kPlain.range_policy);
  CHECK(kDefaults.timing == kPlain.timing);
  CHECK_EQ(kDefaults.allow_general_call_reset, kPlain.allow_general_call_reset);
  CHECK_EQ(kDefaults.restart_before_read, kPlain.restart_before_read);
  CHECK_EQ(kDefaults.consecutive_error_limit, kPlain.consecutive_error_limit);
#endif
}

} // namespace
