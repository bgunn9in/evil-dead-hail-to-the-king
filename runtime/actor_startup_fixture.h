#pragma once
#include "gameplay_actors.h"
#include "source_gui.h"
#include "source_actor_routes.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

static jpc::Value I(int n){return jpc::Value::integer(n);}
struct StartupCase {
    ScriptGame game;SourceGui gui;
    std::unique_ptr<edactors::GameplayActors> actors;
    unsigned arenaStarts=0,arenaStops=0;bool arena=false;int randomChoice=0;
    StartupCase(const std::filesystem::path& script,const std::filesystem::path& assets,bool levelB,
                std::function<void(edactors::Host&)> configureHost={}):game(script,false) {
        auto& vm=game.vm;const auto& p=game.package;
        auto bind=[&](const char* name,jpc::Interpreter::Native fn){const jpc::Method* found=nullptr;
            for(const auto& m:p.methods)if(p.classes[size_t(m.owner)].name=="ed/base/External"&&m.name==name){assert(!found);found=&m;}
            if(!found)throw jpc::Error(std::string("Missing source startup endpoint: ")+name);vm.bind("ed/base/External",name,found->signature,std::move(fn));};
        assert(!game.ash&&!game.level&&!game.scene&&!game.inventory&&vm.staticField(310).null()&&vm.staticField(396).null());
        for(const auto* name:SourceGui::Setters)bind(name,[this,name](auto& v,auto,const auto& a){return gui.native(v,name,a);});
        for(const char* name:{"guiSysCreate","guiItemCreate","guiSysGetFlag","guiItemGetDataID","guiSysAtOffset","guiSysSet"})
            bind(name,[this,name](auto& v,auto,const auto& a){return gui.native(v,name,a);});
        for(const char* name:{"platform","checkRumbleAbility","getMilliseconds"})bind(name,[](auto&,auto,const auto&){return I(0);});
        bind("sndMgrGetStereo",[this](auto&,auto,const auto&){return I(gui.stereo);});
        bind("sndMgrGetMasterVolume",[this](auto&,auto,const auto&){return I(gui.masterVolume);});
        bind("sndMgrSetStereo",[this](auto&,auto,const auto& a){gui.stereo=a[0].integer();return jpc::Value{};});
        bind("sndMgrSetMasterVolume",[this](auto&,auto,const auto& a){gui.masterVolume=a[0].integer();return jpc::Value{};});
        for(const char* name:{"sndMgrGetSNDID","sndMgrPlaySoundVol","sndMgrActorPlaySound"})bind(name,[](auto&,auto,const auto&){return I(-1);});
        bind("scrFxShake",[](auto&,auto,const auto&){return jpc::Value{};}); // Camera presentation is outside this CPU actor fixture.
        bind("memLevelStart",[this](auto&,auto,const auto&){assert(!arena);arena=true;++arenaStarts;return jpc::Value{};});
        bind("memLevelStop",[this](auto&,auto,const auto&){assert(arena);arena=false;++arenaStops;return jpc::Value{};});
        edactors::Host host;host.spawnPose=[](auto,int,int,int){return edactors::Pose{};};host.floorHeight=[](float,float){return 0.0f;};
        host.effect=[](const auto&,const auto&){return 0;};
        host.random=[this](int bound){return randomChoice%int(std::abs(int64_t(bound)));};
        host.facing=[this](auto from,auto to,int angle){const auto& a=actors->at(from);const auto& target=actors->at(to);
            Door d;d.marker={target.pose.x,target.localY,target.pose.z};d.facingArg=float(angle);return facingDoor(d,{a.pose.x,a.pose.z},a.pose.yaw);};
        host.lineOfSight=[](auto,auto){return true;};host.lineOfSightDirection=[](auto,int,int){return false;};host.visibleArea=[](auto){return true;};
        host.move=[](auto& b,int forward,int strafe,int vertical){edactormath::appendMovement(b.pendingMove,forward,strafe,vertical,{{0,b.pose.yaw,0}},b.motionFlags);return 1;};
        host.applyMovement=[](auto& b){const auto move=edactormath::pendingToWorld(b.pendingMove,{{0,b.pose.yaw,0}},b.motionFlags);
            b.pose.x+=move[0];b.pose.y+=move[1];b.pose.z+=move[2];};
        host.rootMotionAllowed=[](auto,const auto&){return false;}; // Stationary combat fixture; curve displacement has independent source tests.
        host.pickupObjects=[](){return std::vector<edactors::PickupObject>{};};
        host.animationEvent=[](const auto&,const auto&){};host.animationSound=[](const auto&,const auto&){}; // Separate PCM/VFX proofs execute presentation.
        host.soundMaterials=[](const auto&){return edanimsound::Materials{};};
        if(configureHost)configureHost(host);
        actors=std::make_unique<edactors::GameplayActors>(game,assets,host);
        bind("actorFace",[this](auto&,auto,const auto& a){actors->at({a[0].integer(),a[1].integer()}).pose.yaw=
            edactormath::modelYawFromHeading(float(double(a[2].integer())*.01745329238474369f));return jpc::Value{};});
        bind("actorSetPosition",[this](auto&,auto,const auto& a){auto& b=actors->at({a[0].integer(),a[1].integer()});
            b.pose.x=float(double(a[2].integer())*edactormath::Centimeter);b.localY=float(double(a[3].integer())*edactormath::Centimeter);
            b.pose.y=b.localY+b.cachedFloorHeight;b.pose.z=float(double(a[4].integer())*edactormath::Centimeter);return jpc::Value{};});
        actors->prepareBootstrap();vm.invoke(p.methodIndex("Main","initialize","()V"));
        assert(vm.staticField(396).null());vm.invoke(p.methodIndex("Main","createLevel","(I)V"),0,{I(levelB?1:0)});
        game.level=vm.staticField(396).reference();actors->sync();actors->captureLevelBoundary();
        assert(vm.objectClass(game.level)==p.classIndex(levelB?"LevelB":"LevelA")&&vm.staticField(397).integer()==int(levelB));
        assert(actors->bodies().size()==9&&arenaStarts==0&&arenaStops==0&&!arena);
        assert(game.ash&&game.inventory&&vm.field(game.ash,13).integer()==100&&vm.objectClass(game.inventory)==p.classIndex("Inventory"));
        assert(actors->at({0,0}).flags==(levelB?55:23)); // Source B.create omits A's clear32 call.
        const auto& offsets=vm.arrayValues(vm.staticField(54).reference());
        assert(offsets[0].integer()==40&&offsets[4].integer()==0&&offsets[2].integer()==4);
        for(const auto& [key,body]:actors->bodies()) {
            assert(key.type==0||key.type==2||key.type==4);assert(body.script&&body.nativeSlot==int(body.allocationOrder));
            assert(vm.field(body.script,5).integer()==key.index&&vm.field(body.script,6).integer()==key.type&&vm.field(body.script,13).integer()==100);
            if(key.type){assert(!(body.flags&1)&&(body.flags&8)&&vm.field(body.script,game.hostField(HostFieldRole::GenericNpc30)).integer()==1);
                assert(vm.objectClass(vm.field(body.script,4).reference())==p.classIndex("ed/base/EDBrain"));}
        }
        const auto& parts=actors->at({0,0}).parts;for(size_t i=0;i<parts.size();++i)assert(parts[i]==(i==0||i==6||i==12));
        assert(actors->at({0,0}).vertices.size()==1710);
        const auto native=actors->snapshot(),scriptState=vm.snapshot(),guiState=gui.snapshot();
        vm.restore(scriptState);actors->restore(native);gui.restore(guiState);gui.validateReferences(vm);assert(actors->snapshot()==native);
        if(levelB){bool rejected=false;try{actors->createType(5);}catch(const jpc::Error&){rejected=true;}assert(rejected&&actors->snapshot()==native);}
    }
};
