/**
 * @file datasheet_expect.hpp
 * @brief What the datasheets say, typed by hand and independent of the driver's
 * table.
 *
 * `test_parts.cpp` compares this list with `sfm::parts::kTable` row by row, and
 * the chip model
 * (`model_sfm.hpp`) behaves as this list says. A digit that is wrong in only
 * one of the two fails a test. The numbers come from the documents listed in
 * docs/datasheets.yml: scale factors and offsets from the scaling tables,
 * product identifiers from the identification sections, timings from the timing
 * tables (maxima), addresses from the I2C sections.
 */
#ifndef SFM_FLOW_METER_TEST_DATASHEET_EXPECT_HPP
#define SFM_FLOW_METER_TEST_DATASHEET_EXPECT_HPP

#include <array>
#include <cstdint>

#include "sfm_flow_meter_types.hpp" // enumerators only: Variant and Gas names

namespace expect {

/// The command-set family as the model sees it.
enum class Kind : uint8_t { Sf06, Gen2, Dual, Sf05, Sf04, Sfx };

/// One calibrated gas as a datasheet states it.
struct GasX {
  sfm::Gas gas{sfm::Gas::Air};
  uint16_t code{0};     ///< The start command, pointer or trigger byte.
  uint8_t nibble{0};    ///< Status word bits 15:12 while this command runs.
  bool argument{false}; ///< A mixture: the command carries an oxygen fraction.
  int32_t scale_num{1}; ///< Counts per slm (counts per 1 for the SF05 and SF04 families).
  int32_t scale_den{1};
  int32_t offset{0};
  uint16_t unit{0};
};

/// One part as the datasheets state it.
struct PartX {
  sfm::Variant variant{sfm::Variant::Auto};
  Kind kind{Kind::Sf06};
  uint8_t address{0};
  uint32_t product_id{0};
  uint8_t crc_init{0xFF};
  int32_t span_min{0};
  int32_t span_max{0};
  int32_t temp_scale{0};
  int32_t temp_offset{0};
  uint32_t first_sample_us{0};
  uint32_t reset_us{0};
  uint32_t warm_up_us{0};
  bool sleep{false};
  bool averaging{false};
  std::array<GasX, 7> gases{};
  uint8_t gas_count{0};
};

inline constexpr uint16_t kSlm = 0x0148;  // unit word, "slm"
inline constexpr uint16_t kSccm = 0x0145; // unit word decoded from the SFM4100 sample code

constexpr GasX G(sfm::Gas gas, uint16_t code, uint8_t nibble, bool arg, int32_t num, int32_t den,
                 int32_t offset, uint16_t unit) {
  return GasX{gas, code, nibble, arg, num, den, offset, unit};
}

/// SFM4300-20 gases: O2, air, N2O, CO2 and three mixtures (4300 datasheet table
/// 15).
constexpr PartX Sfm4300(sfm::Variant v, uint32_t id, int32_t scale, int32_t max, bool full) {
  PartX p{};
  p.variant = v;
  p.kind = Kind::Sf06;
  p.address = 0x2A;
  p.product_id = id;
  p.span_max = max;
  p.temp_scale = 200;
  p.first_sample_us = 12000;
  p.reset_us = 20000;
  p.warm_up_us = 30000;
  p.sleep = true;
  p.averaging = true;
  p.gases[0] = G(sfm::Gas::O2, 0x3603, 0, false, scale, 1, -28672, kSlm);
  p.gases[1] = G(sfm::Gas::Air, 0x3608, 1, false, scale, 1, -28672, kSlm);
  p.gas_count = 2;
  if (full) {
    p.gases[2] = G(sfm::Gas::N2O, 0x3615, 2, false, scale, 1, -28672, kSlm);
    p.gases[3] = G(sfm::Gas::CO2, 0x361E, 3, false, scale, 1, -28672, kSlm);
    p.gases[4] = G(sfm::Gas::AirO2, 0x3632, 6, true, scale, 1, -28672, kSlm);
    p.gases[5] = G(sfm::Gas::N2OO2, 0x3639, 7, true, scale, 1, -28672, kSlm);
    p.gases[6] = G(sfm::Gas::CO2O2, 0x3646, 8, true, scale, 1, -28672, kSlm);
    p.gas_count = 7;
  } else {
    p.gases[2] = G(sfm::Gas::AirO2, 0x3632, 6, true, scale, 1, -28672, kSlm);
    p.gas_count = 3;
  }
  return p;
}

/// SFM3003, SFM3013 and SFM3019, SFM3119 (their datasheets, table "scale
/// factors").
constexpr PartX Sfm3xxx(sfm::Variant v, uint32_t id, uint8_t addr, int32_t scale, int32_t offset,
                        int32_t min, int32_t max, bool heliox) {
  PartX p = Sfm4300(v, id, scale, max, false);
  p.address = addr;
  p.span_min = min;
  p.first_sample_us = 12000;
  p.reset_us = 2000;
  for (uint8_t i = 0; i < p.gas_count; ++i) {
    p.gases[i].offset = offset;
  }
  if (heliox) {
    p.gases[2] = G(sfm::Gas::HeOx, 0x3615, 2, false, scale, 1, offset, kSlm);
    p.gases[3] = G(sfm::Gas::AirO2, 0x3632, 6, true, scale, 1, offset, kSlm);
    p.gases[4] = G(sfm::Gas::HeOxO2, 0x3639, 7, true, scale, 1, offset, kSlm);
    p.gas_count = 5;
  }
  return p;
}

constexpr PartX Sf05(sfm::Variant v, int32_t scale_num, int32_t scale_den, int32_t offset,
                     int32_t min, int32_t max, bool temp, uint32_t first_us) {
  PartX p{};
  p.variant = v;
  p.kind = Kind::Sf05;
  p.address = 0x40;
  p.crc_init = 0x00;
  p.span_min = min;
  p.span_max = max;
  p.temp_scale = temp ? 100 : 0;
  p.temp_offset = temp ? 20000 : 0;
  p.first_sample_us = first_us;
  p.reset_us = 80000;
  p.warm_up_us = first_us;
  p.gases[0] = G(sfm::Gas::Air, 0x1000, 0, false, scale_num, scale_den, offset, 0);
  p.gas_count = 1;
  return p;
}

constexpr PartX Sf04(sfm::Variant v, sfm::Gas gas, uint8_t addr) {
  PartX p{};
  p.variant = v;
  p.kind = Kind::Sf04;
  p.address = addr;
  p.crc_init = 0x00;
  p.span_max = 20000;
  p.reset_us = 20000;
  p.warm_up_us = 500000;
  p.gases[0] = G(gas, 0xF1, 0, false, 1000, 1, 0, kSccm);
  p.gas_count = 1;
  return p;
}

constexpr PartX Sfx(sfm::Variant v, uint32_t id, int32_t max, int32_t a, int32_t b) {
  PartX p{};
  p.variant = v;
  p.kind = Kind::Sfx;
  p.address = 0x24;
  p.product_id = id;
  p.span_max = max;
  p.temp_scale = 200;
  p.first_sample_us = 12000;
  p.reset_us = 30000;
  p.warm_up_us = 1000000;
  p.gases[0] = G(sfm::Gas::O2, 0x3603, 0, false, a, 1, -28672, kSlm);
  p.gases[1] = G(sfm::Gas::Air, 0x3608, 1, false, a, 1, -28672, kSlm);
  p.gases[2] = G(sfm::Gas::CO2, 0x3615, 2, false, b, 1, -28672, kSlm);
  p.gases[3] = G(sfm::Gas::N2O, 0x361E, 3, false, b, 1, -28672, kSlm);
  p.gases[4] = G(sfm::Gas::Ar, 0x3624, 4, false, b, 1, -28672, kSlm);
  p.gases[5] = G(sfm::Gas::AirO2, 0x3650, 10, true, a, 1, -28672, kSlm);
  p.gas_count = 6;
  return p;
}

constexpr PartX Sfm3304() {
  PartX p = Sfm3xxx(sfm::Variant::SFM3304_250_D, 0x04050100, 0x2E, 120, 0, -250000, 250000, false);
  p.kind = Kind::Gen2;
  p.first_sample_us = 4000;
  p.reset_us = 2000;
  p.warm_up_us = 50000;
  p.gases[0] = G(sfm::Gas::Air, 0x3603, 0, false, 120, 1, 0, kSlm);
  p.gas_count = 1;
  return p;
}

constexpr PartX Sfm3505(sfm::Variant v, uint32_t id) {
  PartX p{};
  p.variant = v;
  p.kind = Kind::Dual;
  p.address = 0x2E;
  p.product_id = id;
  p.span_min = -150000;
  p.span_max = 300000;
  p.first_sample_us = 4000;
  p.reset_us = 2000;
  p.warm_up_us = 50000;
  p.averaging = true;
  p.gases[0] = G(sfm::Gas::Air, 0x3603, 0, false, 25600, 1, 8388608, 0);
  p.gas_count = 1;
  return p;
}

/// Every part: the order of `sfm::Variant` after `Auto`.
inline constexpr std::array<PartX, 32> kParts{{
    Sfm4300(sfm::Variant::SFM4300_20_B, 0x04030100, 2500, 20000, true),
    Sfm4300(sfm::Variant::SFM4300_20_O, 0x04030200, 2500, 20000, true),
    Sfm4300(sfm::Variant::SFM4300_20_P, 0x04030300, 2500, 20000, true),
    Sfm4300(sfm::Variant::SFM4300_50_B, 0x04030900, 1000, 50000, false),
    Sfm4300(sfm::Variant::SFM4300_50_O, 0x04030700, 1000, 50000, false),
    Sfm4300(sfm::Variant::SFM4300_50_P, 0x04030600, 1000, 50000, false),
    Sfm3xxx(sfm::Variant::SFM3003_300_CL, 0x04020100, 0x28, 170, -24576, -30000, 300000, false),
    Sfm3xxx(sfm::Variant::SFM3003_300_CE, 0x04020700, 0x2D, 120, -12288, -150000, 300000, false),
    Sfm3xxx(sfm::Variant::SFM3003_300_CET, 0x04020800, 0x2A, 120, -12288, -150000, 300000, false),
    Sfm3xxx(sfm::Variant::SFM3013_300_CL, 0x04020500, 0x2F, 170, -24576, -30000, 300000, false),
    Sfm3xxx(sfm::Variant::SFM3013_300_CLM, 0x04020200, 0x2F, 170, -24576, -30000, 300000, true),
    Sfm3xxx(sfm::Variant::SFM3019_200_CL, 0x04020600, 0x2E, 170, -24576, -10000, 240000, false),
    Sfm3xxx(sfm::Variant::SFM3119_240_CL, 0x04040100, 0x29, 170, -24576, -10000, 240000, false),
    Sfm3304(),
    Sfm3505(sfm::Variant::SFM3505_300, 0x04060200),
    Sfm3505(sfm::Variant::SFM3505_300_X, 0x04060100),
    Sf05(sfm::Variant::SFM3000_200_C, 140, 1, 32000, -200000, 200000, true, 30000),
    Sf05(sfm::Variant::SFM3200, 120, 1, 32768, -100000, 250000, false, 100000),
    Sf05(sfm::Variant::SFM3200_AW, 120, 1, 32768, -100000, 250000, true, 100000),
    Sf05(sfm::Variant::SFM3300_D, 120, 1, 32768, -250000, 250000, false, 100000),
    Sf05(sfm::Variant::SFM3300_AW, 120, 1, 32768, -250000, 250000, true, 100000),
    Sf05(sfm::Variant::SFM3400_D, 800, 1, 32768, -33000, 33000, false, 100000),
    Sf05(sfm::Variant::SFM3400_AW, 800, 1, 32768, -33000, 33000, true, 100000),
    Sf05(sfm::Variant::SFM4200, 256, 1, 0, 0, 160000, true, 100000),
    Sf04(sfm::Variant::SFM4100_AIR, sfm::Gas::Air, 1),
    Sf04(sfm::Variant::SFM4100_O2, sfm::Gas::O2, 2),
    Sf04(sfm::Variant::SFM4100_CO2, sfm::Gas::CO2, 3),
    Sf04(sfm::Variant::SFM4100_N2O, sfm::Gas::N2O, 4),
    Sf04(sfm::Variant::SFM4100_AR, sfm::Gas::Ar, 5),
    Sfx(sfm::Variant::SFM6000D_50, 0x06021184, 50000, 1024, 2560),
    Sfx(sfm::Variant::SFM6000D_20, 0x06021284, 20000, 2560, 5120),
    Sfx(sfm::Variant::SFM6000D_5, 0x06021484, 5000, 10240, 25600),
}};

/// The entry of a variant.
constexpr const PartX* Find(sfm::Variant v) noexcept {
  for (const PartX& p : kParts) {
    if (p.variant == v) {
      return &p;
    }
  }
  return nullptr;
}

} // namespace expect

#endif // SFM_FLOW_METER_TEST_DATASHEET_EXPECT_HPP
