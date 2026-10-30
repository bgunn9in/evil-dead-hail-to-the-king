#include "source_firearm.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

int main() {
    using edfirearm::Actor;
    const Actor ash{0,0,0,1,{{0,0,0}},0,80};
    const Actor near{4,0,2,1,{{0,0,1.5f}},0,30};
    assert(edfirearm::distance(ash,near)==120&&edfirearm::facing15(ash,near));
    auto coincident=near;coincident.local={{0,0,0}};assert(edfirearm::distance(ash,coincident)==70);
    auto farther=near;farther.index=1;farther.nativeSlot=1;farther.local[2]=8;
    auto hits=edfirearm::scan(ash,{near,farther,ash});
    assert(hits.size()==2&&hits[0].targetIndex==1&&hits[1].targetIndex==0);
    for(const auto& h:hits)assert(h.sourceType==0&&h.sourceIndex==0&&h.event==5&&h.selector==-1);
    // Original bit1 gate permits hidden/frozen actors at the producer. The
    // recipient's message dispatcher separately handles visibility and busy.
    auto hidden=near;hidden.flags=1|8|64;assert(edfirearm::scan(ash,{hidden}).size()==1);
    hidden.flags=8;assert(edfirearm::scan(ash,{hidden}).empty());
    auto rear=near;rear.local[2]=-1;assert(edfirearm::scan(ash,{rear}).empty());
    for(float angle:{.999f,1.001f,-.999f,-1.001f}) {
        auto cone=near;cone.local={{float(std::sin(angle)*8),0,float(std::cos(angle)*8)}};
        assert(edfirearm::facing15(ash,cone)==(std::abs(angle)<1));
    }
    auto boundary=near;boundary.radiusCm=0;boundary.local[2]=100;
    assert(edfirearm::scan(ash,{boundary}).empty());boundary.local[2]=99.99f;
    assert(edfirearm::scan(ash,{boundary}).size()==1);
    auto beyond=near;beyond.nativeSlot=-1;assert(edfirearm::scan(ash,{beyond}).empty());
    beyond.nativeSlot=12;assert(edfirearm::scan(ash,{beyond}).size()==1);
    unsigned los=0;auto special=near;special.type=19;special.nativeSlot=3;
    assert(edfirearm::scan(ash,{near,special},[&](const auto&,const auto&){++los;return false;}).size()==1);
    assert(los==1); // The ordinary victim does not ask for LOS.
    bool rejected=false;try{edfirearm::scan(ash,{special});}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    rejected=false;try{edfirearm::scan(ash,{near,near});}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    beyond.nativeSlot=13;rejected=false;try{edfirearm::scan(ash,{beyond});}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    auto npc=ash;npc.type=9;assert(edfirearm::scan(npc,{near}).empty());
    auto invalid=near;invalid.local[0]=std::numeric_limits<float>::quiet_NaN();
    rejected=false;try{edfirearm::scan(ash,{invalid});}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    std::cout<<"source405B40 allocation13/order/flags/radius/cone/type19/message: passed\n";
}
