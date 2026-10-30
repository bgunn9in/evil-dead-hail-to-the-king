// Private real-input route after the earned BM02 walking arrival.
void continueEarnedMerchantWalkWorld(World& world) {
    auto& vm=world.game.vm;std::cout<<std::unitbuf;
    auto require=[](bool ok,const std::string& text){if(!ok)throw std::runtime_error("Earned merchant walk: "+text);};
    auto quantity=[&](int id){int n=0;for(int field:{98,99,100})for(const auto& item:world.game.items(field))if(item.itemID==id)n+=item.quantity;return n;};
    require(world.scene.id=="bm12"&&vm.staticField(531).integer()&&vm.staticField(532).integer()&&!vm.staticField(533).integer()&&!vm.staticField(539).integer()&&!quantity(141),"missing actual walked guardian arrival");
    unsigned frames=0,combat=0;float distance=0;
    auto tick=[&](Vec2 stick=Vec2{},bool aim=false,bool use=false,float dt=.033f,bool run=true){const Vec2 old{world.actor.position[0],world.actor.position[2]};const auto source=world.scene.id;
        ControlFrame input;input.movement=stick;input.down[size_t(ControlAction::Aim)]=aim;input.edges[size_t(ControlAction::SecondaryAttack)]=aim&&dt>0&&world.sourceTick%6==0;
        world.playerControls(input);const auto moved=world.step(stick,run,use,dt);require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);
        require(vm.field(world.game.ash,13).integer()>0,"Ash naturally died");require(world.scene.id=="bm12"||world.scene.id=="bm11","route entered unexpected scene");
        const Vec2 at{world.actor.position[0],world.actor.position[2]};require(floorHeight(world.scene.floor,at).has_value()&&clearGround(world.scene.ground,at,world.actor.clearanceRadius),"input left source floor or entered an active solid");
        require(moved.moved<=moved.requested+ContactSkin*4,"input exceeded requested displacement");if(dt>0){++frames;distance+=source==world.scene.id?length(at-old):moved.moved;}};
    auto ready=[&](){for(unsigned n=0;n<480;++n){if(world.game.mode!=0)world.closeInventory();world.playerControls({});const auto& ash=world.actors->at({0,0});
        if(!(ash.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&!vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&ash.actions.possible(116))return;tick();}
        throw std::runtime_error("Earned merchant walk: source action did not become ready");};
    auto toward=[&](Vec2 target,float amount){const auto& camera=world.scene.cameras.at(world.actor.camera);const auto& held=world.actor.movement;
        const Vec2 right=held.held?held.right:camera.right,forward=held.held?held.forward:camera.forward,delta=target-Vec2{world.actor.position[0],world.actor.position[2]};
        const float det=right.x*forward.z-right.z*forward.x;require(std::abs(det)>.001f,"camera basis is singular");Vec2 stick{(delta.x*forward.z-delta.z*forward.x)/det,(right.x*delta.z-right.z*delta.x)/det};
        const float size=length(stick);return size>.000001f?stick*(amount/size):Vec2{};};
    auto nav=[&](unsigned index){const auto& raw=world.scene.nativeBytes;uint32_t n=0,o=0;require(raw.size()>=56,"missing source navigation header");std::memcpy(&n,raw.data()+48,4);std::memcpy(&o,raw.data()+52,4);
        require(index<n&&o<=raw.size()&&size_t(index)*44+20<=raw.size()-o,"missing source navigation node");GroundPoint p;std::memcpy(&p,raw.data()+o+index*44+8,12);return Vec2{p.x,p.z};};
    auto sample=[&](unsigned index){const auto& volume=world.scene.scripting.volumes.at(index);const auto& points=volume.points;require(!points.empty(),"empty source interaction geometry");
        float x0=points[0].x,x1=x0,z0=points[0].z,z1=z0;for(auto p:points){x0=std::min(x0,p.x);x1=std::max(x1,p.x);z0=std::min(z0,p.z);z1=std::max(z1,p.z);}
        Vec2 chosen{};float best=std::numeric_limits<float>::infinity();const Vec2 center{(x0+x1)*.5f,(z0+z1)*.5f};
        for(unsigned x=1;x<32;++x)for(unsigned z=1;z<32;++z){const Vec2 p{x0+(x1-x0)*float(x)/32,z0+(z1-z0)*float(z)/32};
            if(volume.contains({p.x,0,p.z})&&floorHeight(world.scene.floor,p)&&clearGround(world.scene.ground,p,0)&&length(p-center)<best){best=length(p-center);chosen=p;}}
        require(std::isfinite(best),"source interaction has no floor goal");return chosen;};
    auto walk=[&](Vec2 goal,const std::function<bool()>& done=std::function<bool()>{}){tick({},false,false,0);float previous=std::numeric_limits<float>::infinity();unsigned stalled=0;
        for(unsigned n=0;n<2500;++n){if(done&&done())return;if(world.game.mode!=0){ready();continue;}const auto& ash=world.actors->at({0,0});
            const Vec2 at{ash.pose.x,ash.pose.z};const float remaining=length(goal-at);if(remaining<.15f){tick({},false,false,0);return;}
            const edactors::Body* enemy=nullptr;float nearest=3.1f;
            for(const auto& [key,body]:world.actors->bodies())if(key.type==28&&body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&vm.field(body.script,13).integer()>0){
                const float d=length(Vec2{body.pose.x,body.pose.z}-at),reach=std::min(3.1f,2.4f+float(body.radius)*.01f);
                if(d<nearest&&d<reach&&world.scene.queries.lineOfSight({ash.pose.x,ash.pose.y,ash.pose.z},{body.pose.x,body.localY,body.pose.z},4,world.sourceQueryCamera(),world.scene.enabledVolumes,world.visibilityReady)){nearest=d;enemy=&body;}}
            if(enemy){tick(toward({enemy->pose.x,enemy->pose.z},1),true,false,0,false);tick({},true,false,.033f,false);require(++combat<4000,"source nearby combat budget exceeded");stalled=0;previous=std::numeric_limits<float>::infinity();continue;}
            stalled=remaining>=previous-.00001f?stalled+1:0;if(stalled==119)savefile::atomicWrite(fs::absolute("build"),"merchant-walk-stall.eds",world.saveStateBytes());
            require(stalled<120,"route stalled in "+world.scene.id+" at "+std::to_string(at.x)+","+std::to_string(at.z)+" toward "+std::to_string(goal.x)+","+std::to_string(goal.z));previous=remaining;
            tick(toward(goal,std::min(1.f,remaining/(world.scene.speed*2*.033f))));}
        throw std::runtime_error("Earned merchant walk: bounded route budget exhausted");};
    auto face=[&](unsigned marker,int angle){ready();const auto p=world.scene.scripting.markers.at(marker);tick({},false,false,0,false);tick(toward({p.x,p.z},1),false,false,0,false);
        require(world.scene.scripting.facing(marker,angle,{world.actor.position[0],world.actor.position[2]},world.actor.yaw),"real facing input missed original marker");};
    auto portalGoal=[&](unsigned index){const auto& points=world.scene.nativePortals.at(index).points;Vec2 p{};for(auto v:points)p=p+v;return p*(1.f/float(points.size()));};
    const auto initialTransitions=world.transitions;
    auto playerCorridor=[&](unsigned index){const float z=nav(index).z;const auto& right=world.scene.scripting.volumes.at(0).points;const auto& left=world.scene.scripting.volumes.at(1).points;
        auto xAt=[&](GroundPoint a,GroundPoint b){require(z>=std::min(a.z,b.z)&&z<=std::max(a.z,b.z)&&a.z!=b.z,"source player corridor edge does not cover navigation height");return a.x+(b.x-a.x)*(z-a.z)/(b.z-a.z);};
        const Vec2 goal{(xAt(left.at(2),left.at(3))+xAt(right.at(0),right.at(1)))*.5f,z};
        require(floorHeight(world.scene.floor,goal).has_value()&&clearGround(world.scene.ground,goal,0),"derived source player corridor is not walkable");return goal;};
    for(unsigned i:{1,2,3,4,5,6})walk(playerCorridor(i));require(world.scene.enabledPortals.at(1),"original BM12 return portal is disabled");
    walk(portalGoal(1),[&](){return world.scene.id=="bm11";});require(world.scene.id=="bm11"&&world.transitions==initialTransitions+1,"walk did not enter native BM11 portal");
    std::cout<<"Merchant WALK reached BM11 at "<<world.actor.position[0]<<','<<world.actor.position[2]<<'\n';
    for(unsigned i:{2,1,4,6,7,8,15})walk(nav(i));walk(sample(99));face(27,50);
    require(world.scene.scripting.inside(99,{world.actor.position[0],world.actors->at({0,0}).localY,world.actor.position[2]}),"walk did not reach original stair interaction99");
    const auto sceneRef=world.game.scene;const auto generation=world.progressArenaGeneration;const auto target=world.scene.scripting.markers.at(5);const auto stair=std::find_if(world.scene.portalData.portals.begin(),world.scene.portalData.portals.end(),[](const auto& p){return p.index==2;});require(stair!=world.scene.portalData.portals.end(),"missing original same-scene stair portal2");
    tick({},false,true,0,false);ready();auto arrived=[&](){return world.actor.camera==stair->camera&&length(Vec2{world.actor.position[0]-target.x,world.actor.position[2]-target.z})<.25f;};
    if(!arrived())walk(portalGoal(2),arrived);for(unsigned n=0;n<90&&!arrived();++n)tick();
    require(arrived()&&world.game.scene==sceneRef&&world.progressArenaGeneration==generation&&!world.scene.enabledPortals.at(6),"source stair portal did not apply original marker/camera without recreation");
    for(unsigned i:{41,42,60,63,47,48})walk(nav(i));for(unsigned n=0;n<180&&!vm.staticField(539).integer();++n)tick();require(vm.staticField(539).integer(),"walking through original intro67 did not earn539");ready();
    for(unsigned i:{47,63,60,42,41})walk(nav(i));walk(sample(98));face(26,50);
    const auto lower=world.scene.scripting.markers.at(0);const auto down=std::find_if(world.scene.portalData.portals.begin(),world.scene.portalData.portals.end(),[](const auto& p){return p.index==6;});require(down!=world.scene.portalData.portals.end(),"missing original return stair portal6");
    tick({},false,true,0,false);ready();auto descended=[&](){return world.actor.camera==down->camera&&length(Vec2{world.actor.position[0]-lower.x,world.actor.position[2]-lower.z})<.25f;};
    if(!descended())walk(portalGoal(6),descended);for(unsigned n=0;n<90&&!descended();++n)tick();
    require(descended()&&world.game.scene==sceneRef&&world.progressArenaGeneration==generation&&!world.scene.enabledPortals.at(2),"source return stair did not preserve scene/apply authored arrival");
    for(unsigned i:{15,40,39,38,37,36,35,34,33,32})walk(nav(i));walk(sample(68),[&](){return world.scene.scripting.inside(68,{world.actor.position[0],world.actors->at({0,0}).localY,world.actor.position[2]});});face(43,65);
    require(world.scene.scripting.inside(68,{world.actor.position[0],world.actors->at({0,0}).localY,world.actor.position[2]}),"walk did not enter original rope interaction68");
    tick({},false,true,0,false);for(unsigned n=0;n<180&&!quantity(141);++n)tick();require(quantity(141)==1&&vm.staticField(533).integer(),"actual merchant Use did not grant rope141/report533");ready();
    const auto saved=world.saveStateBytes();for(unsigned n=0;n<3;++n)tick();world.restoreStateBytes(saved,nullptr);require(world.saveStateBytes()==saved,"walked merchant state did not save/load exactly");
    std::cout<<"Earned merchant WALK: BM12 source nav/portal1 ->BM11 source nav/stair99-facing27-portal2 ->intro67/539 ->return98-facing26-portal6/lower road ->Use68-facing43 ->rope141/533; "<<frames<<" frames/"<<distance<<"m displacement including authored stair transfer, "<<combat<<" combat frames, HP"<<vm.field(world.game.ash,13).integer()<<", exactsave, no new pose fixtures\n";
}
