#include "ground.h"
#include "triggers.h"
#undef NDEBUG
#include <cassert>

static GroundPolygon rectangle(float x0, float z0, float x1, float z1, bool inside = false) {
    return {inside, {{x0,z0},{x1,z0},{x1,z1},{x0,z1}}};
}
int main() {
    GroundPolygon circle;circle.points={{0,0}};circle.circleRadius=0.45f;
    assert(!clearGround({circle},{0.5f,0},0.25f));
    const auto circleStop=moveGround({circle},{-2,0},{4,0},0.25f);
    assert(circleStop.x<=-0.6999f&&circleStop.x>=-0.701f&&clearGround({circle},circleStop,0.25f));
    const auto circleRecovery=recoverGround({circle},{0,0},0.25f);
    assert(clearGround({circle},circleRecovery,0.25f));
    circle.enabled=false;
    assert(clearGround({circle},{0,0},0.25f));
    assert(moveGround({circle},{-2,0},{4,0},0.25f).x==2);
    const std::vector<FloorTriangle> ramp{{{0,0,0},{2,2,0},{0,0,2}}};
    assert(std::abs(*floorHeight(ramp,{0.5f,0.5f},0)-0.5f)<0.00001f);
    assert(!floorHeight(ramp,{3,3},0));
    assert(floorHeight(ramp,{1,1},0));
    const std::vector<GroundPolygon> openBoundary{{false,{{0,-2},{0,2}},true}};
    assert(clearGround(openBoundary,{-1,0},0.25f));
    assert(clearGround(openBoundary,{1,0},0.25f));
    assert(moveGround(openBoundary,{-1,0},{2,0},0.25f).x<=-0.25f);
    const float radius = 0.25f;
    const std::vector<GroundPolygon> wall{rectangle(0,-10,0.01f,10)};
    const Vec2 slide = moveGround(wall, {-2,0}, {4,2}, radius);
    assert(slide.x <= -radius && std::abs(slide.z - 2) < 0.001f);
    assert(clearGround(wall, slide, radius));
    const Vec2 fast = moveGround(wall, {-100,0}, {1000,0}, radius);
    assert(fast.x <= -radius && fast.x > -radius - 0.001f);
    const std::vector<GroundPolygon> room{rectangle(-2,-2,2,2,true)};
    const Vec2 corner = moveGround(room, {0,0}, {20,20}, radius);
    assert(corner.x <= 2-radius && corner.z <= 2-radius && clearGround(room,corner,radius));
    const auto escaped = moveGround(wall, {0.005f,0}, {}, radius);
    assert(clearGround(wall, escaped, radius));
    const auto recovered = moveGround(room, {20,0}, {}, radius);
    assert(clearGround(room, recovered, radius));
    const std::vector<GroundPolygon> endpoint{rectangle(0,0,0.01f,1)};
    const Vec2 endpointHit = moveGround(endpoint, {-1,-1}, {2,2}, radius);
    assert(clearGround(endpoint, endpointHit, radius));
    const Vec2 single = moveGround(wall, {-1,0}, {2,1}, radius);
    Vec2 split{-1,0};
    for (unsigned i=0;i<120;++i) split=moveGround(wall,split,{2.0f/120,1.0f/120},radius);
    assert(length(single-split)<0.001f);
    auto reversed = room;
    std::reverse(reversed[0].points.begin(),reversed[0].points.end());
    const Vec2 reverseHit=moveGround(reversed,{0,0},{20,20},radius);
    assert(length(reverseHit-corner)<0.001f);
    // AK01 volume33: nearest-point and signed-line distances round differently.
    const std::vector<GroundPolygon> roundedContact{{false,{
        {-5.400485516f,-0.1506441832f},{-5.400485516f,2.569355011f},
        {-3.520485402f,2.989354610f},{-3.820485115f,-0.1506454945f}}}};
    const Vec2 contactStart{-4.140582561f,-0.4006452262f};
    const Vec2 contactDelta{0.003086357377f,0.002625627210f};
    assert(clearGround(roundedContact,contactStart,radius));
    const Vec2 contactEnd=moveGround(roundedContact,contactStart,contactDelta,radius);
    assert(clearGround(roundedContact,contactEnd,radius));
    assert(contactEnd.x>contactStart.x && contactEnd.z<contactStart.z+ContactSkin);
    const Vec2 capCenter{-3.820485115f,-0.1506454945f};
    const Vec2 capStart{-3.64370846748f,-0.327422201633f}, capDelta{-0.003f,0.003f};
    GroundHit capHit;
    hitCircle(capStart,capDelta,capCenter,radius,capHit);
    assert(capHit.found && capHit.time==0 && dot(capDelta,capHit.normal)<0);
    GroundHit leavingCap;
    hitCircle(capStart,capDelta*-1,capCenter,radius,leavingCap);
    assert(!leavingCap.found);
    assert(clearGround(roundedContact,moveGround(roundedContact,capStart,capDelta,radius),radius));
    {
        // BM01 source2/66: seven point-contact projections leave a tiny tangent
        // remainder whose float addition used to cross source66 by one ULP.
        const std::vector<GroundPolygon> pointCorner{
            {false,{{-10.4391651154f,8.11288452148f},{-11.3391647339f,9.25288581848f},{-8.89916419983f,10.2328853607f},{-8.17916488647f,9.01288509369f}}},
            {false,{{-8.38147163391f,4.64325237274f},{-11.7814712524f,2.55322957039f},{-9.93147182465f,8.39325523376f},{-8.26147079468f,5.69322776794f}}}
        };
        const Vec2 start{-9.89266586304f,8.33051395416f},delta{-.017008099705f,.0393844470382f};
        assert(clearGround(pointCorner,start,0));
        const auto stopped=moveGround(pointCorner,start,delta,0);
        assert(clearGround(pointCorner,stopped,0)&&stopped.x==start.x&&stopped.z==start.z);
        // The residual safeguard must not create a deadzone for ordinary tiny
        // free motion before any contact (including native idle root motion).
        const Vec2 freeStart{-20,-20},tiny{.000002f,.000003f};
        const auto freeEnd=moveGround(pointCorner,freeStart,tiny,0);
        assert(freeEnd.x==freeStart.x+tiny.x&&freeEnd.z==freeStart.z+tiny.z&&length(freeEnd-freeStart)>0);
    }
    {
        // Exact BT04 source14/15 contact: leaving the solid must retain full motion.
        std::vector<GroundPolygon> walls{
            {false,{{43.06930542f,21.15466881f},{42.56930542f,22.25466919f},{44.76930618f,21.31467056f}}},
            {false,{{41.70929337f,22.41460228f},{41.90929413f,24.65460014f},{43.10929108f,21.75460434f}}},
            {true,{{38.86800003f,28.46419907f},{40.21910095f,18.44109917f},{55.27099991f,20.46999931f},{53.91989899f,30.49309921f}}}};
        const Vec2 start{42.90750122f,22.24226189f},delta{.07995132357f,.03113558702f};
        for(unsigned winding=0;winding<2;++winding){
            if(winding)for(auto& wall:walls)std::reverse(wall.points.begin(),wall.points.end());
            assert(clearGround(walls,start,0));const auto end=moveGround(walls,start,delta,0);
            assert(end.x==start.x+delta.x&&end.z==start.z+delta.z&&clearGround(walls,end,0));
        }
        for(unsigned winding=0;winding<2;++winding){
            auto solid=rectangle(-1,-1,1,1);auto room=rectangle(-1,-1,1,1,true);
            if(winding){std::reverse(solid.points.begin(),solid.points.end());std::reverse(room.points.begin(),room.points.end());}
            const Vec2 outside{std::nextafter(1.f,2.f),0},inside{std::nextafter(1.f,0.f),0};
            const auto leave=moveGround({solid},outside,{.1f,0},0);assert(leave.x==outside.x+.1f&&clearGround({solid},leave,0));
            const auto blocked=moveGround({solid},outside,{-.1f,0},0);assert(blocked.x>=1&&clearGround({solid},blocked,0));
            const auto enter=moveGround({room},inside,{-.1f,0},0);assert(enter.x==inside.x-.1f&&clearGround({room},enter,0));
            const auto edge=moveGround({room},inside,{.1f,0},0);assert(edge.x<=1&&clearGround({room},edge,0));
        }
        GroundPolygon line;line.points={{0,-1},{0,1}};
        for(bool open:{false,true}){line.boundaryOnly=open;
            assert(moveGround({line},{-.1f,0},{.2f,0},0).x<0);
            assert(moveGround({line},{.1f,0},{-.2f,0},0).x>0);
        }
    }
    {
        // Literal native DC P1 geometry: AK01 source52 and BM01 source28.
        // Original SH4 8c088a60 accepts these shared-vertex intersections;
        // normalized line/along rounding previously missed both adjacent edges.
        std::array<GroundPolygon,2> nativeVertex{{
            {false,{{0.7996912599f,7.486958027f},{0.9596911669f,7.626959801f},{2.99969101f,7.626959801f},{0.8196928501f,5.286960125f}}},
            {false,{{-7.524211884f,-14.38288498f},{-5.424211502f,-11.82288361f},{-2.604213476f,-11.32288361f},{-6.624211311f,-15.80287933f}}}
        }};
        const std::array<Vec2,2> starts{{{0.7855491042f,7.501100063f},{-5.424353123f,-11.82274246f}}},deltas{{{0.07071070373f,-0.07071065158f},{0.07071070373f,-0.07071065158f}}};
        for(unsigned i=0;i<nativeVertex.size();++i){
            auto& polygon=nativeVertex[i];polygon.sourceIndex=i==0?52:28;polygon.contactFlags=2;
            const std::vector<GroundPolygon> walls{polygon};
            assert(clearGround(walls,starts[i],0));
            GroundMoveTrace trace;unsigned flags=0;
            const auto stopped=moveGround(walls,starts[i],deltas[i],0,&flags,&trace);
            assert(trace.sweepCount>0&&trace.recoveryCount==0&&!trace.contactLimit);
            assert(trace.sweeps[0].sourceIndex==polygon.sourceIndex&&flags==2);
            assert(clearGround(walls,stopped,0));
            assert(length(stopped-(starts[i]+deltas[i]))>.005f);
        }
    }
    const CameraTrigger box{0,{{0,0},{2,2}},{1}};
    const CameraTrigger quad{2,{{0,0},{0,2},{2,2},{2,0}},{2}};
    assert(insideTrigger(box,{0,1}) && insideTrigger(box,{2,2}));
    assert(!insideTrigger(quad,{0,1}) && !insideTrigger(quad,{2,2}));
    assert(insideTrigger(quad,{1,1}));
    const std::vector<CameraTrigger> triggers{box,quad};
    std::vector<bool> membership(2,false);
    assert(enterTriggers(triggers,{1,1},membership,0)==2);
    assert(enterTriggers(triggers,{1,1},membership,0)==0);
    assert(enterTriggers(triggers,{3,3},membership,0)==0);
    assert(enterTriggers(triggers,{0,1},membership,0)==1);
    assert(enterTriggers(triggers,{1,1},membership,1)==2);
    assert(triggerMask({0,0,16,16},{1,2}) == ((1u<<1)|(1u<<18)));
    assert(triggerMask({0,0,16,16},{-0.5f,-0.5f}) == ((1u<<0)|(1u<<16)));
    auto gated=triggers; gated[1].mask=0;
    membership.assign(2,false);
    assert(enterTriggers(gated,{1,1},membership,0)==1);
    const std::vector<CameraTrigger> spatial{{0,{{1,1},{1.5f,1.5f}},{1},(1u<<1)|(1u<<17)}};
    std::vector<bool> spatialMembership(1,false);
    assert(enterTriggers(spatial,{1.2f,1.2f},spatialMembership,0)==1);
    CameraTrigger exitZone{0,{{0,0},{2,2}},{1}};
    exitZone.exitCameras={3};
    std::vector<bool> exitMembership(1,false);
    assert(enterTriggers({exitZone},{1,1},exitMembership,0)==1);
    assert(enterTriggers({exitZone},{3,3},exitMembership,1)==3);
    bool rejected=false;
    try { triggerMask({0,0,1e-8f,1e-8f},{4.25f,0}); }
    catch (const std::runtime_error&) { rejected=true; }
    assert(rejected);
    rejected=false;
    try { triggerMask({}, {std::numeric_limits<float>::quiet_NaN(),0}); }
    catch (const std::runtime_error&) { rejected=true; }
    assert(rejected);
    assert(enterTriggers(spatial,{4,4},spatialMembership,0)==0);
    assert(spatialMembership[0]);
    assert(enterTriggers(spatial,{1.2f,1.2f},spatialMembership,0)==0);
    assert(enterTriggers(spatial,{1.8f,1.8f},spatialMembership,0)==0);
    assert(!spatialMembership[0]);
    assert(enterTriggers(spatial,{1.2f,1.2f},spatialMembership,0)==1);
    {
        GroundPolygon vertical;vertical.boundaryOnly=true;vertical.points={{0,-2},{0,2}};vertical.contactFlags=2;
        GroundPolygon horizontal;horizontal.boundaryOnly=true;horizontal.points={{-2,0},{2,0}};horizontal.contactFlags=4;
        unsigned contacts=99;const auto stopped=moveGround({vertical,horizontal},{-1,-1},{2,2},.1f,&contacts);
        assert(contacts==6&&stopped.x<-.099f&&stopped.z<-.099f);
        moveGround({vertical,horizontal},{-1,-1},{-.1f,-.1f},.1f,&contacts);assert(contacts==0);
        vertical.enabled=false;horizontal.enabled=false;
        moveGround({vertical,horizontal},{-1,-1},{2,2},.1f,&contacts);assert(contacts==0);
    }
    {
        auto a=rectangle(0,-2,.2f,2);a.sourceIndex=17;a.contactFlags=2;
        auto b=rectangle(-2,0,2,.2f);b.sourceIndex=31;b.contactFlags=4;
        GroundMoveTrace trace;unsigned plainFlags=0,tracedFlags=0;
        const auto plain=moveGround({a,b},{-1,-1},{2,2},.1f,&plainFlags);
        const auto traced=moveGround({a,b},{-1,-1},{2,2},.1f,&tracedFlags,&trace);
        assert(plain.x==traced.x&&plain.z==traced.z&&plainFlags==tracedFlags);
        assert(trace.sweepCount==2&&!trace.recoveryCount&&!trace.contactLimit);
        assert(trace.start.x==-1&&trace.delta.x==2&&trace.end.x==traced.x&&trace.end.z==traced.z);
        unsigned seen=0;for(unsigned n=0;n<trace.sweepCount;++n){const auto& hit=trace.sweeps[n];
            assert(hit.found&&hit.time>=0&&hit.time<=1&&std::abs(length(hit.normal)-1)<.00001f);
            assert((hit.polygonIndex==0&&hit.sourceIndex==17)||(hit.polygonIndex==1&&hit.sourceIndex==31));seen|=hit.contactFlags;
        }assert(seen==6);
        const auto tracedRecovery=moveGround({a},{.1f,0},{},0,nullptr,&trace);
        assert(trace.recoveryCount&&trace.firstRecovery.found&&trace.firstRecovery.sourceIndex==17&&trace.firstRecovery.polygonIndex==0);
        assert(trace.firstRecovery.contactFlags==2&&trace.sweepCount==0&&trace.recovered.x==tracedRecovery.x);
        moveGround({a},{-1,-1},{-.1f,0},0,nullptr,&trace);
        assert(!trace.sweepCount&&!trace.recoveryCount&&!trace.firstRecovery.found&&!trace.contactLimit);
        // Diagnostics must not change any float result, including overlapping/point contacts.
        for(float traceRadius:{0.f,.1f,.8f})for(int x=-3;x<=3;++x)for(int z=-3;z<=3;++z)
            for(Vec2 delta:std::array<Vec2,4>{{{.13f,.27f},{-.3f,.1f},{1.7f,-1.3f},{0,0}}}){
                const Vec2 at{float(x)*.21f,float(z)*.23f};
                const auto original=moveGround({a,b},at,delta,traceRadius,&plainFlags);
                const auto observed=moveGround({a,b},at,delta,traceRadius,&tracedFlags,&trace);
                assert(original.x==observed.x&&original.z==observed.z&&plainFlags==tracedFlags&&trace.sweepCount<=trace.sweeps.size());
            }
    }
}
