// example adapted from https://github.com/mLamneck/SDDS/blob/main/examples/led/led.ino

#include "Particle.h"
#include "uTypedef.h"
#include "uMultask.h"
#include "uParamSave.h"
#include "uParticlePlatform.h"
#include "uParticlePublish.h"

sdds_enum(OFF,ON) TonOffState;

// example LED menu handle
class Tled : public TmenuHandle{
    Ttimer timer;
    public:

        sdds_var(TonOffState,ledSwitch,sdds::opt::saveval)
        sdds_var(TonOffState,blinkSwitch,sdds::opt::saveval)
        sdds_var(Tuint16,onTime,sdds::opt::saveval,500)
        sdds_var(Tuint16,offTime,sdds::opt::saveval,500)
        
        Tled(){
            pinMode(LED_BUILTIN, OUTPUT);

            on(ledSwitch){
                (ledSwitch == TonOffState::e::ON) ?
                    digitalWrite(LED_BUILTIN, HIGH):
                    digitalWrite(LED_BUILTIN, LOW);
            };

            on(blinkSwitch){
                (blinkSwitch == TonOffState::e::ON) ?
                    timer.start(0):
                    timer.stop();
            };

            on(timer){
                if (ledSwitch == TonOffState::e::ON){
                    ledSwitch = TonOffState::e::OFF;
                    timer.start(offTime);
                } else {
                    ledSwitch = TonOffState::e::ON;
                    timer.start(onTime);
                }
            };
        }
};

// root structure
class TuserStruct : public TmenuHandle{
    public:

        sdds_var(TparticlePlatform, platform)
        sdds_var(TparticlePublish, publish)
        sdds_var(Tled,led)
        sdds_var(TparamSaveMenu,params)

        TuserStruct(){
        }
} userStruct;

// particle spike
#include "uParticleSpike.h"
TparticleSpike particleHandler(
    userStruct, 
    "cmd", // what particle function to receive on (also the name of the variable that stores the latest)
    "reply", // what particle publish event name to use (also the name of the variable that stores the latest)
    userStruct.publish.status, 
    userStruct.publish.intervalMs
);

// serial spike just for backup
#include "uSerialSpike.h"
TserialSpike serialHandler(userStruct, 115200);

// log handler
SerialLogHandler logHandler(LOG_LEVEL_INFO, { // Logging level for non-application messages
    { "app", LOG_LEVEL_TRACE } // Logging level for application messages (i.e. debug mode)
});

void setup(){

}

void loop(){
  TtaskHandler::handleEvents();
}