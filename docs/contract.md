# Contract

What the SFM flow meter driver promises, what it needs, the rules a caller follows, and what is not verified.

The driver inherits the [Kokeb Labs driver
contract](https://github.com/kokeb-labs/.github/blob/main/standards/driver.md);
this page states only what is specific to it. A change to anything here is a breaking change, listed under
**Breaking** in the [changelog](changelog.md).

!!! warning "Not a safety device"
    The vendor states that these flow meters are not designed for safety-critical or life-supporting applications
    unless the datasheet says otherwise. This driver adds no safety claim: it has been verified against a model of the
    sensor, never against a sensor ([What is not verified](#what-is-not-verified)). A design that depends on a flow
    reading for safety needs its own risk analysis and verification on the real part.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    - Sf04 CRC: Sensirion AG SFM4100 CRC 1.0, CRC calculation, [p. 2](https://sensirion.com/media/documents/FA1A9B3B/6165B558/Sensirion_Mass_Flow_Meters_CRC_Calculation_V1_1.pdf#page=2)
    - Command framing and read framing: Sensirion AG SFM4300 D1 1.0, 4.2 I2C sequences, [p. 9](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=9)
    - Status word: Sensirion AG SFM4300 D1 1.0, 4.3.2 Read measurement results, Table 5, [p. 11](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=11)
    - Stop measurement: Sensirion AG SFM4300 D1 1.0, 4.3.4 Stop continuous measurement, [p. 12](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=12)
    - Scale factor, offset and unit: Sensirion AG SFM4300 D1 1.0, 4.3.6 Read scale factor, offset and flow unit; 4.5 Unit conversion, [p. 13](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=13)
    - Soft reset (general call): Sensirion AG SFM4300 D1 1.0, 4.3.7 Soft reset, [p. 13](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=13)
    - CRC-8 of every word: Sensirion AG SFM4300 D1 1.0, 4.4 Checksum calculation, Table 14, [p. 15](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=15)
    - Sfx6000 frame, start codes and addresses (preliminary): Sensirion AG SFx6000 I2C 1.1, I2C interface reference (preliminary), [p. 6](https://sensirion.com/media/documents/3D857436/65156AC0/GF_AN_SFC6xxx_I2C_InterfaceReference1.1.pdf#page=6)
    - Legacy command set with pointer registers (Sf05): Sensirion AG SFM-05 not printed (vendor page: 10/2026), I2C functional description (commands, EEPROM, serial, CRC), [p. 4](https://sensirion.com/media/documents/BE7405C4/6AC34DFB/GF_AN_SFM-05_I2C_Functional_Description.pdf#page=4)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Promises

- The constructor is `noexcept`, stores the configuration, does no I/O and cannot fail.
- `EnsureInitialized()` is the only way to start the driver. It validates the configuration (`InvalidArgument`,
  nothing sent), brings the bus up, wakes or resets the sensor as its family needs, reads the product identifier and
  matches it to the configured part, and writes the averaging. On any failure the driver stays uninitialised and the
  next call starts again; once it has succeeded, later calls return `Ok` with no I/O.
- Every call that touches the sensor returns `NotInitialized` before initialisation succeeds. No call initialises or
  resets the sensor as a side effect.
- Every port failure propagates; no getter returns a stale or zero value after a failed read.
- Every wait is returned to the caller or bounded; nothing sleeps unless the caller asked for a blocking call and the
  port has `DelayUs()`.
- `PresetFor(variant)` equals the datasheet configuration of that part.
- A missing `DelayUs()` is a compile error only for code that needs it; a misdeclared hook is a compile error.

### The safety invariants

These are what the tests and the fuzzer check after every call.

| Id | Invariant |
| --- | --- |
| I1 | **CRC first.** Every received word is CRC-checked before it is decoded or stored; one bad word discards the whole transfer, including words whose own CRC was fine. Every sent argument word carries its CRC. Reads are a whole number of three-byte words |
| I2 | A failed call returns `T{}`: no stale or partial value leaves in a failed `Result` |
| I3 | A call refused for `InvalidArgument`, `Busy`, `NotInitialized`, `Unsupported` or `StateUnknown` sends nothing |
| I4 | **Mode legality.** The driver never sends a command the datasheet forbids in the current mode; the sensor model records any as a violation and the fuzzer treats one as a failure |
| I5 | **Provenance.** A `Sample` is built only from a frame whose words passed the CRC and whose status word (where the part has one) describes the running measurement |
| I6 | **No clamping.** An out-of-range value is an error or, with `RangePolicy::Flag`, a value with a flag; never an adjusted number |
| I7 | **Bounded.** Every loop and wait has a datasheet or `Config` bound |
| I8 | **A reset is reported, and a healthy sensor is never reported as reset.** A NACK of a read means "no new sample yet" and changes nothing; only a silence longer than the datasheet bound (no reply frame of any kind, rejected ones and raw reads included), confirmed by a real command, ends in `DeviceReset` and `Idle`. With a clock a reset is reported as `DeviceReset`; without one the caller times the silence and calls `Stop()`. The driver never restarts a measurement itself |
| I9 | **Stuck lines are not data.** An all-ones reply fails the CRC, and so does an all-zero one on the parts with CRC init 0xFF; a CRC-valid reply the sensor cannot send is `InvalidReply`, a temperature outside -60 to +150 degC included |
| I10 | **No empty transfer.** The driver never sends a write with no data byte; the model refuses one and the fuzzer treats it as a failure. Polls send the next real command |

I9 has one stated exception: on the CRC-init-0x00 parts (the SF05 and SF04 families: SFM3000, SFM3200, SFM3300,
SFM3400, SFM4200 and SFM4100) an all-zero frame has a valid CRC (the CRC of a zero word with init 0x00 is 0x00), so a
bus stuck low reads as a valid zero flow there. The driver cannot tell it from a real zero; use the error counters and
a plausibility check in the application. Where a zero word is outside what the sensor can send (an identifier, a
scale, a temperature outside the band) it is `InvalidReply`, not data.

## Depends on

| Dependency | Version | Why |
| --- | --- | --- |
| A C++20 compiler | GCC 10, Clang 13 or later | Concepts constrain the port |
| CMake | 3.16 or later | Generates `sfm_flow_meter_version.h` |
| Your port | | The bus, within the bus requirements; optionally the delay, the clock, the logging, the stretching read, the general call and the supply and reset pins ([Porting](porting.md)) |

Nothing else: no RTOS, no SDK, no other repository of the org.

## Threading

The driver is not thread-safe and takes no lock. Use each driver object from one task at a time, or guard every call
on it with one mutex; the layer that owns the driver owns that lock, and the bus lock when several devices share a
bus. Nothing may run in an ISR: a call makes bus transfers. Separate objects share no state.

## Errors

`sfm::Error` inside `sfm::Result<T>`. Values 0 to 9 are the shared base set; 16 and up are specific to this driver.

| `Error` | Value | Returned when |
| --- | --- | --- |
| `Ok` | 0 | The call succeeded |
| `NotInitialized` | 1 | `EnsureInitialized()` has not succeeded, or `Deinitialize()` or a reset ran since |
| `InvalidArgument` | 2 | A `Config` field or argument is out of range (`Validate()`); nothing was sent |
| `Bus` | 3 | A write hook returned `false` |
| `Timeout` | 4 | A bounded wait or poll ended: a wake that stays unanswered, a reset, a stretched read |
| `DeviceIdMismatch` | 5 | The product identifier is not the configured part, or a prototype without `accept_prototype` |
| `Crc` | 6 | A received word failed its CRC-8; the whole transfer was discarded |
| `Busy` | 7 | The call is not allowed in the current mode, or a wait is pending |
| `DeviceFault` | 8 | The scaling the sensor reports differs from the part's datasheet value |
| `Unsupported` | 9 | The part, the port hook or the family mask does not offer this call |
| `NoData` | 16 | The read was NACKed: no new sample yet (or the sensor is idle) |
| `NoDevice` | 17 | The address is not acknowledged on a write, a poll, initialisation, or the command sent after a long silence |
| `OutOfRange` | 18 | A flow or temperature outside the part's span plus margin |
| `StatusMismatch` | 19 | The status word does not describe the measurement the driver started |
| `Stale` | 20 | The sample may not be fresh enough (settling, smoothing or age) |
| `DeviceReset` | 21 | No new sample for the whole datasheet bound and the sensor acknowledged the command the driver then sent: the measurement is over, the sensor is idle, nothing was restarted |
| `StateUnknown` | 22 | A failed command left the sensor's mode unknown; initialise again |
| `InvalidReply` | 23 | A CRC-valid reply the sensor cannot send (stuck line, reserved value) |

The [API guide](api-guide.md#handle-errors) says what to do about each, and [Troubleshooting](troubleshooting.md)
what to check.

## Memory

No heap, no exceptions, no RTTI, no `std::function`, no `std::string`, no `printf` and no static mutable state. A
driver object is about 400 bytes: the port reference, the `Config`, the product information, the scaling, the
counters and the plan of the operation in progress (at most ten steps). The largest buffer is 18 bytes (the
product identifier read).

## Platform

Platform-free: the same code runs on any target with a C++20 compiler, including the host tests. The ESP-IDF port in
[`examples/esp32`](examples.md) is an example, not part of the driver.

## What is not verified

Every statement above holds for the driver against the sensor model in `tests/`. The model is independent code, but
it is written from the same documents by the same author, so a misreading of a document appears in both. None of the
following has been done:

| Not verified | Consequence |
| --- | --- |
| Any behaviour on a real sensor, of any variant | The timings, the NACK behaviour, the lock-up of the SF05 parts and the stuck-line detection are as documented, not as measured |
| The ambiguities listed below | Each has a configuration knob; the default is the conservative reading |
| The MISRA C++:2023 CodeQL scan | The review against the matrix was done by hand; the scan is run by CI and was not run here |
| ESP32 builds | The three examples compile against stubs of the ESP-IDF headers; they have not been built with ESP-IDF |
| The preliminary SFM6000D | Every fact is tagged `[P]` in the table, and `ProductInfo::preliminary` says so; each is one model flip away from changing |
| Pin order and pad names marked `unverified` on the [Hardware page](hardware.md#not-verified) | They are not in the documents that were read |

### Ambiguities and their knobs

Doubt is moved into a documented knob, never dropped. The default is the conservative reading.

| Id | The doubt | Knob and default |
| --- | --- | --- |
| K-TIME | SFM3xxx reset is 2 ms but wake is 16 ms typical; the SFM4300 allows 20 ms | `Config::timing` `Conservative` (20 ms) or `Datasheet` |
| K-RS | A repeated START between command and read is neither shown nor forbidden | Not a hook; `kRepeatedStart = false` |
| K-SCALE | The sensor reports its scale and the datasheet states it; they could differ on a recalibrated part. SF05 parts report the air scale whatever the gas | `Config::scaling_check` `Strict`, `Lenient`, `Off`; the SF05 check compares with the first gas row |
| K-RANGE | The raw range is wider than the specified range | `RangePolicy::Reject` with a 2 % margin, or `Flag` |
| K-SMOOTH | After a read gap above 64 ms the flow is a smoothed estimate | `Config::reject_smoothed` off; `Sample::smoothed` |
| K-WARM | Accuracy is within a few percent during the first 30 ms | `Config::require_settled` off; `Sample::settled` |
| K-ID | The low byte of the product number is "revision, subject to change" | The variant lookup ignores it; `accept_prototype` off |
| K-GC | The general call resets every device that listens | `allow_general_call_reset` off |
| K-CMDECHO | After a NACKed command the sensor may or may not have taken it | `StateUnknown` until initialised again |
| K-SLEEPRESET | A sleeping sensor cannot be soft reset | `SoftReset()` from `Asleep` is `Busy`; wake first |
| K-SF05-RESTART | The repeat of the start command before each read is "recommended" | `Config::restart_before_read` on |
| K-SF05-LOCK | A lock-up after a NACKed first byte needs a hard reset | The port never aborts a read; at the error limit the state is `StateUnknown` and the caller runs `HardReset()` (the port's `PowerCycle()`, SDA and SCL held low) |
| K-SF05-RES | The SFM4200 reset pulse width is in a drawing that was not read | A default width, `unverified` |
| K-CONC | The encoding of the oxygen fraction in per mille for the mixture commands | 0 to 1000 as the SF06 documents state; above 1000 is `InvalidArgument` |
| K-SLEEP-AVG | The datasheets do not say whether the averaging survives sleep | It is written again after `WakeUp()` |
| K-WAKE-INIT | Whether a sensor that sleeps when the driver starts wakes on the poll that identification begins with | The poll sends the stop command (address plus two bytes), which wakes an SF06 sensor and the SFM3304 alike; bench step: put each family to sleep and initialise |
| K-DEVRESET | A reset mid-measurement looks like "a read then gets a NACK", the same as no new sample | `NoData` until the silence bound (first-result time + one averaged period + 10 % + 1 ms, measured from the last reply frame the sensor delivered, whether accepted or not), then the stop command: no answer `NoDevice`, an answer `DeviceReset` and the next command waits for the stop time. Without `NowUs()` the driver never escalates (a count of NACKed reads says nothing about time, a period is up to 64 ms): the caller times the silence and calls `Stop()`, which fails when the sensor is gone |
| K-SF05-SERIAL | AN-05 shows the serial number as one read after 0x31AE; the SFM4200 datasheet 5.2 gives two pointers, 0x31AE and 0x31AF, one word each | Two pointer-and-read pairs. On the SFM3000, whose application note lists 0x31AE only, a refused 0x31AF is not fatal: the serial keeps its high word (low word 0). `Config::sf05_serial_low_word = false` never sends 0x31AF. Bench step: read the serial of one SFM3000, SFM3200 and SFM4200 |
| K-O2-SF05 | The SFM4200 corrects oxygen by dQ = 1.3e-2 Q + 1.2e-4 Q^2 (4.4); the SFM3200 refers to an application note that was not read; only the SFM3000 states an oxygen scale | `Gas::O2` is offered on the SFM3000 only; on the SFM3200 and SFM4200 it is `InvalidArgument` (a flow in oxygen needs a correction the driver does not apply). `Gas::N2` is offered at the air scale on every SF05 part: the media tables of the SFM3200, SFM3300, SFM3400 and SFM4200 list it (1.3), the SFM3300 and SFM3400 scale rows read "Air, N2" (120 and 800) and the SFM3000 states one scale for both (2.1) |
| K-PRELIM | Every SFM6000D fact is preliminary; the status concentration is unchecked, 0xE102 is both the product identifier and the temperature pointer, and the scale reply has five words | Table flag `preliminary`; `Config::scaling_check` `Strict` |
| K-XFAM | What a sensor does with another family's command is not stated | The driver never sends it; `Auto` stays inside the configured family |
| K-AUTO | The same address means different parts across families | `Variant::Auto` only inside one family |
| K-STOPIDLE | `Stop` while idle is not documented | Every idle poll (initialisation, wake, resets) sends the stop command; `Stop()` while idle is a no-op. Not verified on hardware: `Config::poll_with_identifier` polls with the identifier command instead, so a bench result needs no code change. The wake poll gives up a quarter after the datasheet figure; `TimingPolicy::Conservative` (20 ms) is the supported choice for sleep |

Other deliberate choices: the SFM4100 always enters `Measuring` on `StartMeasurement()` (it has no stop), and the
SF05 serial number is read through the two pointers 0x31AE and 0x31AF, one word each (K-SF05-SERIAL).

## Deviations from the standard

| Id | Where | What and why |
| --- | --- | --- |
| MD-CPP-009 | `inc/sfm_flow_meter.hpp`, the include of `src/sfm_flow_meter.ipp` | The implementation of the class template is in an `.ipp` file that the header includes at its end, so the header stays readable. The deviation is recorded at the line with `codeql::misra_deviation_next_line` |
