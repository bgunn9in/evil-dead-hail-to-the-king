#pragma once
// Original boss/door continuation; Ash-only floor approaches are explicit fixtures.
void continueLevelBMud(World& world) {
    auto& vm=world.game.vm;unsigned fixtures=0,damage=0;
    auto require=[](bool value,const std::string& text){if(!value)throw std::runtime_error("Original MudGolem: "+text);};
    auto tick=[&](){world.step({},false,false,.033f);
        if(world.pendingSceneSwitch){const auto request=*world.pendingSceneSwitch;world.pendingSceneSwitch.reset();
            require(!edpackage::reload(request.sourceSceneId,request.destinationId),"unexpected package switch");require(world.switchScene(request,nullptr),"source scene switch failed");}
        require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);require(vm.field(world.game.ash,13).integer()>0,"Ash naturally died");};
    auto ready=[&](){for(unsigned n=0;n<480;++n){if(world.game.mode!=0)world.closeInventory();world.playerControls({});const auto& ash=world.actors->at({0,0});
            if(!(ash.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&
               !vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&ash.actions.possible(116))return;tick();}
        throw std::runtime_error("Original MudGolem: source action did not become ready");};
    auto place=[&](Vec2 p,GroundPoint target){require(floorHeight(world.scene.floor,p).has_value()&&clearGround(world.scene.ground,p,0),"fixture outside original floor");
        auto& ash=world.actors->at({0,0});world.positionProgressBody(ash,{p.x,0,p.z});
        ash.pose.yaw=edactormath::modelYawFromHeading(float(std::atan2(double(target.z)-p.z,double(target.x)-p.x)));
        world.publishProgressBody(ash);world.actor.clearanceRadius=initialActorRadius(world.scene,p);world.actor.movement.held=false;++fixtures;};
    auto sample=[&](const auto& inside,const std::vector<Vec2>& points){require(!points.empty(),"empty source region");
        float x0=points[0].x,x1=x0,z0=points[0].z,z1=z0;for(auto p:points){x0=std::min(x0,p.x);x1=std::max(x1,p.x);z0=std::min(z0,p.z);z1=std::max(z1,p.z);}
        Vec2 chosen{};float best=-std::numeric_limits<float>::infinity();const Vec2 center{(x0+x1)*.5f,(z0+z1)*.5f};
        for(unsigned x=1;x<32;++x)for(unsigned z=1;z<32;++z){Vec2 p{x0+(x1-x0)*float(x)/32,z0+(z1-z0)*float(z)/32};
            if(!inside(p)||!floorHeight(world.scene.floor,p)||!clearGround(world.scene.ground,p,0))continue;
            float clearance=world.scene.radius;while(clearance>0&&!clearGround(world.scene.ground,p,clearance))clearance=std::max(0.f,clearance-.01f);
            const float score=clearance*10-length(p-center);if(score>best){best=score;chosen=p;}}
        require(std::isfinite(best),"source region has no legal approach");return chosen;};
    auto approach=[&](unsigned volume,unsigned marker){ready();const auto& region=world.scene.scripting.volumes.at(volume);std::vector<Vec2> points;
        for(auto p:region.points)points.push_back({p.x,p.z});place(sample([&](Vec2 p){return region.contains({p.x,0,p.z});},points),world.scene.scripting.markers.at(marker));};
    if(world.scene.id=="ba02"){
        require(vm.staticField(563).integer()&&vm.staticField(565).integer()&&vm.staticField(566).integer()&&vm.staticField(610).integer(),"missing earned pump/armory/gear checkpoint");
        approach(10,7);world.use();for(unsigned n=0;n<60&&world.scene.id=="ba02";++n)tick();
    }
    require(world.scene.id=="ba04"&&!vm.staticField(611).integer(),"requires undefeated source BA04");
    approach(9,2);world.use();require(!world.scene.enabledPortals.at(1),"door opened before real boss death");
    bool sawBoss=false;
    for(unsigned n=0;n<8000&&!vm.staticField(611).integer();++n){
        const edactors::Body* enemy=nullptr;for(const auto& [key,body]:world.actors->bodies())if(key.type==27&&vm.field(body.script,13).integer()>0&&!vm.field(body.script,world.game.hostField(HostFieldRole::GenericNpc30)).integer()){enemy=&body;break;}
        if(!enemy){tick();continue;}sawBoss=true;const auto key=enemy->key;const int hp=vm.field(enemy->script,13).integer();
        Vec2 chosen{};float best=-std::numeric_limits<float>::infinity();
        for(float d:{2.35f,2.10f,1.60f})for(unsigned direction=0;direction<32;++direction){const float angle=2*edactormath::Pi*float(direction)/32;
            const Vec2 p{enemy->pose.x+d*std::sin(angle),enemy->pose.z+d*std::cos(angle)};
            if(!floorHeight(world.scene.floor,p)||!clearGround(world.scene.ground,p,world.scene.radius+.02f))continue;
            const float score=std::cos(angle-enemy->pose.yaw-edactormath::Pi)+d*.01f;if(score>best){best=score;chosen=p;}}
        if(!std::isfinite(best)){tick();continue;}place(chosen,{enemy->pose.x,enemy->localY,enemy->pose.z});
        ControlFrame input;input.down[size_t(ControlAction::Aim)]=true;input.edges[size_t(ControlAction::SecondaryAttack)]=world.sourceTick%6==0;world.playerControls(input);tick();
        const auto found=world.actors->bodies().find(key);if(found!=world.actors->bodies().end()&&vm.field(found->second.script,13).integer()<hp)++damage;
        if(n%500==0)std::cout<<"MudGolem targetHP"<<hp<<" AshHP"<<vm.field(world.game.ash,13).integer()<<" damageSteps"<<damage<<'\n';
    }
    require(sawBoss&&damage>0&&vm.staticField(611).integer()==1,"free AI/source contacts did not earn death611");world.playerControls({});
    const unsigned cameraField0=world.game.profile.dreamcast?1445u:1513u,cameraField1=world.game.profile.dreamcast?1446u:1514u;
    for(unsigned n=0;n<240&&(vm.field(world.game.scene,cameraField1).integer()==-1||vm.field(world.game.scene,cameraField0).integer()>=0);++n)tick();
    require(vm.field(world.game.scene,cameraField1).integer()!=-1&&vm.field(world.game.scene,cameraField0).integer()<0,"original post-death camera did not finish");ready();
    approach(9,2);world.use();ready();require(world.scene.id=="ba04"||world.scene.id=="bl03","door reached unexpected scene");
    if(world.scene.id=="ba04"){const auto portal=world.scene.nativePortals.at(1);require(world.scene.enabledPortals.at(1),"door remained closed after611");
        const auto p=sample([&](Vec2 at){return insideTrigger(portal,at);},portal.points);place(p,{p.x,0,p.z+1});
        for(unsigned n=0;n<180&&world.scene.id=="ba04";++n)tick();}
    require(world.scene.id=="bl03","original portal1 did not reach BL03");ready();
    const auto saved=world.saveStateBytes();for(unsigned n=0;n<3;++n)tick();world.restoreStateBytes(saved,nullptr);
    require(world.saveStateBytes()==saved,"boss/door/source state failed exact save/load");
    std::cout<<"Original MudGolem27: real attacks/free AI ->death611 ->Use9/facing2 ->portal1 BL03; "<<damage<<" damage steps, "<<fixtures<<" Ash-only approach fixtures, HP"<<vm.field(world.game.ash,13).integer()<<'\n';
}
