[![led](https://github.com/KopfLab/SDDS_particle_test/actions/workflows/compile-led.yaml/badge.svg?branch=main)](https://github.com/KopfLab/SDDS_particle_test/actions/workflows/compile-led.yaml)

# SDDS particle test

## How to compile on GitHub

This happens automatically for all examples set up in the [GitHub workflows](.github/workflows) for each code push. For the [LED example](/actions/workflows/compile-led.yaml), this is automatically done whenever there are changes to [the example source files](examples/led/src) or changes to the commit checked out for the SDDS submodule, and the compiler runs for 3 different firmware versions + platforms (Photon/Argon/P2). The binaries (if compilation is successful) are stored as artifacts in the workflow runs for each firmware + platform under the *Upload binary* step. The compiler output for each platform is available under the *Compile locally* step.

## How to compile in the cloud

To compile from a local code copy in the cloud:

 - clone this repository and all submodules (`git clone --recurse-submodules https://github.com/KopfLab/SDDS_particle_test`, see [dependencies](#dependencies) for details)
 - install the [Particle Cloud command line interface (CLI)](https://github.com/spark/particle-cli)
 - log into your account with `particle login`
 - from the root of this repo directory, run `particle compile p2 examples/led lib/SDDS/src --target 6.3.2 --saveTo bin/led-p2-6.3.2.bin` (for the photon2 platform and firmware version 6.3.2)
 - alternatively, install ruby gems with `bundle install` and run `rake led` to compile in the cloud (the [Rakefile](Rakefile) provides some other useful shortcats to the particle cli, list them with `rake help`)
 - for development with cloud compile, start the guardfile with `bundle exec guard` and it will recompile the latest binaries (e.g. `led-p2-6.3.2.bin`) whenever there are any code changes

## How co compile locally

To compile locally from a local code copy:

 - install the VS Code extension for the Particle Toolbench following the instructions at https://docs.particle.io/getting-started/developer-tools/workbench/
 - enable pre-release versions of the tool chain as shown here: https://docs.particle.io/getting-started/developer-tools/workbench/#enabling-pre-release-versions
 - use the functionality of the VSCode extension (`Particle: Install Local Compiler Toolchain / Configure Project for device / Compile Appliation (local)`) to select a device OS toolchain (recommended: 6.3.2) and target platform (recommended: P2), and then run the compiler locally
 - note that the Particle compiler looks for sources only in root level `src` and in `lib/**/src` so the examples (of which there are often many) are a pain to compile as each needs to either be copied into the top-level `src` directory or copied somewhere else entirely for compiling (there's probably ways to optimize this with a rake target but I havn't figure out yet how to access the necessary environmental variables from the VS code extension)

## Dependencies

The following third-party software is used in this repository. See the linked GitHub repositories for the respective licensing text and license files.

| **Dependency**    | **Website**                               | **License** |
|-------------------|-------------------------------------------|-------------|
| SDDS              | https://github.com/mLamneck/SDDS          | ?           |




