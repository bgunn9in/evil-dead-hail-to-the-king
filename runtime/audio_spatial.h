#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace edaudiospatial {
inline int Percent(const std::array<float,3>& listener,const std::array<float,3>& source,
                   int requested,int flags,float voiceRadius,float listenerRadius=20.f) {
    for(float value:listener)if(!std::isfinite(value))throw std::runtime_error("Non-finite sound listener");
    for(float value:source)if(!std::isfinite(value))throw std::runtime_error("Non-finite sound position");
    if(!std::isfinite(voiceRadius)||!std::isfinite(listenerRadius))throw std::runtime_error("Non-finite sound radius");
    int percent=std::clamp(requested,0,100);
    if(flags&4){
        //4301d7 stores DX;4301e4 stores DZ but retains its wide value until the multiply.
        const float dx=float(double(listener[0])-source[0]);const double dz=double(listener[2])-source[2];
        const float distance=float(std::sqrt(double(dx)*dx+dz*float(dz)));
        const float radius=(flags&8)?voiceRadius:listenerRadius;
        if((flags&8)?distance>radius:distance>=radius)percent=0;
        else{
            // The PC executes0/0 for flag8 at zero radius/distance; keep this unverified case explicit.
            if(radius==0)throw std::runtime_error("Undefined original zero-radius spatial sound");
            percent=int((double(radius)-distance)*percent/radius);
        }
    }
    return (flags&0x4000)?0:percent;
}
} // namespace edaudiospatial
