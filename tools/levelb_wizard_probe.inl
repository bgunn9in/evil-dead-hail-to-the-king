// Private earned WizardBoss continuation after the actual temple checkpoint.
void continueLevelBWizard(World& world) {
    auto& vm=world.game.vm;unsigned fixtures=0;
    auto require=[](bool value,const std::string& message){if(!value)throw std::runtime_error("Earned B continuation: "+message);};
    auto quantity=[&](int id){int count=0;for(int container:{98,99,100})for(const auto& item:world.game.items(container))if(item.itemID==id)count+=item.quantity;return count;};
    auto tick=[&](){world.step({},false,false,.033f);if(world.pendingSceneSwitch){const auto request=*world.pendingSceneSwitch;world.pendingSceneSwitch.reset();require(!edpackage::reload(request.sourceSceneId,request.destinationId),"unexpected package request in temple");require(world.switchScene(request,nullptr),"original queued scene switch failed");}require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);
        require(world.notice!="action is not connected yet"&&world.notice!="could not perform action","source input failed");
        require(vm.field(world.game.ash,13).integer()>0,"Ash naturally died");};
    auto ready=[&](){for(unsigned n=0;n<480;++n){if(world.game.mode!=0)world.closeInventory();world.playerControls({});const auto& ash=world.actors->at({0,0});
            if(!(ash.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&
                !vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&ash.actions.possible(116))return;tick();}
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

    require(world.scene.id=="bt11"&&vm.staticField(545).integer()&&vm.staticField(547).integer()&&!vm.staticField(609).integer(),"temple checkpoint or original WizardBoss state differs");
    unsigned damage=0;bool sawBoss=false;
    for(unsigned n=0;n<6000&&!vm.staticField(609).integer();++n){
        const edactors::Body* enemy=nullptr;for(const auto& [key,b]:world.actors->bodies())if(key.type==26&&vm.field(b.script,13).integer()>0&&!vm.field(b.script,1251).integer()){enemy=&b;break;}
        if(!enemy){tick();continue;}sawBoss=true;const auto key=enemy->key;const int hp=vm.field(enemy->script,13).integer();
        Vec2 chosen{};float best=-std::numeric_limits<float>::infinity();
        for(float d:{2.10f,1.60f})for(unsigned direction=0;direction<32;++direction){
            const float angle=2*edactormath::Pi*float(direction)/32;const Vec2 p{enemy->pose.x+d*std::sin(angle),enemy->pose.z+d*std::cos(angle)};
            if(!floorHeight(world.scene.floor,p)||!clearGround(world.scene.ground,p,world.scene.radius+.02f))continue;
            const float rear=std::cos(angle-enemy->pose.yaw-edactormath::Pi);const float score=rear+d*.01f;
            if(score>best){best=score;chosen=p;}}
        if(!std::isfinite(best)){tick();continue;}place(chosen,{enemy->pose.x,enemy->localY,enemy->pose.z});
        ControlFrame input;input.down[size_t(ControlAction::Aim)]=true;input.edges[size_t(ControlAction::SecondaryAttack)]=world.sourceTick%6==0;world.playerControls(input);tick();
        const auto found=world.actors->bodies().find(key);if(found!=world.actors->bodies().end()&&vm.field(found->second.script,13).integer()<hp)++damage;
        if(n%500==0)std::cout<<"Wizard combat targetHP"<<hp<<" AshHP"<<vm.field(world.game.ash,13).integer()<<" damageSteps"<<damage<<"\n";
    }
    require(sawBoss&&damage>0&&vm.staticField(609).integer()==1,"natural WizardBoss death did not earn609");
    std::cout<<"Wizard source death609 earned, damageSteps"<<damage<<" mode"<<world.game.mode<<" applied"<<vm.staticField(844).integer()<<"\n";ready();
    for(unsigned n=0;n<300&&world.scene.id=="bt11";++n)tick();require(world.scene.id=="ba01","original Wizard death processFrame did not requestBA01");ready();
    const auto checkpoint=world.saveStateBytes();for(unsigned n=0;n<3;++n)tick();world.restoreStateBytes(checkpoint,nullptr);require(world.saveStateBytes()==checkpoint,"earned Wizard state failed exact save/load");
    std::cout<<"Earned B Wizard: original BT11 native26/freeAI/input17 -> death609 -> original movie+sceneSwitch(BA01,0,1,160); "<<damage<<" damage steps, "<<fixtures<<" Ash-only approach fixtures, HP"<<vm.field(world.game.ash,13).integer()<<'\n';
}
