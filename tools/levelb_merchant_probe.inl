// Private earned continuation after the frozen BM01 bridge checkpoint.
void continueLevelBMerchant(World& world) {
    auto& vm=world.game.vm;unsigned fixtures=0,hits=0;
    auto require=[](bool value,const std::string& message){if(!value)throw std::runtime_error("Earned B continuation: "+message);};
    auto quantity=[&](int id){int count=0;for(int container:{98,99,100})for(const auto& item:world.game.items(container))if(item.itemID==id)count+=item.quantity;return count;};
    auto tick=[&](){world.step({},false,false,.033f);require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);
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
    require(world.scene.id=="bm01"&&vm.staticField(531).integer()==1,"missing earned BM01 bridge");
    require(!vm.staticField(532).integer()&&!vm.staticField(539).integer()&&!vm.staticField(533).integer()&&!quantity(141),"later source progress was present before its earned route");
    passage("bm01",0,"bm02");
    require(!vm.staticField(532).integer()&&!world.scene.enabledPortals.at(1),"BM02 conditional gate was already open");
    approach(7,1);world.use();tick();require(!world.scene.enabledPortals.at(1),"BM02 guard gate opened before original death callback");
    for(unsigned n=0;n<6000&&!vm.staticField(532).integer();++n){
        if(world.game.mode!=0)ready();const edactors::Body* target=nullptr;
        for(const auto& [key,body]:world.actors->bodies())if(key.type==28&&(body.flags&1)&&!(body.flags&8)&&vm.field(body.script,13).integer()>0&&!vm.field(body.script,1251).integer()){target=&body;break;}
        if(!target){tick();continue;}const auto enemy=target->key;const int hp=vm.field(target->script,13).integer();bool moved=false;
        for(float angle:{edactormath::Pi,edactormath::Pi-.2f,edactormath::Pi+.2f,edactormath::HalfPi,-edactormath::HalfPi}){
            const float d=std::max(1.05f,float(target->radius)*.01f+world.scene.radius+.12f);const Vec2 p{target->pose.x+d*std::sin(target->pose.yaw+angle),target->pose.z+d*std::cos(target->pose.yaw+angle)};
            if(!floorHeight(world.scene.floor,p)||!clearGround(world.scene.ground,p,world.scene.radius+.02f))continue;place(p,{target->pose.x,target->localY,target->pose.z});moved=true;break;}
        if(!moved){tick();continue;}ControlFrame input;input.down[size_t(ControlAction::Aim)]=true;input.edges[size_t(ControlAction::SecondaryAttack)]=world.sourceTick%6==0;
        world.playerControls(input);tick();const auto found=world.actors->bodies().find(enemy);if(found!=world.actors->bodies().end()&&vm.field(found->second.script,13).integer()<hp)++hits;
    }
    require(vm.staticField(532).integer()==1,"natural BM02 guardian did not earn532");approach(7,1);world.use();
    require(world.scene.enabledPortals.at(1),"original BM02 Use did not enable portal1 after532");passage("bm02",1,"bm12");passage("bm12",1,"bm11");
    require(!vm.staticField(533).integer()&&!quantity(141),"merchant reward was present before interaction");
    if(!vm.staticField(539).integer()){approach(67,43);for(unsigned n=0;n<30&&!vm.staticField(539).integer();++n)tick();}
    require(vm.staticField(539).integer()==1,"original merchant introduction did not run");
    approach(68,43);world.use();for(unsigned n=0;n<60&&!quantity(141);++n)tick();
    require(quantity(141)==1&&vm.staticField(533).integer()==1,"original merchant did not give rope141/report533");ready();
    approach(20,12);world.use();tick();require(quantity(94)==0&&!vm.staticField(537).integer(),"merchant gave carpart94 before original541 condition");ready();
    const auto saved=world.saveStateBytes();for(unsigned n=0;n<3;++n)tick();world.restoreStateBytes(saved,nullptr);require(world.saveStateBytes()==saved,"earned B merchant state did not roundtrip exactly");
    std::cout<<"Earned B continuation: bridge -> BM02 natural guardian532 -> original flags4 door -> BM12 -> BM11 introduction539/rope141+533 and conditional carpart refusal; "
        <<hits<<" damage steps, "<<fixtures<<" Ash-only approach fixtures, HP"<<vm.field(world.game.ash,13).integer()<<'\n';
}
