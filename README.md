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

## Communicating with the device

The entire device — every setting, action, and live reading — is exposed as a single SDDS tree with the `SYSTEM` structure (described above) as its first entry. The primary way to view and control a device is the web-based [sddsParticle](https://github.com/KopfLab/sddsParticle) GUI, which reads a device's structure tree and automatically builds an editor for it. You can also interact with a device directly via the [Particle CLI](https://github.com/spark/particle-cli) (or the equivalent [Particle Cloud API](https://docs.particle.io/reference/api/) requests). All calls require being logged in (`particle login`) and only work for devices registered to your account.

**Reading the tree** (Particle cloud variables):

- `particle get <deviceID> getSddsValues` — the current values of all SDDS variables
- `particle get <deviceID> getSdds` — the full structure tree (types + values)
- `particle get <deviceID> getSddsSystem` — just the `SYSTEM` subtree (small enough to always fit in a single variable)
- `particle get <deviceID> getSddsCommandLog` — the log of recently received commands and their result codes

Because a single Particle variable is size-limited, `getSdds` and `getSddsValues` may return a response that is split across the `getSddsCh0`–`getSddsCh3` helper channels; the first character of each response indicates the channel and the number of transmissions still remaining. In practice, prefer capturing the published cloud events (see below) or the `sddsParticle` GUI, which reassemble these automatically.

**Setting variables / issuing commands** (Particle cloud function `sdds`): assign values with a `path=value` syntax where the path uses `.` as the separator. Issue several assignments at once by separating them with spaces:

```sh
particle call <deviceID> sdds "led.ledSwitch=ON"
particle call <deviceID> sdds "led.blinkSwitch=ON led.onTime_ms=200 led.offTime_ms=200"
particle call <deviceID> sdds "SYSTEM.publishing.record=ON"
particle call <deviceID> sdds "SYSTEM.action=saveState"
```

The return value is `0` when all assignments succeed. For a single failed assignment it is the negated error code (e.g. `-2` when the path could not be resolved — see the [error codes](lib/SDDS/src/uPlainCommErrors.h)); for a batch of assignments it is a bitmask flagging which of them failed (bit `i` set = assignment `i` failed). Special codes: `-200` = empty command, `-201` = more than 31 assignments in one call.

**Pushing data to the cloud on demand** (Particle cloud functions): `sendSdds`, `sendSddsValues`, and `sendSddsState` publish the structure tree, all current values, or just the saveable state, respectively (each returns `-202` if the payload exceeds the 16 kB cloud-event limit, in which case use the `getSdds*` variables instead). The same three actions are also reachable through `SYSTEM.action` (`sendSdds` / `sendSddsValues` / `sendSddsState`). Regular data logging — recorded values, averaged values, and data bursts — is published on the cloud event named by `SYSTEM`→`publishing`→`event` (default **`sddsData`**) and is configured entirely through the `SYSTEM`→`publishing` settings (see [The SYSTEM structure](#the-system-structure) above). Nothing is published until `SYSTEM`→`publishing`→`record` is turned `ON`.

## How to compile on GitHub

This happens automatically for all examples set up in the [GitHub workflows](.github/workflows) for each code push. For the [LED example](../../actions/workflows/compile-led.yaml), this is automatically done whenever there are changes to [the example source files](examples/led/src) or to this library's own [source files](src), and the compiler runs for 3 different platforms (P2/Boron/M-SoM, all on device-OS 6.3.4). The set of platforms is defined by the `matrix.platform` list in each program's `compile-<program>.yaml` (some examples build for P2 only). The binaries (if compilation is successful) are stored as artifacts in the workflow runs for each platform under the *Upload binary* step. The compiler output for each platform is available under the *Compile locally* step.

## How to compile in the cloud

To compile from a local code copy in the cloud:

 - clone this repository and all submodules (`git clone --recurse-submodules https://github.com/KopfLab/SDDS_particleSpike`, see [dependencies](#dependencies) for details)
 - install the [Particle Cloud command line interface (CLI)](https://github.com/spark/particle-cli)
 - log into your account with `particle login`
 - from the root of this repo directory, run `particle compile p2 examples/led lib/SDDS/src --target 6.3.4 --saveTo bin/led-p2-6.3.4.bin` (for the photon2 platform and firmware version 6.3.4)
 - alternatively, install ruby gems with `bundle install` and run `rake led` to compile in the cloud (the [Rakefile](Rakefile) provides some other useful shortcuts to the particle cli, list them with `rake help`, and the compilable programs with `rake programs`)
 - for development with cloud compile, prime the target once with `rake <program>` and then start auto-recompiling with `rake autoCompile` (or do both in one step with `rake dev <program>`, which also flashes on each change); this watches the source globs defined in the program's `compile-<program>.yaml` and recompiles the latest binary (e.g. `led-p2-6.3.4.bin`) whenever there are code changes

## How to compile locally

To compile locally from a local code copy:

 - install the VS Code extension for the Particle Toolbench following the instructions at https://docs.particle.io/getting-started/developer-tools/workbench/
 - enable pre-release versions of the tool chain as shown here: https://docs.particle.io/getting-started/developer-tools/workbench/#enabling-pre-release-versions
 - use the functionality of the VSCode extension (`Particle: Install Local Compiler Toolchain / Configure Project for device / Compile Appliation (local)`) to select a device OS toolchain (recommended: 6.3.4) and target platform (recommended: P2)
 - run `rake local <program>` (e.g. `rake local led`) to compile a program with the local toolchain — this mirrors the program's sources into `local/` and compiles from there, saving the result as e.g. `bin/led-p2-6.3.4-local.bin`
 - for local development, use `rake dev <program>` which compiles locally, flashes the connected device via USB, and then watches the program's sources to auto-recompile and re-flash on every change; if you're switching to a different example, platform, or version, run `rake cleanLocal` first to clear stale files out of `local/`

## Dependencies

The following third-party software is used in this repository. See the linked GitHub repositories for the respective licensing text and license files.

| **Dependency**    | **Website**                               | **License** |
|-------------------|-------------------------------------------|-------------|
| SDDS v0.1.0       | https://github.com/mLamneck/SDDS          | MIT         |
