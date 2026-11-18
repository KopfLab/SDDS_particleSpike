#pragma once

#include "Particle.h"
#include "uTypedef.h"
#include "uCoreEnums.h"
#include "uParamSave.h"
#include <dirent.h>

// always use system threading to avoid particle cloud synchronization to block the app
#ifndef SYSTEM_VERSION_v620
SYSTEM_THREAD(ENABLED);
#endif

// use th eeprom file size as the max parameter save
// (although this actually stored on the file system
// and could be made larger if needed without issue)
#define SDDS_PS_MAX_SIZE EEPROM_FILE_SIZE

// use semiautomatic mode so that particle doesn't try to connect until setup
// and the system thread isn't tied up during Particle.subscribe/function/variable
SYSTEM_MODE(SEMI_AUTOMATIC);

/**
 * @brief automatically added menu item that holds system variables
 * this is added as the first menu entry with structure type and version
 * as the very first 2 variables which are used on server side to confirm
 * if values (V) and structure tree (T) are compatible
 */
class TparticleSystem : public TmenuHandle
{

private:
    using TonOff = sdds::enums::OnOff;

public:
    // action comes first
    // not implementing size based bursts for now (action sendBurstData), see https://github.com/KopfLab/SDDS_particleSpike/issues/4
    sdds_enum(___, restart, reset, saveState, syncTime, sendVitals, sendSdds, sendSddsValues, sendSddsState) Taction;
    sdds_var(Taction, action); // take a system action

    // structure type & version definitions
    sdds_var(Tstring, type, sdds::opt::readonly);    // device type
    sdds_var(Tuint16, version, sdds::opt::readonly); // device version

    // system variables
    sdds_var(Tstring, id, sdds::opt::readonly);                                     // device ID
    sdds_var(Tstring, name, sdds_joinOpt(sdds::opt::saveval, sdds::opt::readonly)); // cloud derived device name

    sdds_enum(___, complete) TstartupStatus;
    sdds_var(TstartupStatus, startup, sdds::opt::readonly); // keeps track of startup processes

    sdds_enum(connecting, connected, disconnected) TinternetStatus;
    sdds_var(TinternetStatus, internet, sdds::opt::readonly, TinternetStatus::connecting); // internet status

#if Wiring_WiFi && Wiring_WiFi == 1
    // wifi (only on platforms with wifi)
    class Twifi : public TmenuHandle
    {
    public:
        sdds_var(TonOff, state, sdds::opt::saveval, TonOff::ON); // wifi on/off (saveable, i.e. wifi can be turned off permanently)
        sdds_enum(off, connecting, connected) Tstatus;
        sdds_var(Tstatus, status, sdds::opt::readonly, Tstatus::off); // wifi status (connected = on the local network, see internet for the cloud)
        sdds_var(Tstring, mac, sdds::opt::readonly);              // wifi MAC address (useful for whitelisting on wifi networks)
        sdds_var(Tstring, network, sdds::opt::readonly);          // wifi network (SSID) currently connected to
        sdds_var(Tuint8, signal_percent, sdds::opt::readonly, 0); // wifi signal strength

        // saved networks and defining new ones: set ssid, password, security, hidden, then trigger the action
        // (ssid and password are never saved and are cleared as soon as the action has run)
        class Tsetup : public TmenuHandle
        {
        public:
            sdds_var(Tstring, savedNetworks, sdds::opt::readonly); // all wifi networks (SSIDs) the device has credentials for, comma separated
            sdds_var(Tstring, ssid);
            sdds_var(Tstring, password);
            sdds_enum(detect, WPA2, WPA3, WPA, WEP, open) Tsecurity;
            sdds_var(Tsecurity, security, sdds::opt::nothing, Tsecurity::detect); // detect: scan and use the security the network advertises
            sdds_enum(no, yes) Thidden;
            sdds_var(Thidden, hidden, sdds::opt::nothing, Thidden::no); // hidden networks don't show up in scans, the device tries them blindly
            sdds_enum(___, addNetwork, replaceNetworks) Taction;
            sdds_var(Taction, action);                      // addNetwork: add to the saved networks, replaceNetworks: remove all saved networks and add this one instead
            sdds_var(Tstring, result, sdds::opt::readonly); // result of the last action
        };
        sdds_var(Tsetup, setup);
    };
    sdds_var(Twifi, wifi);
#endif

#if Wiring_Cellular && Wiring_Cellular == 1
    // cellular (only on platforms with cellular)
    class Tcellular : public TmenuHandle
    {
    public:
        sdds_var(TonOff, state, sdds::opt::saveval, TonOff::ON); // cellular on/off (saveable, i.e. cellular can be turned off permanently)
        sdds_enum(off, connecting, connected) Tstatus;
        sdds_var(Tstatus, status, sdds::opt::readonly, Tstatus::off); // cellular status (connected = on the cellular network, see internet for the cloud)
        sdds_var(Tuint8, signal_percent, sdds::opt::readonly, 0);     // cellular signal strength
    };
    sdds_var(Tcellular, cellular);
#endif

    // bluetooth (managed by the particle spike's TbleSpike, see uBleSpike.h)
    class Tbluetooth : public TmenuHandle
    {
    public:
        sdds_var(TonOff, state, sdds::opt::saveval, TonOff::ON); // bluetooth on/off (saveable, i.e. bluetooth can be turned off permanently)
        sdds_enum(off, discoverable, connected) Tstatus;
        sdds_var(Tstatus, status, sdds::opt::readonly, Tstatus::off); // current bluetooth status (always off on platforms without bluetooth)
        sdds_var(Tstring, connection, sdds::opt::readonly);            // connected client: announced name ("@client ...") + address, or just its (usually randomized) address
    };
    sdds_var(Tbluetooth, bluetooth);

    // state
    class Tstate : public TmenuHandle
    {
    public:
        sdds_enum(normal, reset, failedLoad, failedSave) Tstatus;
        sdds_var(Tstatus, status, sdds::opt::readonly);
        sdds_var(TonOff, autoSendOnStartup, sdds::opt::saveval, TonOff::ON); // auto-send on startup if publishing is on
        sdds_var(Tstring, lastSave_dt, sdds_joinOpt(sdds::opt::saveval, sdds::opt::readonly), "never");
        sdds_var(Tuint16, size_byte, sdds::opt::readonly);
        sdds_var(TparamError, error, sdds::opt::readonly);
    };
    sdds_var(Tstate, state); // load/save state

    // vitals variables
    sdds_enum(powerUp, userRestart, userReset, watchdogTimeout, outOfMemory, PANIC) TrestartStatus;
    // where the current system time came from (ntp = local time server, e.g. on the mqtt broker host)
    sdds_enum(none, cloud, bluetooth, ntp) TtimeSource;
    class Tvitals : public TmenuHandle
    {
    public:
        sdds_var(Tuint32, publishVitals_sec, sdds::opt::saveval, 60 * 60 * 6);               // how often to publish device vitals in seconds (takes 150 bytes per transmission!), 0 = no regular publishing
        sdds_var(Tstring, time_dt, sdds::opt::readonly);                                     // system time
        sdds_var(TtimeSource, timeSource, sdds::opt::readonly, TtimeSource::none);          // where the system time came from (last sync)
        sdds_var(TrestartStatus, lastRestart, sdds::opt::readonly, TrestartStatus::powerUp); // restart information

// random access memory (RAM, in bytes)
#if (PLATFORM_ID == PLATFORM_ARGON || PLATFORM_ID == PLATFORM_BORON)
        sdds_var(Tuint32, totalRAM_byte, sdds::opt::readonly, 80 * 1024); // approximately 80 KB
#elif (PLATFORM_ID == PLATFORM_P2 || PLATFORM_ID == PLATFORM_MSOM)
        sdds_var(Tuint32, totalRAM_byte, sdds::opt::readonly, 3 * 1024 * 1024); // approximately 3 MB
#else
        sdds_var(Tuint32, totalRAM_byte, sdds::opt::readonly, 0);
#endif
        sdds_var(Tuint32, freeRAM_byte, sdds::opt::readonly); // free memory information

        // flash memory (in bytes) / number of sectors
        inline static const size_t FflashSectorSize_byte = 4 * 1024; // 4 KB
#if (PLATFORM_ID == PLATFORM_MSOM)
        sdds_var(Tuint32, totalFlash_byte, sdds::opt::readonly, 8 * 1024 * 1024); // 8 MB
#elif (PLATFORM_ID == PLATFORM_ARGON || PLATFORM_ID == PLATFORM_P2 || PLATFORM_ID == PLATFORM_BORON)
        sdds_var(Tuint32, totalFlash_byte, sdds::opt::readonly, 2 * 1024 * 1024); // 2 MB
#else
        sdds_var(Tuint32, totalFlash_byte, sdds::opt::readonly, 0);
#endif
        sdds_var(Tuint32, freeFlash_byte, sdds::opt::readonly);

#if (PLATFORM_ID == PLATFORM_MSOM)
        sdds_var(Tuint32, totalSectors, sdds::opt::readonly, 8 * 1024 * 1024 / FflashSectorSize_byte);
#elif (PLATFORM_ID == PLATFORM_ARGON || PLATFORM_ID == PLATFORM_P2 || PLATFORM_ID == PLATFORM_BORON)
        sdds_var(Tuint32, totalSectors, sdds::opt::readonly, 2 * 1024 * 1024 / FflashSectorSize_byte);
#else
        sdds_var(Tuint32, totalSectors, sdds::opt::readonly, 0);
#endif
        sdds_var(Tuint32, freeSectors, sdds::opt::readonly);
    };
    sdds_var(Tvitals, vitals);

    // publishing variables
    class Tpublishing : public TmenuHandle
    {

        class Tbursts : public TmenuHandle
        {
        public:
            // not implementing size based bursts for now, see https://github.com/KopfLab/SDDS_particleSpike/issues/4
            // sdds_var(Tuint32, size_byte, sdds::opt::saveval, 1024); // how many bytes to collect before a data burst is sent?
            sdds_var(Tuint32, timer_ms, sdds::opt::saveval, 3000); // how many milliseconds to wait at minimum before a data burst is sent?
            sdds_var(Tuint32, queued, sdds::opt::readonly, 0);     // number of currently queued bursts
            sdds_var(Tuint32, sending, sdds::opt::readonly, 0);    // number of currently sending bursts
            sdds_var(Tuint32, sent, sdds::opt::readonly, 0);       // number of successfully sent bursts
            sdds_var(Tuint32, failed, sdds::opt::readonly, 0);     // number of failed/requeued bursts
            sdds_var(Tuint32, invalid, sdds::opt::readonly, 0);    // number of invalid/discarded bursts
            sdds_var(Tuint32, discarded, sdds::opt::readonly, 0);  // number of invalid/discarded bursts
        };

    public:
        sdds_var(TonOff, record, sdds::opt::saveval, TonOff::OFF);                // global on/off for publishing/recording to the cloud
        sdds_var(Tstring, event, sdds::opt::saveval, "sddsData");                 // publish event name, default can be overwritten by spike constructor
        sdds_var(Tbursts, bursts);                                                // burst information
        sdds_var(Tuint32, globalInterval_ms, sdds::opt::saveval, 1000 * 60 * 20); // global publish interval (in milliseconds)
        sdds_var(Tstring, nextGlobalPublish, sdds::opt::readonly, "off");
    };
    sdds_var(Tpublishing, publishing);

// debug tools
#ifdef SDDS_PARTICLE_DEBUG
    sdds_enum(___, getValues, getTree, getCommandLog, setVars, setDefaults) TdebugAction;
sdds_var(TdebugAction, debug)  // debug actions
    sdds_var(Tstring, command) // debug actions
#endif

    private :

    // what is this menu item called?
    Tmeta meta() override
    {
        return Tmeta{TYPE_ID, 0, "SYSTEM"};
    }

    // how much free RAM (in bytes) required before forced restart?
    const uint32_t memoryRestartLimit = 5 * 1024; // limit to 5 KB

    // platform check-in timer
    const system_tick_t FcheckInterval = 100; // ms
    Ttimer FsystemCheckTimer;

    // delayed action timers
    Ttimer FdelayedActionTimer;
    Taction::e FdelayedAction = Taction::___;
    bool FconnectionStarted = false; // has startConnection() been called (i.e. the saved state is loaded)?
#if Wiring_WiFi && Wiring_WiFi == 1
    Ttimer FwifiOffTimer;       // delays turning wifi off so the device can still report back
    Ttimer FwifiReconnectTimer; // delays reconnecting to new wifi credentials so the device can still report back
    Ttimer FwifiSetupTimer; // runs the wifi.setup.action after its value went out
    bool FwifiReconnectRestart = false; // wifi was turned off for a replace, turn it back on
    bool FwifiReconnectDisconnect = false; // drop the current wifi connection before reconnecting?
#endif
#if Wiring_Cellular && Wiring_Cellular == 1
    Ttimer FcellularOffTimer; // delays turning cellular off so the device can still report back
#endif

    // time sync timer
    const system_tick_t FtimeSyncInterval = 1000 * 60 * 60 * 6; // every 6 hours should suffice
    Ttimer FsyncTimer;
    bool FresyncSysTime = false;

    // request device name? could reset to true at later point to request (by action or timer)
    bool FrequestName = true;

    // time stamp for the clock update
    time32_t FlastNow = 0;
    system_tick_t FlastCloudTimeSync = 0; // Particle.timeSyncedLast() at the last check (0 = never synced)

    // millisecond epoch anchor
    // Time.now() only resolves whole seconds and on some platforms the RTC has no
    // sub second component at all (the P2's hal_rtc_get_time() always returns
    // tv_usec = 0, the Boron's does carry microseconds), so a millisecond time base
    // cannot be read out directly. Instead the tick counter is anchored to the wall
    // clock every time a second rollover is observed on the system check timer: the
    // rollover happened somewhere between the previous sample and this one, so the
    // anchor is placed at the midpoint of that window. This keeps the anchor error
    // bounded by roughly half the check interval and, because it is re-established
    // on every rollover, free of drift.
    bool FhasNowSample = false;         // is there a previous Time.now() sample to bracket against?
    bool FepochAnchored = false;        // has a second rollover been observed?
    dtypes::uint64 FlastNowTick_ms = 0; // tick of the previous Time.now() sample
    dtypes::uint64 FepochUnix_ms = 0;   // unix time at the anchor [ms]
    dtypes::uint64 FepochTick_ms = 0;   // System.millis() tick at the anchor

public:
    /**
     * @brief has a second rollover been observed, i.e. is unixTime_ms() millisecond precise?
     */
    bool epochAnchored() { return FepochAnchored; }

    /**
     * @brief unix time in milliseconds for an event that happened _elapsed_ms ago
     * @param _elapsed_ms how long ago the event happened [ms], 0 for right now
     * note: pass the elapsed time rather than the tick itself so that callers can use
     * either the 32bit millis() or the 64bit System.millis() time base
     * note: until the epoch is anchored this falls back to the whole second resolution
     * of Time.now(), which is still exact in the elapsed time, just not in the anchor
     */
    dtypes::int64 unixTime_ms(dtypes::uint64 _elapsed_ms = 0)
    {
        dtypes::int64 now_ms = (FepochAnchored)
                                   ? static_cast<dtypes::int64>(FepochUnix_ms) + static_cast<dtypes::int64>(System.millis() - FepochTick_ms)
                                   : static_cast<dtypes::int64>(Time.now()) * 1000;
        return (now_ms - static_cast<dtypes::int64>(_elapsed_ms));
    }

    /**
     * @brief start the network/cloud connection - called by the particle spike once the saved
     * state is loaded (so permanently turned off network interfaces stay off from the start)
     * note: with several interfaces (e.g. M-SoM: wifi + cellular), Device OS' connection manager
     * picks the best available interface for the cloud and moves the cloud session to another
     * interface if the current one goes down (incl. when it is turned off here). An interface
     * turned off with WiFi.off()/Cellular.off() is blocked, i.e. the cloud connection does not
     * turn it back on.
     */
    void startConnection()
    {
        FconnectionStarted = true;
#if Wiring_WiFi && Wiring_WiFi == 1
        if (wifi.state == TonOff::OFF)
            WiFi.off();
#endif
#if Wiring_Cellular && Wiring_Cellular == 1
        if (cellular.state == TonOff::OFF)
            Cellular.off();
#endif
        if (anyNetworkOn())
            connectCloud();
        else
        {
            Log.info("all network interfaces are off, not connecting to the cloud");
            internet = TinternetStatus::disconnected;
        }
    }

    /**
     * @brief is any network interface (that can reach the cloud) turned on?
     */
    bool anyNetworkOn()
    {
        bool any = false;
#if Wiring_WiFi && Wiring_WiFi == 1
        any = any || wifi.state == TonOff::ON;
#endif
#if Wiring_Cellular && Wiring_Cellular == 1
        any = any || cellular.state == TonOff::ON;
#endif
#if !(Wiring_WiFi && Wiring_WiFi == 1) && !(Wiring_Cellular && Wiring_Cellular == 1)
        any = true; // e.g. ethernet only: always connect
#endif
        return any;
    }

    void connectCloud()
    {
        if (!Particle.connected())
            internet = TinternetStatus::connecting;
        Particle.connect(); // ties up the system thread
    }

    /**
     * @brief called after an interface was turned off: if no other interface is left, also
     * disconnect from the cloud (otherwise the connection manager moves the cloud session to
     * the remaining interface on its own)
     */
    void disconnectCloudIfNoNetwork()
    {
        if (!anyNetworkOn())
        {
            Log.info("all network interfaces are off, disconnecting from the cloud");
            internet = TinternetStatus::disconnected;
            Particle.disconnect();
        }
    }

#if Wiring_WiFi && Wiring_WiFi == 1
    void wifiOn()
    {
        Log.info("turning wifi on");
        (WiFi.on)(); // parentheses: "on" is an SDDS macro
        WiFi.connect(WIFI_CONNECT_SKIP_LISTEN); // bring it up even if the cloud is already connected via another interface
        connectCloud();
    }

    void wifiOff()
    {
        Log.info("turning wifi off");
        disconnectCloudIfNoNetwork(); // disconnect first if this was the last interface
        WiFi.off();
    }

    /**
     * @brief add (or replace all saved networks with) the network defined by wifi.setup.ssid,
     * wifi.setup.password, wifi.setup.security and wifi.setup.hidden
     * @return result message (starts with "error" if it failed)
     */
    String defineNetwork(bool _replace)
    {
        String ssid = wifi.setup.ssid.c_str();
        String password = wifi.setup.password.c_str();
        auto sec = wifi.setup.security;
        bool hidden = wifi.setup.hidden == Twifi::Tsetup::Thidden::yes;

        // validate everything BEFORE touching the saved credentials (replace clears them)
        if (ssid.length() == 0 || ssid.length() > 32)
            return "error: ssid must be 1-32 characters";

        WLanSecurityType type;
        String detected;
        if (sec == Twifi::Tsetup::Tsecurity::detect)
        {
            if (hidden)
                return "error: hidden networks can't be found by a scan, set the security";
            int found = scanSecurity(ssid);
            if (found == -2)
                return "error: scan failed (is wifi on?), set the security";
            if (found < 0)
                return String("error: network '") + ssid + "' not found by scan, set the security (and hidden=yes if it is hidden)";
            type = (WLanSecurityType)found;
            if (type == WLAN_SEC_WPA_ENTERPRISE || type == WLAN_SEC_WPA2_ENTERPRISE)
                return "error: enterprise networks are not supported";
            detected = String(" (") + securityName(type) + " detected)";
        }
        else
        {
            type = (sec == Twifi::Tsetup::Tsecurity::WPA3)   ? WLAN_SEC_WPA3
                   : (sec == Twifi::Tsetup::Tsecurity::WPA)  ? WLAN_SEC_WPA
                   : (sec == Twifi::Tsetup::Tsecurity::WEP)  ? WLAN_SEC_WEP
                   : (sec == Twifi::Tsetup::Tsecurity::open) ? WLAN_SEC_UNSEC
                                                             : WLAN_SEC_WPA2;
        }

        if (type == WLAN_SEC_UNSEC)
            password = "";
        else if (password.length() == 0)
            return String("error: password required") + detected;
        else if ((type == WLAN_SEC_WPA || type == WLAN_SEC_WPA2 || type == WLAN_SEC_WPA3) &&
                 (password.length() < 8 || password.length() > 63))
            return String("error: WPA passwords must be 8-63 characters") + detected;

        if (_replace)
        {
            Log.info("replacing all saved wifi networks with '%s'", ssid.c_str());
            WiFi.clearCredentials();
        }
        else
            Log.info("adding wifi network '%s'", ssid.c_str());

        WiFiCredentials cred(ssid.c_str(), (SecurityType)type);
        if (password.length())
            cred.setPassword(password.c_str());
        cred.setHidden(hidden);
        if (!WiFi.setCredentials(cred))
            return String("error: failed to save network '") + ssid + "'";
        ssid += detected + (hidden ? " (hidden)" : "");

        updateSavedNetworks();

        // reconnect if wifi is on: always after a replace (the current network may be gone),
        // after an add only if not connected yet
        if (wifi.state == TonOff::ON && (_replace || !WiFi.ready()))
        {
            FwifiReconnectDisconnect = _replace && (WiFi.ready() || WiFi.connecting());
            FwifiReconnectTimer.start(1000); // delayed so the device can still report back
            return String(_replace ? "replaced all networks with '" : "added '") + ssid + "', reconnecting";
        }
        return String(_replace ? "replaced all networks with '" : "added '") + ssid + "'";
    }

    /**
     * @brief scan for the network and return its security (WLanSecurityType),
     * -1 if not found, -2 if the scan failed (blocks for a few seconds)
     */
    int scanSecurity(const String &_ssid)
    {
        WiFiAccessPoint aps[20];
        int n = WiFi.scan(aps, sizeof(aps) / sizeof(aps[0]));
        if (n < 0)
            return -2;
        for (int i = 0; i < n; i++)
            if (_ssid == aps[i].ssid)
                return aps[i].security;
        return -1;
    }

    static const char *securityName(WLanSecurityType _type)
    {
        switch (_type)
        {
        case WLAN_SEC_UNSEC: return "open";
        case WLAN_SEC_WEP: return "WEP";
        case WLAN_SEC_WPA: return "WPA";
        case WLAN_SEC_WPA2: return "WPA2";
        case WLAN_SEC_WPA3: return "WPA3";
        default: return "enterprise";
        }
    }

    void updateSavedNetworks()
    {
        WiFiAccessPoint ap[10]; // Device OS stores at most 10 wifi networks
        int n = WiFi.getCredentials(ap, sizeof(ap) / sizeof(ap[0]));
        String ssids;
        for (int i = 0; i < n; i++)
        {
            if (i > 0)
                ssids += ", ";
            ssids += ap[i].ssid;
        }
        if (wifi.setup.savedNetworks != ssids.c_str())
            wifi.setup.savedNetworks = ssids.c_str();
    }
#endif

#if Wiring_Cellular && Wiring_Cellular == 1
    void cellularOn()
    {
        Log.info("turning cellular on");
        (Cellular.on)(); // parentheses: "on" is an SDDS macro
        Cellular.connect();
        connectCloud();
    }

    void cellularOff()
    {
        Log.info("turning cellular off");
        disconnectCloudIfNoNetwork();
        Cellular.off();
    }
#endif

    TparticleSystem()
    {

        // system setup
        on(sdds::setup())
        {
            // device should always operate in UTC (web-app will translate)
            Time.zone(0);

            // start hardware watchdog - i.e. how long are individual operations in the application thread allowed to take before the system resets?
            Watchdog.init(WatchdogConfiguration().timeout(1min));
            Watchdog.start();

            // device id
            id = Particle.deviceID();

// store mac ress
#if Wiring_WiFi && Wiring_WiFi == 1
            byte bytes[6];
            WiFi.macAddress(bytes);
            char buff[20];
            snprintf(buff, sizeof(buff), "%02x:%02x:%02x:%02x:%02x:%02x", bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
            wifi.mac = buff;
            updateSavedNetworks();
#endif

            // calculate free flash memory
            // DO NOT do this regularly, too much overhead, instead do it after writes to flash
            checkFlashUsage();

            // subscribe to name handler
            Particle.subscribe("particle/device/name", &TparticleSystem::captureName, this);

            // note: the cloud connection is started by startConnection() once the saved state
            // is loaded (the wifi might be turned off permanently)

            // start system timers
            FsystemCheckTimer.start(FcheckInterval);
            FsyncTimer.start(FtimeSyncInterval);
        };

        // system actions
        on(action)
        {
            if (action == Taction::___)
            {
                // do nothing
                return;
            }
            else if (action == Taction::restart)
            {
                // user requests a restart (execute as delayed action to allow time for device to report back to cloud)
                FdelayedAction = Taction::restart;
                FdelayedActionTimer.start(1000);
            }
            else if (action == Taction::reset)
            {
                // user requests a reset  (execute as delayed action to allow time for device to report back to cloud)
                FdelayedAction = Taction::reset;
                FdelayedActionTimer.start(1000);
            }
            else if (action == Taction::syncTime)
            {
                FresyncSysTime = true;
                action = Taction::___;
            }
            else if (action == Taction::sendVitals)
            {
                // publish vitals to the cloud right now
                Log.trace("publishing vitals");
                Particle.publishVitals(particle::NOW);
                action = Taction::___;
            }
            else if (action == Taction::saveState)
            {
                saveState();
                action = Taction::___;
            }
        };

        // delayed action execution
        on(FdelayedActionTimer)
        {
            if (FdelayedAction == Taction::restart)
                System.reset(static_cast<uint8_t>(TrestartStatus::userRestart));
            else if (FdelayedAction == Taction::reset)
                System.reset(static_cast<uint8_t>(TrestartStatus::userReset));
        };

#if Wiring_WiFi && Wiring_WiFi == 1
        // wifi on/off (ignored until startConnection() is called after the state is loaded)
        on(wifi.state)
        {
            if (!FconnectionStarted)
                return;
            if (wifi.state == TonOff::ON)
            {
                FwifiOffTimer.stop();
                wifiOn();
            }
            else
            {
                // delayed to allow time for the device to report back (e.g. to the cloud)
                FwifiOffTimer.start(1000);
            }
        };

        on(FwifiOffTimer)
        {
            if (wifi.state == TonOff::OFF)
                wifiOff();
        };

        // add / replace wifi networks
        on(wifi.setup.action)
        {
            if (wifi.setup.action == Twifi::Tsetup::Taction::___)
                return;
            // run it from a timer so the action (and the "working" result) reach the clients first -
            // the detect scan blocks for a few seconds
            wifi.setup.result = wifi.setup.security == Twifi::Tsetup::Tsecurity::detect ? "scanning for the network..." : "saving...";
            FwifiSetupTimer.start(100);
        };

        on(FwifiSetupTimer)
        {
            if (wifi.setup.action == Twifi::Tsetup::Taction::___)
                return;
            String result = defineNetwork(wifi.setup.action == Twifi::Tsetup::Taction::replaceNetworks);
            if (result.startsWith("error"))
                Log.warn("wifi: %s", result.c_str());
            // never keep the credentials around
            wifi.setup.ssid = "";
            wifi.setup.password = "";
            wifi.setup.result = result.c_str();
            wifi.setup.action = Twifi::Tsetup::Taction::___;
        };

        // reconnect to newly defined networks
        on(FwifiReconnectTimer)
        {
            if (wifi.state != TonOff::ON || !FconnectionStarted)
                return;
            if (WiFi.listening())
                WiFi.listen(false);
            // replace while connected: turn wifi off and back on so it searches for the new network
            if (FwifiReconnectDisconnect)
            {
                Log.info("turning wifi off and on again to connect to the new network");
                FwifiReconnectDisconnect = false;
                FwifiReconnectRestart = true;
                wifiOff();
                FwifiReconnectTimer.start(2000);
                return;
            }
            if (FwifiReconnectRestart)
            {
                FwifiReconnectRestart = false;
                wifiOn(); // also reconnects the cloud
                return;
            }
            // add while not connected: just connect
            Log.info("connecting wifi to use the new credentials");
            WiFi.connect(WIFI_CONNECT_SKIP_LISTEN);
            connectCloud();
        };
#endif

#if Wiring_Cellular && Wiring_Cellular == 1
        // cellular on/off (ignored until startConnection() is called after the state is loaded)
        on(cellular.state)
        {
            if (!FconnectionStarted)
                return;
            if (cellular.state == TonOff::ON)
            {
                FcellularOffTimer.stop();
                cellularOn();
            }
            else
            {
                // delayed to allow time for the device to report back (e.g. to the cloud)
                FcellularOffTimer.start(1000);
            }
        };

        on(FcellularOffTimer)
        {
            if (cellular.state == TonOff::OFF)
                cellularOff();
        };
#endif

        // vitals publishing interval
        on(vitals.publishVitals_sec)
        {
            Particle.publishVitals(vitals.publishVitals_sec);
        };

        // system check in
        on(FsystemCheckTimer)
        {
            // refresh the hardware watchdog
            Watchdog.refresh();

            // check if memory changed
            if (vitals.freeRAM_byte != System.freeMemory())
            {
                vitals.freeRAM_byte = System.freeMemory();
                if (vitals.freeRAM_byte < memoryRestartLimit)
                {
                    // not enough free memory to keep operating safely
                    // FIXME: should there be some sort of data dump of the queuedData first?
                    // probably good to put it onto the flash drive
                    System.reset(static_cast<uint8_t>(TrestartStatus::outOfMemory));
                }
            }

            // check if connection status changed
            if (internet == TinternetStatus::connecting && Particle.connected())
            {
                internet = TinternetStatus::connected;
            }
            else if (internet == TinternetStatus::connected && !Particle.connected())
            {
                internet = TinternetStatus::connecting;
            }

// check on wifi/cellular info
#if Wiring_WiFi && Wiring_WiFi == 1
            // independent of the cloud connection (also works on local wifi without internet)
            {
                Twifi::Tstatus::e status = (wifi.state == TonOff::OFF || !FconnectionStarted) ? Twifi::Tstatus::off
                                           : WiFi.ready()                                     ? Twifi::Tstatus::connected
                                                                                              : Twifi::Tstatus::connecting;
                if (wifi.status != status)
                {
                    wifi.status = status;
                    if (status == Twifi::Tstatus::connected)
                        updateSavedNetworks(); // credentials may have changed (e.g. via listening mode)
                }
                dtypes::uint8 sig = 0;
                const char *ssid = "";
                if (WiFi.ready())
                {
                    WiFiSignal rssi = WiFi.RSSI();
                    sig = static_cast<dtypes::uint8>(round(rssi.getStrength()));
                    ssid = WiFi.SSID();
                }
                if (wifi.signal_percent != sig)
                    wifi.signal_percent = sig;
                if (wifi.network != ssid)
                    wifi.network = ssid;
            }
#endif

#if Wiring_Cellular && Wiring_Cellular == 1
            {
                Tcellular::Tstatus::e status = (cellular.state == TonOff::OFF || !FconnectionStarted) ? Tcellular::Tstatus::off
                                               : Cellular.ready()                                         ? Tcellular::Tstatus::connected
                                                                                                          : Tcellular::Tstatus::connecting;
                if (cellular.status != status)
                    cellular.status = status;
                dtypes::uint8 sig = 0;
                if (Cellular.ready())
                {
                    CellularSignal rssi = Cellular.RSSI();
                    sig = static_cast<dtypes::uint8>(round(rssi.getStrength()));
                }
                if (cellular.signal_percent != sig)
                    cellular.signal_percent = sig;
            }
#endif

            // resync system time
            if (FresyncSysTime && Particle.connected())
            {
                // sync time with cloud
                Log.trace("resynchronizing time with cloud");
                Particle.syncTime();
                FsyncTimer.stop();
                FsyncTimer.start(FtimeSyncInterval);
                FresyncSysTime = false;
            }

            // track cloud time syncs for vitals.timeSource (other sources set it themselves)
            system_tick_t cloudSync = Particle.timeSyncedLast();
            if (cloudSync != 0 && cloudSync != FlastCloudTimeSync)
            {
                FlastCloudTimeSync = cloudSync;
                if (vitals.timeSource != TtimeSource::cloud)
                    vitals.timeSource = TtimeSource::cloud;
            }

            // update time sdds_var and re-anchor the millisecond epoch on each second rollover
            if (Time.isValid())
            {
                dtypes::uint64 tick_ms = System.millis();
                time32_t now = Time.now();
                if (now != FlastNow)
                {
                    // only a plain second-by-second increment brackets a real second boundary:
                    // the first sample after the clock becomes valid says nothing about where
                    // in the second we are, and a time sync can step the clock by an arbitrary
                    // amount - neither may be used as an anchor (the next genuine rollover,
                    // at most a second later, re-anchors)
                    if (FhasNowSample && now == FlastNow + 1)
                    {
                        FepochUnix_ms = static_cast<dtypes::uint64>(now) * 1000;
                        FepochTick_ms = FlastNowTick_ms + (tick_ms - FlastNowTick_ms) / 2;
                        FepochAnchored = true;
                    }
                    if (now > FlastNow)
                        vitals.time_dt = Time.format(now, TIME_FORMAT_ISO8601_FULL); //"%Y-%m-%d %H:%M:%S %Z");
                    FlastNow = now;
                }
                FlastNowTick_ms = tick_ms;
                FhasNowSample = true;
            }
            else
            {
                // clock is not (or no longer) valid --> the anchor cannot be trusted
                FhasNowSample = false;
                FepochAnchored = false;
            }

            // trigger name handler
            if (FrequestName && Particle.connected())
            {
                Particle.publish("particle/device/name");
                FrequestName = false;
            }

            // check back in later
            FsystemCheckTimer.start(FcheckInterval);
        };

        // time sync
        on(FsyncTimer)
        {
            FresyncSysTime = true;
        };
    }

    // capture name and store it
    void captureName(const char *topic, const char *data)
    {
        if (strcmp(data, name) != 0)
        {
            name = data;
            saveState(); // always save the name right away if it changes
        }
    }

    // save state
    void saveState(bool _reset = false)
    {
        // store current time as last save
        // FIXME: ideally suspend the signal trigger here until save has actually succeeded!
        String oldLastSave = state.lastSave_dt;
        state.lastSave_dt = (_reset) ? "never" : Time.format(Time.now(), TIME_FORMAT_ISO8601_FULL);
        // try to save state into EEPROM
        TmenuHandle *root = findRoot();
        sdds::paramSave::TparamStreamer ps;
        sdds::paramSave::Tstream s;
        ps.save(root, &s);
        // success?
        if (ps.error() == TparamError::___)
        {
            // save was successful (always set status here)
            (_reset) ? state.status = Tstate::Tstatus::reset : state.status = Tstate::Tstatus::normal;
            state.size_byte = s.high();
        }
        else if (state.status != Tstate::Tstatus::failedSave)
        {
            // there was a problem!
            state.lastSave_dt = oldLastSave;
            state.status = Tstate::Tstatus::failedSave;
        }
        state.error = ps.error();
    }

    // load state
    void loadState()
    {
        // try to load state into EEPROM
        TmenuHandle *root = findRoot();
        sdds::paramSave::TparamStreamer ps;
        sdds::paramSave::Tstream s;
        ps.load(root, &s);
        // success?
        if (ps.error() == TparamError::___)
        {
            // load was successful (always set status here)
            state.status = Tstate::Tstatus::normal;
            state.size_byte = s.high();
        }
        else if (state.status != Tstate::Tstatus::failedLoad)
        {
            // there was a problem!
            state.status = Tstate::Tstatus::failedLoad;
        }
        state.error = ps.error();
    }

    size_t bytesToSectors(size_t _bytes)
    {
        return (_bytes + vitals.FflashSectorSize_byte - 1) / vitals.FflashSectorSize_byte;
    }

    size_t getDirSectors(const char *path = "/")
    {
        size_t sectors = 0;
        DIR *dir = opendir(path);
        if (!dir)
            return 0;

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            char fullPath[128];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", path, entry->d_name);

            struct stat s;
            if (stat(fullPath, &s) == 0)
            {
                if (S_ISDIR(s.st_mode))
                {
                    sectors += 1; // dir metadata sector
                    sectors += getDirSectors(fullPath);
                }
                else
                {
                    sectors += 1;                                      // file metadata sector
                    sectors += max(1, (int)bytesToSectors(s.st_size)); // min 1 data sector
                }
            }
        }
        closedir(dir);
        return sectors;
    }

    // calculate flash use
    void checkFlashUsage()
    {
        // LittleFS superblock (2 reserved root sectors) + root directory metadata sector
        size_t usedSectors = 2 + 1 + getDirSectors("/");
        vitals.freeSectors = (usedSectors < vitals.totalSectors) ? vitals.totalSectors - usedSectors : 0;
        vitals.freeFlash_byte = vitals.freeSectors * vitals.FflashSectorSize_byte;
    }
};

/**
 * @brief get the static particle system instance
 */
TparticleSystem &particleSystem()
{
    static TparticleSystem particle;
    return particle;
}