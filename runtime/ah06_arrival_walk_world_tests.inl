#pragma once
#include "source_inventory_ui.inl"
#include "controller_health_pickup.h"
#include "ah12_return_walk_world_tests.inl"

void continueAH06ArrivalWalkWorld(World& world) {
    std::cout<<std::unitbuf;auto& vm=world.game.vm;
    auto require=[](bool condition,const std::string& text){if(!condition)throw std::runtime_error("Original AH06 arrival walk: "+text);};
    auto quantity=[&](int id){int total=0;for(int field:{98,99,100})for(const auto& item:world.game.items(field))if(item.itemID==id)total+=item.quantity;return total;};
    require(world.scene.id=="ah05"&&vm.staticField(451).integer()&&vm.staticField(448).integer()&&vm.staticField(458).integer()&&
        vm.staticField(441).integer()&&!vm.staticField(456).integer()&&quantity(9)==1&&!vm.staticField(604).integer(),
        "requires the same actual walked AH12 amulet return World before Matriarch");
    bool authored=false;const auto saw=vm.field(world.game.ash,102).reference();
    require(vm.field(saw,812).integer()==100,"original primary Chainsaw100 is absent");
    const unsigned previousTransitions=world.transitions;unsigned frames=0,combatFrames=0,hits=0,recoveries=0;float distance=0;
    int previousHealth=vm.field(world.game.ash,13).integer();
    auto tick=[&](Vec2 stick=Vec2{},bool use=false,float dt=.033f,bool attack=false){const std::string area=world.scene.id;const Vec2 old{world.actor.position[0],world.actor.position[2]};
        ControlFrame input;input.movement=stick;input.down[size_t(ControlAction::Aim)]=attack;const bool powered=attack&&vm.field(saw,815).integer()>=2;
        input.edges[size_t(powered?ControlAction::PrimaryAttack:ControlAction::SecondaryAttack)]=attack&&dt>0&&world.sourceTick%6==0;
        input.edges[size_t(ControlAction::ChainsawRev)]=powered&&(vm.field(saw,139).integer()==0||vm.field(saw,139).integer()==2);
        world.playerControls(input);const auto moved=world.step(stick,true,use,dt);require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);
        const auto& body=world.actors->at({0,0});const Vec2 at{body.pose.x,body.pose.z};
        const int health=vm.field(world.game.ash,13).integer();if(health<previousHealth)std::cout<<"Amulet platform route incoming damage "<<previousHealth<<" -> "<<health<<" scene"<<world.scene.id
            <<" tick"<<world.sourceTick<<" at("<<body.pose.x<<','<<body.pose.z<<")\n";
        previousHealth=health;
        require(health>0,"Ash naturally died on the return route");
        // The starting pose is the original climb/item marker; only source descent moves it.
        if(authored)require(length(stick)<.001f&&moved.requested==0,"authored climb/item phase received manual movement");
        else require(floorHeight(world.scene.floor,at).has_value()&&clearGround(world.scene.ground,at,world.actor.clearanceRadius),"walking left floor or penetrated enabled source solids");
        require(moved.requested<=std::min(1.f,length(stick))*world.scene.speed*2*frameSeconds(dt)+ContactSkin*4,"requested motion exceeded the input speed bound");
        if(moved.moved>moved.requested+ContactSkin*4){auto combined=world.scene.ground;for(auto obstacle:world.manualActorObstacles()){
                obstacle.circleRadius+=world.scene.radius;combined.push_back(std::move(obstacle));}
            const auto recovered=recoverGround(combined,old,0);const float correction=length(recovered-old);
            // A source-frame spawn can overlap the old point; certify only the
            // existing dynamic separation, including its static-safe segment.
            require(area==world.scene.id&&clearGround(world.scene.ground,old,0)&&!clearGround(combined,old,0)&&clearGround(combined,at,0)&&
                length(moveGround(world.scene.ground,old,recovered-old,0)-recovered)<ContactSkin*4&&moved.moved<=moved.requested+correction+ContactSkin*4,
                "extra movement was not bounded static-safe dynamic recovery");++recoveries;
            if(recoveries==1)std::cout<<"Verified dynamic recovery: scene"<<area<<" tick"<<world.sourceTick<<" correction"<<correction<<" requested"<<moved.requested<<" moved"<<moved.moved<<'\n';}
        if(world.scene.id==area)distance+=length(at-old);
        frames+=dt>0;};
    auto ready=[&](){for(unsigned n=0;n<480;++n){if(world.game.mode!=0)world.closeInventory();world.playerControls({});const auto& ash=world.actors->at({0,0});
            if(!(ash.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&
               !vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&ash.actions.possible(116))return;tick();}
        throw std::runtime_error("Original AH06 arrival walk: source input did not become ready");};
    auto toward=[&](Vec2 target,float magnitude){const auto& camera=world.scene.cameras.at(world.actor.camera);const auto& latch=world.actor.movement;
        const Vec2 right=latch.held?latch.right:camera.right,forward=latch.held?latch.forward:camera.forward,delta=target-Vec2{world.actor.position[0],world.actor.position[2]};
        const float determinant=right.x*forward.z-right.z*forward.x;require(std::abs(determinant)>.001f,"camera basis is singular");Vec2 stick{(delta.x*forward.z-delta.z*forward.x)/determinant,(right.x*delta.z-right.z*delta.x)/determinant};
        const float size=length(stick);return size>.000001f?stick*(magnitude/size):Vec2{};};
    auto nav=[&](unsigned index){uint32_t count,offset;std::memcpy(&count,world.scene.nativeBytes.data()+48,4);std::memcpy(&offset,world.scene.nativeBytes.data()+52,4);
        require(index<count,"source nav point missing");GroundPoint p;std::memcpy(&p,world.scene.nativeBytes.data()+offset+index*44+8,12);return Vec2{p.x,p.z};};
    auto walk=[&](Vec2 target,const char* destination=""){tick({},false,0);const std::string source=world.scene.id;unsigned stalled=0;float previous=std::numeric_limits<float>::infinity();
        for(unsigned n=0;n<2200;++n){if(world.scene.id!=source){require(world.scene.id==destination,"unexpected portal destination "+source+"->"+world.scene.id+"; expected "+destination);return;}
            if(world.game.mode!=0){ready();continue;}const Vec2 point{world.actor.position[0],world.actor.position[2]};const float remaining=length(target-point);if(remaining<.04f){tick({},false,0);return;}
            const edactors::Body* enemy=nullptr;float nearest=3.1f;const auto& ash=world.actors->at({0,0});
            for(const auto& [key,body]:world.actors->bodies())if(key.type!=0&&body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&vm.field(body.script,13).integer()>0){
                const float d=length(point-Vec2{body.pose.x,body.pose.z});if(d<std::min(nearest,2.4f+float(body.radius)*.01f)&&
                    world.nativeScene().queries.lineOfSight({ash.pose.x,ash.pose.y,ash.pose.z},{body.pose.x,body.localY,body.pose.z},4,world.sourceQueryCamera(),world.nativeScene().enabledVolumes,world.visibilityReady)){enemy=&body;nearest=d;}}
            if(enemy){const auto key=enemy->key;const int health=vm.field(enemy->script,13).integer();tick(toward({enemy->pose.x,enemy->pose.z},1),false,0,true);tick({},false,.033f,true);
                require(++combatFrames<3600,"actual nearby encounters exceeded bounded input budget");const auto current=world.actors->bodies().find(key);
                if(current!=world.actors->bodies().end()&&vm.field(current->second.script,13).integer()<health)++hits;stalled=0;previous=std::numeric_limits<float>::infinity();continue;}
            stalled=remaining>=previous-.00001f?stalled+1:0;
            require(stalled<120,"route stalled in "+source+" at "+std::to_string(point.x)+","+std::to_string(point.z)+" toward "+std::to_string(target.x)+","+std::to_string(target.z));
            previous=remaining;tick(toward(target,std::min(1.0f,remaining/(world.scene.speed*2*.033f))));}
        throw std::runtime_error("Original AH06 arrival walk: leg exceeded bounded input budget");};
    auto save=[&](){ready();const auto bytes=world.saveStateBytes();for(unsigned n=0;n<3;++n)tick();world.restoreStateBytes(bytes,nullptr);require(world.saveStateBytes()==bytes,"earned walking boundary failed exact full save");};
    ready();collectNearbySourceHealth(world);ready();previousHealth=vm.field(world.game.ash,13).integer();std::cout<<"Actual AH05 amulet-return start "<<world.actor.position[0]<<','<<world.actor.position[2]<<" HP"<<previousHealth<<'\n';
    // AH20 returns at the northern keydoor marker, so follow the already
    // authored keydoor corridor in reverse rather than the AH02 arrival chain.
    for(unsigned index:{21,20,19,18,17,16,15,14,13,23,12,10,11}){auto p=nav(index);if(index==23||index==13)p.x-=.12f;if(index==14)p.z-=.15f;if(index==18)p.x=-21.105f;walk(p);}walk({-18.1f,-12.7f});ready();const auto target=world.scene.scripting.markers.at(7);
    tick(toward({target.x,target.z},1),false,0);tick({},true,0);require(vm.field(world.game.ash,117).integer(),"actual platform Use did not request original climb-up");authored=true;
    const auto pending=world.saveStateBytes();unsigned climbSteps=0;for(;climbSteps<300&&!vm.staticField(456).integer();++climbSteps)tick();
    require(vm.staticField(456).integer(),"original climb128/end34 did not reach platform");const auto marker=world.scene.scripting.markers.at(23);
    require(length(Vec2{world.actor.position[0]-marker.x,world.actor.position[2]-marker.z})<.001f,"original climb did not reach source marker23");
    const auto top=world.saveStateBytes();world.restoreStateBytes(pending,nullptr);for(unsigned n=0;n<climbSteps;++n)tick();require(world.saveStateBytes()==top,"pending second climb replay differed");ready();
    // Original Face23 in the climb already leaves Ash facing marker7.
    // Keep that authored pose; an unnecessary manual turn would recover it
    // out of the re-enabled crate45 even though its requested delta is zero.
    require(world.scene.scripting.facing(7,45,{world.actor.position[0],world.actor.position[2]},world.actor.yaw),
        "original climb did not leave Ash facing marker7");
    tick({},true,0);require(!quantity(9)&&!vm.staticField(456).integer()&&world.pendingSceneSwitch,
        "original top Use did not consume earned amulet and queue sceneSwitch");
    const auto request=*world.pendingSceneSwitch;require(request.sourceSceneId==0x61680500&&request.destinationId==0x61680600&&request.camera==5&&request.marker==0&&
        request.rotationXYZ==std::array<float,3>{{0,0,0}},"original amulet sceneSwitch tuple differed");world.pendingSceneSwitch.reset();
    require(world.switchScene(request,nullptr),"actual queued amulet sceneSwitch could not enter AH06");authored=false;
    require(world.scene.id=="ah06"&&world.transitions==previousTransitions+1&&!quantity(9)&&!vm.staticField(604).integer(),"queued source switch did not enter unsolved AH06 once");ready();save();
    std::cout<<"Original walked amulet platform: same AH05 return/9 ->original reverse keydoor nav21..11/volume63 Use ->climb128/end34/marker23 ->source topUse consumes9/clears456/sceneSwitch(AH06,5,0,0) ->actual AH06; "
        <<frames<<" positive steps/"<<distance<<"m aggregate displacement, "<<combatFrames<<" combat frames/"<<hits<<" damage steps, climb replay"<<climbSteps<<", HP"<<previousHealth<<", exact saves; no host pose setters\n";
}

void testAH06ArrivalWalkWorld(const fs::path& root) {
    testEpisode(root,false,640,480,{},[](World& world){continueAH02WalkWorld(world);continueAH02HandleWalkWorld(world);continueAH05WalkWorld(world);continueCrowbarWalkWorld(world);continueAH05PlatformWalkWorld(world);continueAH10KeydoorWalkWorld(world);continueAH10PumpWalkWorld(world);continueAH12AmuletWalkWorld(world);continueAH12ReturnWalkWorld(world);continueAH06ArrivalWalkWorld(world);});
}
