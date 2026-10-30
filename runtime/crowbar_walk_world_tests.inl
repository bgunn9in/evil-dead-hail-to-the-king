#pragma once
#include "source_inventory_ui.inl"
#include "controller_health_pickup.h"
#include "ah05_walk_world_tests.inl"

void continueCrowbarWalkWorld(World& world) {
    std::cout<<std::unitbuf;auto& vm=world.game.vm;
    auto require=[](bool condition,const std::string& text){if(!condition)throw std::runtime_error("Original crowbar walk: "+text);};
    auto quantity=[&](int id){int total=0;for(int field:{98,99,100})for(const auto& item:world.game.items(field))if(item.itemID==id)total+=item.quantity;return total;};
    require(world.scene.id=="ah05"&&vm.staticField(686).integer()&&vm.staticField(430).integer()&&vm.staticField(440).integer()&&
        !vm.staticField(457).integer()&&!quantity(161),"requires the same earned686 World before crowbar161");
    const auto saw=vm.field(world.game.ash,102).reference();require(vm.field(saw,812).integer()==100,"actual primary100 absent");unsigned poweredHits=0;int maximumPoweredHit=0;
    const unsigned previousTransitions=world.transitions;unsigned frames=0,combatFrames=0,hits=0,recoveries=0;float distance=0,minRadius=world.actor.clearanceRadius,recoveryDistance=0;
    int previousHealth=vm.field(world.game.ash,13).integer();
    auto tick=[&](Vec2 stick=Vec2{},bool use=false,float dt=.033f,bool attack=false){const std::string area=world.scene.id;const Vec2 old{world.actor.position[0],world.actor.position[2]};
        ControlFrame input;input.movement=stick;input.down[size_t(ControlAction::Aim)]=attack;const bool powered=attack&&vm.field(vm.field(world.game.ash,103).reference(),812).integer()!=40&&vm.field(saw,815).integer()>=2;
        input.edges[size_t(powered?ControlAction::PrimaryAttack:ControlAction::SecondaryAttack)]=attack&&dt>0&&world.sourceTick%6==0;
        input.edges[size_t(ControlAction::ChainsawRev)]=powered&&(vm.field(saw,139).integer()==0||vm.field(saw,139).integer()==2);
        world.playerControls(input);const auto moved=world.step(stick,true,use,dt);require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);
        const auto& body=world.actors->at({0,0});const Vec2 at{body.pose.x,body.pose.z};
        const int health=vm.field(world.game.ash,13).integer();if(health<previousHealth)std::cout<<"Crowbar route incoming damage "<<previousHealth<<" -> "<<health<<" scene"<<world.scene.id
            <<" tick"<<world.sourceTick<<" at("<<body.pose.x<<','<<body.pose.z<<")\n";
        previousHealth=health;
        require(health>0,"Ash naturally died on the return route");
        require(floorHeight(world.scene.floor,at).has_value()&&clearGround(world.scene.ground,at,world.actor.clearanceRadius),"walking left floor or penetrated enabled source solids");
        require(moved.requested<=std::min(1.f,length(stick))*world.scene.speed*2*frameSeconds(dt)+ContactSkin*4,"requested motion exceeded the input speed bound");
        if(moved.moved>moved.requested+ContactSkin*4){auto combined=world.scene.ground;for(auto obstacle:world.manualActorObstacles()){
                obstacle.circleRadius+=world.scene.radius;combined.push_back(std::move(obstacle));}
            const auto recovered=recoverGround(combined,old,0);const float correction=length(recovered-old);
            // A source-frame spawn can overlap the old point; certify only the
            // existing dynamic separation, including its static-safe segment.
            require(area==world.scene.id&&clearGround(world.scene.ground,old,0)&&!clearGround(combined,old,0)&&clearGround(combined,at,0)&&
                length(moveGround(world.scene.ground,old,recovered-old,0)-recovered)<ContactSkin*4&&moved.moved<=moved.requested+correction+ContactSkin*4,
                "extra movement was not bounded static-safe dynamic recovery");++recoveries;recoveryDistance+=correction;
            if(recoveries==1)std::cout<<"Verified dynamic recovery: scene"<<area<<" tick"<<world.sourceTick<<" correction"<<correction<<" requested"<<moved.requested<<" moved"<<moved.moved<<'\n';}
        if(world.scene.id==area)distance+=length(at-old);
        minRadius=std::min(minRadius,world.actor.clearanceRadius);frames+=dt>0;};
    auto ready=[&](){for(unsigned n=0;n<480;++n){if(world.game.mode!=0)world.closeInventory();world.playerControls({});const auto& ash=world.actors->at({0,0});
            if(!(ash.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&
               !vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&ash.actions.possible(116))return;tick();}
        throw std::runtime_error("Original crowbar walk: source input did not become ready");};
    auto equip=[&](int id){ready();world.openInventory();world.syncInventory();const auto entry=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),
            [&](const auto& item){return item.containerField==98&&item.itemID==id;});require(entry!=world.inventoryView.items.end(),"requested weapon is not genuinely owned");
        world.inventoryView.selected=unsigned(entry-world.inventoryView.items.begin());world.applyInventoryItem();
        require(vm.field(vm.field(world.game.ash,103).reference(),812).integer()==id,"original inventory did not equip the owned weapon");ready();
        std::cout<<"Crowbar route original Inventory equip"<<id<<" loaded"<<vm.field(vm.field(world.game.ash,103).reference(),815).integer()<<" reserve3="<<quantity(3)<<'\n';};
    auto toward=[&](Vec2 target,float magnitude){const auto& camera=world.scene.cameras.at(world.actor.camera);const auto& latch=world.actor.movement;
        const Vec2 right=latch.held?latch.right:camera.right,forward=latch.held?latch.forward:camera.forward,delta=target-Vec2{world.actor.position[0],world.actor.position[2]};
        const float determinant=right.x*forward.z-right.z*forward.x;require(std::abs(determinant)>.001f,"camera basis is singular");Vec2 stick{(delta.x*forward.z-delta.z*forward.x)/determinant,(right.x*delta.z-right.z*delta.x)/determinant};
        const float size=length(stick);return size>.000001f?stick*(magnitude/size):Vec2{};};
    auto nav=[&](unsigned index){uint32_t count,offset;std::memcpy(&count,world.scene.nativeBytes.data()+48,4);std::memcpy(&offset,world.scene.nativeBytes.data()+52,4);
        require(index<count,"source nav point missing");GroundPoint p;std::memcpy(&p,world.scene.nativeBytes.data()+offset+index*44+8,12);return Vec2{p.x,p.z};};
    auto walk=[&](Vec2 target,const char* destination=""){tick({},false,0);const std::string source=world.scene.id;unsigned stalled=0;float previous=std::numeric_limits<float>::infinity();
        for(unsigned n=0;n<2200;++n){if(world.scene.id!=source){require(world.scene.id==destination,"unexpected portal destination "+source+"->"+world.scene.id+"; expected "+destination);return;}
            if(world.game.mode!=0){ready();continue;}const Vec2 point{world.actor.position[0],world.actor.position[2]};const float remaining=length(target-point);if(remaining<.04f){tick({},false,0);return;}
            auto weapon=vm.field(world.game.ash,103).reference();if(vm.field(weapon,812).integer()==40&&vm.field(weapon,815).integer()<=0&&quantity(3)==0){equip(10);weapon=vm.field(world.game.ash,103).reference();}
            const bool firearm=vm.field(weapon,812).integer()==40;const edactors::Body* enemy=nullptr;float nearest=firearm?6.f:3.1f;const auto& ash=world.actors->at({0,0});
            for(const auto& [key,body]:world.actors->bodies())if(key.type!=0&&body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&vm.field(body.script,13).integer()>0){
                const float d=length(point-Vec2{body.pose.x,body.pose.z});if(d<(firearm?nearest:std::min(nearest,2.4f+float(body.radius)*.01f))&&
                    world.nativeScene().queries.lineOfSight({ash.pose.x,ash.pose.y,ash.pose.z},{body.pose.x,body.localY,body.pose.z},4,world.sourceQueryCamera(),world.nativeScene().enabledVolumes,world.visibilityReady)){enemy=&body;nearest=d;}}
            if(enemy){const auto key=enemy->key;const int health=vm.field(enemy->script,13).integer();tick(toward({enemy->pose.x,enemy->pose.z},1),false,0,true);tick({},false,.033f,true);
                require(++combatFrames<3600,"actual nearby encounters exceeded bounded input budget");const auto current=world.actors->bodies().find(key);
                if(current!=world.actors->bodies().end()){const int change=health-vm.field(current->second.script,13).integer();if(change>0){++hits;if(vm.field(saw,139).integer()!=0&&change>=20){++poweredHits;maximumPoweredHit=std::max(maximumPoweredHit,change);}}}stalled=0;previous=std::numeric_limits<float>::infinity();continue;}
            if(collectNearbySourceHealth(world)){previousHealth=vm.field(world.game.ash,13).integer();stalled=0;previous=std::numeric_limits<float>::infinity();continue;}
            stalled=remaining>=previous-.00001f?stalled+1:0;
            require(stalled<120,"route stalled in "+source+" at "+std::to_string(point.x)+","+std::to_string(point.z)+" toward "+std::to_string(target.x)+","+std::to_string(target.z));
            previous=remaining;tick(toward(target,std::min(1.0f,remaining/(world.scene.speed*2*.033f))));}
        throw std::runtime_error("Original crowbar walk: leg exceeded bounded input budget");};
    auto save=[&](){ready();const auto bytes=world.saveStateBytes();for(unsigned n=0;n<3;++n)tick();world.restoreStateBytes(bytes,nullptr);require(world.saveStateBytes()==bytes,"earned walking boundary failed exact full save");};
    ready();equip(40);for(unsigned index:{19,18,17,16,15,14,13,23,12,9,8,22,0}){auto p=nav(index);if(index==23||index==13)p.x-=.12f;if(index==14)p.z-=.15f;if(index==18)p.x=-21.105f;walk(p);}
    walk({-12.25f,-4.68f},"ah02");require(world.scene.id=="ah02","real return portal2 did not reach AH02");save();std::cout<<"Walked boundary AH05->AH02 at tick"<<world.sourceTick<<'\n';
    for(unsigned index:{18,20,21,22,23,24,25,26,28,29,11,98,88,36,37,38,92})walk(nav(index));
    walk({19.1f,-17.8f},"ah50");require(world.scene.id=="ah50","real AH02 portal4 did not reach AH50");save();std::cout<<"Walked boundary AH02->AH50 at tick"<<world.sourceTick<<'\n';
    for(unsigned index:{33,31,3,2,24})walk(nav(index));walk({20.2f,5.6f},"ak09");require(world.scene.id=="ak09","real AH50 portal5 did not reach AK09");save();std::cout<<"Walked boundary AH50->AK09 at tick"<<world.sourceTick<<'\n';
    for(unsigned index:{50,5,7,12})walk(nav(index));walk({2.3933036f,14.55f});ready();const auto crowbar=world.scene.scripting.markers.at(11);
    tick(toward({crowbar.x,crowbar.z},1),false,0);require(world.scene.scripting.facing(11,45,{world.actor.position[0],world.actor.position[2]},world.actor.yaw),"real direction input did not face crowbar11");
    for(unsigned n=0;n<240&&!quantity(161);++n){if(world.game.mode!=0)ready();if(n%60==0)tick({},true,0);tick();}
    require(quantity(161)==1&&vm.staticField(457).integer()==1&&vm.staticField(686).integer()&&vm.staticField(430).integer()&&vm.staticField(435).integer()&&
        vm.staticField(438).integer()&&vm.staticField(440).integer()&&quantity(30)==1&&world.transitions==previousTransitions+3,
        "real pickup contact did not earn crowbar161/457/preserve earlier items/passages");save();
    std::cout<<"Original crowbar walked return: AH05/686 ->AH02 ->AH50 ->AK09 ->real marker11/Use116/contact161/457; "<<frames<<" source frames, "<<distance
        <<"m displacement, "<<combatFrames<<" combat frames/"<<hits<<" observed damage steps, "<<recoveries<<" certified dynamic recoveries/"<<recoveryDistance
        <<"m correction, minClearance"<<minRadius<<", poweredHits"<<poweredHits<<"/max"<<maximumPoweredHit<<", fuel"<<vm.field(saw,815).integer()<<", HP"<<vm.field(world.game.ash,13).integer()<<", exact boundary saves\n";
}

void testCrowbarWalkWorld(const fs::path& root) {
    testEpisode(root,false,640,480,{},[](World& world){continueAH02WalkWorld(world);continueAH02HandleWalkWorld(world);continueAH05WalkWorld(world);continueCrowbarWalkWorld(world);});
}
