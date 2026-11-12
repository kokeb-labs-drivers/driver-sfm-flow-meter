# Changelog

All notable changes to this repository are listed here. Versions follow [Semantic Versioning](https://semver.org/).
Entries under **Breaking** need source changes in code that used the affected API; any of them calls for a major
version bump (a minor bump before 1.0).

## [Unreleased]

### Added

- One C++20 driver for the Sensirion SFM digital mass flow meters on I2C, 32 variants in six protocol families: SF06
  (SFM4300, SFM3003 with -CET, SFM3013, SFM3019, SFM3119), SF06 gen 2 (SFM3304), SF06 dual (SFM3505), SF05
  (SFM3000, SFM3200, SFM3300, SFM3400, SFM4200), SF04 (SFM4100) and the preliminary SFM6000D.
- A part table with a preset and a build-time checked configuration per variant; calls that return the wait they
  need (`Poll()`, `AwaitPending()`); CRC-8 checked before any value is decoded; status, range and staleness checks;
  device-loss detection after the datasheet's silence bound (a NACK between samples is not a fault); per-family
  recovery rules; no empty I2C transfer anywhere (polls send the next real command).
- A port contract with optional hooks found at compile time: delay, clock, log, clock-stretching read, general call,
  power cycle, reset pin.
- An independent sensor model, host tests for every variant, compile-fail tests, a 17-invariant fuzz target, three
  ESP-IDF examples (compiled only), a host example, and the documentation set with the connector view of the
  hardware.
- A reply frame the sensor delivered counts for the silence clock even when a later step of the same call fails (the
  restart write after a read, the restore after a temperature read).
- A temperature is checked against the plausible band (-60 to +150 degC) only; a raw word is never refused for its
  pattern, because 0xFFFF is a real reading on the SFx6000 (-0.005 degC).
- The silence clock follows the last reply frame the sensor delivered (accepted or rejected, and raw reads), so an error
  burst followed by a NACK does not read as a reset. Without `NowUs()` the driver never reports a reset from a count
  of NACKed reads; the caller times the silence and calls `Stop()`. A reported reset holds the next command for the
  stop time. Options for the open hardware questions: `Config::poll_with_identifier` (K-STOPIDLE) and
  `Config::sf05_serial_low_word` (K-SF05-SERIAL); on the SFM3000 a refused 0x31AF keeps the high word of the serial
  number. `Gas::N2` is offered again at the air scale on the SFM3200-AW and SFM4200 (the datasheets list it); oxygen
  stays refused on the air-calibrated parts. Wake and reset polls give up a quarter after the datasheet figure.
- A temperature outside -60 to +150 degC (a stuck bus reads as -200 degC on the SF05 parts) is `InvalidReply`. A failed
  `SetAveraging()` leaves `StateUnknown` (it may have been applied). The optional second SFM3000 serial read is skipped
  when its pointer was refused. `Gas::N2` is offered at the air scale on every SF05 part. The fuzz target asserts
  invariants 9, 13 and 16 as documented and has a seventeenth for the temperature band.
- Not verified on hardware; the SFM6000D protocol is preliminary. See the contract page.
