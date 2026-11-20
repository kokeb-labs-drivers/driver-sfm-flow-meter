# Figures

<!-- Written by kokeb-docs figures from docs/diagrams/figures/*.py. Do not edit by hand. -->

Every figure on this site is drawn by a Python module from facts extracted from the code, so it is rebuilt,
not redrawn, when the code changes. CI runs `kokeb-docs figures --check` and fails when a
committed figure no longer matches the code. Each figure below lists the files it is drawn from.

## Inside the SFM modules

<!-- figure: chip-blocks -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-blocks.dark.svg">
  <img src="chip-blocks.svg"
    alt="Block diagram of the SFM modules: its function blocks, how they connect and which package pins reach
    each block.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/sfm_flow_meter_i2c_interface.hpp`](../../inc/sfm_flow_meter_i2c_interface.hpp)

## SFM modules: Address selection

<!-- figure: chip-circuit-address -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-address.dark.svg">
  <img src="chip-circuit-address.svg"
    alt="Schematic of the SFM modules address selection: the chip with R3 resistor 2.7 kΩ.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/sfm_flow_meter_i2c_interface.hpp`](../../inc/sfm_flow_meter_i2c_interface.hpp)

## SFM modules: Typical application

<!-- figure: chip-circuit-typical -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-typical.dark.svg">
  <img src="chip-circuit-typical.svg"
    alt="Schematic of the SFM modules typical application: the chip with U2 MCU, R1 resistor Rp, R2 resistor
    Rp; the MCU wires carry the port hooks Write(), Read().">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/sfm_flow_meter_i2c_interface.hpp`](../../inc/sfm_flow_meter_i2c_interface.hpp)

## SFM modules package, JST NSH 6-pin 1.0 mm connector

<!-- figure: chip-outline -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-outline.dark.svg">
  <img src="chip-outline.svg"
    alt="Outline of the SFM modules in JST NSH 6-pin 1.0 mm connector drawn to scale from above: body 42.5 x
    26.0 mm, pitch 1.0 mm, 6 pins.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/sfm_flow_meter_i2c_interface.hpp`](../../inc/sfm_flow_meter_i2c_interface.hpp)

## SFM modules pinout, JST NSH 6-pin 1.0 mm connector

<!-- figure: chip-pinout -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-pinout.dark.svg">
  <img src="chip-pinout.svg"
    alt="Pinout of the SFM modules in JST NSH 6-pin 1.0 mm connector, top view: 6 pins, each with its name,
    its electrical type as a colour and the driver hook that uses it.">
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
