#pragma once
#include "ah02_handle_walk_world_tests.inl"

void continueAH05WalkWorld(World& world) {
    std::cout<<std::unitbuf;auto& vm=world.game.vm;const auto arrivalTransitions=world.transitions;
    auto require=[](bool condition,const std::string& text){if(!condition)throw std::runtime_error("Original AH05 walk: "+text);};
    auto quantity=[&](int id){int total=0;for(int field:{98,99,100})for(const auto& item:world.game.items(field))if(item.itemID==id)total+=item.quantity;return total;};
    require(world.scene.id=="ah05"&&vm.staticField(438).integer()&&vm.staticField(440).integer()&&quantity(170)>0&&quantity(30)==1&&
        !vm.staticField(686).integer(),"did not receive the same earned AH05 arrival");
    const auto saw=vm.field(world.game.ash,102).reference();require(vm.field(saw,812).integer()==100,"actual primary100 absent");
    unsigned poweredHits=0;int maximumPoweredHit=0;unsigned frames=0,combatFrames=0,hits=0;float distance=0,minRadius=world.actor.clearanceRadius;int previousHealth=vm.field(world.game.ash,13).integer();
    auto tick=[&](Vec2 stick=Vec2{},bool use=false,float dt=.033f,bool attack=false){const Vec2 old{world.actor.position[0],world.actor.position[2]};ControlFrame input;input.movement=stick;
        input.down[size_t(ControlAction::Aim)]=attack;const bool powered=attack&&vm.field(saw,815).integer()>=2;
        input.edges[size_t(powered?ControlAction::PrimaryAttack:ControlAction::SecondaryAttack)]=attack&&dt>0&&world.sourceTick%6==0;
        input.edges[size_t(ControlAction::ChainsawRev)]=powered&&(vm.field(saw,139).integer()==0||vm.field(saw,139).integer()==2);
        world.playerControls(input);const auto moved=world.step(stick,true,use,dt);require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);
        require(world.scene.id=="ah05","route left AH05 unexpectedly");const int health=vm.field(world.game.ash,13).integer();
        if(health<previousHealth)std::cout<<"AH05 route incoming damage "<<previousHealth<<" -> "<<health<<" at "<<world.actor.position[0]<<','<<world.actor.position[2]<<'\n';previousHealth=health;
        require(health>0,"Ash naturally died on the authored route");const Vec2 at{world.actor.position[0],world.actor.position[2]};
        require(floorHeight(world.scene.floor,at).has_value()&&clearGround(world.scene.ground,at,world.actor.clearanceRadius),"walking left floor or penetrated an active source solid");
        require(moved.moved<=moved.requested+ContactSkin*4,"walking exceeded requested motion");distance+=length(at-old);minRadius=std::min(minRadius,world.actor.clearanceRadius);frames+=dt>0;};
    auto ready=[&](){for(unsigned n=0;n<360;++n){if(world.game.mode!=0)world.closeInventory();world.playerControls({});const auto& ash=world.actors->at({0,0});
            if(!(ash.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&
               !vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&ash.actions.possible(116))return;tick();}
        throw std::runtime_error("Original AH05 walk: source action did not become ready");};
    ready();if(previousHealth<65&&quantity(56)>0){const int before=quantity(56),health=previousHealth;world.openInventory();world.syncInventory();
        const auto medicine=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[](const auto& item){return item.containerField==99&&item.itemID==56;});
        require(medicine!=world.inventoryView.items.end(),"owned medicine56 absent from real inventory");world.inventoryView.selected=unsigned(medicine-world.inventoryView.items.begin());world.applyInventoryItem();
        require(quantity(56)==before-1&&vm.field(world.game.ash,13).integer()>health,"original inventory medicine did not consume/heal");previousHealth=vm.field(world.game.ash,13).integer();
        std::cout<<"AH05 route original medicine56 "<<health<<" -> "<<previousHealth<<'\n';ready();}
    auto toward=[&](Vec2 target,float magnitude){const auto& camera=world.scene.cameras.at(world.actor.camera);const auto& latch=world.actor.movement;
        const Vec2 right=latch.held?latch.right:camera.right,forward=latch.held?latch.forward:camera.forward,delta=target-Vec2{world.actor.position[0],world.actor.position[2]};
        const float determinant=right.x*forward.z-right.z*forward.x;require(std::abs(determinant)>.001f,"camera basis is singular");Vec2 stick{(delta.x*forward.z-delta.z*forward.x)/determinant,(right.x*delta.z-right.z*delta.x)/determinant};
        const float size=length(stick);return size>.000001f?stick*(magnitude/size):Vec2{};};
    auto nav=[&](unsigned index){uint32_t count,offset;std::memcpy(&count,world.scene.nativeBytes.data()+48,4);std::memcpy(&offset,world.scene.nativeBytes.data()+52,4);
        require(index<count,"source nav point missing");GroundPoint p;std::memcpy(&p,world.scene.nativeBytes.data()+offset+index*44+8,12);return Vec2{p.x,p.z};};
    auto walk=[&](Vec2 target){tick({},false,0);unsigned stalled=0;float previous=std::numeric_limits<float>::infinity();
        for(unsigned n=0;n<1500;++n){if(world.game.mode!=0){ready();continue;}const float remaining=length(target-Vec2{world.actor.position[0],world.actor.position[2]});if(remaining<.04f){tick({},false,0);return;}
            const edactors::Body* enemy=nullptr;float nearest=3.1f;const auto& ash=world.actors->at({0,0});
            for(const auto& [key,body]:world.actors->bodies())if(key.type!=0&&body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&vm.field(body.script,13).integer()>0){
                const float separation=length(Vec2{body.pose.x-ash.pose.x,body.pose.z-ash.pose.z});
                // Source melee subtracts the victim radius and requires distance<250cm.
                if(separation<std::min(nearest,2.4f+float(body.radius)*.01f)&&world.nativeScene().queries.lineOfSight({ash.pose.x,ash.pose.y,ash.pose.z},{body.pose.x,body.localY,body.pose.z},
                    4,world.sourceQueryCamera(),world.nativeScene().enabledVolumes,world.visibilityReady)){nearest=separation;enemy=&body;}}
            if(enemy){const auto key=enemy->key;const int health=vm.field(enemy->script,13).integer();const Vec2 position{enemy->pose.x,enemy->pose.z};
                tick(toward(position,1),false,0,true);tick({},false,.033f,true);require(++combatFrames<2400,"nearby source encounter exceeded its input budget");
                const auto current=world.actors->bodies().find(key);if(current!=world.actors->bodies().end()){const int change=health-vm.field(current->second.script,13).integer();if(change>0){++hits;if(vm.field(saw,139).integer()!=0&&change>=20){++poweredHits;maximumPoweredHit=std::max(maximumPoweredHit,change);}}}
                stalled=0;previous=std::numeric_limits<float>::infinity();continue;}
            stalled=remaining>=previous-.00001f?stalled+1:0;require(stalled<120,"route stalled at "+std::to_string(world.actor.position[0])+","+std::to_string(world.actor.position[2])+" toward "+std::to_string(target.x)+","+std::to_string(target.z));
            previous=remaining;tick(toward(target,std::min(1.0f,remaining/(world.scene.speed*2*.033f))));}
        throw std::runtime_error("Original AH05 walk: route exceeded bounded input budget");};
    // Original graph joins the east/west corridors only around the south wall;
    // the route naturally crosses recipe volume6 rather than clipping wall28/32.
    for(unsigned index:{22,8,9,12,23,13,14,15,16,17,18,19}){auto point=nav(index);
        // Native nav points use a point body; leave the existing modern capsule
        // room beside the east wall and around the southern corner.
        if(index==23||index==13)point.x-=.12f;if(index==14)point.z-=.15f;
        if(index==18)point.x=-21.105f; // Center the narrow source barrel gap between volumes52/53.
        walk(point);}
    walk({-21.6f,-6.0f});ready();const auto marker=world.scene.scripting.markers.at(8);tick({},false,0);tick(toward({marker.x,marker.z},1),false,0);
    require(world.scene.scripting.inside(35,{world.actor.position[0],world.actors->at({0,0}).localY,world.actor.position[2]})&&
        world.scene.scripting.facing(8,40,{world.actor.position[0],world.actor.position[2]},world.actor.yaw),"walked approach missed original35/facing8");
    const int before=quantity(170);tick({},true,0);for(unsigned n=0;n<120&&!vm.staticField(686).integer();++n)tick();
    require(vm.staticField(686).integer()==1&&quantity(170)==before+1,"original allowUse1970/pickup170/report1971 did not grant686/add170");ready();
    require(vm.staticField(430).integer()==1&&quantity(130)==1,"southern source route did not naturally earn recipe130/430");
    require(world.transitions==arrivalTransitions&&vm.staticField(435).integer()&&vm.staticField(438).integer()&&vm.staticField(440).integer()&&quantity(30)==1&&!quantity(29),
        "walking changed earlier earned quests/items/passages");
    require(poweredHits>0&&maximumPoweredHit>=20,"original powered primary did not deliver20+ damage");
    const auto saved=world.saveStateBytes();for(unsigned n=0;n<4;++n)tick();world.restoreStateBytes(saved,nullptr);
    require(world.saveStateBytes()==saved,"walked AH05 quests/full state did not restore exactly");
    std::cout<<"Original AH05 walked interaction: actual source southern nav chain -> recipe130/430 -> volume35/facing8/Use140 ->extra170/686; "
        <<frames<<" source frames, "<<distance<<"m displacement, "<<combatFrames<<" original combat frames/"<<hits<<" damage steps, minClearance"<<minRadius
        <<", poweredHits"<<poweredHits<<"/max"<<maximumPoweredHit<<", fuel"<<vm.field(saw,815).integer()<<", HP"<<vm.field(world.game.ash,13).integer()<<", exact full save\n";
}

void testAH05WalkWorld(const fs::path& root) {
    testEpisode(root,false,640,480,{},[](World& world){continueAH02WalkWorld(world);continueAH02HandleWalkWorld(world);continueAH05WalkWorld(world);});
}
