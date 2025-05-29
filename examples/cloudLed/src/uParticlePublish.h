#pragma once

#include "Particle.h"
#include "uTypedef.h"

// sdds enumerations
sdds_enum(publish,store,forget) TpublishState;
sdds_enum(ms,secs,mins,hours,days) TintervalUnitsState;

namespace timeInSeconds{
    constexpr dtypes::uint32 SEC = 1;
    constexpr dtypes::uint32 MIN = 60 * SEC;
    constexpr dtypes::uint32 HOUR = 60 * MIN;
    constexpr dtypes::uint32 DAY = 24 * HOUR;
};

// particle platform menuHandle
class TparticlePublish : public TmenuHandle{

    public:

    // sdds variables
    sdds_var(TpublishState,status,sdds::opt::saveval,TpublishState::e::forget)
    sdds_var(Tuint32,intervalMs,sdds::opt::saveval)
    sdds_var(Tfloat32,interval,sdds::opt::readonly)
    sdds_var(TintervalUnitsState,intervalUnit,sdds::opt::readonly)
    
    // particle platform
    TparticlePublish() {

        // update user friendly publish interval
        on(intervalMs) {
            // provide easy to read output
            std::chrono::milliseconds ms(intervalMs);
            if (intervalMs >= timeInSeconds::DAY * 1000) {
                interval = (std::chrono::duration<float, std::ratio<timeInSeconds::DAY>>(ms)).count();
                intervalUnit = TintervalUnitsState::e::days;
            } else if (intervalMs >= timeInSeconds::HOUR * 1000) {
                interval = (std::chrono::duration<float, std::ratio<timeInSeconds::HOUR>>(ms)).count();
                intervalUnit = TintervalUnitsState::e::hours;
            } else if (intervalMs >= timeInSeconds::MIN * 1000) {
                interval = (std::chrono::duration<float, std::ratio<timeInSeconds::MIN>>(ms)).count();
                intervalUnit = TintervalUnitsState::e::mins;
            } else if (intervalMs >= timeInSeconds::SEC * 1000) {
                interval = (std::chrono::duration<float, std::ratio<timeInSeconds::SEC>>(ms)).count();
                intervalUnit = TintervalUnitsState::e::secs;
            } else {
                interval = static_cast<float>(intervalMs);
                intervalUnit = TintervalUnitsState::e::ms;
            }
        };

        // trigger conversion right away
        intervalMs = timeInSeconds::MIN * 1000;

    }
};