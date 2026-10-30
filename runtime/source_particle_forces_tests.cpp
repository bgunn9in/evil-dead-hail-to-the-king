#include "source_particle_forces.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

int main(){
    edparticle::PhysicalActors actors{};actors[0]={1,{{0,0,0}},0};
    const auto center=edparticle::repel({{0,0,0}},{},actors);
    // Source nonnegative delta chooses-1, including exact zero; no invented unit vector.
    assert(center[0]<0&&center[2]==center[0]&&center[1]>0);
    assert(center[0]==-4.7434163093566895f&&center[1]==.9486833810806274f);
    const auto near=edparticle::repel({{.1f,.2f,-.1f}},{{.01f,-.02f,.03f}},actors);
    assert(near[0]>.01f&&near[1]>-.02f&&near[2]<.03f);
    const auto far=edparticle::repel({{0,1,0}},{},actors);assert((far==edparticle::Vector{}));
    const float edge=std::sqrt(.15f);
    assert((edparticle::repel({{std::nextafter(edge,std::numeric_limits<float>::infinity()),0,0}},{},actors)==edparticle::Vector{}));
    assert(edparticle::repel({{std::nextafter(edge,0.f),0,0}},{},actors)[0]>0);
    actors[0].flags=9;assert((edparticle::repel({},{},actors)==edparticle::Vector{}));
    actors[0].flags=0;assert((edparticle::repel({},{},actors)==edparticle::Vector{}));
    actors[0].flags=0x41;assert(edparticle::repel({},{},actors)==center); // Frozen40 does not suppress this modifier.
    actors[0]={1,{{0,2,0}},-2};assert(edparticle::repel({},{},actors)==center); // LOCAL Y+cached floor.
    actors[12]=actors[0];const auto doubled=edparticle::repel({},{},actors);
    assert(doubled[0]==center[0]*2&&doubled[1]==center[1]*2&&doubled[2]==center[2]*2);
    std::cout<<"source4102B0 shell repulsion: physical13 slots, flags, local+floor, strict radius and zero-direction quirk passed\n";
}
