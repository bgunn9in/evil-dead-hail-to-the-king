#pragma once
#include "story_pages_world_tests.inl"
// Earn both remaining original keys and follow their actual scene-use branches.
// Only Ash position/orientation fixtures are allowed; enemies retain source AI.
void continueTroopStoryWorld(World& world) {
        auto require=[](bool value,const std::string& message){if(!value)throw std::runtime_error("Earned Troop World: "+message);};
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
                tick();}throw std::runtime_error("Earned Troop World: Use did not become ready in "+world.scene.id);};
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


        auto queued=[&](const char* destination){require(world.pendingSceneSwitch&&edpackage::sceneName(world.pendingSceneSwitch->destinationId)==destination,
                "source did not request "+std::string(destination));const auto request=*world.pendingSceneSwitch;world.pendingSceneSwitch.reset();
            require(world.switchScene(request,nullptr)&&world.scene.id==destination,"queued source transition failed: "+world.notice);};
        require(world.scene.id=="ab01"&&vm.staticField(425).integer()==1&&quantity(75)==1,"earned car/page75 state missing");
        ready();{auto& ash=world.actors->at({0,0});const auto marker=world.scene.scripting.markers.at(7);
            ash.pose.yaw=edactormath::modelYawFromHeading(float(std::atan2(double(marker.z)-ash.pose.z,double(marker.x)-ash.pose.x)));
            world.publishProgressBody(ash);world.use();}
        for(unsigned n=0;n<300&&vm.staticField(409).integer();++n)tick();
        require(vm.staticField(409).integer()==0,"source climb-down129/end34 did not leave the car roof");
        pickup(11,6);require(vm.staticField(410).integer()==1,"original car-push object11 was not earned");ready();saveRoundtrip();
        passage("ab01",2,"ab50");passage("ab50",3,"ak09");
        pickup(107,57);require(vm.staticField(402).integer()==1,"original AK09 key107 callback did not set402");ready();saveRoundtrip();
        passage("ak09",5,"ab50");passage("ab50",2,"ab01");passage("ab01",3,"ac52");passage("ac52",3,"ac01");passage("ac01",5,"ac03");
        require(!vm.staticField(403).integer(),"AC03 key gate already solved");useItem(107,51,15);
        require(vm.staticField(403).integer()==1&&quantity(107)==0,"actual AC03 key use did not consume107/set403");queued("ac14");
        ready();useItem(11,10,3);require(vm.staticField(462).integer()==1&&quantity(11)==0,"actual AC14 battery use did not consume11/set462");
        ready();saveRoundtrip();passage("ac14",2,"ac04");passage("ac04",3,"ac05");ready();
        const edactors::Key leader{10,0};for(unsigned n=0;n<300&&!world.actors->bodies().count(leader);++n)tick();
        require(world.actors->bodies().count(leader)!=0,"original AC05 spawn did not create TroopLeader10");
        require(vm.field(world.actors->at(leader).script,13).integer()==100&&!vm.staticField(606).integer(),"TroopLeader source HP/default differs");
        saveRoundtrip();
        if(vm.field(world.game.ash,13).integer()<65&&quantity(56)) {
            world.openInventory();world.syncInventory();const int before=quantity(56),health=vm.field(world.game.ash,13).integer();
            const auto found=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[](const auto& item){return item.containerField==99&&item.itemID==56;});
            require(found!=world.inventoryView.items.end(),"earned medicine not present");world.inventoryView.selected=unsigned(found-world.inventoryView.items.begin());world.applyInventoryItem();
            require(quantity(56)==before-1&&vm.field(world.game.ash,13).integer()>health,"source medicine did not consume/heal");ready();
        }
        const auto boss=world.actors->at(leader).script;unsigned blows=0,lureTicks=0;
        for(unsigned turn=0;turn<10&&vm.field(boss,13).integer()>0;++turn) {
            const unsigned pillar=turn%3+1;const auto& playerRegion=world.scene.scripting.volumes.at(pillar-1);
            const auto center=playerRegion.points[0];const auto& enemyRegion=world.scene.scripting.volumes.at(pillar+2);
            // An opposite-side Ash approach lures the unmodified following AI
            // into the pillar's larger source enemy sphere. Never move the NPC.
            const auto& enemy=world.actors->at(leader);Vec2 selected{};bool found=false;float score=-std::numeric_limits<float>::infinity();
            for(unsigned direction=0;direction<64;++direction){const float angle=float(direction)*edactormath::Tau/64;
                const Vec2 at{center.x+std::sin(angle),center.z+std::cos(angle)};
                if(!playerRegion.contains({at.x,0,at.z})||!floorHeight(world.scene.floor,at)||!clearGround(world.scene.ground,at,world.scene.radius+.05f))continue;
                const float away=length(at-Vec2{enemy.pose.x,enemy.pose.z});if(away>score){score=away;selected=at;found=true;}}
            require(found,"no clear source pillar approach");place(selected,pillar);
            unsigned waited=0;for(;waited<1500&&!enemyRegion.contains({world.actors->at(leader).pose.x,world.actors->at(leader).localY,world.actors->at(leader).pose.z});++waited)tick();
            lureTicks+=waited;require(waited<1500,"source AI did not enter pillar "+std::to_string(pillar)+"; NPC="+
                std::to_string(world.actors->at(leader).pose.x)+","+std::to_string(world.actors->at(leader).pose.z)+" state="+std::to_string(world.actors->at(leader).actions.layers[0].state));
            const int health=vm.field(boss,13).integer();
            for(unsigned frame=0;frame<120&&vm.field(boss,13).integer()==health;++frame){
                place(selected,pillar);
                const auto marker=world.scene.scripting.markers.at(pillar);auto& ash=world.actors->at({0,0});
                ash.pose.yaw=edactormath::modelYawFromHeading(float(std::atan2(double(marker.z)-ash.pose.z,double(marker.x)-ash.pose.x)));world.publishProgressBody(ash);
                ControlFrame input;input.down[size_t(ControlAction::Aim)]=true;input.edges[size_t(ControlAction::SecondaryAttack)]=world.sourceTick%6==0;
                world.playerControls(input);tick();}
            require(vm.field(boss,13).integer()==std::max(0,health-11),"original pillar input did not apply fixed11 damage");++blows;if(blows==1)saveRoundtrip();
            std::cout<<"Earned pillar "<<pillar<<" NPC health "<<vm.field(boss,13).integer()<<" Ash "<<vm.field(world.game.ash,13).integer()<<" lure "<<waited<<std::endl;
        }
        require(blows==10&&vm.field(boss,13).integer()==0,"ten original pillar strikes did not defeat leader");
        for(unsigned n=0;n<650&&!vm.staticField(606).integer();++n)tick();require(vm.staticField(606).integer()==1,"original TroopLeader death did not earn606");
        for(unsigned n=0;n<180&&!quantity(53);++n){if(world.game.mode!=0)world.closeInventory();tick();}
        require(quantity(53)==1&&vm.staticField(414).integer()==1,"actual AC05 death did not award key53/414");ready();saveRoundtrip();
        std::cout<<"Earned Troop World: AB01 object11 +AK09 key107 ->original AC03/AC14 use403/462 ->actual AC05 TroopLeader10/ten pillars/death606/key53; "
            <<positions<<" Ash-only source-region fixtures and "<<lureTicks<<" natural AI lure ticks\n";

}
void testTroopStoryWorld(const fs::path& root,const std::function<void(World&)>& continueStory={}) {
    testPagesStoryWorld(root,[&](World& world){continueTroopStoryWorld(world);if(continueStory)continueStory(world);});
}
