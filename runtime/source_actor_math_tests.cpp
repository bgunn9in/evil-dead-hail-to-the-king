#include "source_actor_math.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

int main() {
    using namespace edactormath;
    const auto close=[](const Vector& actual,const Vector& expected) {
        for(unsigned i=0;i<3;++i)assert(std::abs(actual[i]-expected[i])<3e-7f);
    };
    const Euler zero{{0,0,0}},east{{0,HalfPi,0}},tilted{{.2f,1.f,-.4f}};
    close(nativeTransform({{0,HalfPi,0}},{{1,0,0}}),{{0,0,1}});
    close(nativeTransform({{0,HalfPi,0}},{{0,0,1}}),{{-1,0,0}});
    close(modelTransform(east,{{0,0,1}}),{{1,0,0}});
    close(nativeTransform(tilted,{{.3f,-.7f,1.2f}}),{{-.6956846714f,-.7801392078f,.9630190134f}});
    close(modelTransform(tilted,{{.3f,-.7f,1.2f}}),{{.5930292606f,-1.2544080019f,.3078583181f}});
    close(jointWorld(zero,{{1,2,3}},{{.5f,1,.5f}}),{{-.5f,3,3.5f}});
    close(jointWorld(east,{{1,2,3}},{{.5f,1,.5f}},{{20,-50,40}}),{{3.9f,2.5f,1.7f}});
    close(jointWorld({{HalfPi,0,0}},{{1,2,3}},{{0,0,0}}),{{-1,-3,2}});
    assert(nativeHeading(HalfPi)==0);assert(nativeHeading(-HalfPi)==Pi);
    assert(modelYawFromHeading(0)==HalfPi);assert(modelYawFromHeading(HalfPi)==0);
    close(pendingToWorld({{0,0,1}},{{0,modelYawFromHeading(Pi),0}},6),{{-1,0,0}});
    assert(normalizeAngle(-.5e-6)==0);assert(normalizeAngle(double(Tau)+.5e-6)==0);
    close(moveForwardPending(20,zero,6),{{0,0,.2f}});
    close(moveForwardPending(20,zero,0),{{0,0,.2f}});
    close(pendingToWorld(moveForwardPending(20,east,6),east,6),{{.2f,0,0}});
    close(moveStrafePending(20,zero,6),{{.2f,0,0}});
    close(moveStrafePending(20,zero,0),{{-.2f,0,0}});
    close(moveStrafePending(-20,zero,0),{{.2f,0,0}});
    close(pendingToWorld(moveStrafePending(20,east,6),east,6),{{0,0,-.2f}});
    close(moveStrafePending(20,east,0),{{0,0,.2f}});
    close(moveVerticalPending(-25),{{0,-.25f,0}});
    close(losDirectionOffset(20,40,zero,0),{{.2f,0,.4f}});
    close(losDirectionOffset(20,40,zero,6),{{-.2f,0,.4f}});
    close(losDirectionTarget({{10,3,20}},20,40,zero,6),{{9.8f,3,20.4f}});
    Vector accumulated{{1,2,3}};appendMovement(accumulated,20,40,-25,zero,6);
    close(accumulated,{{1.4f,1.75f,3.2f}});
    // A queued local move follows the heading at physics consumption.
    close(pendingToWorld(moveForwardPending(20,zero,6),east,6),{{.2f,0,0}});
    assert(pendingToWorld({{.1f,.2f,.3f}},east,0)==Vector({{.1f,.2f,.3f}}));
    assert(distanceXZCentimeters({{0,100,0}},{{0,-100,3.01f}})==300);
    assert(distanceXZCentimeters({{1,100,2}},{{1,-100,2}})==100);
    assert(nearestMarker({{0,0,0}},{{{0,2,0}},{{1,0,0}},{{-1,0,0}}},0,101)==1);
    assert(nearestMarker({{0,0,0}},{{{0,0,0}},{{0,0,0}}},0,0)==0);
    // Source squared float bounds can exclude an exact1-world-unit marker.
    assert(nearestMarker({{0,0,0}},{{{1,0,0}}},0,100)==-1);
    // Candidate remains wide while best is roundedfloat: equal points canreplace.
    assert(nearestMarker({{0,0,0}},{{{.2f,0,.3f}},{{.2f,0,.3f}}},0,100)==1);
    bool rejected=false;try{moveForwardPending(20,{{0,INFINITY,0}},6);}catch(const std::exception&){rejected=true;}assert(rejected);
    assert(objectContact({{.6f,0,0}},{{-.05f,0,0}},{{0,0,0}},.9f));
    assert(!objectContact({{.6f,0,0}},{{.05f,0,0}},{{0,0,0}},.9f));
    assert(!objectContact({{.95f,0,0}},{{-.1f,0,0}},{{0,0,0}},.9f));
    assert(!objectContact({{.6f,0,0}},{{0,0,.05f}},{{0,0,0}},.9f));
    assert(objectContact({{.6f,0,0}},{{0,0,0}},{{0,0,0}},.9f));
    close(slideContact({{-.05f,0,.02f}},{{-1,0,0}}),{{0,0,.02f}});
    close(slideContact({{-.2f,0,.03f}},{{-1,0,0}}),{{.02f,0,.03f}});
    close(slideContact({{-.2f,.009f,.03f}},{{1,0,0}}),{{.02f,0,.03f}});
    close(slideContact({{-.2f,.01f,.03f}},{{-1,0,0}}),{{.02f,.01f,.03f}});
    std::cout<<"Native actor movement, vector transforms and LOS direction passed\n";
}
