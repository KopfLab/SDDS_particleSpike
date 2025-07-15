#include "Particle.h"
#include "uTypedef.h"
#include "uCoreEnums.h"
#include "uMultask.h"

// self-describing data structure (SDDS) tree
class TsddsTree : public TmenuHandle{
    public:
        // ADD SDDS VARS HERE
        sdds_var(Tuint8,xyz)

        TsddsTree(){
            // ADD EVENT HANDLERS
            on(xyz) {
            };
        }
} sddsTree;

// serial spike for communication via serial (with baud rate)
#include "uSerialSpike.h"
TserialSpike serialSpike(sddsTree, 115200);

// particle spike for particle communication (with sdds name and version)
#include "uParticleSpike.h"
static TparticleSpike particleSpike(sddsTree, "project", 1);

// optional: log handler for debugging
/*
SerialLogHandler logHandler(
    LOG_LEVEL_INFO, { // INFO for non-app messages
    { "app", LOG_LEVEL_TRACE } // TRACE for app
});
*/

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