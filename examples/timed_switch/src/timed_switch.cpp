#include "Particle.h"
#include "uTypedef.h"
#include "uCoreEnums.h"
#include "uMultask.h"

// Relay via ouput pin with PWM
class TtimedSwitch : public TmenuHandle
{

public:
    // sdds enums
    using TonOff = sdds::enums::OnOff;
    sdds_enum(___, on, off, schedule) Taction;
    sdds_enum(on, off, schedule) Tstate;

    // sdds vars
    sdds_var(Taction, action);
    sdds_var(Tstate, state, sdds_joinOpt(sdds::opt::saveval, sdds::opt::readonly), Tstate::off);
    sdds_var(Tuint8, pin, sdds::opt::saveval, A2);
    sdds_var(Tuint8, intensity_percent, sdds::opt::saveval, 100);
    sdds_var(Tuint32, scheduleOn_sec, sdds::opt::saveval, 60 * 60 * 12);  // 12 hours on
    sdds_var(Tuint32, scheduleOff_sec, sdds::opt::saveval, 60 * 60 * 12); // 12 hours off
    sdds_var(Tuint16, scheduleOnStart_HHMM, sdds::opt::saveval, 1200);
    sdds_var(Tstring, scheduleInfo, sdds::opt::readonly);

private:
    // schedule timer
    Ttimer FscheduleTimer;

    // schedule structure
    struct Schedule
    {
        bool isOn;
        uint32_t secondsToSwitch;
        uint8_t h, m, s;
    };

    Schedule calculateSchedule()
    {
        // schedule object
        Schedule r{false, 0, 0, 0, 0};

        // do we have a valid time?
        if (!Time.isValid())
            return r;

        // figure out how much time has past since the start (today or yesterday)
        const uint16_t start_hour = scheduleOnStart_HHMM.value() / 100;
        const uint16_t start_min = scheduleOnStart_HHMM.value() % 100;
        const uint32_t start_sec = start_hour * 60 * 60 + start_min * 60;
        const uint32_t now_sec = static_cast<uint32_t>(Time.hour()) * 3600 + Time.minute() * 60 + Time.second();
        const uint32_t delta = (start_sec > now_sec) ? 24 * 60 * 60 + now_sec - start_sec : now_sec - start_sec;

        // figure out which phase we are in and when the next phase starts
        const uint32_t period = scheduleOn_sec.value() + scheduleOff_sec.value();
        const uint32_t phase = delta % period;
        if (phase < scheduleOn_sec.value())
        {
            r.isOn = true;
            r.secondsToSwitch = scheduleOn_sec.value() - phase; // ON -> OFF
        }
        else
        {
            r.isOn = false;
            r.secondsToSwitch = period - phase; // OFF -> ON
        }

        // determine time to next in H:M:S
        uint32_t t = r.secondsToSwitch;
        r.h = (uint8_t)(t / 3600);
        t %= 3600;
        r.m = (uint8_t)(t / 60);
        r.s = (uint8_t)(t % 60);
        return r;
    }

    // update with current light state
    void update()
    {
        update(state);
    }

    // update hardware based on settings
    void update(Tstate::e _lightState)
    {
        // light
        bool light = false;
        if (_lightState == Tstate::off)
        {
            // off
            if (scheduleInfo != "")
                scheduleInfo = "";
        }
        else if (_lightState == Tstate::on)
        {
            // on
            light = true;
            if (scheduleInfo != "")
                scheduleInfo = "";
        }
        else if (_lightState == Tstate::schedule)
        {
            // schedule
            if (!Time.isValid())
            {
                // don't have valid time yet - keep off and reschedule check
                if (scheduleInfo != "pending")
                    scheduleInfo = "pending";
                FscheduleTimer.start(1000);
            }
            else
            {

                // determine schedule
                Schedule s = calculateSchedule();
                light = s.isOn;
                const dtypes::string next = (s.isOn) ? "off:" : "on:";

                // when to scheck back in?
                if (s.h == 0 && s.m == 0)
                {
                    scheduleInfo = next + dtypes::string(s.s) + "s";
                    FscheduleTimer.start(1000); // once a second
                }
                else if (s.h == 0)
                {
                    scheduleInfo = next + dtypes::string(s.m) + "m" + dtypes::string(s.s) + "s";
                    FscheduleTimer.start(1000); // once a second
                }
                else
                {
                    scheduleInfo = next + dtypes::string(s.h) + "h" + dtypes::string(s.m) + "m";
                    FscheduleTimer.start((s.s + 1) * 1000); // once we're down a minute
                }
            }
        }

        // update light
        int pwm = intensity_percent * 255 / 100;
        (light) ? analogWrite(pin.value(), pwm) : analogWrite(pin.value(), 0);
    }

public:
    TtimedSwitch()
    {
        // setup
        on(sdds::setup())
        {
            pin.signalEvents();
        };

        // change output pin
        on(pin)
        {
            pinMode(pin.value(), OUTPUT);
        };

        // timer
        on(FscheduleTimer)
        {
            update();
        };

        // light action events
        on(action)
        {
            // stop if no action
            if (action == Taction::___)
                return;

            // process action
            if (action == Taction::on)
            {
                state = Tstate::on;
                update();
            }
            else if (action == Taction::off)
            {
                state = Tstate::off;
                update();
            }
            else if (action == Taction::schedule)
            {
                state = Tstate::schedule;
                update();
            }

            // reset action
            action = Taction::___;
        };

        // change light intensity
        on(intensity_percent)
        {
            if (intensity_percent == 0)
                intensity_percent = 1;
            else if (intensity_percent > 100)
                intensity_percent = 100;
            else
                update();
        };

        // schedule
        on(scheduleOn_sec)
        {
            if (scheduleOn_sec == 0)
                scheduleOn_sec = 1;
            else
                update();
        };
        on(scheduleOff_sec)
        {
            if (scheduleOff_sec == 0)
                scheduleOff_sec = 1;
            else
                update();
        };
        on(scheduleOnStart_HHMM)
        {
            if (scheduleOnStart_HHMM > 2359)
                scheduleOnStart_HHMM = 0;
            else
                update();
        };
    }
};

// self-describing data structure (SDDS) tree
class TsddsTree : public TmenuHandle
{
public:
    sdds_var(TtimedSwitch, timedSwitch);
    TsddsTree() {}
} sddsTree;

// serial spike for communication via serial (with baud rate)
#include "uSerialSpike.h"
TserialSpike serialSpike(sddsTree, 115200);

// particle spike for particle communication (with sdds name and version)
#include "uParticleSpike.h"
static TparticleSpike particleSpike(sddsTree, "timedSwitch", 00101);

// setup
void setup()
{
    // optional: any other configuration changes
    pinMode(D7, INPUT);

    // setup particle spike
    particleSpike.setup();
}

// loop
void loop()
{
    // handle all events
    TtaskHandler::handleEvents();
}
