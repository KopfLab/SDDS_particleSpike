[![ssds](https://github.com/KopfLab/SDDS_particleSpike/actions/workflows/compile-sdds.yaml/badge.svg?branch=main)](https://github.com/KopfLab/SDDS_particleSpike/actions/workflows/compile-sdds.yaml)
[![led](https://github.com/KopfLab/SDDS_particleSpike/actions/workflows/compile-led.yaml/badge.svg?branch=main)](https://github.com/KopfLab/SDDS_particleSpike/actions/workflows/compile-led.yaml)
[![adc](https://github.com/KopfLab/SDDS_particleSpike/actions/workflows/compile-adc.yaml/badge.svg?branch=main)](https://github.com/KopfLab/SDDS_particleSpike/actions/workflows/compile-adc.yaml)
[![switch](https://github.com/KopfLab/SDDS_particleSpike/actions/workflows/compile-timed_switch.yaml/badge.svg?branch=main)](https://github.com/KopfLab/SDDS_particleSpike/actions/workflows/compile-timed_switch.yaml)

# SDDS particleSpike

This library is an extension of the [SDDS library](https://github.com/mLamneck/SDDS) for self-describing data structures. It implements SDDS functionality for managing and communicating with [Particle microcontrollers](https://store.particle.io) via the cloud. The primary supported devices are the [Photon 2](https://store.particle.io/collections/wifi/products/photon-2) for WiFi connectivity, and the [Boron](https://store.particle.io/collections/cellular/products/boron-lte-cat-m1-noram-with-ethersim-4th-gen) for Cellular connectivity, but other particle devices that can run [firmware](https://docs.particle.io/reference/device-os/versions) version 6.3.0 and newer are likely supported out of the box as well (e.g. the [Muon multi-radio devices](https://store.particle.io/collections/multiradio-satellite-lorawan)).

## What can it do?

This library gives any supported Particle device a **self-describing data
structure (SDDS)** interface to the Particle Cloud. On top of your device's own
SDDS variables it automatically adds a common **`SYSTEM`** structure — device identity,
connection status, health/vitals, persisted state, and data recording/publishing
settings — and provides the plumbing to send the structure tree, its values and
live data events to the cloud and to receive commands back. It is the firmware
counterpart to the [sddsParticle](https://github.com/KopfLab/sddsParticle) R
package, whose GUI reads this structure and builds a web-based editor/microcontroller
for it automatically.

## The SYSTEM structure

Every device using the SDDS particleSpike automatically exposes a `SYSTEM`
structure as the first entry of its SDDS tree. It carries the device's identity,
its connection and health information, its persisted state, and everything that
controls how (and how often) the device records and publishes data to the cloud.
The [sddsParticle](https://github.com/KopfLab/sddsParticle) GUI shows the `SYSTEM`
when you enable the **"Show SYSTEM"** toggle in its *Controls* menu.

Fields can be flagged as _read-only_ (reported by the device but intended to be not
editable by the user) or _saveable_ (persisted across restarts, i.e. restored
on boot and re-saved with the `saveState` action). Numeric fields can carry units
as a name suffix (e.g. `_ms`, `_sec`, `_byte`, `_percent`, `_dt` for a date/time, etc.).

The `SYSTEM` structure provides access to the following SDDS variables:

- **`action`** — trigger a one-off system action:
  - `restart`: power cycle the device
  - `reset`: power cycle the device and reset all _saveable_ variables to their default state
  - `saveState`: store the current values for all _saveable_ variables in the device's persistent memory (i.e. preserve the current settings across power outtages)
  - `syncTime`: re-sync the clock with the cloud (this happens automatically at each start-up and is rarely necessary to do manually)
  - `sendVitals`: send the device's current system vitals to the cloud (`vitals`->`publishVitals_sec` determines how often this happens automatically)
  - `sendSdds`: send the device's complete structure tree to the cloud
  - `sendSddsValues`: send all of the device's SDDS variables current values to the cloud
  - `sendSddsState`: send all of the device's _saveable_ SDDS variables and their values to the cloud
  - `disconnect`: disconnect from the cloud (note that without cloud connection, you cannot issue commands to this device via the web anymore)
  - `reconnect`: reconnect to the cloud
- **`type`** — _read-only_ — the device type (structure identifier)
- **`version`** — _read-only_ — the structure version (the [sddsParticle](https://github.com/KopfLab/sddsParticle) GUI uses it to check that a device's values and structure tree are compatible)
- **`id`** — _read-only_ — the Particle unique device ID
- **`name`** — _read-only, saveable_ — the cloud-assigned device name
- **`startup`** — _read-only_ — startup progress (`___` → `complete`).
- **`internet`** — _read-only_ — cloud connection status (`connecting`, `connected`, `disconnected`).
- **`state`** — persisted-state (save/load) information
  - **`status`** — _read-only_ — `normal`, `reset`, `failedLoad` or `failedSave`.
  - **`autoSendOnStartup`** — _saveable_ — send all data once on startup if recording is on
  - **`lastSave_dt`** — _read-only, saveable_ — timestamp of the last successful state save
  - **`size_byte`** — _read-only_ — size of the saved state
  - **`error`** — _read-only_ — save/load error code if there was an error
- **`vitals`** — device health & diagnostics
  - **`publishVitals_sec`** — _saveable_ — how often to automatically publish Particle vitals, in seconds; `0` disables it (default: 6 hours).
  - **`time_dt`** — _read-only_ — the device's system time, synched from the particle cloud (kept in UTC)
  - **`mac`** — _read-only_ — the device's WiFi MAC address (useful for whitelisting on WiFi networks)
  - **`network`** — _read-only_ — the WiFi network (SSID) the device is currently connected to
  - **`signal_percent`** — _read-only_ — the WiFi/cellular signal strength
  - **`lastRestart`** — _read-only_ — the cause of the last device restart (`powerUp`, `userRestart`, `userReset`, `watchdogTimeout`, `outOfMemory`, `PANIC`).
  - **`totalRAM_byte`** / **`freeRAM_byte`** — _read-only_ — total/current RAM usage
  - **`totalFlash_byte`** / **`freeFlash_byte`** — _read-only_ — total/current flash-storage usage
  - **`totalSectors`** / **`freeSectors`** — _read-only_ — total/current flash sector usage
- **`publishing`** — data recording & publishing to the cloud
  - **`record`** — _saveable_ — the global on/off switch for recording/publishing data to the cloud (default: off).
  - **`event`** — _saveable_ — the cloud event name used for all published data/structure tress/values/etc. (default: `sddsData`).
  - **`bursts`** — outgoing data-burst diagnostics
    - **`timer_ms`** — _saveable_ — the minimum delay between data bursts (default: 3000ms  = 3s).
    - **`queued`** / **`sending`** / **`sent`** / **`failed`** / **`invalid`** / **`discarded`** — _read-only_ — counters for the burst send queue
  - **`globalInterval_ms`** — _saveable_ — the global publish interval used by variables set to "average over the global interval" (default: 20 minutes).
  - **`nextGlobalPublish`** — _read-only_ — the time of the next global publish if `record` is on
  - **`varIntervals_ms`** — a mirror of the device's variable tree in which each entry sets how often that individual variable is published: `-1` = average over the global interval (while recording), `0` = never, `1` = on every change (while recording), `2` = on every change (always), or a positive number = a fixed interval in milliseconds. The [sddsParticle](https://github.com/KopfLab/sddsParticle) GUI makes this setting accessible more intuitively with dropdown option for each variable in the structure tree.

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
| SDDS v0.1.0       | https://github.com/mLamneck/SDDS          | MIT         |
