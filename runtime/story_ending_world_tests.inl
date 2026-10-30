#pragma once
#include "story_offering_world_tests.inl"
void continueEarnedEndingWorld(World& world,const std::function<void(World&)>& continueLevelB={}) {
        auto require=[](bool value,const std::string& message){if(!value)throw std::runtime_error("Earned ending World: "+message);};
        auto& vm=world.game.vm;unsigned positions=0;
        auto quantity=[&](int identity){int count=0;for(int field:{98,99,100})
            for(const auto& item:world.game.items(field))if(item.itemID==identity)count+=item.quantity;return count;};
        auto tick=[&](){world.step({},false,false,.033f);
            require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);
            require(world.notice!="action is not connected yet"&&world.notice!="could not perform action","native input failed: "+world.game.diagnostic);
            require(vm.field(world.game.ash,13).integer()>0,"Ash died during the source-position story fixture");};
        auto ready=[&](){world.playerControls({});for(unsigned frame=0;frame<360;++frame){if(world.game.mode!=0)world.closeInventory();
                const auto& body=world.actors->at({0,0});
                if(!(body.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&
                    !vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&body.actions.possible(116))return;
                tick();}throw std::runtime_error("Earned ending World: Use did not become ready in "+world.scene.id);};
        auto place=[&](Vec2 point,unsigned marker){auto& ash=world.actors->at({0,0});
            require(floorHeight(world.scene.floor,point).has_value()&&clearGround(world.scene.ground,point,0),"position fixture is outside decoded floor");
            world.positionProgressBody(ash,{point.x,0,point.z});
            const auto target=world.scene.scripting.markers.at(marker);
            ash.pose.yaw=edactormath::modelYawFromHeading(float(std::atan2(double(target.z)-point.z,double(target.x)-point.x)));
            world.publishProgressBody(ash);world.actor.clearanceRadius=initialActorRadius(world.scene,point);world.actor.movement.held=false;++positions;};
        auto sample=[&](const auto& contains,const std::vector<Vec2>& points){require(!points.empty(),"source region has no points");
            float x0=points[0].x,x1=x0,z0=points[0].z,z1=z0;
            for(Vec2 point:points){x0=std::min(x0,point.x);x1=std::max(x1,point.x);z0=std::min(z0,point.z);z1=std::max(z1,point.z);}
            const Vec2 center{(x0+x1)*.5f,(z0+z1)*.5f};Vec2 chosen{};float best=-std::numeric_limits<float>::infinity();
            for(unsigned x=1;x<32;++x)for(unsigned z=1;z<32;++z){const Vec2 point{x0+(x1-x0)*float(x)/32,z0+(z1-z0)*float(z)/32};
                if(!contains(point)||!floorHeight(world.scene.floor,point)||!clearGround(world.scene.ground,point,0))continue;
                float clearance=world.scene.radius;while(clearance>0&&!clearGround(world.scene.ground,point,clearance))clearance=std::max(0.0f,clearance-.01f);
                const float score=clearance*10-length(point-center);if(score>best){best=score;chosen=point;}}
            require(std::isfinite(best),"source region has no valid floor sample in "+world.scene.id);return chosen;};
        auto approachVolume=[&](unsigned volume,unsigned marker){ready();const auto& region=world.scene.scripting.volumes.at(volume);
            std::vector<Vec2> points;for(const auto& point:region.points)points.push_back({point.x,point.z});
            const auto point=sample([&](Vec2 at){return region.contains({at.x,0,at.z});},points);place(point,marker);};
        auto useVolume=[&](unsigned volume,unsigned marker){approachVolume(volume,marker);world.use();
            require(world.notice!="action is not connected yet","source Use failed: "+world.game.diagnostic);};
        auto useItem=[&](int identity,unsigned volume,unsigned marker){approachVolume(volume,marker);world.openInventory();world.syncInventory();
            const auto item=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[&](const auto& entry){return entry.itemID==identity;});
            require(item!=world.inventoryView.items.end(),"earned item is absent from the source inventory: "+std::to_string(identity));
            world.inventoryView.selected=unsigned(item-world.inventoryView.items.begin());world.applyInventoryItem();
            require(world.notice!="action is not connected yet","source item use failed: "+world.game.diagnostic);};
        auto pickup=[&](int identity,unsigned marker){ready();const auto target=world.scene.scripting.markers.at(marker);
            const std::vector<Vec2> bounds{{target.x-1.3f,target.z-1.3f},{target.x+1.3f,target.z+1.3f}};
            const auto point=sample([&](Vec2 at){const auto distance=length(at-Vec2{target.x,target.z});return distance>.3f&&distance<1.25f;},bounds);
            place(point,marker);for(unsigned frame=0;frame<240&&!quantity(identity);++frame){if(frame%60==0)world.use();tick();}
            require(quantity(identity)>0,"original Use116/object contact did not grant source item "+std::to_string(identity));
            std::cout<<"Earned source object "<<identity<<" in "<<world.scene.id<<" at tick "<<world.sourceTick<<'\n';};
        auto saveRoundtrip=[&](){const auto bytes=world.saveStateBytes();for(unsigned n=0;n<4;++n)tick();world.restoreStateBytes(bytes,nullptr);
            require(world.saveStateBytes()==bytes,"earned inventory/mission/actors state did not restore exactly");};
        auto passage=[&](const char* expectedSource,unsigned index,const char* destination){
            if(world.scene.id!=expectedSource){require(world.scene.id==destination&&std::string(expectedSource)!=destination,"original passage entered an unexpected area");
                std::cout<<"Earned story passage "<<expectedSource<<" -> "<<world.scene.id<<" before original door wait\n";return;}
            require(index<world.scene.nativePortals.size(),"source portal is absent");
            const auto portal=world.scene.nativePortals.at(index);const std::string source=world.scene.id;
            const bool inPlace=source==destination;GroundPoint arrival{};
            if(inPlace){const auto original=std::find_if(world.scene.portalData.portals.begin(),world.scene.portalData.portals.end(),
                    [&](const auto& entry){return entry.index==index;});require(original!=world.scene.portalData.portals.end(),"source portal metadata missing");
                arrival=world.scene.scripting.markers.at(original->marker);}
            ready();if(!inPlace&&world.scene.id==destination){std::cout<<"Earned story passage "<<source<<" -> "<<world.scene.id<<" during original door wait\n";return;}
            require(world.scene.id==source,"source door entered an unexpected area while becoming ready");
            if(inPlace&&length(Vec2{world.actor.position[0]-arrival.x,world.actor.position[2]-arrival.z})<.02f)return;
            const auto point=sample([&](Vec2 at){return insideTrigger(portal,at);},portal.points);
            place(point,0);bool arrived=false;for(unsigned n=0;n<180&&!arrived;++n){tick();
                arrived=inPlace?(world.scene.id==source&&length(Vec2{world.actor.position[0]-arrival.x,world.actor.position[2]-arrival.z})<.02f):world.scene.id!=source;}
            require(arrived&&world.scene.id==destination,"source portal "+source+"/"+std::to_string(index)+" did not reach "+destination+"; "+world.notice);
            std::cout<<"Earned story passage "<<source<<" -> "<<world.scene.id<<" at tick "<<world.sourceTick<<'\n';};


        require(world.scene.id=="au01"&&quantity(101)==1,"earned DeadAsh/AU01 state missing");
        pickup(129,31);ready();useItem(129,31,15);
        require(quantity(129)==0&&vm.staticField(471).integer()==1,"actual AU01 item129 use did not consume/set471");ready();saveRoundtrip();
        if(!vm.staticField(472).integer()){useVolume(61,27);require(quantity(50)>0&&vm.staticField(472).integer()==1,"source AU01 healing pickup was not earned");ready();}
        auto heal=[&](){if(vm.field(world.game.ash,13).integer()>=75||!quantity(50))return;
            const int health=vm.field(world.game.ash,13).integer();world.openInventory();world.syncInventory();
            const auto medicine=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[](const auto& item){return item.containerField==99&&item.itemID==50;});
            require(medicine!=world.inventoryView.items.end(),"earned medicine50 absent");world.inventoryView.selected=unsigned(medicine-world.inventoryView.items.begin());world.applyInventoryItem();
            require(vm.field(world.game.ash,13).integer()>health,"original medicine50 did not heal");world.closeInventory();};
        heal();ready();useVolume(29,13);passage("au01",1,"au04");ready();
        const edactors::Key key{18,0};for(unsigned n=0;n<300&&!world.actors->bodies().count(key);++n)tick();
        require(world.actors->bodies().count(key)!=0,"original AU04 wave did not create Annie18");const auto boss=world.actors->at(key).script;
        const int anniePhaseField=world.game.profile.dreamcast?1392:1460;
        require(vm.field(boss,13).integer()==100&&!vm.field(boss,anniePhaseField).integer(),"original Annie initialization differs");
        bool sawTransform=false,phase=false;unsigned approaches=0,hits=0;int previousHealth=100;
        unsigned unreachable=0;
        auto approach=[&](){const auto& npc=world.actors->at(key);auto& ash=world.actors->at({0,0});
            const Vec2 behind{npc.pose.x-1.1f*std::sin(npc.pose.yaw),npc.pose.z-1.1f*std::cos(npc.pose.yaw)};
            if(length(behind-Vec2{ash.pose.x,ash.pose.z})>.1f){bool found=false;
                for(unsigned n=0;n<=32&&!found;++n)for(float radius:{1.1f,1.3f,1.5f,1.7f,1.9f,2.1f}){if(found)break;
                    const int offset=n==0?0:(n%2?int(n+1)/2:-int(n)/2);
                    const float angle=npc.pose.yaw+edactormath::Pi+float(offset)*edactormath::Tau/32;const Vec2 at{npc.pose.x+radius*std::sin(angle),npc.pose.z+radius*std::cos(angle)};
                    if(!floorHeight(world.scene.floor,at)||!clearGround(world.scene.ground,at,world.scene.radius+.3f))continue;
                    world.positionProgressBody(ash,{at.x,0,at.z});world.publishProgressBody(ash);world.actor.clearanceRadius=initialActorRadius(world.scene,at);world.actor.movement.held=false;++approaches;found=true;}
                if(!found){++unreachable;return false;}}
            ash.pose.yaw=edactormath::modelYawFromHeading(float(std::atan2(double(npc.pose.z)-ash.pose.z,double(npc.pose.x)-ash.pose.x)));world.publishProgressBody(ash);return true;};
        auto fight=[&](bool attack){if(vm.field(boss,13).integer()>0)heal();const bool reachable=approach();sawTransform|=world.actors->at(key).actions.layers[0].state==1;
            ControlFrame input;input.down[size_t(ControlAction::Aim)]=true;input.edges[size_t(ControlAction::SecondaryAttack)]=attack&&reachable&&world.sourceTick%6==0;world.playerControls(input);tick();
            sawTransform|=world.actors->at(key).actions.layers[0].state==1;const int health=vm.field(boss,13).integer();hits+=health<previousHealth?1:0;previousHealth=health;};
        for(unsigned n=0;n<2400&&!phase;++n){fight(vm.field(boss,13).integer()>=50);if(vm.field(boss,anniePhaseField).integer()){
                require(sawTransform&&vm.field(boss,13).integer()==100,"source136/end34 did not regenerate second phase");phase=true;}}
        require(phase&&hits>0,"real source Axe10 did not reach original Annie transformation");
        const auto pending=world.saveStateBytes();for(unsigned n=0;n<24;++n)fight(true);const auto replay=world.saveStateBytes();
        world.restoreStateBytes(pending,nullptr);previousHealth=vm.field(boss,13).integer();for(unsigned n=0;n<24;++n)fight(true);
        require(world.saveStateBytes()==replay,"earned Annie second-phase pending replay differs");
        for(unsigned n=0;n<3600&&!world.pendingSceneSwitch;++n)fight(vm.field(boss,13).integer()>0);
        require(vm.field(boss,13).integer()<=0&&vm.staticField(608).integer()==1,"source earned Annie death did not set608");
        require(world.pendingSceneSwitch&&world.pendingSceneSwitch->sourceSceneId==0x61750400&&world.pendingSceneSwitch->destinationId==0x626d0100&&
            world.pendingSceneSwitch->camera==10&&world.pendingSceneSwitch->marker==0,"actual AU04 ending did not queue original BM01");
        const auto request=*world.pendingSceneSwitch;world.pendingSceneSwitch.reset();const auto before=world.saveStateBytes();const int health=vm.field(world.game.ash,13).integer();
        auto next=stagePackageWorld(world,request);
        require(next->levelBPackage&&next->scene.id=="bm01"&&next->actor.camera==10&&world.saveStateBytes()==before,"earned ending did not stage fresh LevelB atomically");
        require(next->game.vm.field(next->game.ash,13).integer()==health,"real PlayerTransfer lost earned Ash health");
        const auto marker=next->scene.scripting.markers.at(0);const auto& ash=next->actors->at({0,0});
        require(ash.pose.x==marker.x&&ash.pose.z==marker.z&&ash.localY==0&&ash.pose.yaw==0,"real BM01 source marker/Euler differs");
        std::cout<<"Earned ending World: original AU01 item129/471 ->actual AU04 two-phase Annie/native136/end34/death608 ->original BM01 package/PlayerTransfer; "
            <<positions<<" source-region fixtures, "<<approaches<<" Ash-only fight approaches and "<<hits<<" observed damage steps, "<<unreachable<<" transient unreachable ticks; transferredHP "<<health<<"\n";
        if(continueLevelB)continueLevelB(*next);
}
void testEarnedEndingWorld(const fs::path& root,const std::function<void(World&)>& continueLevelB={}) {
    testOfferingStoryWorld(root,[&](World& world){continueEarnedEndingWorld(world,continueLevelB);});
}
