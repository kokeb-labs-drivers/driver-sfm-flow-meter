/**
 * @file sfm_flow_meter_registers.hpp
 * @brief Command codes, status-word helpers and the CRC-8 of the SFM flow
 * meters.
 * @copyright Copyright (c) 2026 The driver-sfm-flow-meter authors.
 * SPDX-License-Identifier: MIT
 *
 * @details
 * The SF06 families and the SFx6000 have no register map: a transaction is a
 * 16-bit command, an optional 16-bit argument with its CRC, and a read of
 * 16-bit words that each end in a CRC-8. The SF05 family uses 16-bit pointer
 * codes, the SF04 family 8-bit commands. The same code can mean different gases
 * on different parts (0x3615 is nitrous oxide on the SFM4300-20, heliox on the
 * SFM3013-300-CLM and carbon dioxide on the SFM6000D), so start codes live in
 * the part table, never here as a gas.
 */
#ifndef SFM_FLOW_METER_REGISTERS_HPP
#define SFM_FLOW_METER_REGISTERS_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "sfm_flow_meter_detail.hpp"

/** @brief Command codes and field helpers. */
namespace sfm::reg {

/**
 * @defgroup sfm_flow_meter_registers Commands and fields
 * @brief Command codes with the datasheet section that defines each.
 * @{
 */

// SF06 families (SFM4300 section 4.3, SFM3xxx section 4.3, SFM3304 section 4.3,
// SFM3505 section 3.3).
inline constexpr uint16_t kCmdStartGas0 = 0x3603; ///< Start continuous measurement, gas 0.
inline constexpr uint16_t kCmdStartGas1 = 0x3608; ///< Start continuous measurement, gas 1.
inline constexpr uint16_t kCmdStartGas2 = 0x3615; ///< Start continuous measurement, gas 2.
inline constexpr uint16_t kCmdStartGas3 = 0x361E; ///< Start continuous measurement, gas 3.
inline constexpr uint16_t kCmdStartGas4 =
    0x3624; ///< Start, gas 4 (SFx6000 argon; reserved on SF06).
inline constexpr uint16_t kCmdStartMixture0 =
    0x3632; ///< Start, mixture 0 (argument: O2 in per mille).
inline constexpr uint16_t kCmdStartMixture1 = 0x3639;    ///< Start, mixture 1.
inline constexpr uint16_t kCmdStartMixture2 = 0x3646;    ///< Start, mixture 2.
inline constexpr uint16_t kCmdStartSfxMixture0 = 0x3650; ///< SFx6000: start, mixture 0.
inline constexpr uint16_t kCmdUpdateConcentration =
    0xE17D;                                           ///< Update concentration, part 1 (argument).
inline constexpr uint16_t kCmdResultPointer = 0xE000; ///< Point the read at the result buffer.
inline constexpr uint16_t kCmdStop = 0x3FF9;          ///< Stop continuous measurement.
inline constexpr uint16_t kCmdAveraging = 0x366A;     ///< SF06 and SFM3304: configure averaging.
inline constexpr uint16_t kCmdAveragingDual = 0x364D; ///< SFM3505: configure averaging.
inline constexpr uint16_t kCmdReadScale =
    0x3661;                                   ///< Read scale factor, offset and unit (argument).
inline constexpr uint16_t kCmdSleep = 0x3677; ///< Enter sleep mode.
inline constexpr uint16_t kCmdProductId = 0xE102;    ///< Read the product identifier (idle).
inline constexpr uint16_t kCmdDeviceIdDual = 0x365B; ///< SFM3505: read the device identifier.
inline constexpr uint16_t kCmdSfxScalePointer =
    0xE151; ///< SFx6000: point the read at the gas information.
inline constexpr uint16_t kCmdSfxTemperature =
    0xE102;                                        ///< SFx6000: point the read at the temperature.
inline constexpr uint8_t kGeneralCallReset = 0x06; ///< Byte sent to address 0x00 to reset.
inline constexpr uint8_t kGeneralCallAddress = 0x00; ///< The I2C general-call address.

// SF05 family (AN-05 section 4 and 8, SFM4200 section 5).
inline constexpr uint16_t kSf05Flow = 0x1000; ///< Start flow measurement / point at the flow.
inline constexpr uint16_t kSf05Temperature = 0x1001; ///< Point at the temperature.
inline constexpr uint16_t kSf05Reset = 0x2000;       ///< Soft reset (SFM3x00).
inline constexpr uint16_t kSf05ScaleFlow = 0x30DE;   ///< Flow scale factor.
inline constexpr uint16_t kSf05OffsetFlow = 0x30DF;  ///< Flow offset.
inline constexpr uint16_t kSf05Serial = 0x31AE; ///< Serial number, most significant word: point at
                                                ///< it, read one word.
inline constexpr uint16_t kSf05SerialLow = 0x31AF; ///< Serial number, least significant word.

// SF04 family (SFM4100 section 4, application note sample code).
inline constexpr uint8_t kSf04TriggerFlow = 0xF1;        ///< Trigger a flow measurement.
inline constexpr uint8_t kSf04TriggerTemperature = 0xF3; ///< Trigger a temperature measurement.
inline constexpr uint8_t kSf04SoftReset = 0xFE;          ///< Soft reset.
inline constexpr uint8_t kSf04UserRegister = 0xE3;       ///< Read the user register.
inline constexpr uint8_t kSf04ReadOnly2 = 0xE9;          ///< Read read-only register 2.
inline constexpr uint8_t kSf04Eeprom = 0xFA;             ///< Read the EEPROM.
inline constexpr uint16_t kSf04EepromSerial =
    0x02F8; ///< EEPROM address of the product serial number.
inline constexpr uint16_t kSf04EepromScale = 0x02B6; ///< EEPROM address of the scale factor.
inline constexpr uint16_t kSf04EepromUnit = 0x02B7;  ///< EEPROM address of the flow-unit word.
inline constexpr uint16_t kSf04FieldStride = 0x0300; ///< EEPROM size of one calibration field.

/// The flow unit word of every SF06 part: standard litre per minute at 20 C
/// (SFM4300 section 4.5.4).
inline constexpr uint16_t kUnitSlm = 0x0148;
/// The SFM4100 EEPROM flow unit decoded from the sample code: millilitre per
/// minute (sccm).
inline constexpr uint16_t kUnitSccm = 0x0145;

inline constexpr uint16_t kStatusPureGas = 0x3FF; ///< Status bits 9:0 of a pure gas.

/// Status word, bits 15:12: the index of the running start command (SF06,
/// SFx6000).
using StatusCommand = detail::Field<uint16_t, 12U, 4U>;
/// Status word, bit 11: exponential smoothing is active (SF06 and SFM3304).
using StatusSmoothing = detail::Field<uint16_t, 11U, 1U>;
/// Status word, bit 10: fixed-N averaging is active (SF06 and SFM3304).
using StatusFixedN = detail::Field<uint16_t, 10U, 1U>;
/// Status word, bits 9:0: 0x3FF for a pure gas, else the oxygen fraction in per
/// mille (SF06).
using StatusConcentration = detail::Field<uint16_t, 0U, 10U>;
/// SFM3304 status word, bit 9: a filter argument was received.
using StatusFilterArg = detail::Field<uint16_t, 9U, 1U>;
/// SFM3304 status word, bits 8:0: the configured number of averages.
using StatusAverages = detail::Field<uint16_t, 0U, 9U>;

/**
 * @brief CRC-8 of one 16-bit word as the chip computes it: polynomial 0x31 (x^8
 * + x^5 + x^4 + 1), no reflection, final XOR 0x00. The initial value is the
 * family's (0xFF for SF06, SFM3304, SFM3505 and SFx6000; 0x00 for SF05 and
 * SF04).
 * @param init The initial value.
 * @param high The high data byte (sent first).
 * @param low The low data byte.
 * @return The CRC.
 */
constexpr uint8_t Crc8(uint8_t init, uint8_t high, uint8_t low) noexcept {
  uint32_t crc = init; // all arithmetic in uint32_t (KB-CPP20-021)
  for (uint32_t byte = 0U; byte < 2U; ++byte) {
    crc ^= (byte == 0U) ? static_cast<uint32_t>(high) : static_cast<uint32_t>(low);
    for (uint32_t bit = 0U; bit < 8U; ++bit) {
      crc = ((crc & 0x80U) != 0U) ? ((crc << 1U) ^ 0x31U) : (crc << 1U);
      crc &= 0xFFU;
    }
  }
  return static_cast<uint8_t>(crc);
}

/**
 * @brief The CRC vectors of the datasheets, proved at build time (see the
 * static_assert below).
 * @return true when every vector holds.
 */
[[nodiscard]] constexpr bool CrcVectorsHold() noexcept {
  using detail::operator""_u8;
  // CRC(0xBEEF) = 0x92 with initial value 0xFF (every SF06 document, Table 14).
  bool ok = Crc8(0xFF_u8, 0xBE_u8, 0xEF_u8) == 0x92_u8;
  // The same word with the SF05 initial value 0x00 (AN-SFM-04): 0x13.
  ok = ok && (Crc8(0x00_u8, 0xBE_u8, 0xEF_u8) == 0x13_u8);
  // The SFx6000 worked sequence `36 61 36 08 D0`: CRC(0x3608) = 0xD0.
  ok = ok && (Crc8(0xFF_u8, 0x36_u8, 0x08_u8) == 0xD0_u8);
  // The SFM3304 scale-read argument 0x3603 carries CRC 0x3A (SFM3304
  // section 4.3.5).
  ok = ok && (Crc8(0xFF_u8, 0x36_u8, 0x03_u8) == 0x3A_u8);
  // A zero word is not a valid frame with the SF06 initial value: a stuck-low
  // bus fails its CRC.
  ok = ok && (Crc8(0xFF_u8, 0x00_u8, 0x00_u8) == 0x81_u8);
  // A zero word with the SF05 initial value is valid (CRC 0x00), so SF05 needs
  // a reply check too.
  return ok && (Crc8(0x00_u8, 0x00_u8, 0x00_u8) == 0x00_u8);
}
static_assert(CrcVectorsHold());

/** @} */

} // namespace sfm::reg

#endif // SFM_FLOW_METER_REGISTERS_HPP
