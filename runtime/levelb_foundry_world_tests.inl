#pragma once
#include "levelb_pump_world_tests.inl"
// Earned source components/forge; only Ash approaches and real inputs are fixtures.
void continueLevelBFoundry(World& world) {
    auto& vm=world.game.vm;unsigned fixtures=0;
    auto require=[](bool value,const std::string& message){if(!value)throw std::runtime_error("Earned B continuation: "+message);};
    auto quantity=[&](int id){int count=0;for(int container:{98,99,100})for(const auto& item:world.game.items(container))if(item.itemID==id)count+=item.quantity;return count;};
    auto tick=[&](){world.step({},false,false,.033f);if(world.pendingSceneSwitch){const auto request=*world.pendingSceneSwitch;world.pendingSceneSwitch.reset();require(!edpackage::reload(request.sourceSceneId,request.destinationId),"unexpected package request in temple");require(world.switchScene(request,nullptr),"original queued scene switch failed");}require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);
        require(world.notice!="action is not connected yet"&&world.notice!="could not perform action","source input failed");
        require(vm.field(world.game.ash,13).integer()>0,"Ash naturally died");};
    auto ready=[&](){for(unsigned n=0;n<480;++n){if(world.game.mode!=0)world.closeInventory();const auto& ash=world.actors->at({0,0});
            if(!(ash.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&
                !vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&ash.actions.possible(116)){world.playerControls({});return;}
            // A release rejected during an attack needs a real Aim hold/release once Lower23 becomes possible.
            ControlFrame input;
            if(world.game.profile.dreamcast&&vm.field(world.game.ash,123).integer()&&!vm.field(world.game.ash,11).integer()&&
                !world.guard&&ash.actions.possible(23)&&!world.modernAimHeld)input.down[size_t(ControlAction::Aim)]=true;
            world.playerControls(input);tick();}
        throw std::runtime_error("Earned B continuation: input did not become ready");};
    auto place=[&](Vec2 point,GroundPoint target){require(floorHeight(world.scene.floor,point).has_value()&&clearGround(world.scene.ground,point,0),"fixture outside original floor");
        auto& ash=world.actors->at({0,0});world.positionProgressBody(ash,{point.x,0,point.z});
        ash.pose.yaw=edactormath::modelYawFromHeading(float(std::atan2(double(target.z)-point.z,double(target.x)-point.x)));
        world.publishProgressBody(ash);world.actor.clearanceRadius=initialActorRadius(world.scene,point);world.actor.movement.held=false;++fixtures;};
    auto sample=[&](const auto& inside,const std::vector<Vec2>& points){require(!points.empty(),"source region has no points");
        float x0=points[0].x,x1=x0,z0=points[0].z,z1=z0;for(auto p:points){x0=std::min(x0,p.x);x1=std::max(x1,p.x);z0=std::min(z0,p.z);z1=std::max(z1,p.z);}
        const Vec2 center{(x0+x1)*.5f,(z0+z1)*.5f};Vec2 chosen{};float best=-std::numeric_limits<float>::infinity();
        for(unsigned x=1;x<32;++x)for(unsigned z=1;z<32;++z){Vec2 p{x0+(x1-x0)*float(x)/32,z0+(z1-z0)*float(z)/32};
            if(!inside(p)||!floorHeight(world.scene.floor,p)||!clearGround(world.scene.ground,p,0))continue;
            float c=world.scene.radius;while(c>0&&!clearGround(world.scene.ground,p,c))c=std::max(0.f,c-.01f);
            const float score=c*10-length(p-center);if(score>best){best=score;chosen=p;}}
        require(std::isfinite(best),"source region has no original floor approach");return chosen;};
    auto approach=[&](unsigned index,unsigned marker){ready();const auto& region=world.scene.scripting.volumes.at(index);std::vector<Vec2> points;
        for(auto p:region.points)points.push_back({p.x,p.z});const auto at=sample([&](Vec2 p){return region.contains({p.x,0,p.z});},points);
        place(at,world.scene.scripting.markers.at(marker));};
    auto passage=[&](const char* expected,unsigned index,const char* destination){
        if(world.scene.id!=expected){require(world.scene.id==destination,"early source passage reached unexpected area");
            std::cout<<"Earned B passage "<<expected<<" -> "<<world.scene.id<<" before door wait\n";return;}
        ready();if(world.scene.id==destination){std::cout<<"Earned B passage "<<expected<<" -> "<<world.scene.id<<" during door wait\n";return;}
        require(world.scene.id==expected,"source passage changed area unexpectedly while becoming ready");const auto source=world.scene.id;const auto portal=world.scene.nativePortals.at(index);
        require(world.scene.enabledPortals.at(index),"source passage not enabled: "+source+"/"+std::to_string(index));
        const auto p=sample([&](Vec2 at){return insideTrigger(portal,at);},portal.points);place(p,{p.x,0,p.z+1});
        for(unsigned n=0;n<180&&world.scene.id==source;++n)tick();require(world.scene.id==destination,"source passage did not reach "+std::string(destination)+"; actual "+world.scene.id+"; "+world.notice);
        std::cout<<"Earned B passage "<<source<<" -> "<<world.scene.id<<" at tick"<<world.sourceTick<<'\n';};

    require(world.scene.id=="ba11"&&vm.staticField(563).integer()&&!vm.staticField(562).integer()&&!vm.staticField(610).integer()&&!vm.staticField(585).integer(),"missing earned pump checkpoint or seeded foundry state");
    unsigned damage=0;
    auto pickup=[&](int id){if(quantity(id))return true;const auto object=std::find_if(world.nativeProperties.objects.begin(),world.nativeProperties.objects.end(),
            [&](const auto& e){return(e.second.flags&1)&&e.second.setup[0]==id;});if(object==world.nativeProperties.objects.end())return false;
        const auto target=object->second.position;ready();const auto p=sample([&](Vec2 at){const float d=length(at-Vec2{target.x,target.z});return d>.35f&&d<1.2f;},
            {{target.x-1.25f,target.z-1.25f},{target.x+1.25f,target.z+1.25f}});
        for(unsigned n=0;n<240&&!quantity(id);++n){if(world.game.mode!=0)ready();if(n%60==0){place(p,target);world.use();}tick();}
        if(!quantity(id)){std::cerr<<"Foundry pickup diagnostics id="<<id<<" target="<<target.x<<","<<target.y<<","<<target.z<<" actor="<<world.actor.position[0]<<","<<world.actor.position[1]<<","<<world.actor.position[2]<<" flags="<<world.actors->at({0,0}).flags<<" action="<<vm.field(world.game.ash,11).integer()<<" notice="<<world.notice<<"\n";}require(quantity(id)>0,"source contact did not collect relic"+std::to_string(id));ready();std::cout<<"Earned foundry object "<<id<<" in "<<world.scene.id<<'\n';return true;};
    auto intro=[&](unsigned volume,unsigned marker,int flag){if(!vm.staticField(flag).integer()){approach(volume,marker);for(unsigned n=0;n<360&&!vm.staticField(flag).integer();++n)tick();}
        require(vm.staticField(flag).integer()==1,"original temple intro did not finish"+std::to_string(flag));ready();};
    std::function<void(int,GroundPoint)> enterPuzzleRoom;
    auto earn=[&](int id){for(unsigned n=0;n<6000&&!quantity(id);++n){
            if((id==48||id==147)&&std::any_of(world.nativeProperties.objects.begin(),world.nativeProperties.objects.end(),[&](const auto& p){return(p.second.flags&1)&&p.second.setup[0]==id;}))
                if(pickup(id))return;
            if(world.game.mode!=0)ready();const edactors::Body* enemy=nullptr;
            for(const auto& [key,b]:world.actors->bodies())if((key.type==28||key.type==38)&&vm.field(b.script,13).integer()>0&&!vm.field(b.script,world.game.hostField(HostFieldRole::GenericNpc30)).integer()){
                const int flavor=vm.field(b.script,world.game.profile.dreamcast?1170u:1238u).integer();if(!enemy||(id==48&&flavor==41)||(id==147&&flavor==42))enemy=&b;}
            if(!enemy){if(pickup(id))return;tick();continue;}
            const bool guardedRoom=id==48||id==147;
            if(guardedRoom&&enterPuzzleRoom&&!world.scene.queries.visible(2,world.sourceQueryCamera(),{enemy->pose.x,enemy->pose.y,enemy->pose.z})){
                enterPuzzleRoom(id,{enemy->pose.x,enemy->pose.y,enemy->pose.z});continue;}
            const auto key=enemy->key;const int hp=vm.field(enemy->script,13).integer();const bool hidden=!(enemy->flags&1)||(enemy->flags&8);bool moved=false;
            Vec2 chosen{};float best=-std::numeric_limits<float>::infinity();
            for(float d:{2.45f+float(enemy->radius)*.01f,2.35f,2.10f,1.60f,1.05f})for(unsigned direction=0;direction<32;++direction){
                const float angle=2*edactormath::Pi*float(direction)/32;
                const Vec2 p{enemy->pose.x+d*std::sin(angle),enemy->pose.z+d*std::cos(angle)};
                if(!floorHeight(world.scene.floor,p)||!clearGround(world.scene.ground,p,world.scene.radius+.02f))continue;
                if(guardedRoom){auto members=world.actor.membership;const auto camera=enterTriggers(world.scene.triggers,p,members,world.actor.camera,world.scene.triggerGrid);if(!world.scene.queries.visible(2,camera,{enemy->pose.x,enemy->pose.y,enemy->pose.z}))continue;}
                if(!world.scene.queries.visible(2,world.sourceQueryCamera(),{enemy->pose.x,enemy->pose.y,enemy->pose.z})&&
                   !world.scene.queries.lineOfSight({enemy->pose.x,enemy->pose.y,enemy->pose.z},{p.x,0,p.z},2,world.sourceQueryCamera(),world.scene.enabledVolumes,world.visibilityReady))continue;
                float safety=10;bool overlap=false;const auto& hero=world.actors->at({0,0});
                for(const auto& [otherKey,other]:world.actors->bodies())if(otherKey.type!=0&&other.nativeSlot>=0&&(other.flags&1)&&!(other.flags&8)&&vm.field(other.script,13).integer()>0){
                    const float distance=length(p-Vec2{other.pose.x,other.pose.z});if(distance<float(hero.radius+other.radius)*.01f+.02f){overlap=true;break;}
                    safety=std::min(safety,distance);}if(overlap)continue;
                const float rear=std::cos(angle-enemy->pose.yaw-edactormath::Pi);
                const float score=safety+rear*.02f;if(score>best){best=score;chosen=p;moved=true;}}
            if(moved)place(chosen,{enemy->pose.x,enemy->localY,enemy->pose.z});
            if(!moved){tick();continue;}ControlFrame input;if(!hidden){input.down[size_t(ControlAction::Aim)]=true;input.edges[size_t(ControlAction::SecondaryAttack)]=world.sourceTick%6==0;}world.playerControls(input);tick();
            const auto found=world.actors->bodies().find(key);if(found!=world.actors->bodies().end()&&vm.field(found->second.script,13).integer()<hp)++damage;
            if(n%500==0)std::cout<<"Foundry combat "<<world.scene.id<<" camera"<<world.actor.camera<<" flavor"<<vm.field(enemy->script,world.game.profile.dreamcast?1170u:1238u).integer()<<" item"<<id<<" targetHP"<<hp<<" AshHP"<<vm.field(world.game.ash,13).integer()<<" damageSteps"<<damage<<"\n";
        }if(!quantity(id))for(const auto& [key,b]:world.actors->bodies())if(key.type==28||key.type==38)
            std::cerr<<"Foundry enemy "<<key.type<<":"<<key.index<<" hp"<<vm.field(b.script,13).integer()<<" flavor"<<vm.field(b.script,world.game.profile.dreamcast?1170u:1238u).integer()<<" flags"<<b.flags<<" pos"<<b.pose.x<<","<<b.pose.y<<","<<b.pose.z<<" visible"<<world.scene.queries.visible(2,world.sourceQueryCamera(),{b.pose.x,b.pose.y,b.pose.z})<<" state"<<b.actions.layers[0].state<<" enabled18="<<(world.scene.enabledVolumes.size()>18?world.scene.enabledVolumes[18]:false)<<"\n";
        require(quantity(id)>0,"natural foundry encounter did not yield"+std::to_string(id));};

    auto samePassage=[&](unsigned index){ready();const auto native=std::find_if(world.scene.portalData.portals.begin(),world.scene.portalData.portals.end(),[&](const auto& p){return p.index==index;});
        require(native!=world.scene.portalData.portals.end()&&native->destination==world.scene.id,"missing original same-scene portal");
        const auto target=world.scene.scripting.markers.at(native->marker);
        if(world.actor.camera==native->camera&&length(Vec2{world.actor.position[0]-target.x,world.actor.position[2]-target.z})<.12f)return;
        require(world.scene.enabledPortals.at(index),"original same-scene door not enabled");const auto reference=world.game.scene;const auto generation=world.progressArenaGeneration;
        const auto portal=world.scene.nativePortals.at(index);const auto p=sample([&](Vec2 v){return insideTrigger(portal,v);},portal.points);place(p,{p.x,0,p.z+1});tick();
        require(world.game.scene==reference&&world.progressArenaGeneration==generation&&world.actor.camera==native->camera&&
            length(Vec2{world.actor.position[0]-target.x,world.actor.position[2]-target.z})<.12f,"same-scene source door did not preserve scene/apply original marker+camera");};
    auto healEarned=[&](){ready();for(unsigned n=0;n<16&&vm.field(world.game.ash,13).integer()<vm.field(world.game.ash,95).integer();++n){world.openInventory();world.syncInventory();
        const auto value=world.game.package.methodIndex("ObjDB","getObjectValueHealth","(I)I");const auto flags=world.game.package.methodIndex("ObjDB","getObjectFlags","(I)I");auto selected=world.inventoryView.items.end();
        for(auto i=world.inventoryView.items.begin();i!=world.inventoryView.items.end();++i)if(i->containerField==99&&(vm.invoke(flags,0,{jpc::Value::integer(i->itemID)}).integer()&4)&&vm.invoke(value,0,{jpc::Value::integer(i->itemID)}).integer()>0){selected=i;break;}
        if(selected==world.inventoryView.items.end()){world.closeInventory();ready();break;}const int before=vm.field(world.game.ash,13).integer(),id=selected->itemID;
        world.inventoryView.selected=unsigned(selected-world.inventoryView.items.begin());world.applyInventoryItem();ready();require(vm.field(world.game.ash,13).integer()>before,"real owned source food did not heal");
        std::cout<<"Earned source food "<<id<<" HP"<<before<<"->"<<vm.field(world.game.ash,13).integer()<<'\n';}};
    auto use=[&](unsigned volume,unsigned marker){approach(volume,marker);world.use();};
    auto useItem=[&](int id,unsigned volume,unsigned marker){approach(volume,marker);world.openInventory();world.syncInventory();
        const auto selected=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[&](const auto& e){return e.containerField==99&&e.itemID==id;});
        require(selected!=world.inventoryView.items.end(),"earned foundry item absent from real inventory"+std::to_string(id));world.inventoryView.selected=unsigned(selected-world.inventoryView.items.begin());world.applyInventoryItem();ready();};
    enterPuzzleRoom=[&](int id,GroundPoint target){
        const std::array<std::array<unsigned,3>,2> doors=id==48?
            std::array<std::array<unsigned,3>,2>{{{3,22,12},{4,23,13}}}:
            std::array<std::array<unsigned,3>,2>{{{1,22,12},{2,23,13}}};
        for(const auto& door:doors){const auto portal=std::find_if(world.scene.portalData.portals.begin(),world.scene.portalData.portals.end(),
                [&](const auto& value){return value.index==door[0];});
            require(portal!=world.scene.portalData.portals.end(),"missing original guard-room portal");
            if(world.scene.queries.visible(2,portal->camera,target)){use(door[1],door[2]);samePassage(door[0]);return;}}
        throw std::runtime_error("Earned B continuation: no original inner-door camera sees the target guard");};
    use(25,8);ready();require(vm.staticField(562).integer()==1,"original BA11 lever did not earn562");
    use(26,10);passage("ba11",4,"bt01");use(16,14);passage("bt01",5,"bm11");healEarned();
    require(!world.scene.enabledPortals.at(4),"BG13 opened before source ArmoryBoss610");use(65,42);passage("bm11",3,"bg02");
    use(18,17);passage("bg02",4,"bg06");use(17,10);passage("bg06",3,"bg07");intro(1,2,581);earn(146);
    require(vm.staticField(573).integer(),"actual foundry146 pickup did not report573");use(world.game.profile.dreamcast?12u:13u,5);passage("bg07",2,"bg06");
    use(14,8);passage("bg06",2,"bg02");useItem(146,19,18);require(vm.staticField(576).integer(),"actual item146 did not unlock576");
    use(19,18);passage("bg02",5,"bg08");intro(26,4,572);earn(150);require(vm.staticField(575).integer(),"actual Captain drop150 did not report575");
    if(vm.field(world.game.ash,13).integer()<vm.field(world.game.ash,95).integer()&&!quantity(56)){use(22,10);ready();require(quantity(56)>0,"original BG08 medicine interaction did not grant56");}healEarned();
    use(19,7);passage("bg08",1,"bg02");use(18,17);passage("bg02",4,"bg06");
    useItem(150,15,9);require(vm.staticField(586).integer()&&quantity(149),"original forge150 did not transform to149/586");
    use(17,10);passage("bg06",3,"bg07");useItem(149,world.game.profile.dreamcast?13u:14u,6);
    require(vm.staticField(583).integer()&&vm.staticField(582).integer()&&vm.staticField(577).integer(),"source149 forge report did not earn583/582/577");earn(148);
    use(world.game.profile.dreamcast?12u:13u,5);passage("bg07",2,"bg06");use(14,8);passage("bg06",2,"bg02");use(17,16);passage("bg02",3,"bg03");intro(0,6,567);use(23,13);samePassage(4);earn(48);
    use(22,12);samePassage(3);
    use(20,10);passage("bg03",2,"bg02");useItem(48,16,15);
    require(vm.staticField(571).integer()&&!world.scene.enabledVolumes.at(14),"actual key48 did not unlock source obstacle14/571");
    use(17,16);passage("bg02",3,"bg03");use(21,11);passage("bg03",5,"bg10");use(23,13);samePassage(2);
    {ready();const auto marker=world.scene.scripting.markers.at(5);const auto spawnApproach=sample([&](Vec2 p){const float d=length(p-Vec2{marker.x,marker.z});return d>1.8f&&d<2.8f;},
        {{marker.x-2.8f,marker.z-2.8f},{marker.x+2.8f,marker.z+2.8f}});place(spawnApproach,marker);for(unsigned n=0;n<45;++n)tick();}
    earn(147);require(vm.staticField(580).integer(),"actual skeleton drop147 did not report580");
    use(22,12);samePassage(1);use(19,11);passage("bg10",3,"bg03");use(20,10);passage("bg03",2,"bg02");use(18,17);passage("bg02",4,"bg06");
    approach(13,7);world.openInventory();world.syncInventory();auto slot=[&](int id){for(const auto& e:world.inventoryView.items)if(e.containerField==99&&e.itemID==id)return e.slot;return -1;};
    require(world.combineInventorySlots(slot(147),slot(148))==36&&quantity(36)&&!quantity(147)&&!quantity(148),"original scene/zone/facing-conditioned147+148=36 recipe failed");
    world.closeInventory();use(17,10);passage("bg06",3,"bg07");useItem(36,world.game.profile.dreamcast?11u:12u,3);
    for(unsigned n=0;n<240&&world.scene.id=="bg07";++n)tick();require(vm.staticField(585).integer()&&world.scene.id=="bg12","original source36 use did not requestBG12/585");ready();
    const auto checkpoint=world.saveStateBytes();for(unsigned n=0;n<3;++n)tick();world.restoreStateBytes(checkpoint,nullptr);require(world.saveStateBytes()==checkpoint,"earned foundry state failed exact save/load");
    std::cout<<"Earned B foundry: actual armory drops48/150/147, source item uses/forge149+148 and scene-conditioned147+148=36 -> original source36 use585/BG12; "<<damage<<" damage steps, "<<fixtures<<" Ash-only approach fixtures, HP"<<vm.field(world.game.ash,13).integer()<<'\n';
}

void testLevelBFoundryWorld(const fs::path& levelA,const std::function<void(World&)>& continuation={}){testLevelBStoryWorld(levelA,[&](World& w){continueLevelBMerchant(w);continueLevelBBomb(w);continueLevelBTemple(w);continueLevelBWizard(w);continueLevelBPump(w);continueLevelBFoundry(w);if(continuation)continuation(w);});}
