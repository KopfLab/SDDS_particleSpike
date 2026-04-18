#pragma once

#include "Particle.h"
#include "uTypedef.h"
#include "uCoreEnums.h"
#include "uParamSave.h"

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
    sdds_enum(___, restart, reconnect, disconnect, reset, saveState, syncTime, sendVitals, sendSdds, sendSddsValues, sendSddsState) Taction;
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

    // state
    class Tstate : public TmenuHandle
    {
    public:
        sdds_enum(normal, reset, failedLoad, failedSave) Tstatus;
        sdds_var(Tstatus, status, sdds::opt::readonly);
        sdds_var(Tstring, lastSave_dt, sdds_joinOpt(sdds::opt::saveval, sdds::opt::readonly), "never");
        sdds_var(Tuint16, size_byte, sdds::opt::readonly);
        sdds_var(TparamError, error, sdds::opt::readonly);
    };
    sdds_var(Tstate, state); // load/save state

    // vitals variables
    sdds_enum(powerUp, userRestart, userReset, watchdogTimeout, outOfMemory, PANIC) TrestartStatus;
    class Tvitals : public TmenuHandle
    {
    public:
        sdds_var(Tuint32, publishVitals_sec, sdds::opt::saveval, 60 * 60 * 6);               // how often to publish device vitals in seconds (takes 150 bytes per transmission!), 0 = no regular publishing
        sdds_var(Tstring, time_dt, sdds::opt::readonly);                                     // system time
        sdds_var(Tstring, mac, sdds::opt::readonly);                                         // wifi network - can get this from device list
        sdds_var(Tstring, network, sdds::opt::readonly);                                     // wifi network - can get this from device vitals
        sdds_var(Tuint8, signal_percent, sdds::opt::readonly);                               // wifi signal strength - can get this from device vitals
        sdds_var(TrestartStatus, lastRestart, sdds::opt::readonly, TrestartStatus::powerUp); // restart information

// random access memory (RAM, in bytes)
#if (PLATFORM_ID == PLATFORM_ARGON || PLATFORM_ID == PLATFORM_BORON)
        sdds_var(Tuint32, totalRAM_B, sdds::opt::readonly, 80 * 1024); // approximately 80 KB
#elif (PLATFORM_ID == PLATFORM_P2 || PLATFORM_ID == PLATFORM_MSOM)
        sdds_var(Tuint32, totalRAM_byte, sdds::opt::readonly, 3 * 1024 * 1024); // approximately 3 MB
#else
        sdds_var(Tuint32, totalRAM_byte, sdds::opt::readonly, 0);
#endif
        sdds_var(Tuint32, freeRAM_byte, sdds::opt::readonly); // free memory information

        // flash memory (in bytes) / number of sectors
        inline static const size_t flashSectorSize_byte = 4 * 1024; // 4 KB
#if (PLATFORM_ID == PLATFORM_MSOM)
        sdds_var(Tuint32, totalFlash_byte, sdds::opt::readonly, 8 * 1024 * 1024); // 8 MB
        sdds_var(Tuint32, totalSectors, sdds::opt::readonly, 8 * 1024 * 1024 / flashSectorSize_byte);
#elif (PLATFORM_ID == PLATFORM_ARGON || PLATFORM_ID == PLATFORM_P2 || PLATFORM_ID == PLATFORM_BORON)
        sdds_var(Tuint32, totalFlash_byte, sdds::opt::readonly, 2 * 1024 * 1024); // 2 MB
        sdds_var(Tuint32, totalSectors, sdds::opt::readonly, 2 * 1024 * 1024 / flashSectorSize_byte);
#else
        sdds_var(Tuint32, totalFlash_byte, sdds::opt::readonly, 0);
        sdds_var(Tuint32, totalSectors, sdds::opt::readonly, 0);
#endif
        sdds_var(Tuint32, freeFlash_byte, sdds::opt::readonly); // free flash (FIXME: not yet used)
        sdds_var(Tuint32, freeSectors, sdds::opt::readonly);    // free sectors (FIXME: not yet used)
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
        sdds_var(TonOff, publish, sdds::opt::saveval, TonOff::OFF);               // global on/off for publishing to the cloud
        sdds_var(Tstring, event, sdds::opt::saveval, "sddsData");                 // publish event name
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

    // time sync timer
    const system_tick_t FtimeSyncInterval = 1000 * 60 * 60 * 6; // every 6 hours should suffice
    Ttimer FsyncTimer;
    bool FresyncSysTime = false;

    // request device name? could reset to true at later point to request (by action or timer)
    bool FrequestName = true;

    // time stamp for the clock update
    time32_t FlastNow = 0;

public:
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
#if Wiring_WiFi
            byte bytes[6];
            WiFi.macAddress(bytes);
            char buff[20];
            snprintf(buff, sizeof(buff), "%02x:%02x:%02x:%02x:%02x:%02x", bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
            vitals.mac = buff;
#endif

            // subscribe to name handler
            Particle.subscribe("particle/device/name", &TparticleSystem::captureName, this);

            // connect to the cloud (ties up the system thread)
            Particle.connect();

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
            else if (action == Taction::disconnect)
            {
                // user requests to disconnect (execute as delayed action to allow time for device to report back to cloud)
                FdelayedAction = Taction::disconnect;
                FdelayedActionTimer.start(1000);
            }
            else if (action == Taction::reconnect && internet == TinternetStatus::disconnected)
            {
                // user requests to reconnect
                Log.trace("reconnecting to the cloud");
                internet = TinternetStatus::connecting;
                Particle.connect();
                action = Taction::___;
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
            else if (FdelayedAction == Taction::disconnect && internet != TinternetStatus::disconnected)
            {
                Log.trace("disconnecting from the cloud");
                internet = TinternetStatus::disconnected;
                // note: this does NOT turn wifi/cellular modem off!
                // if that's intended (e.g. for power safe), see the restrictions about cellular sim card locks
                // at https://docs.particle.io/reference/device-os/api/cellular/off/
                Particle.disconnect();
            }
        };

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
            if (WiFi.ready() && Particle.connectionInterface() == WiFi)
            {
                WiFiSignal rssi = WiFi.RSSI();
                dtypes::uint8 sig = static_cast<dtypes::uint8>(round(rssi.getStrength()));
                if (vitals.signal_percent != sig)
                    vitals.signal_percent = sig;
                if (vitals.network != WiFi.SSID())
                    vitals.network = WiFi.SSID();
            }
#endif

#if Wiring_Cellular && Wiring_Cellular == 1
            if (Cellular.ready() && Particle.connectionInterface() == Cellular)
            {
                CellularSignal rssi = Cellular.RSSI();
                dtypes::uint8 sig = static_cast<dtypes::uint8>(round(rssi.getStrength()));
                if (vitals.signal_percent != sig)
                    vitals.signal_percent = sig;
                // FIXME: what's the cellular network equivalent to SSID?
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

            // update time sdds_var
            if (Time.isValid() && Time.now() > FlastNow)
            {
                FlastNow = Time.now();
                vitals.time_dt = Time.format(FlastNow, TIME_FORMAT_ISO8601_FULL); //"%Y-%m-%d %H:%M:%S %Z");
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
};

/**
 * @brief get the static particle system instance
 */
TparticleSystem &particleSystem()
{
    static TparticleSystem particle;
    return particle;
}