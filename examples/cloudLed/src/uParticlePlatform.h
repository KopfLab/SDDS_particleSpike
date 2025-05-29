#pragma once

#include "Particle.h"
#include "uTypedef.h"

// always use system threading to avoid particle cloud synchronization to block the app
#ifndef SYSTEM_VERSION_v620
SYSTEM_THREAD(ENABLED);
#endif

// use semiautomatic mode so that particle doesn't try to connect until setup
// and the system thread isn't tied up during Particle.subscribe/function/variable
SYSTEM_MODE(SEMI_AUTOMATIC);

// sdds enumerations
sdds_enum(___, restart, connect, disconnect) TsystemAction;
sdds_enum(connecting, connected, disconnected) TinternetStatus;
sdds_enum(nominal, userRestart, watchdogTimeout, outOfMemory, PANIC) TresetStatus;

// particle platform menuHandle
class TparticlePlatform : public TmenuHandle{

    public:

    // sdds variables
    sdds_var(TsystemAction,action)
    sdds_var(TinternetStatus,internet,sdds::opt::readonly,TinternetStatus::e::connecting)
    sdds_var(TresetStatus,lastReset,sdds::opt::readonly,TresetStatus::e::nominal)
    sdds_var(Tuint32,freeMemory,sdds::opt::readonly)

    protected:

    // wifi
    #if Wiring_WiFi
    const bool hasWifi = true;
    #else
    const bool hasWifi = false;
    #endif

    // cellular
    #if Wiring_Cellular
    const bool hasCellular = true;
    #else
    const bool hasCellular = false;
    #endif

    // flash memory (in bytes)
    #if (PLATFORM_ID == PLATFORM_ARGON || PLATFORM_ID == PLATFORM_P2 || PLATFORM_ID == PLATFORM_BORON)
    const size_t flashSize = 2 * 1024 * 1024; // 2 MB
    #else
    const size_t flashSize = 0;
    #endif

    // flash sectors
    const size_t flashSectorSize = 4 * 1024; // 4 KB
    const size_t flashSectors = flashSize / flashSectorSize; // available sectors

    // random access memory (RAM, in bytes)
    #if (PLATFORM_ID == PLATFORM_ARGON || PLATFORM_ID == PLATFORM_BORON)
    const size_t ramSize = 80 * 1024; // approximately 80 KB
    #elif (PLATFORM_ID == PLATFORM_P2)
    const size_t ramSize = 3 * 1024 * 1024; // approximately 3 MB
    #else
    const size_t ramSize = 0;
    #endif

    // how much free RAM (in bytes) required before forced restart?
    const uint32_t memoryRestartLimit = 5 * 1024; // limit to 5 KB

    // platform check-in timer
    unsigned long checkInterval = 100; // ms
    Ttimer checkTimer;

    // particle platform
    TparticlePlatform() {

        // system setup
        on(sdds::setup()){

            // process device reset information
            System.enableFeature(FEATURE_RESET_INFO);
            if (System.resetReason() == RESET_REASON_PANIC) {
                // uh oh - reset due to PANIC (e.g. segfault)
                // https://docs.particle.io/reference/cloud-apis/api/#spark-device-last_reset
                uint32_t panicCode = System.resetReasonData();
                Log.error("restarted due to PANIC, code: %d", panicCode);
                lastReset = TresetStatus::e::PANIC;
                //System.enterSafeMode(); // go straight to safe mode?
            } else if (System.resetReason() == RESET_REASON_WATCHDOG) {
                // hardware watchdog detected a timeout
                Log.warn("restarted due to watchdog (=timeout)");
                lastReset = TresetStatus::e::watchdogTimeout;
            } else if (System.resetReason() == RESET_REASON_USER) {
                // software triggered resets
                uint32_t userReset = System.resetReasonData();
                if (userReset == static_cast<uint8_t>(TresetStatus::e::outOfMemory)) {
                    // low memory detected
                    Log.warn("restarted due to low memory");
                    lastReset = TresetStatus::e::outOfMemory;
                } else if (userReset == static_cast<uint8_t>(TresetStatus::e::userRestart)) {
                    // user requested a restart
                    Log.trace("restarted per user request");
                    lastReset = TresetStatus::e::userRestart;
                } 
            } else {
                // report any of the other reset reasons? 
                // https://docs.particle.io/reference/cloud-apis/api/#spark-device-last_reset
            }

            // start hardware watchdog - i.e. how long are individual operations in the application thread allowed to take before the system resets?
            Watchdog.init(WatchdogConfiguration().timeout(1min));
            Watchdog.start();

            // connect to the cloud (ties up the system thread)
            Particle.connect();
        };

        // system actions
        on(action){
            
            if (action==TsystemAction::e::restart){
                // user requests a restart
                System.reset(static_cast<uint8_t>(TresetStatus::e::userRestart));
            } else if (action==TsystemAction::e::disconnect && internet != TinternetStatus::e::disconnected) {
                // user requests to disconnect
                Log.trace("disconnecting from the cloud");
                internet = TinternetStatus::e::disconnected;
                Particle.disconnect();
                // note: this does NOT turn wifi/cellular modem off! 
                // if that's intended (e.g. for power safe), see the restrictions about cellular sim card locks
                // at https://docs.particle.io/reference/device-os/api/cellular/off/
            } else if (action==TsystemAction::e::connect && internet == TinternetStatus::e::disconnected) {
                // user requests to reconnect
                Log.trace("reconnecting to the cloud");
                internet = TinternetStatus::e::connecting;
                Particle.connect();
            }

            // reset action
            if (action != TsystemAction::e::___) {
                action = TsystemAction::e::___;
            }
        };

        // system check in
        checkTimer.start(checkInterval);
        on(checkTimer) {
            // refresh the hardware watchdog
            Watchdog.refresh(); 

            // check if memory changed
            if (freeMemory != System.freeMemory()) {
                freeMemory = System.freeMemory();
                if (freeMemory < memoryRestartLimit) {
                    // not enough free memory to keep operating safely
                    System.reset(static_cast<uint8_t>(TresetStatus::e::outOfMemory));
                }
            }

            // check if connection status changed
            if (internet == TinternetStatus::e::connecting && Particle.connected()) {
               internet = TinternetStatus::e::connected;
            } else if (internet == TinternetStatus::e::connected && !Particle.connected()) {
               internet = TinternetStatus::e::connecting;
            }

            // check back in later
            checkTimer.start(checkInterval);
        };

    }
};