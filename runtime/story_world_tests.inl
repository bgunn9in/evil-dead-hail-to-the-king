#pragma once
#include "inventory_combine_tests.inl"
// Continue the earned eight-passage episode. Only Ash's source-volume approach
// positions are fixtures; inventory, missions, enemies and doors remain JPC.
void continueStoryWorld(World& world) {
        auto require=[](bool value,const std::string& message){if(!value)throw std::runtime_error("Original story World: "+message);};
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
                tick();}throw std::runtime_error("Original story World: Use did not become ready in "+world.scene.id);};
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
        require(world.scene.id=="ah02"&&vm.staticField(435).integer()==1&&quantity(29)==0,
            "continuation did not receive the original earned cord29/AH02 state");
        require(vm.staticField(438).integer()==0&&quantity(170)==0,"AH02 source item was already earned");
        useVolume(48,31);
        require(vm.staticField(438).integer()==1&&quantity(170)==1,"AH02 allowUse1953/report1955 did not grant170/set438");
        ready();saveRoundtrip();useVolume(48,31);
        require(quantity(170)==1&&vm.staticField(438).integer()==1,"repeated original AH02 Use duplicated its item");
        std::cout<<"Earned AH02: original trigger48/marker31 -> Use140 -> pickup170/report1955/438, repeat and exact save passed\n";
        testInventoryCombineAtCheckpoint(world,false);ready();
        pickup(30,6);require(vm.staticField(440).integer()==1,"AH02 original handle pickup did not set440");
        passage("ah02",3,"ah05");
        useVolume(35,8);require(vm.staticField(686).integer()==1,"AH05 source trigger35 did not report170/set686");
        ready();saveRoundtrip();
        passage("ah05",2,"ah02");passage("ah02",4,"ah50");passage("ah50",5,"ak09");
        require(!quantity(161)&&!vm.staticField(457).integer(),"source crowbar was present before its original object contact");
        pickup(161,11);require(vm.staticField(457).integer()==1,"AK09 source reportPickupItem did not set457");saveRoundtrip();
        passage("ak09",6,"ah50");passage("ah50",4,"ah02");passage("ah02",3,"ah05");
        approachVolume(6,7);tick();require(vm.staticField(430).integer()==1&&quantity(130)==1,"AH05 original recipe trigger did not grant document130/set430");
        ready();testInventoryCombineAtCheckpoint(world,true);ready();saveRoundtrip();
        require(vm.staticField(441).integer()==1&&vm.staticField(456).integer()==0,"AH05 source object/trigger30 did not prepare the unclimbed platform");
        useVolume(63,7);
        require(vm.field(world.game.ash,117).integer()==1,"original Use did not request climb-up");
        const auto climbStart=world.saveStateBytes();unsigned climbTicks=0;
        for(;climbTicks<300&&!vm.staticField(456).integer();++climbTicks)tick();
        require(vm.staticField(456).integer()==1,"original actionClimb712/128/end34 did not reach platform");
        const auto climbed=world.saveStateBytes();world.restoreStateBytes(climbStart,nullptr);
        for(unsigned n=0;n<climbTicks;++n)tick();require(world.saveStateBytes()==climbed,"original pending climb save/replay differs");
        std::cout<<"Earned AH05: source441 object-in-trigger30 -> Use676 -> thinker712/climb128/end34/mission456 and pending replay passed\n";
        require(!vm.staticField(458).integer()&&!quantity(54),"source platform/key puzzle was solved before crowbar use");
        useItem(161,63,7);require(vm.staticField(458).integer()==1,"original platform crowbar callback did not set458");
        for(unsigned n=0;n<30&&!quantity(54);++n)tick();require(quantity(54)==1,"original pending platform reward did not grant key54");
        ready();saveRoundtrip();useVolume(64,19);passage("ah05",4,"ah10");
        require(!vm.staticField(444).integer(),"AH10 source lock was already solved");useItem(54,23,22);
        require(vm.staticField(444).integer()==1,"original key54 callback did not unlock AH10 door444");
        ready();saveRoundtrip();useVolume(23,22);passage("ah10",3,"ah10");
        pickup(93,21);require(vm.staticField(445).integer()==1,"original engine pickup did not set445");
        useVolume(25,23);passage("ah10",2,"ah10");
        useItem(93,27,26);require(vm.staticField(446).integer()==1&&!vm.staticField(448).integer(),"source pump engine step bypassed its missing handle");
        ready();saveRoundtrip();useItem(30,27,26);
        require(vm.staticField(447).integer()==1&&vm.staticField(448).integer()==1,"earned source engine+handle did not start AH10 pump");
        ready();saveRoundtrip();passage("ah10",4,"ah20");
        useVolume(28,0);passage("ah20",3,"ah12");
        require(!vm.staticField(451).integer()&&!quantity(9),"amulet was present before AH12 source object contact");
        pickup(9,1);require(vm.staticField(451).integer()==1,"original amulet report did not set451");ready();saveRoundtrip();
        useVolume(7,3);passage("ah12",1,"ah20");useVolume(23,15);passage("ah20",1,"ah05");
        useVolume(63,7);require(!quantity(9)&&vm.staticField(456).integer()==0,"earned platform Use did not consume amulet/reset456");
        require(world.pendingSceneSwitch&&world.pendingSceneSwitch->destinationId==0x61680600u,"original platform did not request AH06");
        const auto request=*world.pendingSceneSwitch;world.pendingSceneSwitch.reset();
        require(world.switchScene(request,nullptr)&&world.scene.id=="ah06","earned queued platform request did not enter actual AH06");
        ready();saveRoundtrip();
        const edactors::Key bossKey{14,0};for(unsigned n=0;n<180&&!world.actors->bodies().count(bossKey);++n)tick();
        require(world.actors->bodies().count(bossKey)!=0,"earned AH06 activation did not create original Matriarch wave");
        const auto boss=world.actors->at(bossKey).script;require(vm.field(boss,13).integer()==100&&!vm.staticField(604).integer(),"source Matriarch was not a fresh100HP boss");
        unsigned fightFixtures=0,waitingFront=0,hits=0;int health=100;
        std::cout<<"Natural Matriarch source radius "<<world.actors->at(bossKey).radius<<" xyz "<<world.actors->at(bossKey).pose.x<<","<<world.actors->at(bossKey).pose.z<<'\n';
        auto fight=[&](){const auto& npc=world.actors->at(bossKey);auto& ash=world.actors->at({0,0});bool placed=false;
            const float separation=std::max(1.1f,float(npc.radius)*.01f+world.scene.radius+.12f);
            for(float extra:{0.f,.2f,.4f})for(float angle:{0.f,-.174532925f,.174532925f,-.34906585f,.34906585f}) {
                if(placed)break;const float distance=separation+extra;const Vec2 point{npc.pose.x+distance*std::sin(npc.pose.yaw+edactormath::HalfPi+angle),npc.pose.z+distance*std::cos(npc.pose.yaw+edactormath::HalfPi+angle)};
                if(!floorHeight(world.scene.floor,point)||!clearGround(world.scene.ground,point,world.scene.radius+.05f))continue;
                world.positionProgressBody(ash,{point.x,0,point.z});
                ash.pose.yaw=edactormath::modelYawFromHeading(float(std::atan2(double(npc.pose.z)-point.z,double(npc.pose.x)-point.x)));
                world.publishProgressBody(ash);world.actor.clearanceRadius=initialActorRadius(world.scene,point);world.actor.movement.held=false;
                placed=true;++fightFixtures;break;}
            if(!placed){++waitingFront;tick();return;}ControlFrame input;input.down[size_t(ControlAction::Aim)]=true;
            input.edges[size_t(ControlAction::SecondaryAttack)]=world.sourceTick%6==0;world.playerControls(input);tick();
            const int next=vm.field(boss,13).integer();if(next<health)++hits;health=next;};
        for(unsigned n=0;n<5000&&!vm.staticField(604).integer();++n)fight();
        require(hits>0&&vm.field(boss,13).integer()==0&&vm.staticField(604).integer()==1,"actual source input/Matriarch vulnerability did not earn death604; HP="+
            std::to_string(vm.field(boss,13).integer())+" sideWait="+std::to_string(waitingFront)+" xyz="+std::to_string(world.actors->at(bossKey).pose.x)+","+
            std::to_string(world.actors->at(bossKey).pose.z)+" state="+std::to_string(world.actors->at(bossKey).actions.layers[0].state));
        for(unsigned n=0;n<180&&!vm.staticField(452).integer();++n)tick();
        require(vm.staticField(452).integer()==1&&quantity(73)==1&&quantity(132)==1,"original AH06 death reward did not grant73/132");
        ready();saveRoundtrip();
        std::cout<<"Earned AH06 Matriarch: actual input17 -> original vulnerable attack/reaction -> death604/rewards73+132, "<<hits<<" damage steps and "<<fightFixtures<<" Ash-only side approach samples\n";
        std::cout<<"Original story continuation passed earned handle/crowbar/recipe/climb/key/engine/pump/amulet -> actual AH06 boss death/rewards; "<<positions<<" Ash-only source-region position fixtures\n";
}

void testStoryWorld(const fs::path& root,const std::function<void(World&)>& continueStory={}) {testEpisode(root,false,640,480,{},[&](World& world){
    if(world.transitions!=8)throw std::runtime_error("Original story World: earned episode transition count differs");continueStoryWorld(world);
    if(continueStory)continueStory(world);});}
