# SDDS particleSpike 0.3.0

## Fixed: averaged values with a single data point published without `n`

Variables published as averages (running stats, `AVG_GLOBAL`) that received only one value during an
averaging interval were published like discrete values (`v` only, without `n`). They now include `n: 1`
(still without `s`, since there is no standard deviation for a single point), so they can be told apart
from discrete value changes in the database (`data_n`).

## Offline data: burst splitting and memory limit

- data collected for a long time before the time is valid (or bursts that grow beyond a cloud event)
  are now split by time into bursts that fit into a cloud event (16 kB) instead of being discarded whole
- the burst queue is limited by free RAM: below a reserve (256 kB on P2/M-SoM, 48 kB on Argon/Boron,
  `SDDS_PARTICLE_RESERVE_RAM` to override) the oldest queued bursts are discarded (counted in
  `publishing.bursts.discarded`), then the oldest data of the current burst, instead of running out of memory
- bluetooth clients can download copies of the queued data with `@data [time base]` (only bursts newer than
  the given time base); the queue itself is never changed by this, the data still goes to the cloud
- the open burst is no longer serialized every burst timer cycle while waiting for a valid time
  (only when trace logging is on)

## Bluetooth access to the SDDS tree

On platforms with Bluetooth LE, `TparticleSpike` now also exposes the SDDS tree via bluetooth
(Nordic UART Service, same line-based protocol as the serial spike), see the new *Bluetooth* section
in the README. New in the `SYSTEM` structure (this changes the structure tree, devices need a
structure version bump):

- `bluetooth` submenu (after `internet`) with
  - `state` (`ON`/`OFF`, saveable) to turn bluetooth on/off, permanently if the state is saved
  - `status` (`off`, `discoverable`, `connected`)
  - `connection`: the connected client (announced name and/or address)
- bluetooth clients can set the device time if it is not yet valid (`@time <unix ms>`, sent automatically
  by the SDDS browser on connect), so devices without a Particle cloud connection (local wifi only)
  get the correct time from the first bluetooth connection after power up
- `wifi` submenu (after `internet`, before `bluetooth`, only on platforms with wifi) with
  - `state` (`ON`/`OFF`, saveable) to turn wifi (and the cloud connection) on/off, permanently if the state is saved
  - `status` (`off`, `connecting`, `connected`)
  - `mac`, `network` and `signal_percent` (moved here from `vitals`), now updated whenever wifi is up, also
    without a cloud connection (local wifi only)
  - `setup` submenu with `savedNetworks` (all wifi networks the device has credentials for) and `ssid`,
    `password`, `security` (`detect` scans for the network's security), `hidden` and `action` (`addNetwork`, `replaceNetworks`) plus `result` to define wifi
    networks from the SDDS tree (e.g. via bluetooth); the device reconnects to the new network if needed.
    Passwords are never saved, cleared right after use and masked in the cloud command log.
- `cellular` submenu (only on platforms with cellular) with `state`, `status` and `signal_percent` (moved from `vitals`)
- on devices with wifi and cellular (M-SoM), turning one off moves the cloud connection to the other;
  the cloud is only disconnected when all interfaces are off
- removed the `disconnect` and `reconnect` actions from `SYSTEM.action` (use `wifi.state` instead)
- `vitals.timeSource` (`none`, `cloud`, `bluetooth`, `ntp`): where the system time came from
  (`ntp` is reserved for a local time server, not implemented yet)

## SDDS browser (`assets/sddsBrowser.html`)

New self-contained web page (no dependencies) to browse and control a device's SDDS tree from
the browser, without the cloud:

- connects via **Bluetooth** (Web Bluetooth: Chrome/Edge on desktop and Android, Bluefy on iOS) or
  via **USB serial** (Web Serial: Chrome/Edge on desktop, for devices running the SDDS serial spike)
- shows the tree with live values: expanding a struct subscribes to its values, collapsing it
  unsubscribes (keeps bluetooth traffic low)
- edit values in place; enums (including one-shot actions) are shown as buttons with the current
  value highlighted, read-only values as text
- units in variable names are shown as `[units]` (e.g. `setpoint_rpm` -> `setpoint [rpm]`,
  `flow_mL_min` -> `flow [mL/min]`)
- optional details column (type, saved, read-only) and a collapsible console with raw command input
  (both hidden by default, choice remembered per browser; a red dot flags new errors while the
  console is collapsed)
- `SYSTEM`, `HARDWARE` and the variable publish intervals (`SYSTEM.publishing.varIntervals_ms`) are hidden in the
  tree unless their *system* / *hardware* / *intervals* checkboxes are checked (unchecked by default, remembered
  per browser); the details column is also a checkbox now
- tree search: opens (and subscribes to) all submenus with variables whose name contains the search term,
  closes the others and highlights the matches
- works on phone screens (stacked layout, larger touch targets)
- *Device data* panel (*Fetch device data* button, bluetooth): downloads the device's queued data bursts (only the ones not yet stored
  in the browser), keeps them in the browser and saves them as JSON (same format as the cloud event data)
- on disconnect the tree is grayed out; reconnecting to the same device restores the open submenus
  (also on reload)
- flash application firmware (`.bin`) over USB from the browser (desktop Chrome/Edge): reads the module
  header (platform, address, required Device OS), puts the device into DFU mode and writes the firmware
- launch locally with `rake sddsBrowser` (serves the page on `http://localhost:8000`, opens it in
  Chrome and reloads it automatically whenever the file changes; `PORT=` / `NO_OPEN=1` options)

# SDDS particleSpike 0.2.0

This release is focused entirely on the **time base (`tb`)** that data bursts,
command logs and state snapshots are published with. All of the issues below
could put a wrong absolute timestamp on published data without any error being
raised, so devices that have been running for a long time (or that were just
cold-booted) may have logged data with timestamps that are off.

## Millisecond precision time base

The `tb` of every payload is now published as an ISO8601 timestamp **with
milliseconds** (e.g. `2026-04-10T15:20:32.123Z`) instead of being rounded to
whole seconds. The per data point offsets (`o`) have always been in
milliseconds, so a whole second time base threw every point off by up to half a
second.

## Fixed: 32 bit tick overflows corrupting the time base

`millis()` is a 32 bit millisecond counter and wraps every ~49.7 days. Several
places compared or combined those ticks in ways that broke at or before the wrap.

## Fixed: command log time base going stale by 49.7 days

Command log entries are only evicted when the log buffer overflows, never by age,
so an entry can easily outlive the `millis()` wrap. When it did, the whole log's
time base jumped forward by exactly 49.7 days. The command log now keeps its ticks
in **64 bit `System.millis()` time base**.

## Fixed: state snapshots stamped 2000-01-01 after a cold boot

A state snapshot published on startup could be stamped `2000-01-01T00:00:01`.
The startup snapshot is now **deferred** until the device is genuinely ready to
publish (valid wall clock and known device name), retrying once a second. 
`publishState()` also refuses outright when the clock is not valid, returning 
the new error code `ERR_NOT_READY` (-203), which covers the `sendSddsState` 
function and the `SYSTEM.action` entry point as well. 

Note that the snapshot now reflects the state as of clock sync rather than as of
boot. `loadState()` completes before either point, so this is the same data unless
a value is changed over serial in the intervening seconds.

# SDDS particleSpike 0.1.0

Initial release.
