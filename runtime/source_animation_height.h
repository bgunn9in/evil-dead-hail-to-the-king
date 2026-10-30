#pragma once
#include "source_playback_pool.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace edactions {
// PC4060e0 / DC8c061b20: one deferred height register, outside the50 slots.
struct AnimationHeight {
    // shortcut: owner uses actor identity; verify native pointer reuse before supporting other height-animated actor types.
    PlaybackOwner owner{};
    float value=0;
    bool pending=false;
    int32_t centimeters(bool singlePrecision) const {
        const double cm=singlePrecision?double(value*100.0f):double(value)*100.0;
        if(!std::isfinite(cm)||cm<std::numeric_limits<int32_t>::min()||cm>std::numeric_limits<int32_t>::max())
            throw std::runtime_error("Invalid deferred animation height");
        return int32_t(cm);
    }
    template<class RootDelta,class SetHeight>
    void beforeFrame(PlaybackOwner actor,uint32_t flags,int frame,int start,int end,
                     bool singlePrecision,RootDelta rootDelta,SetHeight setHeight) {
        if(frame==start&&(flags&0x20))setHeight(0);
        if(pending&&owner==actor){setHeight(centimeters(singlePrecision));pending=false;}
        if(frame==end){
            if(flags&0x10){owner=actor;value=rootDelta();pending=true;}
            if(flags&0x40){owner=actor;value=0;pending=true;}
        }
    }
};
} // namespace edactions
