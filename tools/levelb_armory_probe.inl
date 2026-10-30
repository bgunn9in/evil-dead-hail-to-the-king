// Private natural ArmoryBoss29 continuation from the genuinely earned Foundry snapshot.
void continueLevelBArmoryProbe(World& world) {
    auto require=[](bool ok,const std::string& text){if(!ok)throw std::runtime_error("Earned Armory: "+text);};
    auto& vm=world.game.vm;unsigned fixtures=0;
    require(world.scene.id=="bg12"&&vm.staticField(585).integer()&&!vm.staticField(610).integer(),"missing genuine Foundry checkpoint");
    auto tick=[&](){world.playerControls({});world.step({},false,false,.033f);require(!world.scriptFault,"source frame: "+world.game.diagnostic);
        require(vm.field(world.game.ash,13).integer()>0,"Ash naturally died");
        if(world.pendingSceneSwitch){const auto request=*world.pendingSceneSwitch;world.pendingSceneSwitch.reset();require(!edpackage::reload(request.sourceSceneId,request.destinationId),"unexpected package switch");require(world.switchScene(request,nullptr),"source queued scene switch failed");}};
    auto ready=[&](){for(unsigned n=0;n<480;++n){if(world.game.mode!=0)world.closeInventory();const auto& ash=world.actors->at({0,0});
        if(!(ash.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&!vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&ash.actions.possible(116))return;tick();}
        throw std::runtime_error("Earned Armory: input did not become ready");};
    auto place=[&](Vec2 p){require(floorHeight(world.scene.floor,p).has_value()&&clearGround(world.scene.ground,p,0),"Ash lure is not on original floor");
        auto& ash=world.actors->at({0,0});world.positionProgressBody(ash,{p.x,0,p.z});world.publishProgressBody(ash);world.actor.clearanceRadius=initialActorRadius(world.scene,p);world.actor.movement.held=false;++fixtures;};
    auto sample=[&](unsigned volume){const auto& points=world.scene.scripting.volumes.at(volume).points;require(points.size()==2,"expected source rectangle");
        Vec2 chosen{};float best=-1;const Vec2 center{(points[0].x+points[1].x)*.5f,(points[0].z+points[1].z)*.5f};
        for(unsigned x=1;x<32;++x)for(unsigned z=1;z<32;++z){Vec2 p{points[0].x+(points[1].x-points[0].x)*float(x)/32,points[0].z+(points[1].z-points[0].z)*float(z)/32};
            if(!world.scene.scripting.inside(volume,{p.x,0,p.z})||!floorHeight(world.scene.floor,p)||!clearGround(world.scene.ground,p,0))continue;
            const float score=10.f-length(p-center);if(score>best){chosen=p;best=score;}}
        require(best>=0,"source valve has no floor approach");return chosen;};
    auto bossRef=[&](){return vm.field(world.game.scene,1605).reference();};
    for(unsigned n=0;n<240&&vm.field(world.game.scene,1605).null();++n)tick();
    require(!vm.field(world.game.scene,1605).null(),"original source wave did not create ArmoryBoss");
    const auto ref=bossRef();const edactors::Key key{vm.field(ref,6).integer(),vm.field(ref,5).integer()};
    std::cout<<"Armory source init key"<<key.type<<":"<<key.index<<" HP"<<vm.field(ref,13).integer()<<" range"<<vm.field(ref,1328).integer()<<"/"<<vm.field(ref,1330).integer()<<" AshHP"<<vm.field(world.game.ash,13).integer()<<'\n';
    auto valve=[&](unsigned index){ready();place(sample(index));world.use();require(world.notice!="action is not connected yet"&&world.notice!="could not perform action","actual source valve input failed");};
    valve(11);ready();require(vm.field(world.game.scene,1613).integer(),"original first molten movie did not run");
    valve(10);ready();require(vm.field(world.game.scene,1612).integer(),"original first steam movie did not run");
    auto lure=[&](unsigned bossVolume,unsigned playerVolume){ready();
        const auto& points=world.scene.scripting.volumes.at(bossVolume).points;const Vec2 target{(points[0].x+points[1].x)*.5f,(points[0].z+points[1].z)*.5f};
        const Vec2 lurePoint{bossVolume==23?target.x-2.15f:target.x+1.1f,target.z};place(lurePoint);
        auto nav=[&](unsigned index){const auto& raw=world.scene.nativeBytes;uint32_t count=0,offset=0;require(raw.size()>=56,"missing original navigation header");
            std::memcpy(&count,raw.data()+48,4);std::memcpy(&offset,raw.data()+52,4);require(index<count&&offset<=raw.size()&&size_t(index)*44+20<=raw.size()-offset,"invalid original lure navigation point");
            GroundPoint point;std::memcpy(&point,raw.data()+offset+index*44+8,12);return Vec2{point.x,point.z};};
        const std::vector<Vec2> detour{nav(7),nav(6),nav(5),nav(4),lurePoint};size_t detourIndex=0;bool detouring=false;
        for(unsigned n=0;n<3600;++n){const auto& npc=world.actors->at(key);if(world.scene.scripting.inside(bossVolume,{npc.pose.x,npc.localY,npc.pose.z})){
                const auto& ash=world.actors->at({0,0});if(!(ash.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&ash.actions.possible(116)){
                    place(sample(playerVolume));world.use();std::cout<<"Armory actual valve"<<playerVolume<<" boss"<<npc.pose.x<<","<<npc.pose.z<<" at"<<n<<" AshHP"<<vm.field(world.game.ash,13).integer()<<" hot"<<vm.field(world.game.scene,1611).integer()<<"\n";return;}}
            if(bossVolume==23&&n==150){detouring=true;place(detour[0]);}
            if(detouring&&detourIndex+1<detour.size()&&length(Vec2{npc.pose.x,npc.pose.z}-detour[detourIndex])<2.5f){++detourIndex;place(detour[detourIndex]);}
            if(n%180==0)std::cout<<"Armory lure"<<bossVolume<<" tick"<<n<<" NPC"<<npc.pose.x<<","<<npc.pose.z<<" yaw"<<npc.pose.yaw<<" state"<<npc.actions.layers[0].state<<" hp"<<vm.field(ref,13).integer()<<" Ash"<<world.actor.position[0]<<","<<world.actor.position[2]<<" HP"<<vm.field(world.game.ash,13).integer()<<"\n";
            tick();}
        throw std::runtime_error("Earned Armory: natural NPC did not enter original valve region");};
    for(unsigned cycle=0;cycle<2;++cycle){lure(23,11);ready();require(vm.field(world.game.scene,1611).integer(),"actual molten valve did not heat natural NPC");
        for(unsigned n=0;n<100;++n)tick();lure(22,10);ready();for(unsigned n=0;n<100&&world.scene.id=="bg12";++n)tick();
        std::cout<<"Armory completed cycle"<<cycle<<" scene"<<world.scene.id<<" NPC_HP"<<vm.field(ref,13).integer()<<" AshHP"<<vm.field(world.game.ash,13).integer()<<"\n";}
    for(unsigned n=0;n<240&&world.scene.id=="bg12";++n)tick();
    require(vm.staticField(610).integer()&&world.scene.id=="bg13","natural original molten/steam death did not request BG13/610");ready();
    const auto exact=world.saveStateBytes();tick();world.restoreStateBytes(exact,nullptr);require(world.saveStateBytes()==exact,"earned Armory exact save differs");
    std::cout<<"Earned natural Armory PASS, fixtures"<<fixtures<<" HP"<<vm.field(world.game.ash,13).integer()<<'\n';
}
