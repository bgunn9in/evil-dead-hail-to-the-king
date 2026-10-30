#pragma once
#include "crowbar_walk_world_tests.inl"
#include "inventory_combine_tests.inl"

void continueAH05PlatformWalkWorld(World& world) {
    std::cout<<std::unitbuf;auto& vm=world.game.vm;
    auto require=[](bool condition,const std::string& text){if(!condition)throw std::runtime_error("Original AH05 platform walk: "+text);};
    auto quantity=[&](int id){int total=0;for(int field:{98,99,100})for(const auto& item:world.game.items(field))if(item.itemID==id)total+=item.quantity;return total;};
    require(world.scene.id=="ak09"&&vm.staticField(457).integer()&&quantity(161)==1&&vm.staticField(430).integer()&&vm.staticField(686).integer()&&
        !vm.staticField(456).integer()&&!vm.staticField(458).integer()&&!quantity(54),"requires the same actually walked crowbar World");
    bool authored=false;unsigned poweredHits=0;int maximumPoweredHit=0;const auto saw=vm.field(world.game.ash,102).reference();
    require(vm.field(saw,812).integer()==100,"original primary Chainsaw100 is absent");
    const unsigned previousTransitions=world.transitions;unsigned frames=0,combatFrames=0,hits=0,recoveries=0;float distance=0,minRadius=world.actor.clearanceRadius,recoveryDistance=0;
    int previousHealth=vm.field(world.game.ash,13).integer();
    auto tick=[&](Vec2 stick=Vec2{},bool use=false,float dt=.033f,bool attack=false){const std::string area=world.scene.id;const Vec2 old{world.actor.position[0],world.actor.position[2]};
        ControlFrame input;input.movement=stick;input.down[size_t(ControlAction::Aim)]=attack;const bool powered=attack&&vm.field(saw,815).integer()>=2;
        input.edges[size_t(powered?ControlAction::PrimaryAttack:ControlAction::SecondaryAttack)]=attack&&dt>0&&world.sourceTick%6==0;
        input.edges[size_t(ControlAction::ChainsawRev)]=powered&&(vm.field(saw,139).integer()==0||vm.field(saw,139).integer()==2);
        world.playerControls(input);const auto moved=world.step(stick,true,use,dt);require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);
        const auto& body=world.actors->at({0,0});const Vec2 at{body.pose.x,body.pose.z};
        const int health=vm.field(world.game.ash,13).integer();if(health<previousHealth)std::cout<<"Platform route incoming damage "<<previousHealth<<" -> "<<health<<" scene"<<world.scene.id
            <<" tick"<<world.sourceTick<<" at("<<body.pose.x<<','<<body.pose.z<<")\n";
        previousHealth=health;
        require(health>0,"Ash naturally died on the return route");
        // Source712 places marker23 inside re-enabled crate45; this phase has no manual movement.
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
                "extra movement was not bounded static-safe dynamic recovery");++recoveries;recoveryDistance+=correction;
            if(recoveries==1)std::cout<<"Verified dynamic recovery: scene"<<area<<" tick"<<world.sourceTick<<" correction"<<correction<<" requested"<<moved.requested<<" moved"<<moved.moved<<'\n';}
        if(world.scene.id==area)distance+=length(at-old);
        minRadius=std::min(minRadius,world.actor.clearanceRadius);frames+=dt>0;};
    auto ready=[&](){for(unsigned n=0;n<480;++n){if(world.game.mode!=0)world.closeInventory();world.playerControls({});const auto& ash=world.actors->at({0,0});
            if(!(ash.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&
               !vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&ash.actions.possible(116))return;tick();}
        throw std::runtime_error("Original AH05 platform walk: source input did not become ready");};
    auto useOwned=[&](int id){ready();world.openInventory();world.syncInventory();const auto entry=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),
            [&](const auto& item){return item.containerField==99&&item.itemID==id;});require(entry!=world.inventoryView.items.end(),"requested item is not actually owned");
        world.inventoryView.selected=unsigned(entry-world.inventoryView.items.begin());world.applyInventoryItem();require(!world.scriptFault,"original item use failed");};
    auto refuel=[&](int id){if(vm.field(saw,815).integer()>=vm.field(saw,817).integer()||!quantity(id))return;const int count=quantity(id),before=vm.field(saw,815).integer();
        useOwned(id);require(quantity(id)==count-1&&vm.field(saw,815).integer()>before,"owned fuel was not consumed by original inventory use");
        std::cout<<"Original fuel item"<<id<<" "<<before<<" -> "<<vm.field(saw,815).integer()<<'\n';ready();};
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
                if(current!=world.actors->bodies().end()){const int change=health-vm.field(current->second.script,13).integer();if(change>0){++hits;
                    if(vm.field(saw,139).integer()!=0&&change>=20){++poweredHits;maximumPoweredHit=std::max(maximumPoweredHit,change);}}}stalled=0;previous=std::numeric_limits<float>::infinity();continue;}
            stalled=remaining>=previous-.00001f?stalled+1:0;
            require(stalled<120,"route stalled in "+source+" at "+std::to_string(point.x)+","+std::to_string(point.z)+" toward "+std::to_string(target.x)+","+std::to_string(target.z));
            previous=remaining;tick(toward(target,std::min(1.0f,remaining/(world.scene.speed*2*.033f))));}
        throw std::runtime_error("Original AH05 platform walk: leg exceeded bounded input budget");};
    auto save=[&](){ready();const auto bytes=world.saveStateBytes();for(unsigned n=0;n<3;++n)tick();world.restoreStateBytes(bytes,nullptr);require(world.saveStateBytes()==bytes,"earned walking boundary failed exact full save");};
    ready();refuel(24);
    for(unsigned index:{12,7,5,50})walk(nav(index));walk({-19.2529526f,-10.5664947f},"ah50");require(world.scene.id=="ah50","native AK09 portal6 did not reach AH50");save();
    std::cout<<"Platform walking boundary AK09->AH50 tick"<<world.sourceTick<<'\n';
    for(unsigned index:{24,2,3,31,33})walk(nav(index));walk({-11.2635319f,5.31365061f},"ah02");require(world.scene.id=="ah02","earned AH50 portal4 did not reach AH02");save();
    std::cout<<"Platform walking boundary AH50->AH02 tick"<<world.sourceTick<<'\n';
    for(unsigned index:{92,38,37,36,88,98,11,29,28,26,25,24,23,22,21,20,18,19})walk(nav(index));walk({-14.85f,-3.4f},"ah05");
    require(world.scene.id=="ah05","native AH02 portal3 did not reach AH05");save();std::cout<<"Platform walking boundary AH02->AH05 tick"<<world.sourceTick<<'\n';
    require(world.transitions==previousTransitions+3,"return did not use exactly three original portals");ready();testInventoryCombineAtCheckpoint(world,true);ready();refuel(34);
    require(vm.staticField(441).integer()&&!vm.staticField(456).integer(),"original object-in-trigger30 did not prepare the platform");
    for(unsigned index:{0,22,8,9,10,11})walk(nav(index));walk({-18.1f,-12.7f});ready();
    const auto marker=world.scene.scripting.markers.at(7);tick(toward({marker.x,marker.z},1),false,0);tick({},true,0);
    require(vm.field(world.game.ash,117).integer()==1,"actual volume63/facing7/Use did not request climb-up");authored=true;
    const auto climb=world.saveStateBytes();unsigned climbSteps=0;for(;climbSteps<300&&!vm.staticField(456).integer();++climbSteps)tick();
    require(vm.staticField(456).integer()==1,"original actionClimb712/128/end34 did not complete");const auto topMarker=world.scene.scripting.markers.at(23);
    require(length(Vec2{world.actor.position[0]-topMarker.x,world.actor.position[2]-topMarker.z})<.001f,"original climb did not place Ash at authored marker23");
    const auto top=world.saveStateBytes();world.restoreStateBytes(climb,nullptr);for(unsigned n=0;n<climbSteps;++n)tick();require(world.saveStateBytes()==top,"pending original climb did not replay exactly");
    ready();const size_t movies=world.nativeProperties.movies.size();useOwned(161);require(vm.staticField(458).integer()==1,"original platform crowbar allowUseItem did not set458");
    for(unsigned n=0;n<180&&!quantity(54);++n){if(world.game.mode==2||world.game.mode==3)world.closeInventory();tick();}
    require(quantity(54)==1&&world.nativeProperties.movies.size()>movies,"original crowbar action/movie/report did not grant key54");save();
    require(poweredHits>0&&maximumPoweredHit>=20,"actual powered Chainsaw input did not deliver a source20+ damage hit");
    std::cout<<"Original walked platform: actual crowbar AK09 ->AH50 ->AH02 ->AH05; real recipe/converter/fuel, volume63/Use ->source climb128/marker23 ->actual item161/movie/key54; "
        <<frames<<" positive steps/"<<distance<<"m aggregate displacement (includes authored climb/replay), "<<combatFrames<<" combat frames/"<<hits<<" damage steps, "<<poweredHits<<" powered20+ hits/max"<<maximumPoweredHit
        <<", fuel"<<vm.field(saw,815).integer()<<", HP"<<vm.field(world.game.ash,13).integer()<<", climb replay"<<climbSteps<<", exact boundary saves; no host pose setters\n";
}

void testAH05PlatformWalkWorld(const fs::path& root) {
    testEpisode(root,false,640,480,{},[](World& world){continueAH02WalkWorld(world);continueAH02HandleWalkWorld(world);continueAH05WalkWorld(world);continueCrowbarWalkWorld(world);continueAH05PlatformWalkWorld(world);});
}
