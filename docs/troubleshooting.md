# Troubleshooting

Find the cause of an error or a wrong reading. The first line of every answer is the error name that the call
returned (`sfm::ToString(error)` gives it for logs).

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    - IRQn and start-up behaviour: Sensirion AG SFM4300 D1 1.0, 3.2 Sensor start-up and warm-up; 3.3 IRQn, [p. 8](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=8)
    - Start measurement, results and the NACK of a read without data: Sensirion AG SFM4300 D1 1.0, 4.3.1 Start continuous measurement, [p. 10](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=10)
    - Status word: Sensirion AG SFM4300 D1 1.0, 4.3.2 Read measurement results, Table 5, [p. 11](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=11)
    - CRC-8 of every word: Sensirion AG SFM4300 D1 1.0, 4.4 Checksum calculation, Table 14, [p. 15](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=15)
    - Product identifier and serial number: Sensirion AG SFM4300 D1 1.0, 4.3.9 Read product identifier, Table 13, [p. 15](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=15)
    - Sfx6000 frame, start codes and addresses (preliminary): Sensirion AG SFx6000 I2C 1.1, I2C interface reference (preliminary), [p. 6](https://sensirion.com/media/documents/3D857436/65156AC0/GF_AN_SFC6xxx_I2C_InterfaceReference1.1.pdf#page=6)
    - Legacy command set with pointer registers (Sf05): Sensirion AG SFM-05 not printed (vendor page: 10/2026), I2C functional description (commands, EEPROM, serial, CRC), [p. 4](https://sensirion.com/media/documents/BE7405C4/6AC34DFB/GF_AN_SFM-05_I2C_Functional_Description.pdf#page=4)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## By error

| Error | Where you see it | What to check |
| --- | --- | --- |
| `NoDevice` | Initialisation, a poll, a write, a read after a long silence | Nothing acknowledged the address. Check the supply (see the [variant table](hardware.md#variants): the SF05 parts need 5 V), SDA and SCL wiring and pull-ups, and the address: the SFM4300 has 0x2A to 0x2D by its ADDR pin, the other parts a fixed one. A sleeping sensor does not answer until woken |
| `DeviceIdMismatch` | Initialisation | A sensor answered but it is not the configured part. Check `Config::variant` and `address`; with `Variant::Auto` check that the part is in the configured family. A prototype (revision 8x) needs `accept_prototype` |
| `Bus` | Any write | A write hook returned `false`: a NACK on a write, or a bus error. Check the pull-ups, the clock against the part's limit, and that the port sends a STOP |
| `Timeout` | Wake, reset, a stretched read | The sensor did not answer in time. After `Sleep()` the wake can take up to 20 ms; a reset up to 100 ms on the SF05 parts. A sensor held in reset, or SCL held low, stays silent |
| `NoData` | `ReadSample()` | Not a fault: no new sample yet. The SF06 parts make one every 0.5 ms; after an averaging of N, every N x 0.5 ms. Ask again later. After a silence longer than the datasheet bound the driver asks with a real command and returns `DeviceReset` or `NoDevice` |
| `DeviceReset` | `ReadSample()` | No new sample for longer than the datasheet bound, and the sensor acknowledged the command the driver then sent: it was reset (a power dip, a general call from another device, a brown-out) or is now stopped. The driver is `Idle`; start again. With IRQn wired, low after a start also means reset |
| `Crc` | A read | A word failed its CRC and the whole transfer was dropped. Check SCL speed, wire length, noise and the pull-ups. A line stuck low or high gives this, not a value |
| `InvalidReply` | A read | The CRC is valid but the reply cannot be sent by the sensor, for example an all-zero identifier. Check for a stuck bus or another device at the address |
| `StatusMismatch` | `ReadSample()` | The status word does not say the measurement the driver started is running (wrong gas index or concentration). Stop and start again; on a bus with another master, check that nothing else sent a start command |
| `Stale` | `ReadSample()` | The sample is inside the warm-up time (`require_settled`), smoothed (`reject_smoothed`) or older than `max_age_us`. Not a fault; the policy is yours |
| `OutOfRange` | `ReadSample()` | The flow is beyond the part's span plus the margin. Check the installation and the gas; `RangePolicy::Flag` returns the value flagged |
| `DeviceFault` | `StartMeasurement()`, `ReadScaling()` | The scale, offset or unit the sensor reports differs from the datasheet. A recalibrated part, a wrong variant, or a corrupted sensor. `scaling_check` chooses how strict to be |
| `StateUnknown` | After a failed command, or at the SF05 error limit | The driver cannot know the sensor's mode. Call `EnsureInitialized()` (or `SoftReset()` first) |
| `Busy` | Any call | An operation is pending (finish it with `Poll()` or `AwaitPending()`), or the call is illegal in this mode: configuration only when idle, a read only while measuring, a wake only when asleep |
| `NotInitialized` | Any call | `EnsureInitialized()` has not succeeded, or a reset or `Deinitialize()` ran since |
| `Unsupported` | A call | The part, the port or the family mask does not offer the call: `SoftReset()` needs `allow_general_call_reset` and the port's `WriteGeneralCall()`; `HardReset()` needs `PowerCycle()` or `SetResetPin()`; `Sleep()` is for the SF06 parts and SFM3304 |
| `InvalidArgument` | A call, initialisation | A `Config` field or argument is out of range; `Validate(config)` says so without touching the bus. Typical: an address outside the part's set, `Auto` with `TimingPolicy::Datasheet`, averaging on a part without it, an oxygen fraction above 1000 |

## By symptom

| Symptom | Likely cause |
| --- | --- |
| A flow that is always 0 on an SFM4200 or SFM4100 | An all-zero frame has a valid CRC there (I9 exception); check the bus level and `GetStats()` |
| Flow reads too high or too low by a fixed factor | The wrong gas: `StartMeasurement(gas)` selects the calibration. A mixture needs its oxygen fraction (`UpdateConcentration()`) |
| The first samples are off | The warm-up is 30 ms after a start (50 ms on the SFM3304 and SFM3505); use `Sample::settled` |
| Readings stop after a while on an SF05 part | A lock-up after an aborted read: the port must never abort a read early. At the error limit the driver is `StateUnknown`; call `HardReset()` (the port's `PowerCycle()`, which must hold SDA and SCL low while the supply is off) and initialise again |
| Another device on the bus resets | `SoftReset()` is a general call and resets every device that listens. Leave `allow_general_call_reset` off when the bus is shared |
| Two sensors at one address | 0x2E is the SFM3019, SFM3304 and SFM3505; 0x40 is every SF05 part. Use the SFM4300's ADDR pin or separate buses |
| Nothing on an SFM4100 | It needs a clock-stretching read: the port needs `ReadStretch()` and a controller that tolerates a stretching slave |
| An SFM3304 not found after the MCU reset | It was asleep. Send the general call reset or cycle the supply, then initialise |

## Counters

`GetStats()` returns `crc_errors`, `no_data`, `bus_errors`, `device_resets`, `consecutive_errors` and
`power_cycles`. A rising `crc_errors` with a clean `bus_errors` is a signal-integrity problem; a rising
`device_resets` is a supply problem.
