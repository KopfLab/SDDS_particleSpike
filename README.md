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
- **`type`** — _read-only_ — the device type (structure identifier)
- **`version`** — _read-only_ — the structure version (the [sddsParticle](https://github.com/KopfLab/sddsParticle) GUI uses it to check that a device's values and structure tree are compatible)
- **`id`** — _read-only_ — the Particle unique device ID
- **`name`** — _read-only, saveable_ — the cloud-assigned device name
- **`startup`** — _read-only_ — startup progress (`___` → `complete`).
- **`internet`** — _read-only_ — cloud connection status (`connecting`, `connected`, `disconnected`).
- **`wifi`** — wifi settings and connection info, _only on platforms with wifi_ (e.g. Photon 2, M-SoM). Updated regardless of the cloud connection, i.e. also on a local wifi network without internet.
  - **`state`** — _saveable_ — turn wifi `ON` (default) or `OFF`. `OFF` turns wifi off entirely (after a 1 second delay so the device can still report back); `ON` turns it back on and (re)connects to the cloud. Save the state to keep wifi off across restarts (e.g. for devices only used via bluetooth). If no other network interface is on, turning wifi off also disconnects from the cloud - note that without a cloud connection, you cannot issue commands to the device via the web anymore.
  - **`status`** — _read-only_ — `off`, `connecting` or `connected` (connected to the wifi network; see `internet` for the cloud connection)
  - **`mac`** — _read-only_ — the device's WiFi MAC address (useful for whitelisting on WiFi networks)
  - **`network`** — _read-only_ — the WiFi network (SSID) the device is currently connected to (empty if not connected)
  - **`signal_percent`** — _read-only_ — the WiFi signal strength (`0` if not connected)
  - **`setup`** — saved networks and defining new ones (set `ssid`, `password`, `security` and `hidden`, then trigger the `action`)
    - **`savedNetworks`** — _read-only_ — all WiFi networks (SSIDs) the device has credentials for, comma separated (Device OS stores up to 10)
    - **`ssid`**, **`password`**, **`security`** — define a WiFi network to add: the network name, its password and its security type (`detect` (default), `WPA2`, `WPA3`, `WPA`, `WEP` or `open`). With `detect` the device scans for the network (a few seconds) and uses the security it advertises; this needs wifi to be on and the network in range. `ssid` and `password` are never saved and are cleared as soon as the `action` has run; passwords are masked (`password=***`) in the cloud command log and in the SDDS browser.
    - **`action`** — `addNetwork`: add the defined network to the saved networks (if wifi is on but not connected, the device tries to connect right away), `replaceNetworks`: remove all saved networks and save only the defined one instead (if wifi is on, the device reconnects using the new network, also if it was connected to one of the removed networks). Both validate the input first (e.g. WPA passwords must be 8-63 characters) so a failed `replaceNetworks` never leaves the device without saved networks. Reconnects happen after a 1 second delay so the device can still report back. Note: commands sent via the cloud are split at spaces, so an ssid or password with spaces can only be set via bluetooth or serial.
    - **`hidden`** — `no` (default) or `yes`: hidden networks don't show up in scans, so the device only joins them if they are marked hidden (it then tries them after all visible saved networks). Hidden networks need an explicit `security`. The WiFi cipher is not needed, Device OS determines it automatically.
    - **`result`** — _read-only_ — the result of the last `action`, e.g. `added 'labnet', reconnecting` or `added 'labnet' (WPA2 detected), reconnecting` or `error: network 'labnet' not found by scan, set the security (and hidden=yes if it is hidden)`
- **`cellular`** — cellular settings and connection info, _only on platforms with cellular_ (e.g. Boron, M-SoM)
  - **`state`** — _saveable_ — turn cellular `ON` (default) or `OFF`, works like `wifi.state`. Note: avoid toggling cellular frequently, carriers may block devices that reconnect too aggressively.
  - **`status`** — _read-only_ — `off`, `connecting` or `connected` (connected to the cellular network)
  - **`signal_percent`** — _read-only_ — the cellular signal strength (`0` if not connected)

  On devices with both wifi and cellular (M-SoM), Device OS picks the best available interface for the cloud connection and automatically moves the cloud connection to the other interface if the current one goes down or is turned off here. The cloud is only disconnected when both are off.
- **`bluetooth`** — bluetooth settings and status (see [Bluetooth](#bluetooth))
  - **`state`** — _saveable_ — turn bluetooth `ON` (default) or `OFF`. `OFF` disconnects any client and turns bluetooth off entirely (the device is no longer discoverable); save the state to keep it off across restarts.
  - **`status`** — _read-only_ — `off`, `discoverable` (on, waiting for a connection) or `connected`
  - **`connection`** — _read-only_ — the connected client: the name it announced (e.g. `Chrome on macOS (xx:xx:xx:xx:xx:xx)`) or just its bluetooth address (which phones and computers usually randomize); empty if no client is connected. Note: the signal strength of a bluetooth connection is not available (Device OS does not support it).
- **`state`** — persisted-state (save/load) information
  - **`status`** — _read-only_ — `normal`, `reset`, `failedLoad` or `failedSave`.
  - **`autoSendOnStartup`** — _saveable_ — send all data once on startup if recording is on
  - **`lastSave_dt`** — _read-only, saveable_ — timestamp of the last successful state save
  - **`size_byte`** — _read-only_ — size of the saved state
  - **`error`** — _read-only_ — save/load error code if there was an error
- **`vitals`** — device health & diagnostics
  - **`publishVitals_sec`** — _saveable_ — how often to automatically publish Particle vitals, in seconds; `0` disables it (default: 6 hours).
  - **`time_dt`** — _read-only_ — the device's system time, synched from the particle cloud (kept in UTC)
  - **`timeSource`** — _read-only_ — where the system time came from: `none` (no valid time yet, the default after a power up), `cloud` (Particle cloud time sync), `bluetooth` (set by a bluetooth client via `@time` because the device had no valid time), `ntp` (reserved for a local time server, e.g. on the same host as an MQTT broker)
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

  Data recorded while the device is offline is kept in RAM: until the time is valid (cloud sync or bluetooth `@time`) it collects in the current burst, afterwards finished bursts wait in the send queue until the device is connected. Bursts larger than a cloud event (16 kB) are split by time into several bursts. If free memory drops below a reserve (256 kB on P2/M-SoM, 48 kB on Argon/Boron; override with `#define SDDS_PARTICLE_RESERVE_RAM <bytes>` before including the library) the oldest queued bursts are discarded first (counted in `discarded`), then the oldest data of the current burst.
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

**Pushing data to the cloud on demand** (Particle cloud functions): `sendSdds`, `sendSddsValues`, and `sendSddsState` publish the structure tree, all current values, or just the saveable state, respectively (each returns `-202` if the payload exceeds the 16 kB cloud-event limit, in which case use the `getSdds*` variables instead). The same three actions are also reachable through `SYSTEM.action` (`sendSdds` / `sendSddsValues` / `sendSddsState`). Regular data logging — recorded values, averaged values, and data bursts — is published on the cloud event named by `SYSTEM`→`publishing`→`event` (default **`sddsData`**) and is configured entirely through the `SYSTEM`→`publishing` settings (see [The SYSTEM structure](#the-system-structure) above).

## Bluetooth

On platforms with Bluetooth LE (e.g. Photon 2 / P2, Argon, Boron), the particle spike also gives access to the entire SDDS tree via bluetooth, without any internet connection. The device advertises a [Nordic UART Service](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/libraries/bluetooth/services/nus.html) under its cloud device name (`SYSTEM.name`, or its device type until the name is known) and speaks the same line-based protocol as the [SDDS serial spike](https://github.com/mLamneck/SDDS) (`T` = type tree, `L <port> <path>` / `U <port>` = subscribe/unsubscribe, `path` = read, `path=value` = write; each message is terminated by a newline). Compatible clients include web pages using Web Bluetooth (Chrome/Edge on desktop and Android, the Bluefy browser on iOS) and generic "BLE UART" terminal apps (nRF Connect, Bluefruit Connect, Serial Bluetooth Terminal).

- Responses are split into notifications of up to `ATT MTU - 3` bytes (244 bytes on the P2 if the client negotiates the maximum MTU, 20 bytes otherwise) that the client reassembles up to the newline.
- Clients can identify themselves by sending `@client <description>`, which is shown in `SYSTEM.bluetooth.connection`.
- Clients can provide the current time by sending `@time <unix time in ms>`. The device only uses it if it has no valid time yet (e.g. after a power up with local wifi only and no Particle cloud connection), it never overrides a valid time and a later cloud time sync takes precedence. The SDDS browser sends this automatically when it connects via bluetooth, so connecting once after power up is enough to give a cloud-less device the correct time (data buffered before that still gets correct timestamps).
- Clients can download copies of the queued data (see `publishing` above, e.g. data recorded while the device is offline) by sending `@data` (all) or `@data <time base>` (only bursts with a later time base `tb`, i.e. ones the client doesn't have yet). The device answers with `@data begin <n>`, then each burst's JSON (same as in the cloud event) in parts of up to 1024 characters (`@d+ <part>` for all but the last part, `@d <part>` for the last part), and `@data end <n>`. The queue on the device is **not** changed - the data is still published to the cloud once the device is online. The SDDS browser does this with its *Device data* panel (*Fetch device data* button).
- Only one client can be connected at a time. There is no pairing/authentication: anyone in range can connect and change settings — set `SYSTEM.bluetooth.state=OFF` to turn bluetooth off (and save the state to keep it off after restarts).
- Bluetooth starts once the saved state is loaded at start-up, so a device whose saved `SYSTEM.bluetooth.state` is `OFF` never becomes discoverable.
- Implementation: [`src/uBleSpike.h`](src/uBleSpike.h) (`TbleSpike`), managed automatically by `TparticleSpike`.

## Local SDDS browser

[`assets/sddsBrowser.html`](assets/sddsBrowser.html) is a self-contained web page (no dependencies) that connects to a device either via **Bluetooth** (Web Bluetooth, see above) or via **USB serial** (Web Serial, for devices running the SDDS serial spike `TserialSpike` at 115200 baud, as in the examples), shows its SDDS tree with live values (only expanded structs are subscribed to, to keep bluetooth traffic low), lets you edit values/trigger actions, and has a raw command console. To launch it locally run `rake sddsBrowser`. Stop the server with `Ctrl+C`. Alternatively, any static web server works (e.g. `python3 -m http.server -d assets 8000`, then open `http://localhost:8000/sddsBrowser.html`.

The `SYSTEM` and `HARDWARE` menus and the publish intervals of all variables (`SYSTEM.publishing.varIntervals_ms`) are hidden unless their *system* / *hardware* / *intervals* checkboxes in the tree header are checked (remembered per browser; checking *intervals* also shows `SYSTEM`, hiding `SYSTEM` also hides the intervals). The *details* checkbox shows each variable's type and whether it is saved/read-only. The *search* field in the tree header opens (and subscribes to) all submenus containing variables whose name contains the search term (at least 2 characters), closes all others and highlights the matches; *Escape* clears it.

*Fetch device data* (bluetooth only) opens the *Device data* panel, which downloads copies of the data queued on the device (see `@data` above) and stores them in the browser (IndexedDB, per device name), so *Fetch data* only transfers bursts it doesn't have yet. *Save JSON* saves all stored bursts of the device as a JSON array (the same format as the cloud event data), *Clear cache* deletes them from the browser (not from the device), so the next *Fetch data* downloads everything again.

*Connect Serial* works the same way as *Connect Bluetooth* but needs no bluetooth (and no cloud connection). The serial port can only be open in one place at a time, so stop any serial monitor (`rake monitor`) first. Any other output on the serial port (e.g. from a `SerialLogHandler`) shows up in the page's console; if log messages get printed in the middle of a long response (e.g. the type tree), that response can't be parsed - use *Reload tree*.

Web Bluetooth and Web Serial only work in a [secure context](https://developer.mozilla.org/en-US/docs/Web/Security/Secure_Contexts) (`https://` or `http://localhost`) Web Bluetooth is supported by Chrome/Edge on macOS, Windows, Linux, ChromeOS and Android, but not by Safari/iOS - on iPhones, use the Bluefy browser app (App Store) instead. Web Serial is supported by Chrome/Edge on desktop computers only.

**Flashing firmware**: *Flash firmware* (desktop Chrome/Edge only, needs Web Serial and [WebUSB](https://developer.mozilla.org/en-US/docs/Web/API/WebUSB_API)) flashes an application `.bin` over USB, without the Particle CLI:

1. pick the `.bin` - the page reads its module header and shows the target platform, flash address and the Device OS (system module) version it requires; anything that is not an application firmware (e.g. Device OS system parts or a bootloader) is refused
2. *Flash* puts the device into DFU mode (blinking yellow) by opening its USB serial port at 14400 baud - you select the serial port, unless the page is already connected via serial or the port was selected before
3. *Select device in DFU mode* - the device shows up as a different USB device in DFU mode, so it has to be selected again (only the first time, the browser remembers it afterwards); the page checks that the device's platform matches the firmware, then erases the needed flash sectors, writes the firmware and restarts the device

Close any serial monitor first. If the device runs an older Device OS than the firmware requires, it restarts in safe mode (breathing magenta) until Device OS is updated (e.g. with `particle update`). Remember that `0.x` level firmware changes may change the SDDS structure and require a state reset. On Linux, WebUSB may need Particle's udev rules. The DFU implementation follows the one in Particle's [particle-usb](https://github.com/particle-iot/particle-usb) library.

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
