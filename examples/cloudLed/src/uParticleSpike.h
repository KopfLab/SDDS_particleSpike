#ifndef PARTICLESPIKE_H
#define PARTICLESPIKE_H

#include "uPlainCommHandler.h"
#include "uParticlePublish.h"
#include "Particle.h"

class TparticleSpike : public TstringStream{
  private:
    TplainCommHandler FcommHandler;
    #ifdef SYSTEM_VERSION_630
    CloudEvent FcloudEvent; // cloud event handler, only availabe since particle device OS 6.3.0
    #endif
    const char* Fsend; // publish event name 
    unsigned long FlastSend = 0; // last time we published

    TpublishState& FpublishState; // reference to publish state
    Tuint32& FpublishInterval; // reference to publish interval

    char FreceiveLog[particle::protocol::MAX_FUNCTION_ARG_LENGTH] = "[]\0";
    char FsendLog[particle::protocol::MAX_FUNCTION_ARG_LENGTH] = "[]\0";

  public:
    TparticleSpike(TmenuHandle* _ds, const char* _receive, const char* _send, TpublishState& _publishState, Tuint32& _publishInterval) 
      : TstringStream()
      , FcommHandler(_ds,this)
      , Fsend(_send)
      , FpublishState(_publishState)
      , FpublishInterval(_publishInterval)
    {
        // handle receiving
        Particle.function(_receive, &TparticleSpike::handleMessage, this);

        // handle sending
        #ifdef SYSTEM_VERSION_630
        FcloudEvent.name(_send);
        #endif

        // logs
        Particle.variable(_receive, FreceiveLog);
        Particle.variable(_send, FsendLog);
    }    

    // SENDING

    void flush() override { 
      sendMessage(data());
      TstringStream::clear();
    }

    void sendMessage(const char* _msg){
        // send cloud data
        // FIXME: this should
        //  - collect data bursts (over x seconds) into a single event
        //  - when offline, keep storing event data in memory and/or on flash memory
        //  - when online, manage (re) publishing events in memory/flash and delete from the queue once sucessful
        //  - add device name information here? somehow need to get this unique identifier in the path
        //  - how can the channels be linked to the modules if we don't have the structure stored? it seems almost like we need to store the entire structure in the databse when subscribing to a logger
        //  - NOT deal with the publishing interval! that should happen on the sdds_var side I think so the ParticleSpike can just do its own thing and be done wiht it

        // log in Particle variable
        logMessage(_msg, FsendLog);

        // check if state is to just forget all messages
        if (FpublishState == TpublishState::e::forget) {
            Log.trace("not publishing to cloud or storing: %s", _msg);
            return;
        }

        if (FpublishState == TpublishState::e::publish && Particle.connected() && millis() - FlastSend > FpublishInterval) {
            // publishing, connected, and it's been long enough since the last publish
            Log.trace("publishing to cloud: %s", _msg);
            #ifdef SYSTEM_VERSION_630
                // event based publish (non-blocking)
                FcloudEvent.data(_msg);
                Particle.publish(FcloudEvent);
            #else
                // legacy publish (blocking) - would need a wrapper (or not supported, okay for SK)
                bool bResult = Particle.publish(Fsend, _msg);
                (bResult) ?
                    Log.info("publish succeeded") :
                    Log.info("publish failed");
            #endif
            FlastSend = millis();
        } else {
            // storing, disconnected, or too early --> store instead and revisit later
            // FIXME: this is NOT implemented at all
            Log.trace("storing in memory/on flash: %s", _msg);
            return;
        }
    }

    // RECEIVING

    int handleMessage(String _msg){
        // receive cloud data
        // FIXME: this should
        //  - pull out params like param= and note= ?
        //  - process multiple commands at once (\n separated) - should that be possible? maybe easier if not
        //  - log the command as a json publish (to a specific webhook or with a tag about what datatype it is?)
        //  - capture error codes from each command (is that possible?)
        //  - return the path code if successful so that the caller knows what path to expect a response on? (and the error codes as negative?)
        //  - auto-save setting changes, subscription changes, etc. (actually, that should probably happen on the client side by sending params.action=save explicitly)
        Log.trace("received from cloud: %s", _msg.c_str());
        logMessage(_msg.c_str(), FreceiveLog);
        FcommHandler.handleMessage(_msg.c_str());
        return(0);
    }

    // VARIABLES

    protected:

    void logMessage(const char* _msg, char* _var) {
        // append to call log
        Variant msg_log = Variant::fromJSON(_var);
        Variant msg = Variant(_msg);
        msg_log.append(msg);
        size_t msg_log_size = msg_log.toJSON().length();
        while (msg_log_size  >= particle::protocol::MAX_FUNCTION_ARG_LENGTH && !msg_log.isEmpty()) {
            // remove the oldest entries until they fit
            msg_log.removeAt(0);
            msg_log_size = msg_log.toJSON().length();
        }
        if (msg_log.isEmpty()) {
            msg_log.append(
                String::format("msg too long (%d characters), %d allowed", 
                    msg.toJSON().length(), particle::protocol::MAX_FUNCTION_ARG_LENGTH)
            );
        }
        // assign call log
        snprintf(_var, particle::protocol::MAX_FUNCTION_ARG_LENGTH, "%s", msg_log.toJSON().c_str());
    }

};

#endif