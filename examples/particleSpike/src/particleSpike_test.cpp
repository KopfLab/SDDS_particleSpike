// example adapted from https://github.com/mLamneck/SDDS/blob/main/examples/led/led.ino

#define SDDS_PARTICLE_DEBUG 1
#include "Particle.h"
#include "uTypedef.h"
#include "uCoreEnums.h"
#include "uMultask.h"
#include "uParticleSpike.h"


// example LED menu handle
class Tled : public TmenuHandle{

    using TonOff = sdds::enums::OnOff;

    Ttimer timer;
    public:

        sdds_var(TonOff,ledSwitch,sdds::opt::saveval)
        sdds_var(TonOff,blinkSwitch,sdds::opt::saveval)
        sdds_var(Tuint16,onTime,sdds::opt::saveval,500)
        sdds_var(Tuint16,offTime,sdds::opt::saveval,500)
        
        Tled(){
            pinMode(LED_BUILTIN, OUTPUT);

            on(ledSwitch){
                (ledSwitch == TonOff::e::ON) ?
                    digitalWrite(LED_BUILTIN, HIGH):
                    digitalWrite(LED_BUILTIN, LOW);
            };

            on(blinkSwitch){
                (blinkSwitch == TonOff::e::ON) ?
                    timer.start(0):
                    timer.stop();
            };

            on(timer){
                if (ledSwitch == TonOff::e::ON){
                    ledSwitch = TonOff::e::OFF;
                    timer.start(offTime);
                } else {
                    ledSwitch = TonOff::e::ON;
                    timer.start(onTime);
                }
            };
        }
};

// root structure
class TuserStruct : public TmenuHandle{
    public:

        sdds_var(Tled,led)
        TuserStruct(){
        }
} userStruct;

// serial spike for output
#include "uSerialSpike.h"
TserialSpike serialHandler(userStruct, 115200);

// log handler
SerialLogHandler logHandler(LOG_LEVEL_INFO, { // Logging level for non-application messages
    { "app", LOG_LEVEL_TRACE } // Logging level for application messages (i.e. debug mode)
});

#include "uParticleSpike.h"
static TparticleSpike particleSpike(userStruct, "mytest", 2);

void setup(){
    particleSpike.setup();
}

void loop(){
  TtaskHandler::handleEvents();
}