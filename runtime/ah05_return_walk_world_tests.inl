#pragma once
#include "ah05_walk_world_tests.inl"

void continueAH05ReturnWalkWorld(World& world) {
    std::cout<<std::unitbuf;auto& vm=world.game.vm;
    auto require=[](bool value,const std::string& text){if(!value)throw std::runtime_error("Original AH05 return walk: "+text);};
    auto quantity=[&](int id){int result=0;for(int field:{98,99,100})for(const auto& item:world.game.items(field))if(item.itemID==id)result+=item.quantity;return result;};
    require(world.scene.id=="ah05"&&vm.staticField(686).integer()&&vm.staticField(430).integer()&&vm.staticField(440).integer()&&
        !vm.staticField(457).integer()&&!quantity(161),"requires the same earned686 World before the crowbar");
    const unsigned previousTransitions=world.transitions;const int moonshine=quantity(170);unsigned frames=0,combat=0,hits=0;float distance=0;
    auto tick=[&](Vec2 stick=Vec2{},bool use=false,float dt=.033f,bool attack=false){const std::string area=world.scene.id;const Vec2 old{world.actor.position[0],world.actor.position[2]};
        ControlFrame input;input.movement=stick;input.down[size_t(ControlAction::Aim)]=attack;
        input.edges[size_t(ControlAction::SecondaryAttack)]=attack&&dt>0&&world.sourceTick%6==0;world.playerControls(input);
        const auto moved=world.step(stick,true,use,dt);require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);
        const Vec2 at{world.actor.position[0],world.actor.position[2]};require(vm.field(world.game.ash,13).integer()>0,"Ash died on the return route");
        require(floorHeight(world.scene.floor,at).has_value()&&clearGround(world.scene.ground,at,world.actor.clearanceRadius),"route left floor or penetrated an active source solid");
        require(moved.moved<=moved.requested+ContactSkin*4,"walking exceeded requested motion");if(area==world.scene.id)distance+=length(at-old);frames+=dt>0;};
    auto ready=[&](){for(unsigned n=0;n<480;++n){if(world.game.mode!=0)world.closeInventory();world.playerControls({});const auto& ash=world.actors->at({0,0});
            if(!(ash.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&
                !vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&ash.actions.possible(116))return;tick();}
        throw std::runtime_error("Original AH05 return walk: source input did not become ready");};
    auto toward=[&](Vec2 target,float magnitude){const auto& camera=world.scene.cameras.at(world.actor.camera);const auto& latch=world.actor.movement;
        const Vec2 right=latch.held?latch.right:camera.right,forward=latch.held?latch.forward:camera.forward,delta=target-Vec2{world.actor.position[0],world.actor.position[2]};
        const float determinant=right.x*forward.z-right.z*forward.x;require(std::abs(determinant)>.001f,"singular camera basis");
        Vec2 stick{(delta.x*forward.z-delta.z*forward.x)/determinant,(right.x*delta.z-right.z*delta.x)/determinant};
        const float size=length(stick);return size>.000001f?stick*(magnitude/size):Vec2{};};
    auto nav=[&](unsigned index){uint32_t count,offset;std::memcpy(&count,world.scene.nativeBytes.data()+48,4);std::memcpy(&offset,world.scene.nativeBytes.data()+52,4);
        require(index<count,"missing source navigation point");GroundPoint p;std::memcpy(&p,world.scene.nativeBytes.data()+offset+index*44+8,12);return Vec2{p.x,p.z};};
    auto walk=[&](Vec2 target,bool portal=false){tick({},false,0);unsigned stalled=0;float previous=std::numeric_limits<float>::infinity();
        for(unsigned n=0;n<1800;++n){if(world.scene.id!="ah05"){require(portal&&world.scene.id=="ah02","unexpected scene transition");return;}
            if(world.game.mode!=0){ready();continue;}const auto& ash=world.actors->at({0,0});const Vec2 at{ash.pose.x,ash.pose.z};const float remaining=length(target-at);
            if(remaining<.04f){tick({},false,0);return;}const edactors::Body* enemy=nullptr;float nearest=3.1f;
            for(const auto& [key,body]:world.actors->bodies())if(key.type!=0&&body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&vm.field(body.script,13).integer()>0){
                const float d=length(at-Vec2{body.pose.x,body.pose.z});if(d<std::min(nearest,2.4f+float(body.radius)*.01f)&&
                    world.nativeScene().queries.lineOfSight({ash.pose.x,ash.pose.y,ash.pose.z},{body.pose.x,body.localY,body.pose.z},4,world.sourceQueryCamera(),world.nativeScene().enabledVolumes,world.visibilityReady)){enemy=&body;nearest=d;}}
            if(enemy){const auto key=enemy->key;const int health=vm.field(enemy->script,13).integer();tick(toward({enemy->pose.x,enemy->pose.z},1),false,0,true);tick({},false,.033f,true);
                require(++combat<1800,"nearby encounter exceeded bounded input budget");const auto current=world.actors->bodies().find(key);
                if(current!=world.actors->bodies().end()&&vm.field(current->second.script,13).integer()<health)++hits;stalled=0;previous=std::numeric_limits<float>::infinity();continue;}
            stalled=remaining>=previous-.00001f?stalled+1:0;require(stalled<120,"route stalled before the original return portal");previous=remaining;
            tick(toward(target,std::min(1.f,remaining/(world.scene.speed*2*.033f))));}
        throw std::runtime_error("Original AH05 return walk: route exceeded bounded input budget");};
    ready();
    // Reverse the published source-nav corridor; these are route waypoints, not pose setters.
    for(unsigned index:{19,18,17,16,15,14,13,23,12,9,8,22,0}){auto point=nav(index);
        if(index==23||index==13)point.x-=.12f;if(index==14)point.z-=.15f;if(index==18)point.x=-21.105f;walk(point);}
    walk({-12.25f,-4.68f},true);require(world.scene.id=="ah02"&&world.transitions==previousTransitions+1,"native portal2 did not reach AH02");ready();
    require(vm.staticField(435).integer()&&vm.staticField(438).integer()&&vm.staticField(440).integer()&&vm.staticField(430).integer()&&vm.staticField(686).integer()&&
        !vm.staticField(457).integer()&&quantity(30)==1&&quantity(170)==moonshine&&!quantity(161),"return changed earned missions/items or granted the later crowbar");
    const auto saved=world.saveStateBytes();for(unsigned n=0;n<3;++n)tick();world.restoreStateBytes(saved,nullptr);
    require(world.saveStateBytes()==saved,"actual AH02 arrival did not restore byte-exactly");
    std::cout<<"Original AH05 walked return: same earned686 -> reverse authored southern nav/barrel gaps -> native portal2/AH02; "<<frames<<" source frames, "<<distance
        <<"m displacement, "<<combat<<" ordinary combat frames/"<<hits<<" damage steps, HP"<<vm.field(world.game.ash,13).integer()<<", exact full save; no pose setters\n";
}

void testAH05ReturnWalkWorld(const fs::path& root) {
    testEpisode(root,false,640,480,{},[](World& world){continueAH02WalkWorld(world);continueAH02HandleWalkWorld(world);continueAH05WalkWorld(world);continueAH05ReturnWalkWorld(world);});
}
