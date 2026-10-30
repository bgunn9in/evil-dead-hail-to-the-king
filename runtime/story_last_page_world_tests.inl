#pragma once
#include "story_troop_world_tests.inl"
void continueLastPageStoryWorld(World& world) {
        auto require=[](bool value,const std::string& message){if(!value)throw std::runtime_error("Earned last page World: "+message);};
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
                tick();}throw std::runtime_error("Earned last page World: Use did not become ready in "+world.scene.id);};
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
        require(world.scene.id=="ac05"&&vm.staticField(606).integer()==1&&quantity(53)==1,"earned TroopLeader/key53 state missing");
        useVolume(17,4);passage("ac05",1,"ac04");useVolume(13,6);passage("ac04",2,"ac03");passage("ac03",3,"ac01");
        useItem(53,54,8);require(vm.staticField(415).integer()==1&&quantity(53)==0,"original AC01 key53 use failed");
        ready();saveRoundtrip();useVolume(54,8);passage("ac01",4,"ac02");
        require(!quantity(76)&&!vm.staticField(426).integer(),"last page was already earned");pickup(76,5);
        require(quantity(76)==1&&vm.staticField(426).integer()==1,"original last-page pickup did not set426");ready();
        const int archerField=world.game.profile.dreamcast?1485:1553;
        const int ambushField=world.game.profile.dreamcast?1476:1544;
        for(unsigned n=0;n<300&&(!vm.field(world.game.scene,archerField).reference()||!vm.field(world.game.scene,archerField+1).reference()||!vm.field(world.game.scene,archerField+2).reference());++n)tick();
        require(vm.field(world.game.scene,archerField).reference()&&vm.field(world.game.scene,archerField+1).reference()&&vm.field(world.game.scene,archerField+2).reference(),"original three archer spawn did not complete");
        approachVolume(world.game.profile.dreamcast?11:10,10);tick();require(vm.staticField(ambushField).integer()==1&&vm.staticField(ambushField+1).integer()==1,"authored archery ambush trigger did not activate");
        std::vector<Vec2> floorPoints;for(const auto& t:world.scene.floor)for(const auto& p:{t.a,t.b,t.c})floorPoints.push_back({p.x,p.z});
        auto chooseSafe=[&](const edactors::Body* enemy){return sample([&](Vec2 at){
            const unsigned first=world.game.profile.dreamcast?12:11;
            for(unsigned volume=first;volume<first+7;++volume)if(world.scene.scripting.volumes.at(volume).contains({at.x,0,at.z}))return false;
            return !enemy||world.nativeScene().queries.lineOfSight({enemy->pose.x,enemy->pose.y,enemy->pose.z},{at.x,0,at.z},2,world.sourceQueryCamera(),world.nativeScene().enabledVolumes,world.visibilityReady);
        },floorPoints);};
        const Vec2 safe=chooseSafe(&world.actors->at({9,0}));
        place(safe,10); // Outside all authored archer attack trigger regions.
        world.openInventory();world.syncInventory();const auto gun=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[](const auto& item){return item.containerField==98&&item.itemID==40;});
        require(gun!=world.inventoryView.items.end(),"source-startup Gun40 not present");world.inventoryView.selected=unsigned(gun-world.inventoryView.items.begin());world.applyInventoryItem();ready();
        require(vm.field(vm.field(world.game.ash,103).reference(),812).integer()==40,"actual inventory did not equip Gun40");
        unsigned gunInputs=0,approaches=0;const int startingAmmo=quantity(3);
        for(unsigned frame=0;frame<3000&&!vm.staticField(ambushField+2).integer();++frame){
            const edactors::Body* enemy=nullptr;for(const auto& [key,body]:world.actors->bodies())
                if(key.type==9&&(body.flags&1)&&!(body.flags&8)&&vm.field(body.script,13).integer()>0){enemy=&body;break;}
            if(enemy){auto& ash=world.actors->at({0,0});const Vec2 at=chooseSafe(enemy);
                world.positionProgressBody(ash,{at.x,0,at.z});ash.pose.yaw=edactormath::modelYawFromHeading(float(std::atan2(double(enemy->pose.z)-at.z,double(enemy->pose.x)-at.x)));
                world.publishProgressBody(ash);world.actor.clearanceRadius=initialActorRadius(world.scene,at);world.actor.movement.held=false;++approaches;
                ControlFrame input;input.down[size_t(ControlAction::Aim)]=true;input.edges[size_t(ControlAction::SecondaryAttack)]=world.sourceTick%6==0;
                gunInputs+=input.edges[size_t(ControlAction::SecondaryAttack)]?1:0;world.playerControls(input);}
            tick();}
        require(vm.staticField(ambushField+2).integer()==1,"original Gun40/input/raw7/three archer death callbacks did not finish source wave; inputs="+std::to_string(gunInputs));
        require(quantity(3)<startingAmmo,"gun path did not consume original ammunition");ready();saveRoundtrip();
        approachVolume(world.game.profile.dreamcast?0:18,10);tick();require(quantity(42)==1&&vm.staticField(690).integer()==1,"source completed-wave reward did not grant Rifle42/690");ready();
        useVolume(46,6);passage("ac02",1,"ac01");passage("ac01",6,"ac51");passage("ac51",5,"al50");passage("al50",7,"at01");
        for(int id:{72,73,74,75,76})require(quantity(id)==1,"an earned page was lost before the original five-page gate");
        useVolume(9,11);for(int id:{72,73,74,75,76})require(quantity(id)==0,"original five-page gate did not consume page"+std::to_string(id));queued("ag01");ready();saveRoundtrip();
        std::cout<<"Earned last page World: actual key53/AC01 ->page76/archery ambush ->Gun40 source ammo/contact/three deaths ->Rifle42/690 ->all five pages consumed ->AG01; "
            <<positions<<" source-region fixtures and "<<approaches<<" Ash-only gun approaches\n";
}
void testLastPageStoryWorld(const fs::path& root,const std::function<void(World&)>& continueStory={}) {
    testTroopStoryWorld(root,[&](World& world){continueLastPageStoryWorld(world);if(continueStory)continueStory(world);});
}
