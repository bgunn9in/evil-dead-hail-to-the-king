#include "source_actor_routes.h"
#undef NDEBUG
#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>
template<class F>bool rejects(F fn){try{fn();return false;}catch(const std::exception&){return true;}}
int main(int argc,char**argv){try{
    if(argc!=2)throw std::runtime_error("Expected generated source asset root");const std::filesystem::path root=argv[1];
    std::ifstream input(root/"ak09/scene.native",std::ios::binary);std::vector<uint8_t> payload(std::istreambuf_iterator<char>(input),{});
    ScriptGame game(root/"scripts/levelA.jpc");game.enterScene("ak09");
    game.vm.bind("ed/base/External","guiItemSetPadObj","(ILed/base/EDBase;)V",[](auto&,auto,const auto&){return jpc::Value{};});
    edactors::Host host;host.spawnPose=[](auto k,int,int,int){return edactors::Pose{k.type==0?0.0f:3.0f,10,k.type==0?0.0f:4.0f,0};};
    host.floorHeight=[](float,float){return 10.0f;};host.effect=[](const auto&,const auto&){return 0;};
    host.move=[](auto& body,int forward,int strafe,int vertical){edactormath::appendMovement(body.pendingMove,forward,strafe,vertical,{{0,body.pose.yaw,0}},body.motionFlags);return 1;};
    edactors::GameplayActors actors(game,root,host);std::vector<bool> enabled(ScriptScene(payload).volumes.size(),true);
    SourceActorRoutes::Host callbacks;callbacks.camera=[](){return 0u;};callbacks.enabledVolumes=[&]()->const std::vector<bool>&{return enabled;};
    callbacks.collisionStatus=[&](auto k){return actors.at(k).motionFlags2;};
    unsigned randomCalls=0;callbacks.random=[&](int maximum){assert(maximum==40);++randomCalls;return 10;};
    SourceActorRoutes routes(game.vm,actors,payload,callbacks);routes.bind();
    actors.initializeAsh();actors.createDeadite();
    auto call=[&](const char* name,std::initializer_list<int> values){std::vector<jpc::Value> args;for(int value:values)args.push_back(jpc::Value::integer(value));
        const jpc::Method* method=nullptr;for(const auto& m:game.package.methods)if(game.package.classes[size_t(m.owner)].name=="ed/base/External"&&m.name==name)method=&m;
        assert(method);return game.vm.invoke(game.package.methodIndex("ed/base/External",name,method->signature),0,args);};
    assert(call("actorRouteStatus",{4,0}).integer()==0&&call("actorRoutePosition",{4,0}).integer()==0);
    assert(call("actorDistanceToRecordedPosition",{4,0,0}).integer()==500);
    assert(game.vm.invoke(game.package.methodIndex("ed/base/EDNpc","distanceToSpot","(I)I"),actors.at({4,0}).script,{jpc::Value::integer(0)}).integer()==500);
    assert(rejects([&]{call("actorDistanceToSpot",{4,0,3});}));
    call("actorRecordActorSpot",{4,0,0,0,3});assert(call("actorDistanceToSpot",{4,0,3}).integer()==500);
    auto& ash=actors.at({0,0});ash.pose={3,99,4,0};ash.localY=7;
    call("actorRecordActorSpot",{4,0,0,0,1});assert(call("actorDistanceBetweenSpots",{4,0,3,1}).integer()==500);
    assert(call("actorDistanceToRecordedPosition",{4,0,1}).integer()==100); // Local XZ coincides:421b60 fallback1.
    actors.at({4,0}).localY=7;assert(call("actorDistanceToRecordedPosition",{4,0,1}).integer()==100);actors.at({4,0}).localY=0; // Y ignored.
    assert(rejects([&]{call("actorDistanceToRecordedPosition",{4,0,5});}));
    assert(call("actorDistanceBetweenSpots",{4,0,1,1}).integer()==100); // Source421b60 zero-vector fallback.
    call("actorRecordMarkerSpot",{4,0,0,2});call("actorRecordNavPointSpot",{4,0,0,4});
    assert(call("sceneGetNavPointCount",{}).integer()==87);
    assert(rejects([&]{call("actorRecordActorSpot",{4,0,0,0,5});}));
    const auto saved=routes.snapshot();
    {statebytes::Reader in{saved};assert(in.text()=="EDROUTES2");in.get<uint32_t>();assert(in.count(9)==1);
        assert(in.get<int32_t>()==4&&in.get<int32_t>()==0);for(unsigned i=0;i<3;++i)in.get<int32_t>();
        for(unsigned i=0;i<5;++i){const bool recorded=in.flag();const GroundPoint p{in.get<float>(),in.get<float>(),in.get<float>()};
            if(i==1)assert(recorded&&p.x==3&&p.y==7&&p.z==4);}}
    routes.actorCreated({4,0});assert(call("actorDistanceToSpot",{4,0,3}).integer()==500);
    for(int i=0;i<16;++i)assert(call("routeCreate",{}).integer()==i);
    assert(call("routeCreate",{}).integer()==-1);call("actorSetRoute",{4,0,5});
    assert(call("actorRouteStatus",{4,0}).integer()==1&&call("actorRoutePosition",{4,0}).integer()==2);
    assert(rejects([&]{routes.update();}));call("routeDestroy",{5});assert(call("actorRouteStatus",{4,0}).integer()==0);
    call("actorSetRoute",{4,0,-1});assert(call("actorRoutePosition",{4,0}).integer()==0);
    routes.restore(saved);assert(routes.snapshot()==saved);routes.update();
    auto invalid=saved;invalid.pop_back();assert(rejects([&]{routes.restore(invalid);}));assert(routes.snapshot()==saved);
    auto u32=[&](size_t at){uint32_t n;std::memcpy(&n,payload.data()+at,4);return n;};
    auto nav=[&](unsigned index){GroundPoint p;std::memcpy(&p,payload.data()+u32(52)+index*44+8,12);return p;};
    auto& npc=actors.at({4,0});const auto first=nav(0);npc.pose={first.x,first.y,first.z,0};npc.localY=first.y;npc.flags&=~8;
    const int target=86,selected=call("routeToNavigationPoint",{4,0,target}).integer();assert(selected>=0&&call("routeStatus",{selected}).integer()==1);
    call("actorSetRoute",{4,0,selected});routes.update();assert(call("routeStatus",{selected}).integer()==2);
    const auto searching=routes.snapshot();unsigned ticks=1;while(call("routeStatus",{selected}).integer()!=3&&ticks<200){routes.update();++ticks;}
    const std::vector<int> expected{0,43,54,1,50,65,75,2,3,4,5,6,7,8,10,13,51,17,18,86};
    assert(ticks==55);const auto result=routes.snapshot();const auto path=routes.navigationPath(selected);assert(path==expected);
    routes.restore(searching);for(unsigned n=1;n<ticks;++n)routes.update();assert(routes.snapshot()==result);
    auto corruptReservation=result;corruptReservation.back()^=1;assert(rejects([&]{routes.restore(corruptReservation);}));assert(routes.snapshot()==result);
    for(size_t i=0;i<path.size();++i){const auto p=nav(unsigned(path[i]));npc.pose={p.x,p.y,p.z,0};npc.localY=p.y;routes.steerActors();routes.steerActors();}
    const auto goal=nav(unsigned(target));npc.pose={goal.x,goal.y,goal.z,0};npc.localY=goal.y;routes.steerActors();
    assert(call("actorRoutePosition",{4,0}).integer()==1);call("routeDestroy",{selected});
    call("actorFaceNavPoint",{4,0,0});assert(call("actorIsFacingNavPoint",{4,0,0,45}).integer()==1);
    npc.pose.yaw=edactormath::normalizeAngle(double(npc.pose.yaw)+edactormath::Pi);assert(call("actorIsFacingNavPoint",{4,0,0,45}).integer()==0);
    const int direct=call("routeToNavigationPoint",{4,0,target}).integer();assert(call("routeStatus",{direct}).integer()==3);call("actorSetRoute",{4,0,direct});
    npc.motionFlags2=2;npc.pendingMove={};routes.steerActors();assert(randomCalls==1&&npc.pendingMove[0]==float(20*double(edactormath::Centimeter)));
    assert(npc.pendingMove[1]==0&&npc.pendingMove[2]==0&&call("actorRoutePosition",{4,0}).integer()==1);
    bool levelArena=false;
    game.vm.bind("ed/base/External","memLevelStart","()V",[&](auto&,auto,const auto&){levelArena=true;return jpc::Value{};});
    game.vm.bind("ed/base/External","memLevelStop","()V",[&](auto&,auto,const auto&){levelArena=false;return jpc::Value{};});
    assert(actors.createSkeleton()&&!levelArena);routes.actorCreated({6,0});
    assert(call("actorRouteStatus",{6,0}).integer()==0&&call("actorRoutePosition",{6,0}).integer()==0);
    call("actorRecordActorSpot",{6,0,0,0,0});call("actorRecordNavPointSpot",{6,0,target,1});
    assert(call("actorDistanceBetweenSpots",{6,0,0,0}).integer()==100);
    const int skeletonRoute=call("routeToActor",{4,0,6,0}).integer();assert(skeletonRoute>=0);
    const auto skeletonState=routes.snapshot();routes.restore(skeletonState);assert(routes.snapshot()==skeletonState);
    call("routeDestroy",{skeletonRoute});assert(rejects([&]{routes.actorCreated({6,4});}));
    for(int type:{7,8,9,11,12,13,35}) {
        assert(actors.createType(type)&&!levelArena);routes.actorCreated({type,0});
        assert(call("actorRouteStatus",{type,0}).integer()==0&&call("actorRoutePosition",{type,0}).integer()==0);
        call("actorRecordActorSpot",{type,0,0,0,0});call("actorRecordNavPointSpot",{type,0,target,1});
        const int route=call("routeToActor",{4,0,type,0}).integer();assert(route>=0);
        const auto savedActor=routes.snapshot();routes.restore(savedActor);assert(routes.snapshot()==savedActor);call("routeDestroy",{route});
        assert(rejects([&]{routes.actorCreated({type,edactors::actorCapacity(type)});}));
    }
    std::cout<<"Original local spots, pool lifecycle and AK09 asynchronous route:";for(int n:path)std::cout<<' '<<n;
    std::cout<<" ("<<ticks<<" scheduler ticks), types6/7/8/9/11/12/13/35 target/memory/save and end cursor: passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
