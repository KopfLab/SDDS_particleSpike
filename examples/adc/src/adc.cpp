#include "Particle.h"
#include "uTypedef.h"
#include "uCoreEnums.h"
#include "uMultask.h"

// adc example
class Tadc : public TmenuHandle{
    Ttimer timer;
    public:
        sdds_var(Tuint8,pin,sdds::opt::saveval)
        sdds_var(Tuint16,readInterval,sdds::opt::saveval,100)
        sdds_var(Tuint16,value,sdds::opt::readonly)
        sdds_var(Tfloat64,voltage,sdds::opt::readonly)
        sdds_var(Tstring,unit,sdds::opt::readonly,"mV")

        Tadc(){
          on(pin){
			//check pin to be a valid pin...
            pinMode(pin, INPUT);
            timer.start(0);
          };

          on(timer){
            value = analogRead(pin); 
            voltage = static_cast<dtypes::float64>(value) * 3300. / 4096.;
            timer.start(readInterval); 
          };
        }
};

// self-describing data structure (SDDS) tree
class TsddsTree : public TmenuHandle{
    public:
    
        sdds_var(Tadc, adc1)
        sdds_var(Tadc, adc2)

        TsddsTree(){}
} sddsTree;

// serial spike for communication via serial (with baud rate)
#include "uSerialSpike.h"
TserialSpike serialSpike(sddsTree, 115200);

// particle spike for particle communication (with sdds name and version)
#include "uParticleSpike.h"
static TparticleSpike particleSpike(sddsTree, "adc", 1);

// setup
void setup(){
    // setup particle spike
    particleSpike.setup();
}

// loop
void loop(){
    // handle all events
    TtaskHandler::handleEvents();
}