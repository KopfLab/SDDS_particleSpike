# SDDS particleSpike 0.3.0

## Bluetooth access to the SDDS tree

On platforms with Bluetooth LE, `TparticleSpike` now also exposes the SDDS tree via bluetooth
(Nordic UART Service, same line-based protocol as the serial spike), see the new *Bluetooth* section
in the README. New in the `SYSTEM` structure (this changes the structure tree, devices need a
structure version bump):

- `bluetooth` submenu (after `internet`) with
  - `state` (`ON`/`OFF`, saveable) to turn bluetooth on/off, permanently if the state is saved
  - `status` (`off`, `discoverable`, `connected`)
  - `connection`: the connected client (announced name and/or address)

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
- optional info column (type, saved, read-only) and a collapsible console with raw command input
  (both hidden by default, choice remembered per browser; a red dot flags new errors while the
  console is collapsed)
- works on phone screens (stacked layout, larger touch targets)
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
