#pragma once

#include "Particle.h"
#include "uTypedef.h"
#include "uCoreEnums.h"
#include "uParamSave.h"

// always use system threading to avoid particle cloud synchronization to block the app
#ifndef SYSTEM_VERSION_v620
SYSTEM_THREAD(ENABLED);
#endif

// use semiautomatic mode so that particle doesn't try to connect until setup
// and the system thread isn't tied up during Particle.subscribe/function/variable
SYSTEM_MODE(SEMI_AUTOMATIC);

/**
 * @brief automatically added menu item that keeps track of publishing intervals for all variables
 */
class TparticleVariableIntervals : public TmenuHandle {

    private:

        Tmeta meta() override { return Tmeta{TYPE_ID, 0, "varIntervalsMS"}; }

    public:

        TparticleVariableIntervals() {}
};

// sdds enumerations
sdds_enum(___, restart, reconnect, disconnect, reset, syncTime, sendVitals, snapshot) TsystemAction;
sdds_enum(connecting, connected, disconnected) TinternetStatus;
sdds_enum(nominal, userRestart, userReset, watchdogTimeout, outOfMemory, PANIC) TresetStatus;
sdds_enum(___, complete) TstartuStatus;

#ifdef SDDS_PARTICLE_DEBUG
sdds_enum(___, getValues, getTree) TdebugAction;
#else
sdds_enum(___) TdebugAction;
#endif

/**
 * @brief automatically added menu item that holds system variables
 * this is added as the first menu entry with structure type and version
 * as the very first 2 variables which are used on server side to confirm
 * if values (V) and structure tree (T) are compatible
 */
class TparticleSystem : public TmenuHandle {

	private:
		using TonOff = sdds::enums::OnOff;

	public:

        // key structure type & version definitions
        sdds_var(Tstring,type,sdds::opt::readonly) // device type
        sdds_var(Tuint16,version,sdds::opt::readonly) // device version

        // system variables
        sdds_var(Tstring,id,sdds::opt::readonly) // device ID
        sdds_var(Tstring,name,sdds_joinOpt(sdds::opt::saveval, sdds::opt::readonly)) // cloud derived device name 
        sdds_var(TstartuStatus,startup,sdds::opt::readonly) // keeps track of startup processes
        sdds_var(TinternetStatus,internet,sdds::opt::readonly,TinternetStatus::e::connecting) // internet status
        sdds_var(TparamSaveMenu,state) // load/save state

        // vitals variables
        class Tvitals : public TmenuHandle{
            public:
                sdds_var(Tuint32,publishVitalsSEC,sdds::opt::saveval,0) // how often to publish device vitals in seconds (takes 150 bytes per transmission!), 0 = no regular publishing
                sdds_var(Tstring,time,sdds::opt::readonly) // system time
                sdds_var(Tstring,mac, sdds::opt::readonly) // wifi network - can get this from device list
                sdds_var(Tstring,network, sdds::opt::readonly) // wifi network - can get this from device vitals
                sdds_var(Tuint8,signal, sdds::opt::readonly) // wifi signal strength - can get this from device vitals
                sdds_var(TresetStatus,lastRestart,sdds::opt::readonly,TresetStatus::e::nominal) // reset information

                // random access memory (RAM, in bytes)
                #if (PLATFORM_ID == PLATFORM_ARGON || PLATFORM_ID == PLATFORM_BORON)
                sdds_var(Tuint32,totalRAM,sdds::opt::readonly, 80 * 1024) // approximately 80 KB
                #elif (PLATFORM_ID == PLATFORM_P2)
                sdds_var(Tuint32,totalRAM,sdds::opt::readonly, 3 * 1024 * 1024) // approximately 3 MB
                #else
                sdds_var(Tuint32,totalRAM,sdds::opt::readonly, 0)
                #endif
                sdds_var(Tuint32,freeRAM,sdds::opt::readonly) // free memory information

                // flash memory (in bytes) / number of sectors
                inline static const size_t flashSectorSize = 4 * 1024; // 4 KB
                #if (PLATFORM_ID == PLATFORM_ARGON || PLATFORM_ID == PLATFORM_P2 || PLATFORM_ID == PLATFORM_BORON)
                sdds_var(Tuint32,totalFlash,sdds::opt::readonly, 2 * 1024 * 1024) // 2 MB
                sdds_var(Tuint32,totalSectors,sdds::opt::readonly, 2 * 1024 * 1024 / flashSectorSize)
                #else
                sdds_var(Tuint32,totalFlash,sdds::opt::readonly, 0)
                sdds_var(Tuint32,totalSectors,sdds::opt::readonly, 0)
                #endif
                sdds_var(Tuint32,freeFlash,sdds::opt::readonly) // free flash (FIXME: not yet used)
                sdds_var(Tuint32,freeSectors,sdds::opt::readonly) // free sectors (FIXME: not yet used)

        };
        sdds_var(Tvitals,vitals)

        // publishing variables
        class Tpublishing : public TmenuHandle{

            // FIMXE: revisit the naming for these as it should include all messages not just bursts! (also other publish)
            // FIXME: add a method that adds to the dataQueue --> rename the queue to be burst independent
            // publishing bursts
            class Tbursts : public TmenuHandle{
                public:
                    sdds_var(Tuint32, timerMS, sdds::opt::saveval, 1000) // how many MS is a data burst?
                    sdds_var(Tuint32, queued, sdds::opt::readonly, 0) // number of currently queued bursts
                    sdds_var(Tuint32, sending, sdds::opt::readonly, 0) // number of currently sending bursts
                    sdds_var(Tuint32, sent, sdds::opt::readonly, 0) // number of successfully sent bursts
                    sdds_var(Tuint32, failed, sdds::opt::readonly, 0) // number of failed/requeued bursts
                    sdds_var(Tuint32, invalid, sdds::opt::readonly, 0) // number of invalid/discarded bursts
            };

            public:
                sdds_var(TonOff, publish, sdds::opt::saveval,TonOff::e::OFF) // global on/off for publishing to the cloud
                sdds_var(Tstring, event, sdds::opt::saveval, "data") // publish event name
                sdds_var(Tbursts, bursts) // burst information
                sdds_var(Tuint32, globalIntervalMS, sdds::opt::saveval, 1000 * 60 * 10) // global publish interval
        };
        sdds_var(Tpublishing,publishing)
        sdds_var(TsystemAction,action) // take a system action
        sdds_var(TdebugAction,debug) // debug actions

	private:
		
        // what is this menu item called?
		Tmeta meta() override { return Tmeta{TYPE_ID, 0, "SYSTEM"}; }

		// how much free RAM (in bytes) required before forced restart?
		const uint32_t memoryRestartLimit = 5 * 1024; // limit to 5 KB

		// platform check-in timer
		const system_tick_t FcheckInterval = 100; // ms
		Ttimer FpublishCheckTimer;

        // time sync timer
        const system_tick_t FtimeSyncInterval = 1000 * 60 * 60 * 6; // every 6 hours should suffice
        Ttimer FsyncTimer;
        bool FresyncSysTime = false;

        // request device name? could reset to true at later point to request (by action or timer)
        bool FrequestName = true;

        // time stamp for the clock update
        time32_t FlastNow = 0;

       

	public:

		TparticleSystem() {

			// system setup
			on(sdds::setup()){
				// start hardware watchdog - i.e. how long are individual operations in the application thread allowed to take before the system resets?
				Watchdog.init(WatchdogConfiguration().timeout(1min));
				Watchdog.start();

                // device id
                id = Particle.deviceID();

                // store mac address
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
			};

			// system actions
			on(action){
				bool reset = true;
				if (action==TsystemAction::e::restart){
					// user requests a restart
					System.reset(static_cast<uint8_t>(TresetStatus::e::userRestart));
                } else if (action==TsystemAction::e::reset){
					// user requests a restart
					System.reset(static_cast<uint8_t>(TresetStatus::e::userReset));
				} else if (action==TsystemAction::e::disconnect && internet != TinternetStatus::e::disconnected) {
					// user requests to disconnect
					Log.trace("disconnecting from the cloud");
					internet = TinternetStatus::e::disconnected;
					Particle.disconnect();
					// note: this does NOT turn wifi/cellular modem off! 
					// if that's intended (e.g. for power safe), see the restrictions about cellular sim card locks
					// at https://docs.particle.io/reference/device-os/api/cellular/off/
				} else if (action==TsystemAction::e::reconnect && internet == TinternetStatus::e::disconnected) {
					// user requests to reconnect
					Log.trace("reconnecting to the cloud");
					internet = TinternetStatus::e::connecting;
					Particle.connect();
				} else if (action==TsystemAction::e::syncTime) {
                    FresyncSysTime = true;
                } else if (action==TsystemAction::e::sendVitals) {
                    // publish vitals to the cloud right now
                    Particle.publishVitals(particle::NOW);
                } else {
                    // skip reset
                    reset = false;
                }

				if (reset) action = TsystemAction::e::___;
			};

            // vitals publishing interval
            on(vitals.publishVitalsSEC) {
                Particle.publishVitals(vitals.publishVitalsSEC);
            };

			// system check in
			FpublishCheckTimer.start(FcheckInterval);
			on(FpublishCheckTimer) {
				// refresh the hardware watchdog
				Watchdog.refresh(); 

				// check if memory changed
				if (vitals.freeRAM != System.freeMemory()) {
					vitals.freeRAM = System.freeMemory();
					if (vitals.freeRAM < memoryRestartLimit) {
						// not enough free memory to keep operating safely
                        // FIXME: should there be some sort of data dump of the queuedData first?
						System.reset(static_cast<uint8_t>(TresetStatus::e::outOfMemory));
					}
				}

                // check if connection status changed
				if (internet == TinternetStatus::e::connecting && Particle.connected()) {
				    internet = TinternetStatus::e::connected;
				} else if (internet == TinternetStatus::e::connected && !Particle.connected()) {
				    internet = TinternetStatus::e::connecting;
				}

                // check on wifi/cellular info
                #if Wiring_WiFi
                if (WiFi.ready() && Particle.connectionInterface() == WiFi) {
                    WiFiSignal rssi = WiFi.RSSI();
                    dtypes::uint8 sig = static_cast<dtypes::uint8>(round(rssi.getStrength()));
                    if (vitals.signal != sig) vitals.signal = sig;
                    if (vitals.network != WiFi.SSID()) vitals.network = WiFi.SSID();
                }
                #endif

                #if Wiring_Cellular
                if (Cellular.ready() && Particle.connectionInterface() == Cellular) {}
                    CellularSignal rssi = Cellular.RSSI();
                    dtypes::uint8 sig = static_cast<dtypes::uint8>(round(rssi.getStrength()));
                    if (signal != sig) signal = sig;
                    // FIXME: what's the cellular network equivalent to SSID?
                }
                #endif 

                // resync system time
                if (FresyncSysTime && Particle.connected()) {
                    // sync time with cloud
                    Log.trace("resynchronizing time with cloud");
                    Particle.syncTime();
                    FsyncTimer.stop();
                    FsyncTimer.start(FtimeSyncInterval);
                    FresyncSysTime = false;
                }

                // update time sdds_var
                if (Time.isValid() && Time.now() > FlastNow) {
                    FlastNow = Time.now();
                    vitals.time = Time.format(FlastNow, "%Y-%m-%d %H:%M:%S %Z");
                }

                // trigger name handler
                if (FrequestName && Particle.connected()) {
                    Particle.publish("particle/device/name");
                    FrequestName = false;
                }

				// check back in later
				FpublishCheckTimer.start(FcheckInterval);
			};

            // time sync
            FsyncTimer.start(FtimeSyncInterval);
            on(FsyncTimer) {
                FresyncSysTime = true;
            };

            
		}

        // methods
        void captureName(const char *topic, const char *data) {
            if (strcmp(data, name) != 0) {
                name = data;
                state.action = TenLoadSave::e::save;
            }
        }

};