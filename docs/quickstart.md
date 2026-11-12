# Quickstart

From a bare sensor to a flow reading on your MCU: wire it, write the port, read a sample.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Sensirion AG SFM4300 D1 1.0:

    - Start measurement, results and the NACK of a read without data: 4.3.1 Start continuous measurement, [p. 10](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=10)
    - Product identifier and serial number: 4.3.9 Read product identifier, Table 13, [p. 15](https://sensirion.com/media/documents/024CBAA1/62987ADF/Sensirion_Datasheet_SFM4300.pdf#page=15)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## 1. Wire it

Every module has a small connector with four to six circuits: supply, ground, SDA and SCL, and on some parts an
address pin or a ready line. The pin order of each connector is on the [Hardware page](hardware.md#connectors).

| MCU | Sensor | Notes |
| --- | --- | --- |
| I2C SDA | SDA | External pull-up to the bus voltage; the datasheets give no value |
| I2C SCL | SCL | External pull-up to the bus voltage |
| 3.3 V or 5 V | VDD | By part: see the supply column of [the variant table](hardware.md#variants) |
| GND | GND | |

Check the supply of your part before you connect it: the SF05 parts (SFM3000, 3200, 3300, 3400, 4200) need 4.75 V to
5.25 V, the SFM4100 3.5 V to 9 V and the SFM6000D 24 V; a sensor on a higher supply needs a bus that tolerates its
levels or a level shifter.

## 2. Write the port

The port is a class with a bus start-up, a write, a read and, for the blocking calls, a delay. The driver never
calls an SDK. This is the smallest port that runs an SF06 part:

```cpp
--8<-- "tests/docs/my_port.hpp:port"
```

Write the hooks with exactly these signatures, `noexcept`, returning `bool`: a hook declared any other way does not
satisfy the `I2cPort` concept and the build says which one. [Porting](porting.md) has the contract of each hook,
including the optional ones.

## 3. Read a flow

```cpp
--8<-- "tests/docs/quickstart.cpp:quickstart"
```

What happens, in order:

1. `PresetFor()` gives the datasheet configuration of the SFM3003-300-CL: its variant and address. The constructor
   stores it and sends nothing.
2. `EnsureInitialized()` wakes the sensor, reads the product identifier and checks that it is the configured part.
   A wrong part is `DeviceIdMismatch`; no answer at the address is `NoDevice`.
3. `StartMeasurement(Gas::Air)` reads the sensor's scale factor, offset and unit and compares them with the
   datasheet (a difference is `DeviceFault`), then sends the start command and returns the wait until the first
   sample.
4. `AwaitPending()` runs that wait with your `DelayUs()`. Without a delay hook, sleep for the wait yourself and call
   `Poll()` ([API guide](api-guide.md#run-without-blocking)).
5. `ReadSample()` reads one frame, checks the CRC of every word and the status word, and returns the flow in
   millilitres per minute and the temperature in milli-degrees Celsius.

`NoData` is not a failure: the sensor answers a read with a NACK until a new sample exists. Ask again later.

## If it does not work

The first line of the answer is the error name. [Troubleshooting](troubleshooting.md) has a table with the check to
make for each one.
