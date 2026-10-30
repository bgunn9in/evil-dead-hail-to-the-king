#pragma once
#include "source_actor_math.h"

// Native4102B0, used by the original kind8 shell modifier7.
namespace edparticle {
using Vector=edactormath::Vector;
struct Actor {uint32_t flags=0;Vector local{};float cachedFloor=0;};
using PhysicalActors=std::array<Actor,13>; // Context463090+938..96C, never arena allocation order.
inline Vector repel(Vector position,Vector velocity,const PhysicalActors& actors) {
    edactormath::finite(position);edactormath::finite(velocity);
    for(const auto& actor:actors) {
        if(!(actor.flags&1)||(actor.flags&8))continue;
        edactormath::finite(actor.local);if(!std::isfinite(actor.cachedFloor))throw std::runtime_error("Non-finite particle repeller floor");
        const float worldY=float(double(actor.local[1])+actor.cachedFloor);
        const float dx=float(double(actor.local[0])-position[0]),dz=float(double(actor.local[2])-position[2]);
        const double dy=double(worldY)-position[1],wideDz=double(actor.local[2])-position[2];
        const double squared=(wideDz*dz+dy*dy)+double(dx)*dx;
        if(squared>=.15000000596046448f)continue;
        const float softened=float(squared+9.999999747378752e-06f);
        const double scale=3.f/std::sqrt(double(softened));
        velocity[0]=float(double(velocity[0])+scale*.004999999888241291f*(dx<0?1.:-1.));
        velocity[1]=float(double(velocity[1])+scale*.0010000000474974513f);
        velocity[2]=float(double(velocity[2])+scale*.004999999888241291f*(dz<0?1.:-1.));
    }
    edactormath::finite(velocity);return velocity;
}
} // namespace edparticle
