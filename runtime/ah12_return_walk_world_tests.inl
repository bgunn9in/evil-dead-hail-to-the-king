#pragma once
#include "ah12_amulet_walk_world_tests.inl"

void continueAH12ReturnWalkWorld(World& world) {
    std::cout<<std::unitbuf;auto& vm=world.game.vm;
    auto require=[](bool condition,const std::string& text){if(!condition)throw std::runtime_error("Original AH12 return walk: "+text);};
    auto quantity=[&](int id){int total=0;for(int field:{98,99,100})for(const auto& item:world.game.items(field))if(item.itemID==id)total+=item.quantity;return total;};
    require(world.scene.id=="ah12"&&vm.staticField(448).integer()&&vm.staticField(451).integer()&&quantity(9)==1,
        "requires the same actual walked amulet9/451 AH12 World");
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
        const int health=vm.field(world.game.ash,13).integer();if(health<previousHealth)std::cout<<"Basement return route incoming damage "<<previousHealth<<" -> "<<health<<" scene"<<world.scene.id
            <<" tick"<<world.sourceTick<<" at("<<body.pose.x<<','<<body.pose.z<<")\n";
        previousHealth=health;
        require(health>0,"Ash naturally died on the return route");
        // Source-authored portal phases advance only zero manual input.
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
        throw std::runtime_error("Original AH12 return walk: source input did not become ready");};
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
        throw std::runtime_error("Original AH12 return walk: leg exceeded bounded input budget");};
    auto save=[&](){ready();const auto bytes=world.saveStateBytes();for(unsigned n=0;n<3;++n)tick();world.restoreStateBytes(bytes,nullptr);require(world.saveStateBytes()==bytes,"earned walking boundary failed exact full save");};
    ready();walk(nav(1));ready();require(world.scene.scripting.volumes.at(7).contains({world.actor.position[0],0,world.actor.position[2]}),"source nav1 return point missed basement volume7");
    const auto ladder=world.scene.scripting.markers.at(3);tick(toward({ladder.x,ladder.z},1),false,0);authored=true;tick({},true,0);
    for(unsigned n=0;n<180&&world.scene.id=="ah12";++n)tick();require(world.scene.id=="ah20"&&world.transitions==previousTransitions+1,"original AH12 return Use117/portal1 did not enter AH20");
    authored=false;ready();save();std::cout<<"Actual basement return AH12->AH20 tick"<<world.sourceTick<<'\n';
    for(unsigned index:{3,2,1,0})walk(nav(index));ready();const auto door=world.scene.scripting.markers.at(15);tick(toward({door.x,door.z},1),false,0);tick({},true,0);
    for(unsigned n=0;n<180&&world.scene.id=="ah20";++n)tick();require(world.scene.id=="ah05"&&world.transitions==previousTransitions+2,"original AH20 volume23/facing15/sound/portal1 did not return AH05");
    ready();save();require(quantity(9)==1&&vm.staticField(451).integer()&&vm.staticField(448).integer(),"actual return lost earned amulet or pump flags");
    std::cout<<"Original walked amulet return: same AH12/9/451 ->source nav1/volume7/facing3 Use117/portal1 ->AH20 nav3/2/1/0/volume23/facing15/sound/portal1 ->AH05; "
        <<frames<<" positive steps/"<<distance<<"m aggregate displacement, "<<combatFrames<<" combat frames/"<<hits<<" damage steps, HP"<<previousHealth<<", exact saves; no host pose setters\n";
}

void testAH12ReturnWalkWorld(const fs::path& root) {
    testEpisode(root,false,640,480,{},[](World& world){continueAH02WalkWorld(world);continueAH02HandleWalkWorld(world);continueAH05WalkWorld(world);continueCrowbarWalkWorld(world);continueAH05PlatformWalkWorld(world);continueAH10KeydoorWalkWorld(world);continueAH10PumpWalkWorld(world);continueAH12AmuletWalkWorld(world);continueAH12ReturnWalkWorld(world);});
}
