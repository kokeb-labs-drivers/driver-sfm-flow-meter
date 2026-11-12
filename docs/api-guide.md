# API guide

The driver's API by task, with code that compiles: initialise, measure, update a mixture, run without blocking,
sleep and wake, reset, handle errors and shut down.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    - Gen2 start with a filter argument and its status layout: Sensirion AG SFM3304 1.3, Digital interface description (start 0x3603 with filter, status word), [p. 11](https://sensirion.com/media/documents/8C3FC034/6AA3DD2E/GF_DS_SFM3304-xxx-D.pdf#page=11)
    - Dual-gas frame of 24 bits: Sensirion AG SFM3505 D3 1.0, Digital interface description (24-bit frame, commands), [p. 10](https://sensirion.com/media/documents/03EE880B/68F8CF90/GF_DS_SFM3505.pdf#page=10)
    - Sf04 trigger bytes and the hold-master read: Sensirion AG SFM4100 1.4, Digital interface (trigger, clock stretching, address per gas), [p. 4](https://sensirion.com/media/documents/6A24D8A6/65A002E6/GF_DS_SFM4100.pdf#page=4)
    - Start measurement, results and the NACK of a read without data: Sensirion AG SFM4300 D1 1.0, 4.3.1 Start continuous measurement, [p. 10](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=10)
    - Status word: Sensirion AG SFM4300 D1 1.0, 4.3.2 Read measurement results, Table 5, [p. 11](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=11)
    - Concentration update: Sensirion AG SFM4300 D1 1.0, 4.3.3 Update concentration, [p. 11](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=11)
    - Stop measurement: Sensirion AG SFM4300 D1 1.0, 4.3.4 Stop continuous measurement, [p. 12](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=12)
    - Averaging: Sensirion AG SFM4300 D1 1.0, 3.1 Measurement modes; 4.3.5 Configure averaging, [p. 12](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=12)
    - Scale factor, offset and unit: Sensirion AG SFM4300 D1 1.0, 4.3.6 Read scale factor, offset and flow unit; 4.5 Unit conversion, [p. 13](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=13)
    - Soft reset (general call): Sensirion AG SFM4300 D1 1.0, 4.3.7 Soft reset, [p. 13](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=13)
    - Sleep and wake-up: Sensirion AG SFM4300 D1 1.0, 4.3.8 Entering and exiting sleep mode, [p. 14](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=14)
    - Product identifier and serial number: Sensirion AG SFM4300 D1 1.0, 4.3.9 Read product identifier, Table 13, [p. 15](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=15)
    - Sfx6000 frame, start codes and addresses (preliminary): Sensirion AG SFx6000 I2C 1.1, I2C interface reference (preliminary), [p. 6](https://sensirion.com/media/documents/3D857436/65156AC0/GF_AN_SFC6xxx_I2C_InterfaceReference1.1.pdf#page=6)
    - Legacy command set with pointer registers (Sf05): Sensirion AG SFM-05 not printed (vendor page: 10/2026), I2C functional description (commands, EEPROM, serial, CRC), [p. 4](https://sensirion.com/media/documents/BE7405C4/6AC34DFB/GF_AN_SFM-05_I2C_Functional_Description.pdf#page=4)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## The shape of the API

| Group | Calls |
| --- | --- |
| Lifecycle | `BeginInitialize()`, `EnsureInitialized()`, `Deinitialize()`, `SoftReset()`, `HardReset()` |
| Measurement | `StartMeasurement(gas, o2_permille)`, `ReadSample()`, `ReadTemperature()`, `Stop()` |
| Setup | `SetAveraging()`, `ReadScaling()`, `UpdateConcentration()` |
| Power | `Sleep()`, `WakeUp()` |
| Waiting | `Poll()`, `Poll(elapsed_us)`, `AwaitPending()`, `HasPending()` |
| State | `GetState()`, `GetProductInfo()`, `GetScaling()`, `GetStats()`, `GetConfig()`, `GetCapabilities()`, `GetTemperature()` |

Every call that touches the sensor returns a `Result`: check `ok()` (or the `bool` conversion) and read `value`, or
read `error`. `Result` is `[[nodiscard]]`, so an unchecked call is a compiler warning. A call that may need a wait
returns `Result<Wait>`: `wait.us` is the time you owe the sensor before you call `Poll()`, and 0 means the
operation is done. Which calls a part offers is in `GetCapabilities()`; a call the part does not offer returns
`Unsupported` and sends nothing.

## Initialise

```cpp
--8<-- "tests/docs/api_tasks.cpp:lifecycle"
```

The constructor stores the port and the configuration and does no I/O. `EnsureInitialized()` is the one way to start
the sensor. It validates the configuration, brings the bus up through the port, wakes or resets the sensor as its
family needs, reads the product identifier and matches it to the configured part (the SF05 parts report a serial
number, and the SFM4100 has neither), and writes the averaging. Once it has succeeded it returns `Ok` at once with no
traffic, so a handler can call it before every use. After a failure the driver stays uninitialised and the next call
starts again.

Before initialisation every other call returns `NotInitialized` and sends nothing. `GetProductInfo()` has the
identifier, the serial number and whether it is a prototype or a preliminary part.

## Read a flow

```cpp
--8<-- "tests/docs/quickstart.cpp:quickstart"
```

`StartMeasurement(gas)` reads the sensor's scale factor, offset and unit and compares them with the datasheet
(`Config::scaling_check`), then starts the measurement. The first sample is ready after the start wait; accurate
after the warm-up time (`Sample::settled`, which needs `NowUs()`). `ReadSample()` never waits for data: a sensor
without a new sample answers the read with a NACK, which is `NoData` and changes nothing (with an averaging of N a new
sample comes every N x 0.5 ms). Only a silence longer than the datasheet bound makes the driver ask with a real command:
no answer is `NoDevice`, an answer is `DeviceReset`. Without `NowUs()` the driver never escalates: time the silence
yourself and call `Stop()`, a real command that fails when the sensor is gone.

A `Sample` has the flow in millilitres per minute (rounded to nearest, never clamped), the raw value, the
temperature in milli-degrees Celsius where the part has it, the status word and its decoded fields, and a timestamp
from `NowUs()`. The SFM3505 also has the oxygen channel in `flow2_ml_per_min`.

## Read the raw words

`ReadRawReply()` is the single-shot raw access for bring-up and diagnostics: one read of the running measurement that
returns the CRC-checked words (`RawReply`) with no decoding, no status check and no range check. `GetLastReply()`
returns the words of the last frame read by either call, without I/O. Use `ReadSample()` for values: the raw reply
skips every check that makes a sample trustworthy. It never reports a silent sensor either: a reset sensor read only
through this call returns `NoData` for ever, and `ReadSample()` is the call that reports `DeviceReset`.

## Update a mixture

On parts with mixtures (`Gas::AirO2` and the others of the table row) the oxygen fraction goes in per mille, from 0
to 1000. `UpdateConcentration()` changes it while measuring, without a restart:

```cpp
--8<-- "tests/docs/api_tasks.cpp:mixture"
```

The sensor needs the two commands of an update back to back, with no read between them; the driver does that.
Do not call it more often than once per millisecond. A fraction above 1000 stops the measurement, which is why
the driver refuses it with `InvalidArgument` before sending.

## Run without blocking

The calls return the wait the sensor needs; the application owns the clock. `EnsureInitialized()` and the other
blocking forms are `Begin...()` plus `AwaitPending()`, and need the port's `DelayUs()`.

```cpp
--8<-- "tests/docs/api_tasks.cpp:polled"
```

`Poll()` uses the port's `NowUs()`; `Poll(elapsed_us)` takes the time you waited, and a smaller value than the wait
returns the remainder. While an operation is pending every other call returns `Busy`. For an interrupt-driven
design, the SFM4300's IRQn pin is the sensor's own "result ready" line: wire it to an interrupt and call
`ReadSample()` from the handler's task.

## Handle errors

```cpp
--8<-- "tests/docs/api_tasks.cpp:errors"
```

| Error | Meaning | Usual action |
| --- | --- | --- |
| `NoData` | The read was NACKed: no new sample yet | Ask again later; it is not a fault |
| `DeviceReset` | No new sample for the whole bound and the sensor then answered a command: the measurement is over and the sensor is idle | Start the measurement again |
| `NoDevice` | The address is not acknowledged | Check wiring, supply and address |
| `Crc` | A word failed its CRC; the whole transfer was dropped | Retry; count it (`GetStats()`) |
| `InvalidReply` | A CRC-valid reply the sensor cannot send (stuck line, reserved value) | Treat as a bus fault |
| `StatusMismatch` | The status word does not describe the measurement that was started | Stop and start again |
| `Stale` | The sample may not be fresh enough | Use or drop it by policy |
| `OutOfRange` | A flow outside the part's span plus margin | Check the installation |
| `DeviceFault` | The sensor's scaling differs from the datasheet | Do not trust the sensor; see `scaling_check` |
| `StateUnknown` | A failed command left the sensor's mode unknown | `EnsureInitialized()` again |
| `Busy` | An operation is pending, or the call is illegal in this mode | Finish with `Poll()` |

All errors with their checks are in [Troubleshooting](troubleshooting.md).

## Sleep and wake

```cpp
--8<-- "tests/docs/api_tasks.cpp:sleep"
```

Sleep is for the SF06 parts and the SFM3304, idle only. `WakeUp()` polls with the stop command until the
sensor acknowledges it (`Timeout` when it stays asleep) and writes the averaging again. `SoftReset()` while asleep is
`Busy` and sends nothing: a sleeping sensor cannot be soft reset (datasheet 4.3.8); wake it first. The poll that
`EnsureInitialized()` starts with wakes a sleeping sensor too (K-WAKE-INIT).

## Reset

`SoftReset()` is the part's reset: the general call 0x06 for the SF06 families, SFM3505 and SFM6000D (it needs
`Config::allow_general_call_reset` and the port's `WriteGeneralCall()`, because it resets every device on the bus that
listens), or a command for the SF05 and SF04 parts. Afterwards the driver is `Uninitialized`. `HardReset()` pulses the
SFM4200's /RES pin or cycles the supply through the port; without the hook it is `Unsupported`. A failed reset leaves
`StateUnknown`.

The legacy parts count errors in a row (`Config::consecutive_error_limit`); at the limit the driver reports
`StateUnknown`. It does not cycle the supply inside `ReadSample()`: call `HardReset()` (the port's `PowerCycle()`,
which must hold SDA and SCL low while the supply is off) and initialise again.

## Check the scaling

```cpp
--8<-- "tests/docs/api_tasks.cpp:scaling"
```

## Shut down

```cpp
--8<-- "tests/docs/api_tasks.cpp:deinit"
```

`Deinitialize()` makes no bus traffic: stop the measurement first if one runs.

## Temperature on the legacy parts

The SF05 parts with a temperature output (SFM3000, the -AW variants and the SFM4200) read it by a pointer sequence
that needs 5 ms: `ReadTemperature()` returns the wait, and `GetTemperature()` has the value in milli-degrees once the
operation ended. A failure after the pointer moved leaves `StateUnknown`.
