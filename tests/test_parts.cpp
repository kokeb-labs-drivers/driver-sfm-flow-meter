/**
 * @file test_parts.cpp
 * @brief The part table against the hand-typed datasheet values, the CRC
 * against an independent implementation, the conversions at their extremes, and
 * `Validate()`.
 */
#include <cstdio>

#include "model_sfm.hpp"
#include "sfm_flow_meter.hpp"
#include "test_harness.hpp"

namespace {

sfm::Family FamilyOf(expect::Kind k) {
  switch (k) {
  case expect::Kind::Sf06:
    return sfm::Family::Sf06;
  case expect::Kind::Gen2:
    return sfm::Family::Sf06Gen2;
  case expect::Kind::Dual:
    return sfm::Family::Sf06Dual;
  case expect::Kind::Sf05:
    return sfm::Family::Sf05;
  case expect::Kind::Sf04:
    return sfm::Family::Sf04;
  case expect::Kind::Sfx:
    return sfm::Family::Sfx6000;
  default:
    break;
  }
  return sfm::Family::Sf06;
}

TEST_CASE("parts: every row equals the datasheet values typed by hand") {
  CHECK_EQ(sfm::parts::kTable.size(), expect::kParts.size());
  for (const expect::PartX& x : expect::kParts) {
    const sfm::PartInfo* row = sfm::FindPart(x.variant);
    REQUIRE(row != nullptr);
    CHECK(row->family == FamilyOf(x.kind));
    CHECK_EQ(row->addresses.list[0], x.address);
    CHECK_EQ(row->crc_init, x.crc_init);
    CHECK_EQ(row->span_min_mlpm, x.span_min);
    CHECK_EQ(row->span_max_mlpm, x.span_max);
    CHECK_EQ(row->temperature.scale, x.temp_scale);
    CHECK_EQ(row->temperature.offset, x.temp_offset);
    CHECK_EQ(row->timing.first_sample_us, x.first_sample_us);
    CHECK_EQ(row->timing.warm_up_us, x.warm_up_us);
    CHECK_EQ(sfm::Has(*row, sfm::feature::kSleep), x.sleep);
    CHECK_EQ(sfm::Has(*row, sfm::feature::kAveraging), x.averaging);
    if (x.product_id != 0U) {
      CHECK_EQ(row->id_value, x.product_id);
    }
    if (x.kind != expect::Kind::Sf04 && x.kind != expect::Kind::Sf05) {
      CHECK_EQ(row->timing.soft_reset_us, x.reset_us);
    }
    for (uint8_t i = 0; i < x.gas_count; ++i) {
      const sfm::GasRow* g = sfm::FindGas(*row, x.gases[i].gas);
      REQUIRE(g != nullptr);
      CHECK_EQ(g->code, x.gases[i].code);
      CHECK_EQ(g->status_index, x.gases[i].nibble);
      CHECK_EQ(g->takes_argument, x.gases[i].argument);
      CHECK_EQ(static_cast<int32_t>(g->scaling.num), x.gases[i].scale_num);
      CHECK_EQ(static_cast<int32_t>(g->scaling.den), x.gases[i].scale_den);
      CHECK_EQ(g->scaling.offset, x.gases[i].offset);
      CHECK_EQ(g->scaling.unit, x.gases[i].unit);
    }
  }
}

TEST_CASE("parts: lookups refuse what no row matches") {
  CHECK(sfm::FindPart(sfm::Variant::Auto) == nullptr);
  CHECK(sfm::FindPart(ut::OutOfRange<sfm::Variant>(uint8_t{200})) == nullptr);
  // A controller product (SFC) and a revision byte the row does not mask do not
  // select an SFM6000 row.
  CHECK(sfm::FindById(sfm::Family::Sfx6000, 0x06021384U, 0x24) == nullptr);
  CHECK(sfm::FindById(sfm::Family::Sfx6000, 0x06021185U, 0x24) == nullptr);
  CHECK(sfm::FindById(sfm::Family::Sfx6000, 0x06021184U, 0x24) != nullptr);
  CHECK(sfm::FindById(sfm::Family::Sfx6000, 0x06021184U, 0x10) == nullptr);
  // The same identifier in another family never matches.
  CHECK(sfm::FindById(sfm::Family::Sf06, 0x06021184U, 0x24) == nullptr);
  // The revision byte is not part of an SF06 match; the address chooses among
  // the SFM3003 rows.
  const sfm::PartInfo* cl = sfm::FindById(sfm::Family::Sf06, 0x040201FFU, 0x28);
  REQUIRE(cl != nullptr);
  CHECK(cl->variant == sfm::Variant::SFM3003_300_CL);
  CHECK(sfm::FindById(sfm::Family::Sf06, 0x040201FFU, 0x2D) == nullptr);
  CHECK(sfm::FindGas(*cl, sfm::Gas::Ar) == nullptr);
  CHECK(sfm::FamilyRow(sfm::Family::Sf05) != nullptr);
}

TEST_CASE("crc: the driver's CRC-8 equals the model's for every word and both "
          "initial values") {
  for (uint32_t w = 0; w < 65536U; ++w) {
    const auto hi = static_cast<uint8_t>(w >> 8U);
    const auto lo = static_cast<uint8_t>(w & 0xFFU);
    if (sfm::reg::Crc8(0xFF, hi, lo) != model::Crc(0xFF, hi, lo) ||
        sfm::reg::Crc8(0x00, hi, lo) != model::Crc(0x00, hi, lo)) {
      CHECK(false);
      return;
    }
  }
  // The datasheet vectors.
  CHECK_EQ(sfm::reg::Crc8(0xFF, 0xBE, 0xEF), uint8_t{0x92});
  CHECK_EQ(sfm::reg::Crc8(0x00, 0xBE, 0xEF), uint8_t{0x13});
  CHECK_EQ(sfm::reg::Crc8(0xFF, 0x36, 0x08), uint8_t{0xD0});
  CHECK_EQ(sfm::reg::Crc8(0xFF, 0x36, 0x03), uint8_t{0x3A});
  CHECK_EQ(sfm::reg::Crc8(0xFF, 0x00, 0x00), uint8_t{0x81});
  CHECK_EQ(sfm::reg::Crc8(0x00, 0x00, 0x00), uint8_t{0x00});
}

TEST_CASE("conversions: rounding to nearest, ties away from zero, no overflow "
          "at the extremes") {
  const sfm::Scaling air{2500, 1, -28672, 0x0148};
  CHECK_EQ(sfm::FlowMlPerMin(-28672, air), 0);
  CHECK_EQ(sfm::FlowMlPerMin(-28672 + 2500, air), 1000);
  CHECK_EQ(sfm::FlowMlPerMin(-28672 + 1, air), 0); // 0.4 ml/min
  CHECK_EQ(sfm::FlowMlPerMin(-28672 - 1, air), 0); // -0.4
  CHECK_EQ(sfm::FlowMlPerMin(-28672 + 5, air), 2); // 2.0
  const sfm::Scaling o2{1428, 10, 32000, 0};
  CHECK_EQ(sfm::FlowMlPerMin(32000 + 1428, o2), 10000);
  const sfm::Scaling dual{25600, 1, 8388608, 0};
  CHECK(sfm::FlowMlPerMin(16777215, dual) > 0);
  CHECK(sfm::FlowMlPerMin(0, dual) < 0);
  const sfm::Scaling slow{1, 1, 0, 0};
  CHECK_EQ(sfm::FlowMlPerMin(32767, slow), 32767000);
  CHECK_EQ(sfm::DivideRounded(-5, 2), -3);
  CHECK_EQ(sfm::DivideRounded(5, 2), 3);
  const sfm::TempRow t{200, 0};
  CHECK_EQ(sfm::TemperatureMdegC(5000, t), 25000);
  CHECK_EQ(sfm::TemperatureMdegC(1, sfm::TempRow{0, 0}), 0);
}

TEST_CASE("validate: what a configuration may and may not say") {
  sfm::Config c{};
  CHECK(sfm::Validate(c) == sfm::Error::Ok);
  c.timing = sfm::TimingPolicy::Datasheet; // Auto needs the conservative waits
  CHECK(sfm::Validate(c) == sfm::Error::InvalidArgument);
  c = sfm::PresetFor(sfm::Variant::SFM3003_300_CE);
  CHECK(sfm::Validate(c) == sfm::Error::Ok);
  CHECK_EQ(c.address, uint8_t{0x2D});
  c.address = 0x28; // the CL address, not the CE's
  CHECK(sfm::Validate(c) == sfm::Error::InvalidArgument);
  c = sfm::PresetFor(sfm::Variant::SFM3003_300_CE);
  c.averaging = 129;
  CHECK(sfm::Validate(c) == sfm::Error::InvalidArgument);
  c.averaging = 0;
  CHECK(sfm::Validate(c) == sfm::Error::Ok);
  c = sfm::PresetFor(sfm::Variant::SFM3304_250_D);
  c.averaging = 0; // average-until-read exists (SFM3304 datasheet, 3.2)
  CHECK(sfm::Validate(c) == sfm::Error::Ok);
  c.averaging = 129;
  CHECK(sfm::Validate(c) == sfm::Error::InvalidArgument);
  c = sfm::PresetFor(sfm::Variant::SFM3300_D);
  c.averaging = 4;
  CHECK(sfm::Validate(c) == sfm::Error::InvalidArgument);
  c = sfm::PresetFor(sfm::Variant::SFM3300_D);
  c.filter_enabled = true;
  CHECK(sfm::Validate(c) == sfm::Error::InvalidArgument);
  c = sfm::PresetFor(sfm::Variant::SFM4300_20_B);
  c.consecutive_error_limit = 0;
  CHECK(sfm::Validate(c) == sfm::Error::InvalidArgument);
  c = sfm::PresetFor(sfm::Variant::SFM4300_20_B);
  c.range_margin_permille = 1001;
  CHECK(sfm::Validate(c) == sfm::Error::InvalidArgument);
  c = sfm::PresetFor(sfm::Variant::SFM4300_20_B);
  c.scaling_check = ut::OutOfRange<sfm::ScalingCheck>(uint8_t{9});
  CHECK(sfm::Validate(c) == sfm::Error::InvalidArgument);
  c = sfm::Config{};
  c.family = sfm::Family::Sf05; // Auto does not exist for the families without
                                // an identifier
  CHECK(sfm::Validate(c) == sfm::Error::InvalidArgument);
  c.family = sfm::Family::Sf06Gen2;
  c.address = 0x28; // no SFM3304 row uses it
  CHECK(sfm::Validate(c) == sfm::Error::InvalidArgument);
  c.address = 0x2E;
  CHECK(sfm::Validate(c) == sfm::Error::Ok);
  c = sfm::PresetFor(sfm::Variant::SFM4300_20_B);
  c.family = sfm::Family::Sf05; // a variant names its own family
  CHECK(sfm::Validate(c) == sfm::Error::InvalidArgument);
  CHECK(sfm::PresetFor(sfm::Variant::Auto).variant == sfm::Variant::Auto);
  constexpr sfm::CheckedConfig kChecked{sfm::PresetFor(sfm::Variant::SFM4300_50_O)};
  CHECK(kChecked.Get().variant == sfm::Variant::SFM4300_50_O);
}

} // namespace
