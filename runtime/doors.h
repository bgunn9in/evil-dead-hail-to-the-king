#pragma once
#include "ground.h"
#include "triggers.h"
#include <string>

struct Door {
    unsigned portalIndex=0;
    CameraTrigger interaction,portal;
    GroundPoint marker{};
    float facingArg=0;
    unsigned soundId=0,delayTicks=0,maxTicks=0,guardTicks=0;
    std::string destination;
    unsigned destinationCamera=0;
    GroundPoint spawn{};
    float yaw=0;
};
inline bool insideDoorRegion(const CameraTrigger& region,const TriggerGrid& grid,Vec2 position) {
    if (region.points.empty()) return false;
    const uint32_t mask=triggerMask(grid,position)&region.mask;
    return (mask&0xffffu)&&(mask&0xffff0000u)&&insideTrigger(region,position);
}
inline bool facingDoor(const Door& door,Vec2 position,float yaw) {
    constexpr double pi=3.14159265358979323846;
    const double x=double(door.marker.x)-position.x,z=double(door.marker.z)-position.z;
    const double angle=(x==0&&z==0)?pi/2:std::atan2(x,z);
    const double difference=std::remainder(double(yaw)-angle,2*pi);
    // Source facing test truncates wrapped radians before comparing with degrees.
    return std::abs(std::trunc(difference))<std::abs(door.facingArg)*pi/180;
}
struct GateState {
    bool enabled=false,pending=false;
    unsigned elapsed=0;
};
inline void tickGate(GateState& state,const Door& door,bool voicePlaying) {
    if (!state.pending) return;
    ++state.elapsed;
    if ((!voicePlaying&&state.elapsed>=door.delayTicks)||state.elapsed>=door.maxTicks) {
        state.enabled=true; state.pending=false;
    }
}
