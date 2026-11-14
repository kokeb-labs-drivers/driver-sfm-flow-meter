# Figures

<!-- Written by kokeb-docs figures from docs/diagrams/figures/*.py. Do not edit by hand. -->

Every figure on this site is drawn by a Python module from facts extracted from the code, so it is rebuilt,
not redrawn, when the code changes. CI runs `kokeb-docs figures --check` and fails when a
committed figure no longer matches the code. Each figure below lists the files it is drawn from.

## Inside the SFM flow meter

<!-- figure: chip-blocks -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-blocks.dark.svg">
  <img src="chip-blocks.svg"
    alt="Block diagram of the SFM flow meter: its function blocks, how they connect and which package pins
    reach each block.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/sfm_flow_meter_i2c_interface.hpp`](../../inc/sfm_flow_meter_i2c_interface.hpp)

## What the SFM flow meter does

<!-- figure: chip-capabilities -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-capabilities.dark.svg">
  <img src="chip-capabilities.svg"
    alt="Capability map of the SFM flow meter: Flow measurement (supported): Starts continuous measurement
    and reads each CRC-checked sample as a flow in millilitres per minute; Gas and gas mixture
    (supported): Picks the calibrated gas or an oxygen mixture, and changes the mixture without stopping
    the sensor; Temperature (supported): Reads the sensor temperature from the sample where the part
    sends it, or by a pointer sequence; Averaging (supported): Averages until the read, or over a fixed
    number of the 2 kHz measurements; Scale factor and offset (supported): Reads the sensor's scale
    factor, offset and unit and checks them against the datasheet table; Product identification
    (supported): Reads the product identifier or serial number and matches it to one of the 32 variants;
    Sleep and reset (supported): Stops the measurement, sleeps and wakes the sensor, and resets it by
    command, reset pin or supply cycle; Status and fault checks (supported): Checks each sample's status
    word and range, and reports a reset, a stale value or a CRC error; Result-ready line (not covered):
    IRQn goes low when a reading is ready; the driver polls over I2C and leaves the pin open.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/sfm_flow_meter_i2c_interface.hpp`](../../inc/sfm_flow_meter_i2c_interface.hpp)

## SFM flow meter: Address selection

<!-- figure: chip-circuit-address -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-address.dark.svg">
  <img src="chip-circuit-address.svg"
    alt="Schematic of the SFM flow meter address selection: the chip with R3 resistor 2.7 kΩ.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/sfm_flow_meter_i2c_interface.hpp`](../../inc/sfm_flow_meter_i2c_interface.hpp)

## SFM flow meter: Typical application

<!-- figure: chip-circuit-typical -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-typical.dark.svg">
  <img src="chip-circuit-typical.svg"
    alt="Schematic of the SFM flow meter typical application: the chip with U2 MCU, R1 resistor Rp, R2
    resistor Rp; the MCU wires carry the port hooks Write(), Read().">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/sfm_flow_meter_i2c_interface.hpp`](../../inc/sfm_flow_meter_i2c_interface.hpp)

## SFM flow meter

<!-- figure: chip-hero -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-hero.dark.svg">
  <img src="chip-hero.svg"
    alt="Overview of the SFM flow meter, a family of digital gas mass flow sensors on I2C: each part reports
    the flow of its calibrated gases and gas mixtures and its own temperature as 16-bit words, and the
    reference SFM4300 does so for air, oxygen, nitrous oxide and carbon dioxide. A MCU talks to it over
    I2C; it reads back Gas flow, Sensor temperature. Headline figures: 20 or 50 slm SFM4300 flow range;
    3.0 to 5.5 V SFM4300 supply; 1 MHz I2C clock at most; 2 kHz flow update rate, no averaging.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/sfm_flow_meter_i2c_interface.hpp`](../../inc/sfm_flow_meter_i2c_interface.hpp)

## SFM flow meter package, JST NSH 6-pin connector

<!-- figure: chip-outline -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-outline.dark.svg">
  <img src="chip-outline.svg"
    alt="Outline of the SFM flow meter in JST NSH 6-pin connector drawn to scale from above: body 42.5 x 26.0
    mm, pitch 1.0 mm, 6 pins.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/sfm_flow_meter_i2c_interface.hpp`](../../inc/sfm_flow_meter_i2c_interface.hpp)

## SFM flow meter pinout, JST NSH 6-pin connector

<!-- figure: chip-pinout -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-pinout.dark.svg">
  <img src="chip-pinout.svg"
    alt="Pinout of the SFM flow meter in JST NSH 6-pin connector, top view: 6 pins, each with its name, its
    electrical type as a colour and the driver hook that uses it.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/sfm_flow_meter_i2c_interface.hpp`](../../inc/sfm_flow_meter_i2c_interface.hpp)

## Wiring: Typical application

<!-- figure: chip-wiring-typical -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-wiring-typical.dark.svg">
  <img src="chip-wiring-typical.svg"
    alt="Wiring of the SFM flow meter for the Typical application: MCU I2C SDA to SDA (Write(), Read()); MCU
    I2C SCL to SCL (Write(), Read()). Supplies, grounds and the support parts are listed under the
    drawing.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/sfm_flow_meter_i2c_interface.hpp`](../../inc/sfm_flow_meter_i2c_interface.hpp)

## The layers, one of them yours

<!-- figure: driver-layers -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="driver-layers.dark.svg">
  <img src="driver-layers.svg"
    alt="Layer diagram: your application calls the SfmFlowMeter core, which sends register reads and writes
    through the I2C port contract to the port class you write, which drives the chip; the core and the
    contract are the same on every MCU.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/driver-layers.py`](../../docs/diagrams/figures/driver-layers.py)

Drawn from:

- `public calls` in [`inc/sfm_flow_meter.hpp`](../../inc/sfm_flow_meter.hpp)
- `I2cInterface` in [`inc/sfm_flow_meter_i2c_interface.hpp`](../../inc/sfm_flow_meter_i2c_interface.hpp)

## Driver states

<!-- figure: driver-states -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="driver-states.dark.svg">
  <img src="driver-states.svg"
    alt="State machine of the SFM driver with the states Uninitialized, Idle, Measuring, Asleep and
    StateUnknown, and the calls that move it between them.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/driver-states.state.yml`](../model/driver-states.state.yml)

Drawn from:

- [`docs/model/driver-states.state.yml`](../model/driver-states.state.yml)

## Start, wait and read one measurement

<!-- figure: measurement-steps -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="measurement-steps.dark.svg">
  <img src="measurement-steps.svg"
    alt="One measurement with the SFM executor: StartMeasurement reads the scale factors and starts the
    sensor, your code sleeps the returned wait and polls, and ReadSample then reads nine bytes, checks
    each CRC and returns a sample.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/measurement-steps.seq.yml`](../model/measurement-steps.seq.yml)

Drawn from:

- [`docs/model/measurement-steps.seq.yml`](../model/measurement-steps.seq.yml)
