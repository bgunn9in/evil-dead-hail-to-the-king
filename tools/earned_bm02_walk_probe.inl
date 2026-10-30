// Private input-only continuation from the actual earned bridge arrival.
void continueEarnedBM02WalkWorld(World& world) {
    auto& vm=world.game.vm;std::cout<<std::unitbuf;
    auto require=[](bool ok,const std::string& text){if(!ok)throw std::runtime_error("Earned BM02 walk: "+text);};
    require(world.scene.id=="bm02"&&vm.staticField(531).integer()&&!vm.staticField(532).integer()&&!world.scene.enabledPortals.at(1),"missing actual first BM02 arrival");
    const auto initialTransitions=world.transitions;const Vec2 arrival{world.actor.position[0],world.actor.position[2]};
    unsigned frames=0,combatFrames=0,hits=0;float distance=0,minRadius=world.actor.clearanceRadius;
    auto tick=[&](Vec2 stick=Vec2{},bool attack=false,bool use=false,float dt=.033f){const Vec2 old{world.actor.position[0],world.actor.position[2]};
        ControlFrame input;input.movement=stick;input.down[size_t(ControlAction::Aim)]=attack;
        input.edges[size_t(ControlAction::SecondaryAttack)]=attack&&dt>0&&world.sourceTick%6==0;
        const auto beforeStep=world.saveStateBytes();world.playerControls(input);const auto moved=[&](){try{return world.step(stick,true,use,dt);}catch(const std::exception& error){
            savefile::atomicWrite(fs::absolute("build"),"earned-bm02-walk-failure.eds",beforeStep);
            uint32_t bits[3];std::memcpy(bits,&stick.x,4);std::memcpy(bits+1,&stick.z,4);std::memcpy(bits+2,&dt,4);
            std::ofstream capture(fs::absolute("build/earned-bm02-walk-failure-input.json"));
            capture<<"{\"format\":\"EDCONTROLREPLAY1\",\"movement_f32_bits\":["<<bits[0]<<','<<bits[1]<<"],\"dt_f32_bits\":"<<bits[2]<<",\"run\":true,\"use_edge\":"<<(use?"true":"false")<<",\"aim\":"<<(attack?"true":"false")<<",\"secondary_attack_edge\":"<<(attack&&dt>0&&world.sourceTick%6==0?"true":"false")<<"}\n";
            std::cerr<<std::setprecision(9);
            std::cerr<<"BM02 movement exception "<<error.what()<<" before"<<old.x<<","<<old.z<<" stick"<<stick.x<<","<<stick.z<<" dt"<<dt<<" aim"<<attack<<" radius"<<world.actor.clearanceRadius<<" after"<<world.actor.position[0]<<","<<world.actor.position[2]<<"\n";
            for(const auto& [key,body]:world.actors->bodies())if(key.type==28)std::cerr<<" NPC"<<key.index<<" hp"<<vm.field(body.script,13).integer()<<" xyz"<<body.pose.x<<","<<body.pose.y<<","<<body.pose.z<<" flags"<<body.flags<<"\n";throw;}}();
        require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);
        require(world.notice!="could not perform action"&&world.notice!="action is not connected yet","source input failed: "+world.notice);
        require(vm.field(world.game.ash,13).integer()>0,"Ash naturally died while walking/fighting");
        require(world.scene.id=="bm02"||world.scene.id=="bm12","walking entered an unexpected scene");
        const Vec2 at{world.actor.position[0],world.actor.position[2]};
        require(floorHeight(world.scene.floor,at).has_value()&&clearGround(world.scene.ground,at,world.actor.clearanceRadius),"movement left original floor or penetrated an active source solid");
        require(moved.moved<=moved.requested+ContactSkin*4,"movement exceeded requested distance");
        if(dt>0){++frames;distance+=world.scene.id=="bm02"?length(at-old):moved.moved;}
        minRadius=std::min(minRadius,world.actor.clearanceRadius);};
    auto ready=[&](){for(unsigned n=0;n<480;++n){if(world.game.mode!=0)world.closeInventory();world.playerControls({});const auto& ash=world.actors->at({0,0});
        if(!(ash.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&!vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&ash.actions.possible(116))return;tick();}
        throw std::runtime_error("Earned BM02 walk: source input did not become ready");};
    auto toward=[&](Vec2 target,float magnitude){const auto& camera=world.scene.cameras.at(world.actor.camera);const auto& latch=world.actor.movement;
        const Vec2 right=latch.held?latch.right:camera.right,forward=latch.held?latch.forward:camera.forward,delta=target-Vec2{world.actor.position[0],world.actor.position[2]};
        const float determinant=right.x*forward.z-right.z*forward.x;require(std::abs(determinant)>.001f,"camera movement basis is singular");
        Vec2 stick{(delta.x*forward.z-delta.z*forward.x)/determinant,(right.x*delta.z-right.z*delta.x)/determinant};const float size=length(stick);
        return size>.000001f?stick*(magnitude/size):Vec2{};};
    auto nav=[&](unsigned index){const auto& raw=world.scene.nativeBytes;uint32_t count=0,offset=0;require(raw.size()>=56,"missing navigation header");
        std::memcpy(&count,raw.data()+48,4);std::memcpy(&offset,raw.data()+52,4);require(index<count&&offset<=raw.size()&&size_t(index)*44+20<=raw.size()-offset,"source navigation point is missing");
        GroundPoint p;std::memcpy(&p,raw.data()+offset+index*44+8,12);return Vec2{p.x,p.z};};
    auto walk=[&](Vec2 target,bool permitArrival=false,bool awaitGuardian=false){tick({},false,false,0);unsigned stalled=0;float previous=std::numeric_limits<float>::infinity();
        for(unsigned n=0;n<3000;++n){if(world.scene.id=="bm12"){require(permitArrival,"route left BM02 before opening the guardian door");return;}
            if(world.game.mode!=0){ready();continue;}
            const auto& ash=world.actors->at({0,0});const Vec2 at{ash.pose.x,ash.pose.z};const float remaining=length(target-at);
            if(remaining<.3f&&(!awaitGuardian||vm.staticField(532).integer())){tick({},false,false,0);return;}
            const edactors::Body* enemy=nullptr;const edactors::Body* chase=nullptr;float nearest=3.1f,nearestChase=std::numeric_limits<float>::infinity();
            for(const auto& [key,body]:world.actors->bodies())if(key.type==28&&body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&vm.field(body.script,13).integer()>0){
                const float separation=length(Vec2{body.pose.x,body.pose.z}-at);
                if(world.scene.queries.lineOfSight({ash.pose.x,ash.pose.y,ash.pose.z},{body.pose.x,body.localY,body.pose.z},4,
                    world.sourceQueryCamera(),world.scene.enabledVolumes,world.visibilityReady)){
                    if(separation<nearest){nearest=separation;enemy=&body;}if(separation<nearestChase){nearestChase=separation;chase=&body;}}}
            if(enemy){const auto key=enemy->key;const int before=vm.field(enemy->script,13).integer();const Vec2 point{enemy->pose.x,enemy->pose.z};
                tick(toward(point,1),true,false,0);tick({},true);
                require(++combatFrames<3000,"real guardian encounter exceeded its input budget");const auto found=world.actors->bodies().find(key);
                if(found!=world.actors->bodies().end()&&vm.field(found->second.script,13).integer()<before)++hits;
                if(combatFrames%180==0)std::cout<<"BM02 real combat HP"<<(found!=world.actors->bodies().end()?vm.field(found->second.script,13).integer():-1)<<" AshHP"<<vm.field(world.game.ash,13).integer()<<" at"<<world.actor.position[0]<<','<<world.actor.position[2]<<" hits"<<hits<<'\n';
                stalled=0;previous=std::numeric_limits<float>::infinity();continue;}
            if(combatFrames>0&&!vm.staticField(532).integer()&&chase){const Vec2 point{chase->pose.x,chase->pose.z};
                tick(toward(point,1));stalled=0;previous=std::numeric_limits<float>::infinity();continue;}
            if(remaining<.3f){tick();stalled=0;previous=std::numeric_limits<float>::infinity();continue;}
            stalled=remaining>=previous-.00001f?stalled+1:0;
            if(stalled==119){std::cerr<<"BM02 stall radius"<<world.actor.clearanceRadius<<" full"<<world.scene.radius<<" targetClear"<<clearGround(world.scene.ground,target,world.actor.clearanceRadius)<<" hereClear"<<clearGround(world.scene.ground,at,world.actor.clearanceRadius)<<"\n";
                for(const auto& polygon:world.scene.ground)if(polygon.enabled){const auto boundary=nearestBoundary(polygon,at);if(boundary.distance<1)std::cerr<<" boundary"<<polygon.sourceIndex<<" d"<<boundary.distance<<"\n";}
                for(const auto& [key,body]:world.actors->bodies())if(key.type==28&&vm.field(body.script,13).integer()>0)std::cerr<<" NPC"<<key.index<<" hp"<<vm.field(body.script,13).integer()<<" xyz"<<body.pose.x<<","<<body.pose.y<<","<<body.pose.z<<" flags"<<body.flags<<"\n";}
            require(stalled<120,"source path stalled at "+std::to_string(at.x)+","+std::to_string(at.z)+" toward "+std::to_string(target.x)+","+std::to_string(target.z));previous=remaining;
            tick(toward(target,std::min(1.f,remaining/(world.scene.speed*2*.033f))));}
        throw std::runtime_error("Earned BM02 walk: bounded route budget exhausted");};
    for(unsigned index:{10,9,8,7,6,5,4,1,12})walk(nav(index),false,index==12);
    require(hits>0&&vm.staticField(532).integer()==1,"actual free guardian combat did not earn532");ready();
    const auto marker=world.scene.scripting.markers.at(1);tick({},false,false,0);tick(toward({marker.x,marker.z},1),false,false,0);
    require(world.scene.scripting.inside(7,{world.actor.position[0],world.actors->at({0,0}).localY,world.actor.position[2]})&&
        world.scene.scripting.facing(1,50,{world.actor.position[0],world.actor.position[2]},world.actor.yaw),"walked source door approach missed volume7/facing1");
    tick({},false,true,0);for(unsigned n=0;n<180&&world.scene.id=="bm02";++n)tick();
    require(world.scene.id=="bm12"&&world.transitions==initialTransitions+1,"actual guardian gate/portal1 did not reach BM12 exactly once");ready();
    const auto saved=world.saveStateBytes();for(unsigned n=0;n<3;++n)tick();world.restoreStateBytes(saved,nullptr);require(world.saveStateBytes()==saved,"walked guardian arrival did not save/load byte-identically");
    std::cout<<"Earned BM02 WALK: actual bridge arrival"<<arrival.x<<','<<arrival.z<<" -> original nav10/9/8/7/6/5/4/1/12 -> real guardian532 ->Use7/facing1/native portal1/BM12; "
        <<frames<<" source frames/"<<distance<<"m displacement, "<<combatFrames<<" combat frames/"<<hits<<" damage steps, minClearance"<<minRadius<<", HP"<<vm.field(world.game.ash,13).integer()<<", exact save, no new pose fixtures\n";
}
