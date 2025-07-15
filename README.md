[![ssds](https://github.com/KopfLab/SDDS_particleSpike/actions/workflows/compile-sdds.yaml/badge.svg?branch=main)](https://github.com/KopfLab/SDDS_particleSpike/actions/workflows/compile-sdds.yaml)
[![led](https://github.com/KopfLab/SDDS_particleSpike/actions/workflows/compile-led.yaml/badge.svg?branch=main)](https://github.com/KopfLab/SDDS_particleSpike/actions/workflows/compile-led.yaml)
[![adc](https://github.com/KopfLab/SDDS_particleSpike/actions/workflows/compile-adc.yaml/badge.svg?branch=main)](https://github.com/KopfLab/SDDS_particleSpike/actions/workflows/compile-adc.yaml)

# SDDS particleSpike

This library is an extension of the [SDDS library](https://github.com/mLamneck/SDDS) for self-describing data structures. It implements SDDS functionality for managing and communicating with [Particle microcontrollers](https://store.particle.io) via the cloud. The primary supported devices are the [Photon 2](https://store.particle.io/collections/wifi/products/photon-2) for WiFi connectivity, and the [Boron](https://store.particle.io/collections/cellular/products/boron-lte-cat-m1-noram-with-ethersim-4th-gen) for Cellular connectivity, but other particle devices that can run [firmware](https://docs.particle.io/reference/device-os/versions) version 6.3.0 and newer are likely supported out of the box as well (e.g. the [Muon multi-radio devices](https://store.particle.io/collections/multiradio-satellite-lorawan)).

## What can it do?



## How to compile on GitHub

This happens automatically for all examples set up in the [GitHub workflows](.github/workflows) for each code push. For the [LED example](../../actions/workflows/compile-led.yaml), this is automatically done whenever there are changes to [the example source files](examples/led/src) or changes to the commit checked out for the SDDS submodule, and the compiler runs for 3 different firmware versions + platforms (Photon/Argon/P2). The binaries (if compilation is successful) are stored as artifacts in the workflow runs for each firmware + platform under the *Upload binary* step. The compiler output for each platform is available under the *Compile locally* step.

## How to compile in the cloud

To compile from a local code copy in the cloud:

 - clone this repository and all submodules (`git clone --recurse-submodules https://github.com/KopfLab/SDDS_particleSpike`, see [dependencies](#dependencies) for details)
 - install the [Particle Cloud command line interface (CLI)](https://github.com/spark/particle-cli)
 - log into your account with `particle login`
 - from the root of this repo directory, run `particle compile p2 examples/led lib/SDDS/src --target 6.3.2 --saveTo bin/led-p2-6.3.2.bin` (for the photon2 platform and firmware version 6.3.2)
 - alternatively, install ruby gems with `bundle install` and run `rake led` to compile in the cloud (the [Rakefile](Rakefile) provides some other useful shortcats to the particle cli, list them with `rake help`)
 - for development with cloud compile, start the guardfile with `bundle exec guard` and it will recompile the latest binaries (e.g. `led-p2-6.3.2.bin`) whenever there are any code changes

## How co compile locally

To compile locally from a local code copy:

 - install the VS Code extension for the Particle Toolbench following the instructions at https://docs.particle.io/getting-started/developer-tools/workbench/
 - enable pre-release versions of the tool chain as shown here: https://docs.particle.io/getting-started/developer-tools/workbench/#enabling-pre-release-versions
 - use the functionality of the VSCode extension (`Particle: Install Local Compiler Toolchain / Configure Project for device / Compile Appliation (local)`) to select a device OS toolchain (recommended: 6.3.2) and target platform (recommended: P2), and then run the compiler locally with `rake guardLocal` which automatically copies sources of the last cloud-compiled firmware into `local/` and compiles from there whenever something changes (and flashes the new code to a connected device via USB); if you're switching to a different example or have changes in a library, it is recommended to run `rake cleanLocal` before resuming with `rake guardLocal`
 
## Dependencies

The following third-party software is used in this repository. See the linked GitHub repositories for the respective licensing text and license files.

| **Dependency**    | **Website**                               | **License** |
|-------------------|-------------------------------------------|-------------|
| SDDS              | https://github.com/mLamneck/SDDS          | MIT         |




