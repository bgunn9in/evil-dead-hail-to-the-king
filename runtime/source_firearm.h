#pragma once
#include "source_actor_math.h"
#include <algorithm>
#include <functional>
#include <set>

// Player-only native405B40. The raw animation line is a visual argument;
// this producer never reads either endpoint and does not consume random values.
namespace edfirearm {
struct Actor {
    int type=0,index=0,nativeSlot=-1;uint32_t flags=0;
    edactormath::Vector local{};float yaw=0;int radiusCm=0;
};
struct Hit {int targetType=0,targetIndex=0,sourceType=0,sourceIndex=0,event=5,selector=-1;};
using LineOfSight19=std::function<bool(const Actor&,const Actor&)>;
inline void validate(const Actor& actor) {
    edactormath::finite(actor.local);
    if(actor.type<0||actor.type>65535||actor.index<0||actor.index>65535||actor.nativeSlot< -1||actor.nativeSlot>=13||actor.radiusCm<0||!std::isfinite(actor.yaw))
        throw std::runtime_error("Invalid source firearm actor");
}
inline int distance(const Actor& from,const Actor& target) {
    // 421BA0 rounds the subtracted components before421B60's length.
    const float dx=float(double(target.local[0])-from.local[0]),dz=float(double(target.local[2])-from.local[2]);
    const double squared=double(dx)*dx+double(dz)*dz;
    double value=squared==0?1:std::sqrt(squared);
    const float radius=float(double(target.radiusCm)*edactormath::Centimeter); // 403800.
    if(value>radius)value-=radius;
    value*=100;
    if(!std::isfinite(value)||value>std::numeric_limits<int>::max())throw std::runtime_error("Source firearm distance exceeds native range");
    return int(value);
}
inline bool facing15(const Actor& from,const Actor& target) {
    const float dx=float(double(target.local[0])-from.local[0]),dz=float(double(target.local[2])-from.local[2]);
    double angle=std::atan2(double(dz),double(dx));if(angle<0)angle+=edactormath::Tau;
    angle-=edactormath::nativeHeading(from.yaw);
    if(angle < -double(edactormath::Pi))angle+=edactormath::Tau;
    if(angle > edactormath::Pi)angle-=edactormath::Tau;
    const float threshold=float(15*double(.01745329238474369f));
    return std::abs(int(angle))<threshold; // 404A70/451F70: integer abs, not fabs.
}
inline std::vector<Hit> scan(const Actor& shooter,const std::vector<Actor>& allocation,const LineOfSight19& lineOfSight19={}) {
    validate(shooter);if(shooter.type!=0)return {};
    std::vector<const Actor*> ordered;std::set<int> slots;
    for(const auto& actor:allocation) {
        // 405B54..405CA6 scans thirteen native pointer slots, including holes.
        validate(actor);if(actor.nativeSlot<0)continue;
        if(!slots.insert(actor.nativeSlot).second)throw std::runtime_error("Duplicate source firearm registration slot");
        ordered.push_back(&actor);
    }
    std::sort(ordered.begin(),ordered.end(),[](auto* a,auto* b){return a->nativeSlot<b->nativeSlot;});
    std::vector<Hit> hits;
    for(const auto* target:ordered) {
        if(target->type==0||!(target->flags&1)||distance(shooter,*target)>=10000||!facing15(shooter,*target))continue;
        // 412F80 receives both actor-local positions and class4. There is no
        // visibility shortcut or wall test for any other original target type.
        if(target->type==19) {
            if(!lineOfSight19)throw std::runtime_error("Missing source firearm type19 LOS");
            if(!lineOfSight19(shooter,*target))continue;
        }
        hits.push_back({target->type,target->index,shooter.type,shooter.index,5,-1});
    }
    return hits;
}
} // namespace edfirearm
