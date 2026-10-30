#pragma once
#include "story_tree_world_tests.inl"
// Continue the same earned Tree World. Ash approach poses are explicit fixtures;
// all item/mission gates, native contacts, AI and scene changes execute source JPC.
void continuePagesStoryWorld(World& world) {
        auto require=[](bool value,const std::string& message){if(!value)throw std::runtime_error("Earned pages World: "+message);};
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
                tick();}throw std::runtime_error("Earned pages World: Use did not become ready in "+world.scene.id);};
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

        require(world.scene.id=="at01"&&vm.staticField(605).integer()==1&&vm.staticField(424).integer()==1&&quantity(74)==1,
            "the earned Tree chapter was not retained");
        require(quantity(55)==1&&vm.staticField(461).integer()==1,"original AT01 activation did not earn car key55");
        passage("at01",3,"al50");passage("al50",6,"ak09");useVolume(58,52);passage("ak09",7,"ak01");
        require(!vm.staticField(422).integer()&&!quantity(72),"cabin page72 was already earned");
        useVolume(77,34);for(unsigned n=0;n<240&&!quantity(72);++n)tick();
        require(quantity(72)==1&&vm.staticField(422).integer()==1,"original AK01 Use116/contact did not earn page72/422");
        ready();saveRoundtrip();
        useVolume(74,32);passage("ak01",2,"ak09");passage("ak09",5,"ab50");passage("ab50",2,"ab01");
        require(!vm.staticField(407).integer()&&!vm.staticField(411).integer()&&!quantity(75),"AB01 car/page gate was already solved");
        useItem(55,33,33);require(vm.staticField(407).integer()==1&&vm.staticField(411).integer()==1,
            "actual car-key use did not unlock/push car");
        ready();saveRoundtrip();useVolume(32,32);
        require(vm.field(world.game.ash,117).integer()==1,"actual car Use did not request climb-up");
        for(unsigned n=0;n<300&&!vm.staticField(409).integer();++n)tick();
        require(vm.staticField(409).integer()==1,"source climb128/end34 did not reach car roof");ready();saveRoundtrip();
        // Keep the native climb's resulting LOCAL position and turn only Ash.
        // The two authored facing cases mean climb-down / look-at-page; every
        // other roof-facing direction executes the original page pickup branch.
        auto& ash=world.actors->at({0,0});bool direction=false;
        for(unsigned n=0;n<32&&!direction;++n){const float yaw=float(n)*edactormath::Tau/32;
            if(world.scene.scripting.facing(7,45,{ash.pose.x,ash.pose.z},yaw)||
                    world.scene.scripting.facing(26,45,{ash.pose.x,ash.pose.z},yaw))continue;
            ash.pose.yaw=yaw;direction=true;}
        require(direction,"no source-valid roof-facing direction");world.publishProgressBody(ash);world.use();
        require(quantity(75)==1&&vm.staticField(425).integer()==1,"actual source roof Use did not earn page75/425");
        ready();saveRoundtrip();
        std::cout<<"Earned pages World: original AK01 page72 -> AT01 key55/car push/climb -> AB01 page75; "<<positions<<" Ash-only source-region fixtures\n";
}
void testPagesStoryWorld(const fs::path& root,const std::function<void(World&)>& continueStory={}) {
    testTreeStoryWorld(root,[&](World& world){continuePagesStoryWorld(world);if(continueStory)continueStory(world);});
}
