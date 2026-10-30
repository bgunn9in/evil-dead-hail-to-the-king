#include "doors.h"
#undef NDEBUG
#include <cassert>

int main() {
    Door door; door.marker={0,0,1}; door.facingArg=40;
    assert(facingDoor(door,{0,0},0.999f));
    assert(!facingDoor(door,{0,0},1.0f));
    assert(facingDoor(door,{0,0},-0.999f));
    assert(!facingDoor(door,{0,0},-1.0f));
    Door coincident=door;coincident.marker={0,0,0};
    assert(!facingDoor(coincident,{0,0},0));assert(facingDoor(coincident,{0,0},1.57079633f));
    door.delayTicks=30; door.maxTicks=45; door.guardTicks=45;
    GateState state; state.pending=true;
    for (unsigned tick=0;tick<29;++tick) { tickGate(state,door,false); assert(!state.enabled); }
    tickGate(state,door,false); assert(state.enabled&&!state.pending);
    state={false,true,0};
    for (unsigned tick=0;tick<44;++tick) { tickGate(state,door,true); assert(!state.enabled); }
    tickGate(state,door,true); assert(state.enabled&&!state.pending);
    state={true,true,0}; tickGate(state,door,true); assert(state.enabled);
    assert(!insideDoorRegion(door.interaction,{},{}));
}
