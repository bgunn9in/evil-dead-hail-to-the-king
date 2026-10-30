#pragma once
#include "levelb_story_world_tests.inl"

// The earned prefix has historical Ash approach/camera fixtures. This new leg
// starts at that exact callback pose and uses only real movement input.
void continueEarnedBridgeWalkWorld(World& world) {
    auto& vm=world.game.vm;
    auto require=[](bool ok,const std::string& text){if(!ok)throw std::runtime_error("Earned bridge walk: "+text);};
    require(world.scene.id=="bm01"&&vm.staticField(531).integer()==1&&!world.scene.enabledVolumes.at(2),"missing actual earned mechanism531");
    require(vm.staticField(844).integer()==0&&!vm.staticField(532).integer(),"source bridge callback is not ready for its first crossing");
    const auto barrier=world.scene.scripting.volumes.at(2);
    const Vec2 start{world.actor.position[0],world.actor.position[2]};
    const auto initialTransitions=world.transitions;
    unsigned frames=0,leadFrames=0;float distance=0,leadDistance=0;
    bool crossedBarrier=false,sawBridgeCamera=false;
    auto nav=[&](unsigned index){const auto& raw=world.scene.nativeBytes;uint32_t count=0,offset=0;
        require(raw.size()>=56,"missing original navigation header");std::memcpy(&count,raw.data()+48,4);std::memcpy(&offset,raw.data()+52,4);
        require(index<count&&offset<=raw.size()&&size_t(index)*44+20<=raw.size()-offset,"original route navigation point is missing");
        GroundPoint point;std::memcpy(&point,raw.data()+offset+index*44+8,12);return Vec2{point.x,point.z};};
    auto tick=[&](Vec2 stick,float dt=.033f){const Vec2 old{world.actor.position[0],world.actor.position[2]};
        if(world.scene.id=="bm01"){
            crossedBarrier|=barrier.contains({old.x,world.actors->at({0,0}).localY,old.z});
            sawBridgeCamera|=world.actor.camera==1&&world.cameraTimeline&&world.cameraTimeline->state&&world.cameraTimeline->state->frame==1;}
        ControlFrame input;input.movement=stick;world.playerControls(input);const auto moved=world.step(stick,false,false,dt);
        require(!world.scriptFault,"source movement failed: "+world.game.diagnostic);
        require(vm.field(world.game.ash,13).integer()>0,"Ash died during the noncombat bridge route");
        require(world.scene.id=="bm01"||world.scene.id=="bm02","walking entered an unexpected source scene");
        const Vec2 current{world.actor.position[0],world.actor.position[2]};
        require(floorHeight(world.scene.floor,current).has_value()&&clearGround(world.scene.ground,current,world.actor.clearanceRadius),"walking left original floor or penetrated an active source solid");
        require(moved.moved<=moved.requested+ContactSkin*4,"movement exceeded its requested distance");
        if(dt>0){++frames;if(world.scene.id=="bm01")distance+=length(current-old);else distance+=moved.moved;}
    };
    auto walk=[&](Vec2 goal,bool permitArrival){tick({},0);float previous=std::numeric_limits<float>::infinity();unsigned stalled=0;
        for(unsigned frame=0;frame<1800;++frame){
            if(world.scene.id=="bm02"){require(permitArrival,"lead-in crossed the source portal unexpectedly");return;}
            const Vec2 delta=goal-Vec2{world.actor.position[0],world.actor.position[2]};const float remaining=length(delta);
            if(remaining<.04f){tick({},0);require(!permitArrival,"walked to the far-bank goal without entering native portal0");return;}
            stalled=remaining>=previous-.00001f?stalled+1:0;
            require(stalled<120,"source route stalled at "+std::to_string(world.actor.position[0])+","+std::to_string(world.actor.position[2])+" toward "+std::to_string(goal.x)+","+std::to_string(goal.z));
            previous=remaining;const auto& camera=world.scene.cameras.at(world.actor.camera);const auto& latch=world.actor.movement;
            const Vec2 right=latch.held?latch.right:camera.right,forward=latch.held?latch.forward:camera.forward;
            const float determinant=right.x*forward.z-right.z*forward.x;require(std::abs(determinant)>.001f,"source camera movement basis is singular");
            Vec2 stick{(delta.x*forward.z-delta.z*forward.x)/determinant,(right.x*delta.z-right.z*delta.x)/determinant};
            const float magnitude=length(stick);require(magnitude>.000001f,"nonzero route has no movement direction");
            stick=stick*(std::min(1.f,remaining/(world.scene.speed*.033f))/magnitude);tick(stick);
        }
        throw std::runtime_error("Earned bridge walk: bounded input budget exhausted");};
    // The source helper's final camera regression changes its historical pose.
    // Walk back to the real mechanism volume via authored nav2, then follow the
    // original nav2<->nav1 bridge connection without another pose setter or Use.
    walk(nav(2),false);
    require(world.scene.scripting.inside(3,{world.actor.position[0],world.actors->at({0,0}).localY,world.actor.position[2]}),"lead-in did not walk into original mechanism volume3");
    leadFrames=frames;leadDistance=distance;
    walk(nav(1),false);walk({-10.7f,10.2f},true);
    require(world.scene.id=="bm02"&&world.transitions==initialTransitions+1&&crossedBarrier&&sawBridgeCamera,"route did not cross former blocker2/camera1/native portal0 exactly once");
    require(vm.staticField(531).integer()==1&&!vm.staticField(532).integer(),"walking changed the earned mechanism or unearned guardian progress");
    const auto saved=world.saveStateBytes();for(unsigned n=0;n<3;++n)tick({});world.restoreStateBytes(saved,nullptr);
    require(world.saveStateBytes()==saved,"real BM02 arrival did not save/load byte-identically");
    std::cout<<"Earned bridge WALK: callback"<<start.x<<','<<start.z<<" -> source nav2/mechanism volume3 lead-in "<<leadFrames<<" frames/"<<leadDistance
        <<"m -> source nav1/former blocker2/authored camera1/native portal0/BM02; total "<<frames<<" frames/"<<distance<<"m, HP"<<vm.field(world.game.ash,13).integer()<<", exact arrival save; no new pose fixtures\n";
}

void testEarnedBridgeWalkWorld(const fs::path& levelA){testLevelBStoryWorld(levelA,[](World& world){continueEarnedBridgeWalkWorld(world);});}
