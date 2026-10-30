#pragma once
#include "story_last_page_world_tests.inl"
#include "source_inventory_ui.inl"
void continueOfferingStoryWorld(World& world) {
        auto require=[](bool value,const std::string& message){if(!value)throw std::runtime_error("Earned offering World: "+message);};
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
                tick();}throw std::runtime_error("Earned offering World: Use did not become ready in "+world.scene.id);};
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
        require(world.scene.id=="ag01"&&vm.staticField(422).integer()&&vm.staticField(423).integer()&&vm.staticField(424).integer()&&vm.staticField(425).integer()&&vm.staticField(426).integer(),
            "original earned five-page gate state missing");
        pickup(44,11);ready();saveRoundtrip();useVolume(35,28);passage("ag01",4,"ag02");
        pickup(134,19);ready();pickup(99,12);ready();saveRoundtrip();useVolume(18,6);passage("ag02",4,"ag01");
        useItem(44,18,47);require(quantity(44)==0&&vm.staticField(466).integer()==1,"original first offering was not consumed");
        ready();useItem(134,29,24);require(quantity(134)==0&&vm.staticField(468).integer()==1,"original second offering was not consumed");
        ready();useItem(99,31,26);require(quantity(99)==0&&vm.staticField(467).integer()==1,"original third offering was not consumed");
        ready();for(unsigned n=0;n<180&&!vm.staticField(469).integer();++n)tick();require(vm.staticField(469).integer()==1,"original1914 three-offering solve did not set469");
        ready();saveRoundtrip();useVolume(34,27);passage("ag01",8,"ag03");ready();
        const edactors::Key key{17,0};for(unsigned n=0;n<300&&!world.actors->bodies().count(key);++n)tick();
        require(world.actors->bodies().count(key)!=0,"original AG03 spawn did not create DeadAsh17");const auto boss=world.actors->at(key).script;
        require(vm.field(boss,13).integer()==100&&!vm.staticField(607).integer(),"DeadAsh original initialization differs");
        world.openInventory();world.syncInventory();const auto axe=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[](const auto& item){return item.containerField==98&&item.itemID==10;});
        require(axe!=world.inventoryView.items.end(),"original Axe10 is absent");world.inventoryView.selected=unsigned(axe-world.inventoryView.items.begin());world.applyInventoryItem();ready();
        require(vm.field(vm.field(world.game.ash,103).reference(),812).integer()==10,"real inventory did not equip Axe10");
        unsigned hits=0,approaches=0;int previous=100;
        for(unsigned frame=0;frame<4000&&vm.field(boss,13).integer()>0;++frame){const auto& npc=world.actors->at(key);auto& ash=world.actors->at({0,0});bool found=false;
            // Native distanceToTarget subtracts Ash's radius; stay outside the source's full-circle attack reach.
            const float distance=std::max(world.game.profile.dreamcast?float(vm.field(boss,1260).integer()+ash.radius)*.01f+.2f:1.1f,float(npc.radius)*.01f+world.scene.radius+.3f);
            for(unsigned n=0;n<32&&!found;++n){const float angle=npc.pose.yaw+edactormath::Pi+float(n)*edactormath::Tau/32;
                const Vec2 at{npc.pose.x+distance*std::sin(angle),npc.pose.z+distance*std::cos(angle)};
                if(!floorHeight(world.scene.floor,at)||!clearGround(world.scene.ground,at,world.scene.radius+.12f))continue;
                world.positionProgressBody(ash,{at.x,0,at.z});ash.pose.yaw=edactormath::modelYawFromHeading(float(std::atan2(double(npc.pose.z)-at.z,double(npc.pose.x)-at.x)));
                world.publishProgressBody(ash);world.actor.clearanceRadius=initialActorRadius(world.scene,at);world.actor.movement.held=false;++approaches;found=true;}
            require(found,"no decoded-floor melee approach to original DeadAsh");
            if(world.game.profile.dreamcast)useSourceOwnedFuelItem(world);
            const auto saw=vm.field(world.game.ash,102).reference();
            ControlFrame input;input.down[size_t(ControlAction::Aim)]=true;
            input.edges[size_t(world.game.profile.dreamcast?ControlAction::PrimaryAttack:ControlAction::SecondaryAttack)]=world.sourceTick%6==0;
            input.edges[size_t(ControlAction::ChainsawRev)]=world.game.profile.dreamcast&&vm.field(saw,815).integer()>=2&&(vm.field(saw,139).integer()==0||vm.field(saw,139).integer()==2);
            world.playerControls(input);tick();
            const int health=vm.field(boss,13).integer();hits+=health<previous?1:0;previous=health;}
        require(vm.field(boss,13).integer()==0&&hits>0,"actual source melee did not defeat DeadAsh; HP="+std::to_string(vm.field(boss,13).integer())+" hits="+std::to_string(hits));
        world.playerControls({});
        for(unsigned n=0;n<650&&!quantity(101);++n){if(world.game.mode!=0)world.closeInventory();tick();}
        for(unsigned n=0;n<180&&!world.pendingSceneSwitch;++n){if(world.game.mode!=0)world.closeInventory();tick();}
        require(quantity(101)==1,"original DeadAsh callback did not award weapon101");queued("au01");ready();saveRoundtrip();
        std::cout<<"Earned offering World: real44/134/99 pickups ->three offerings466/468/467/469 ->DeadAsh17 "<<(world.game.profile.dreamcast?"powered primary Chainsaw100":"Axe10")<<" melee/death callback/weapon101 ->original AU01; "
            <<positions<<" source-region fixtures, "<<hits<<" damage steps and "<<approaches<<" Ash-only melee approaches\n";
}
void testOfferingStoryWorld(const fs::path& root,const std::function<void(World&)>& continueStory={}) {
    testLastPageStoryWorld(root,[&](World& world){continueOfferingStoryWorld(world);if(continueStory)continueStory(world);});
}
