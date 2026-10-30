#pragma once
#include "ah02_walk_world_tests.inl"

void continueAH02HandleWalkWorld(World& world) {
    std::cout<<std::unitbuf;auto& vm=world.game.vm;
    auto require=[](bool condition,const std::string& text){if(!condition)throw std::runtime_error("Original AH02 handle walk: "+text);};
    auto quantity=[&](int id){int total=0;for(int field:{98,99,100})for(const auto& item:world.game.items(field))if(item.itemID==id)total+=item.quantity;return total;};
    require(world.scene.id=="ah02"&&vm.staticField(438).integer()&&quantity(170)==1&&vm.staticField(435).integer()&&!quantity(29)&&
        !vm.staticField(440).integer()&&!quantity(30),"did not receive the same earned moonshine World");
    require(vm.field(vm.field(world.game.ash,103).reference(),812).integer()==10,"actual source left Axe10 is not equipped");
    unsigned combatFrames=0,hits=0,recoveries=0;unsigned frames=0;float distance=0;int lastHealth=vm.field(world.game.ash,13).integer();
    auto tick=[&](Vec2 stick=Vec2{},bool use=false,float dt=.033f,bool attack=false){const Vec2 old{world.actor.position[0],world.actor.position[2]};const std::string area=world.scene.id;ControlFrame input;input.movement=stick;input.down[size_t(ControlAction::Aim)]=attack;input.edges[size_t(ControlAction::SecondaryAttack)]=attack&&dt>0&&world.sourceTick%6==0;
        world.playerControls(input);const auto result=world.step(stick,true,use,dt);require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);
        const int health=vm.field(world.game.ash,13).integer();if(health<lastHealth)std::cout<<"Handle route incoming damage "<<lastHealth<<" -> "<<health<<" at "<<world.actor.position[0]<<','<<world.actor.position[2]<<'\n';lastHealth=health;
        require(health>0,"Ash naturally died on the route");const Vec2 point{world.actor.position[0],world.actor.position[2]};
        require(floorHeight(world.scene.floor,point).has_value()&&clearGround(world.scene.ground,point,world.actor.clearanceRadius),"walking left floor or penetrated an active source solid");
        if(result.moved>result.requested+ContactSkin*4){auto combined=world.scene.ground;for(auto obstacle:world.manualActorObstacles()){obstacle.circleRadius+=world.scene.radius;combined.push_back(std::move(obstacle));}
            const auto recovered=recoverGround(combined,old,0);const float correction=length(recovered-old);
            require(area==world.scene.id&&clearGround(world.scene.ground,old,0)&&!clearGround(combined,old,0)&&clearGround(combined,point,0)&&
                length(moveGround(world.scene.ground,old,recovered-old,0)-recovered)<ContactSkin*4&&result.moved<=result.requested+correction+ContactSkin*4,
                "extra movement was not bounded static-safe dynamic recovery");++recoveries;}
        require(result.requested<=std::min(1.f,length(stick))*world.scene.speed*2*frameSeconds(dt)+ContactSkin*4,"requested motion exceeded source input bound");if(world.scene.id==area)distance+=length(point-old);frames+=dt>0;
    };
    auto ready=[&](){for(unsigned n=0;n<360;++n){if(world.game.mode!=0)world.closeInventory();world.playerControls({});const auto& ash=world.actors->at({0,0});
            if(!(ash.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&
               !vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&ash.actions.possible(116))return;tick();}
        throw std::runtime_error("Original AH02 handle walk: action did not become ready");};
    ready();if(lastHealth<65&&quantity(56)>0){const int before=quantity(56),health=lastHealth;world.openInventory();world.syncInventory();
        const auto medicine=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[](const auto& item){return item.containerField==99&&item.itemID==56;});
        require(medicine!=world.inventoryView.items.end(),"owned source medicine56 was absent from the actual UI");world.inventoryView.selected=unsigned(medicine-world.inventoryView.items.begin());world.applyInventoryItem();
        require(quantity(56)==before-1&&vm.field(world.game.ash,13).integer()>health,"real medicine56 did not consume/heal");lastHealth=vm.field(world.game.ash,13).integer();
        std::cout<<"Handle route original Inventory/Ash medicine56 "<<health<<" -> "<<lastHealth<<'\n';ready();}
    auto equip=[&](int id){ready();world.openInventory();world.syncInventory();const auto entry=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),
            [&](const auto& item){return item.containerField==98&&item.itemID==id;});require(entry!=world.inventoryView.items.end(),"requested weapon is not genuinely owned");
        world.inventoryView.selected=unsigned(entry-world.inventoryView.items.begin());world.applyInventoryItem();require(vm.field(vm.field(world.game.ash,103).reference(),812).integer()==id,"original inventory did not equip owned weapon");ready();
        std::cout<<"Handle original Inventory equip"<<id<<" loaded"<<vm.field(vm.field(world.game.ash,103).reference(),815).integer()<<" reserve3="<<quantity(3)<<'\n';};
    equip(40);
    auto toward=[&](Vec2 target,float magnitude){const auto& camera=world.scene.cameras.at(world.actor.camera);const auto& latch=world.actor.movement;
        const Vec2 right=latch.held?latch.right:camera.right,forward=latch.held?latch.forward:camera.forward,delta=target-Vec2{world.actor.position[0],world.actor.position[2]};
        const float determinant=right.x*forward.z-right.z*forward.x;require(std::abs(determinant)>.001f,"camera basis is singular");Vec2 stick{(delta.x*forward.z-delta.z*forward.x)/determinant,(right.x*delta.z-right.z*delta.x)/determinant};
        const float size=length(stick);return size>.000001f?stick*(magnitude/size):Vec2{};};
    auto nav=[&](unsigned index){uint32_t count,offset;std::memcpy(&count,world.scene.nativeBytes.data()+48,4);std::memcpy(&offset,world.scene.nativeBytes.data()+52,4);
        require(index<count,"source navigation point is missing");GroundPoint point;std::memcpy(&point,world.scene.nativeBytes.data()+offset+index*44+8,12);return Vec2{point.x,point.z};};
    auto walk=[&](Vec2 target,bool mayTransfer=false){tick({},false,0);unsigned stalled=0;float previous=std::numeric_limits<float>::infinity();
        for(unsigned n=0;n<1500;++n){if(world.scene.id!="ah02"){require(mayTransfer&&world.scene.id=="ah05","unexpected source transition");return;}
            const float remaining=length(target-Vec2{world.actor.position[0],world.actor.position[2]});if(remaining<.04f){tick({},false,0);return;}
            const Vec2 point{world.actor.position[0],world.actor.position[2]};const auto& ash=world.actors->at({0,0});auto weapon=vm.field(world.game.ash,103).reference();if(vm.field(weapon,812).integer()==40&&vm.field(weapon,815).integer()<=0&&quantity(3)==0){equip(10);weapon=vm.field(world.game.ash,103).reference();}
            // Stay within the original Gun close-damage band; this is an input tactic.
            const bool firearm=vm.field(weapon,812).integer()==40;const edactors::Body* enemy=nullptr;float nearest=firearm?2.25f:3.1f;
            for(const auto& [key,body]:world.actors->bodies())if(key.type!=0&&body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&vm.field(body.script,13).integer()>0){
                const float d=length(point-Vec2{body.pose.x,body.pose.z});if(d<(firearm?nearest:std::min(nearest,2.4f+float(body.radius)*.01f))&&
                    world.nativeScene().queries.lineOfSight({ash.pose.x,ash.pose.y,ash.pose.z},{body.pose.x,body.localY,body.pose.z},4,world.sourceQueryCamera(),world.nativeScene().enabledVolumes,world.visibilityReady)){enemy=&body;nearest=d;}}
            if(enemy){const auto key=enemy->key;const int health=vm.field(enemy->script,13).integer();tick(toward({enemy->pose.x,enemy->pose.z},1),false,0,true);tick({},false,.033f,true);
                require(++combatFrames<3600,"actual weapon encounters exceeded bounded input budget");const auto now=world.actors->bodies().find(key);if(now!=world.actors->bodies().end()&&vm.field(now->second.script,13).integer()<health)++hits;
                stalled=0;previous=std::numeric_limits<float>::infinity();continue;}
            stalled=remaining>=previous-.00001f?stalled+1:0;require(stalled<120,"route stalled at "+std::to_string(world.actor.position[0])+","+std::to_string(world.actor.position[2]));
            previous=remaining;tick(toward(target,std::min(1.0f,remaining/(world.scene.speed*2*.033f))));}
        throw std::runtime_error("Original AH02 handle walk: route exceeded its bounded input budget");};
    for(unsigned index:{7,8,9,12,14,15,16,17,20,21,22,23,24,25,26,28,31})walk(nav(index));
    walk({2.05f,-18.65f});ready();const auto object=world.scene.scripting.markers.at(6);tick({},false,0);tick(toward({object.x,object.z},1),false,0);
    require(world.scene.scripting.facing(6,45,{world.actor.position[0],world.actor.position[2]},world.actor.yaw),"direction input did not face original handle marker6");
    for(unsigned n=0;n<240&&!quantity(30);++n){if(n%60==0)tick({},true,0);tick();}
    require(quantity(30)==1&&vm.staticField(440).integer()==1,"real Use116/contact did not grant source30/report440");ready();
    const auto saved=world.saveStateBytes();for(unsigned n=0;n<4;++n)tick();world.restoreStateBytes(saved,nullptr);require(world.saveStateBytes()==saved,"earned handle/full state did not roundtrip byte-exactly");
    std::cout<<"Original AH02 handle run: actual nav chain -> marker6/Use116/contact ->30/440; "<<frames<<" source frames, "<<distance<<"m, HP"<<vm.field(world.game.ash,13).integer()<<", exact save\n";
    for(unsigned index:{31,28,26,25,24,23,22,21,20,18,19})walk(nav(index));
    walk({-14.85f,-3.4f},true);for(unsigned n=0;n<60&&world.scene.id=="ah02";++n)tick();
    require(world.scene.id=="ah05"&&vm.staticField(435).integer()&&vm.staticField(438).integer()&&vm.staticField(440).integer()&&quantity(170)==1&&quantity(30)==1,
        "actual walked portal3 did not retain earned quests/items in AH05");
    if(vm.field(vm.field(world.game.ash,103).reference(),812).integer()!=10)equip(10);
    const auto arrivalSaved=world.saveStateBytes();world.restoreStateBytes(arrivalSaved,nullptr);require(world.saveStateBytes()==arrivalSaved,"walked AH05 arrival did not roundtrip byte-exactly");
    std::cout<<"Original AH02 walked return -> portal3/AH05; "<<frames<<" source frames, "<<distance<<"m total running, "<<combatFrames<<" real weapon frames/"<<hits<<" damage steps/"<<recoveries<<" certified recoveries, HP"<<vm.field(world.game.ash,13).integer()<<", exact arrival save\n";
}

void testAH02HandleWalkWorld(const fs::path& root) {
    testEpisode(root,false,640,480,{},[](World& world){continueAH02WalkWorld(world);continueAH02HandleWalkWorld(world);});
}
