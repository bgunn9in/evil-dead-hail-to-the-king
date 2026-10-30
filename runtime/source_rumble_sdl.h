#pragma once
#include "source_rumble.h"
#include <SDL3/SDL.h>

namespace edrumble {
inline bool available(SDL_Gamepad* pad,int port=0) {
    return port==0&&pad&&SDL_GamepadConnected(pad)&&
        SDL_GetBooleanProperty(SDL_GetGamepadProperties(pad),SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN,false);
}
inline void silence(SDL_Gamepad* pad) {
    if(available(pad))SDL_RumbleGamepad(pad,0,0,0);
}
class Feedback {
    uint16_t desired=0,submitted=0;
    Chainsaw previous;
    bool observed=false;
    uint16_t burst=0;
    Uint64 burstUntil=0;
    void pulse(uint16_t strength,Uint64 duration,Uint64 now){
        if(now>=burstUntil)burst=0;
        burst=std::max(burst,strength);burstUntil=std::max(burstUntil,now+duration);
    }
public:
    void command(Command value){if(value.port==0)desired=modernMotor(value).amplitude;}
    void reset(SDL_Gamepad* pad){silence(pad);desired=submitted=burst=0;burstUntil=0;previous={};observed=false;}
    void update(SDL_Gamepad* pad,bool active,Chainsaw saw={},Uint64 now=SDL_GetTicks()) {
        if(!available(pad)){desired=submitted=burst=0;burstUntil=0;previous=saw;observed=true;return;}
        if(!saw.enabled)desired=0;
        if(!active||!saw.enabled||!saw.gameplay||!saw.running){burst=0;burstUntil=0;}
        else if(observed){
            const bool same=saw.weapon==previous.weapon;
            if(!same||!previous.running)pulse(36044,200,now);
            if(same&&saw.phase==3&&(previous.phase!=3||saw.revTimer>previous.revTimer))pulse(42598,180,now);
            if(same&&saw.attackPitch>previous.attackPitch)pulse(49151,140,now);
        }
        previous=saw;observed=true;
        const uint16_t engine=active&&saw.enabled&&saw.gameplay&&saw.running?
            std::max(uint16_t(saw.revving?19660:9830),now<burstUntil?burst:uint16_t(0)):0;
        const auto strength=active&&saw.enabled?std::max(desired,engine):uint16_t(0);
        if(!strength&&!submitted)return;
        // Keep original impulses; the modern engine layer never writes JPC state.
        if(SDL_RumbleGamepad(pad,strength,strength,strength?100:0))submitted=strength;
    }
};
}
