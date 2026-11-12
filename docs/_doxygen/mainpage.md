# SFM Flow Meter Driver API reference {#mainpage}

The classes, types and part table of the SFM flow meter driver, generated from the headers in `inc/`.

Start with sfm::SfmFlowMeter, the driver, and sfm::I2cInterface, the base of the port you write. Errors are
sfm::Error inside sfm::Result; the configuration is sfm::Config, a measurement is sfm::Sample, and the parts are the
rows of sfm::PartInfo in sfm::parts::kTable.

The guides, with examples that compile, are on the documentation site: <a href="../">SFM flow meter driver
documentation</a>.
