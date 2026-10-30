#pragma once
// Fresh LevelB story fixture. Only Ash approach positions/facing are supplied;
// enemies, drops, inventory, mission flags and passages remain original JPC.
void continueLevelBStoryWorld(World& world,const std::function<void(World&)>& continuation={}) {
    std::cout<<std::unitbuf;auto& vm=world.game.vm;
    auto require=[](bool condition,const std::string& message){if(!condition)throw std::runtime_error("Original LevelB story: "+message);};
    unsigned positions=0,hits=0;
    auto quantity=[&](int id){int count=0;for(int container:{98,99,100})for(const auto& entry:world.game.items(container))if(entry.itemID==id)count+=entry.quantity;return count;};
    auto tick=[&](){world.step({},false,false,.033f);require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);
        require(world.notice!="could not perform action"&&world.notice!="action is not connected yet","source input native failed");
        require(vm.field(world.game.ash,13).integer()>0,"Ash naturally died in the approach fixture");};
    auto ready=[&](){for(unsigned n=0;n<480;++n){if(world.game.mode!=0)world.closeInventory();world.playerControls({});
            const auto& ash=world.actors->at({0,0});if(!(ash.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&
                !vm.field(world.game.ash,123).integer()&&!vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&ash.actions.possible(116))return;
            tick();}throw std::runtime_error("Original LevelB story: source Use did not become ready");};
    auto place=[&](Vec2 point,GroundPoint target){require(floorHeight(world.scene.floor,point).has_value()&&clearGround(world.scene.ground,point,0),"Ash fixture left source floor");
        auto& ash=world.actors->at({0,0});world.positionProgressBody(ash,{point.x,0,point.z});
        ash.pose.yaw=edactormath::modelYawFromHeading(float(std::atan2(double(target.z)-point.z,double(target.x)-point.x)));
        world.publishProgressBody(ash);world.actor.clearanceRadius=initialActorRadius(world.scene,point);world.actor.movement.held=false;++positions;};
    auto sample=[&](const auto& inside,const std::vector<Vec2>& points){require(!points.empty(),"source region has no geometry");
        float x0=points[0].x,x1=x0,z0=points[0].z,z1=z0;for(const auto& p:points){x0=std::min(x0,p.x);x1=std::max(x1,p.x);z0=std::min(z0,p.z);z1=std::max(z1,p.z);}
        const Vec2 center{(x0+x1)*.5f,(z0+z1)*.5f};Vec2 chosen{};float best=-std::numeric_limits<float>::infinity();
        for(unsigned x=1;x<32;++x)for(unsigned z=1;z<32;++z){const Vec2 p{x0+(x1-x0)*float(x)/32,z0+(z1-z0)*float(z)/32};
            if(!inside(p)||!floorHeight(world.scene.floor,p)||!clearGround(world.scene.ground,p,0))continue;
            float clearance=world.scene.radius;while(clearance>0&&!clearGround(world.scene.ground,p,clearance))clearance=std::max(0.f,clearance-.01f);
            const float score=clearance*10-length(p-center);if(score>best){best=score;chosen=p;}}
        require(std::isfinite(best),"source region has no valid floor approach");return chosen;};
    auto volumePoint=[&](unsigned index){const auto& volume=world.scene.scripting.volumes.at(index);std::vector<Vec2> points;
        for(const auto& p:volume.points)points.push_back({p.x,p.z});return sample([&](Vec2 p){return volume.contains({p.x,0,p.z});},points);};
    auto approachVolume=[&](unsigned index,unsigned marker){ready();place(volumePoint(index),world.scene.scripting.markers.at(marker));};
    auto pickupVisible=[&](int item){
        if(quantity(item))return true;
        const auto found=std::find_if(world.nativeProperties.objects.begin(),world.nativeProperties.objects.end(),[&](const auto& entry){return (entry.second.flags&1)&&entry.second.setup[0]==item;});
        if(found==world.nativeProperties.objects.end())return false;const auto target=found->second.position;ready();
        const auto point=sample([&](Vec2 p){const float distance=length(p-Vec2{target.x,target.z});return distance>.35f&&distance<1.2f;},
            {{target.x-1.25f,target.z-1.25f},{target.x+1.25f,target.z+1.25f}});
        place(point,target);for(unsigned n=0;n<240&&!quantity(item);++n){
            if(world.game.mode!=0)ready();
            if(n%60==0){place(point,target);world.use();}tick();}
        if(!quantity(item)) {
            const auto& ash=world.actors->at({0,0});std::cerr<<"B pickup diagnostic item"<<item<<" object"<<found->first<<" target"<<target.x<<','<<target.z
                <<" Ash"<<ash.pose.x<<','<<ash.pose.z<<" yaw"<<ash.pose.yaw<<" mode"<<world.game.mode<<'/'<<vm.staticField(844).integer()
                <<" guard"<<world.guard<<" busy"<<vm.field(world.game.ash,11).integer()<<" aim"<<vm.field(world.game.ash,123).integer()
                <<" pickup71="<<vm.staticField(71).integer()<<" state"<<ash.actions.layers[0].state<<'\n';
            for(const auto& entry:world.game.items())std::cerr<<" B item "<<entry.itemID<<'x'<<entry.quantity;
            std::cerr<<'\n';
        }
        require(quantity(item)>0,"original object contact did not grant earned item"+std::to_string(item));
        std::cout<<"Earned B source drop "<<item<<" at tick"<<world.sourceTick<<'\n';ready();return true;};
    require(world.scene.id=="bm01"&&!vm.staticField(531).integer()&&!quantity(20)&&!quantity(21)&&!quantity(22)&&!quantity(157),"fresh source BM01 bridge state differs");
    approachVolume(3,9);world.use();tick();require(!vm.staticField(531).integer()&&world.scene.enabledVolumes.at(2),"empty-handed bridge interaction opened the gate");
    // Original BM01.processFrame1956 owns the intro/wave trigger, camera track
    // and120-tick input guard; the fixture only enters its authored trigger6.
    ready();place(volumePoint(6),world.scene.scripting.markers.at(0));
    for(unsigned n=0;n<240&&!vm.staticField(526).integer();++n)tick();
    require(vm.staticField(526).integer()==1,"original intro trigger6 did not complete");ready();
    std::cout<<"Earned B intro526 at tick"<<world.sourceTick<<"; sourceArmorySkeleton wave active\n";
    for(unsigned frame=0;frame<14000&&!(quantity(20)&&quantity(21)&&quantity(22));++frame){
        bool picked=false;for(int item:{20,21,22})if(!quantity(item)&&pickupVisible(item))picked=true;
        if(picked)continue;
        const edactors::Body* target=nullptr;
        for(const auto& [key,body]:world.actors->bodies())if(key.type==28&&(body.flags&1)&&!(body.flags&8)&&
            vm.field(body.script,13).integer()>0&&!vm.field(body.script,world.game.hostField(HostFieldRole::GenericNpc30)).integer()) {
            const auto drop=vm.field(body.script,world.game.profile.dreamcast?1170u:1238u).integer();
            if(!target||drop>=36&&drop<=38)target=&body;
        }
        if(!target){tick();continue;}
        const auto enemyKey=target->key;const int before=vm.field(target->script,13).integer();bool positioned=false;
        Vec2 selected{};float best=-std::numeric_limits<float>::infinity();
        const auto& ash=world.actors->at({0,0});
        // Keep the Ash-only fixture within axe reach and clear of every living physical NPC.
        for(float range:{2.35f,2.10f})for(unsigned n=0;n<32;++n){const float offset=float(n)*edactormath::Tau/32,angle=target->pose.yaw+edactormath::Pi+offset;
            const Vec2 point{target->pose.x+range*std::sin(angle),target->pose.z+range*std::cos(angle)};
            if(!floorHeight(world.scene.floor,point)||!clearGround(world.scene.ground,point,world.scene.radius+.02f))continue;
            float clearance=std::numeric_limits<float>::infinity();bool overlap=false;
            for(const auto& [key,body]:world.actors->bodies())if(key.type!=0&&body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&vm.field(body.script,13).integer()>0){
                const float distance=length(point-Vec2{body.pose.x,body.pose.z});
                if(distance<float(ash.radius+body.radius)*.01f+.02f){overlap=true;break;}clearance=std::min(clearance,distance);}
            if(overlap)continue;const float score=clearance+.02f*std::cos(offset);
            if(score>best){best=score;selected=point;positioned=true;}}
        if(positioned)place(selected,{target->pose.x,target->localY,target->pose.z});
        if(!positioned){tick();continue;}
        ControlFrame input;input.down[size_t(ControlAction::Aim)]=true;input.edges[size_t(ControlAction::SecondaryAttack)]=world.sourceTick%6==0;
        world.playerControls(input);tick();
        const auto current=world.actors->bodies().find(enemyKey);
        if(current!=world.actors->bodies().end()&&vm.field(current->second.script,13).integer()<before){++hits;
            if(hits%8==0)std::cout<<"B natural contact hits"<<hits<<"; AshHP"<<vm.field(world.game.ash,13).integer()<<", parts"<<quantity(20)<<quantity(21)<<quantity(22)<<'\n';}
    }
    require(quantity(20)&&quantity(21)&&quantity(22),"natural original skeleton waves did not yield all bridge components");
    require(vm.staticField(527).integer()&&vm.staticField(528).integer()&&vm.staticField(529).integer(),"source pickup reports527/528/529 did not follow the three contacts");
    ready();world.openInventory();world.syncInventory();
    auto slot=[&](int id){for(const auto& item:world.inventoryView.items)if(item.containerField==99&&item.itemID==id)return item.slot;return -1;};
    require(world.combineInventorySlots(slot(20),slot(21))==154&&quantity(20)==0&&quantity(21)==0&&quantity(154)==1,"original20+21 bridge recipe failed");
    require(world.combineInventorySlots(slot(154),slot(22))==157&&quantity(154)==0&&quantity(22)==0&&quantity(157)==1,"original154+22 final bridge recipe failed");
    world.closeInventory();approachVolume(41,9);tick();
    require(world.actor.camera==0,"original camera41 did not select closed-bridge camera0");ready();
    const auto& bridgeRegion=world.scene.scripting.volumes.at(3);std::vector<Vec2> bridgePoints;
    for(const auto& p:bridgeRegion.points)bridgePoints.push_back({p.x,p.z});
    const auto nearSide=sample([&](Vec2 p){const GroundPoint local{p.x,0,p.z};return bridgeRegion.contains(local)&&
        !world.scene.scripting.volumes.at(42).contains(local)&&!world.scene.scripting.volumes.at(48).contains(local);},bridgePoints);
    place(nearSide,world.scene.scripting.markers.at(9));world.openInventory();world.syncInventory();
    const auto mechanism=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[](const auto& item){return item.containerField==99&&item.itemID==157;});
    require(mechanism!=world.inventoryView.items.end(),"earned bridge mechanism absent from actual UI");world.inventoryView.selected=unsigned(mechanism-world.inventoryView.items.begin());world.applyInventoryItem();
    for(unsigned n=0;n<240&&!vm.staticField(531).integer();++n)tick();
    require(vm.staticField(531).integer()==1&&quantity(157)==0&&!world.scene.enabledVolumes.at(2),"original use/report did not consume157/open bridge531");
    for(unsigned n=0;n<16&&(world.scene.enabledVolumes.at(41)||world.scene.enabledVolumes.at(42)||!world.scene.enabledVolumes.at(48)||!world.scene.enabledVolumes.at(49));++n)tick();
    require(!world.scene.enabledVolumes.at(41)&&!world.scene.enabledVolumes.at(42)&&world.scene.enabledVolumes.at(48)&&world.scene.enabledVolumes.at(49),"original bridge camera regions did not change");
    require(world.actor.camera==0&&world.cameraTimeline&&world.cameraTimeline->state&&
        world.cameraTimeline->state->request.segment==1&&world.cameraTimeline->state->frame==0,
        "earned bridge did not retain original camera0/reserved segment1 on its near side");
    world.openInventory();for(unsigned n=0;n<8&&vm.staticField(844).integer()!=2;++n)tick();
    require(vm.staticField(844).integer()==2,"real inventory did not enter source mode2 on bridge camera0");
    world.closeInventory();ready();
    require(world.actor.camera==0&&world.cameraTimeline->state->request.segment==1,
        "real inventory close lost the original reserved bridge camera");
    approachVolume(48,9);tick();
    require(world.actor.camera==1&&world.cameraTimeline&&world.cameraTimeline->state&&world.cameraTimeline->state->frame==1,
        "earned bridge did not select its original authored camera1/frame1");
    ready();const auto earned=world.saveStateBytes();for(unsigned n=0;n<3;++n)tick();world.restoreStateBytes(earned,nullptr);
    require(world.saveStateBytes()==earned,"earned bridge/mission/components did not roundtrip exactly");
    std::cout<<"Original LevelB earned bridge: freshBM01 -> natural skeleton drops20/21/22 -> sourceCombine154/157 -> actual use157/531, blocker2 and camera regions; "
        <<hits<<" damage steps, "<<positions<<" Ash-only approach fixtures; HP"<<vm.field(world.game.ash,13).integer()<<'\n';
    if(continuation)continuation(world);
}

void testLevelBStoryWorld(const fs::path& levelA,const std::function<void(World&)>& continuation={}) {
    std::cout<<std::unitbuf;
    checked(SDL_Init(SDL_INIT_GAMEPAD),"Initialize original LevelB story continuation");
    struct Quit{~Quit(){SDL_Quit();}} quit;
    World world(levelA/"levelB",nullptr,presentMovieHeadless,true);
    continueLevelBStoryWorld(world,continuation);
}
