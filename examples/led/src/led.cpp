// example adapted from https://github.com/mLamneck/SDDS/blob/main/examples/led/led.ino

#define SDDS_PARTICLE_DEBUG 1
#include "Particle.h"
#include "uTypedef.h"
#include "uCoreEnums.h"
#include "uMultask.h"

// example LED menu handle
class Tled : public TmenuHandle
{

    using TonOff = sdds::enums::OnOff;
    Ttimer timer;

public:
    sdds_var(TonOff, ledSwitch, sdds::opt::saveval);
    sdds_var(TonOff, blinkSwitch, sdds::opt::saveval);
    sdds_var(Tuint16, onTime, sdds::opt::saveval, 500);
    sdds_var(Tuint16, offTime, sdds::opt::saveval, 500);

    // some value and unit to record
    sdds_var(Tuint16, value);
    sdds_var(Tstring, valueUnit, sdds::opt::saveval, "ms");

    Tled()
    {
        pinMode(LED_BUILTIN, OUTPUT);

        on(ledSwitch)
        {
            (ledSwitch == TonOff::e::ON) ? digitalWrite(LED_BUILTIN, HIGH) : digitalWrite(LED_BUILTIN, LOW);
        };

        on(blinkSwitch)
        {
            (blinkSwitch == TonOff::e::ON) ? timer.start(0) : timer.stop();
        };

        on(timer)
        {
            if (ledSwitch == TonOff::e::ON)
            {
                ledSwitch = TonOff::e::OFF;
                timer.start(offTime);
            }
            else
            {
                ledSwitch = TonOff::e::ON;
                timer.start(onTime);
            }
        };
    }
};

// root structure
class TuserStruct : public TmenuHandle
{
public:
    sdds_var(Tled, led)
        TuserStruct() {}
} userStruct;

// serial spike for communication via serial
#include "uSerialSpike.h"
TserialSpike serialSpike(userStruct, 115200);

// log handler
SerialLogHandler logHandler(LOG_LEVEL_INFO, {
                                                // Logging level for non-application messages
                                                {"app", LOG_LEVEL_TRACE} // Logging level for application messages (i.e. debug mode)
                                            });

// particle spike for paritcle communication
#include "uParticleSpike.h"
static TparticleSpike particleSpike(
    userStruct, // self-describing data structure (SDDS)
    "led",      // structure type name
    2,          // structure version
    "Unit"      // auto-detect sdds vars' units based on this suffix
);

void setup()
{
    // setup particle communication
    using publish = TparticleSpike::publish;
    particleSpike.setup(
        // set default publishing intervals for anything that should be different from publish::OFF
        {
            // --> all variables that are stored in EEPROM (saveeval option) should report all changes
            {publish::IMMEDIATELY, sdds::opt::saveval},
            // --> all floats should inherit from the globalInterval (regardless of whether they are saved or not)
            {publish::INHERIT, {sdds::Ttype::FLOAT32, sdds::Ttype::FLOAT64}},
            // --> for this particular class, the ledSwitch should also inherit from globalInterval
            {publish::INHERIT, &userStruct.led.ledSwitch}});
}

void loop()
{
    TtaskHandler::handleEvents();
}