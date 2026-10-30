// Private earned four-relic temple continuation.
void continueLevelBTemple(World& world) {
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

    require(world.scene.id=="bt02"&&vm.staticField(540).integer()&&!vm.staticField(543).integer()&&!vm.staticField(544).integer()&&
        !vm.staticField(545).integer()&&!vm.staticField(547).integer(),"missing earned BT02 bomb checkpoint or seeded relic state");
    unsigned damage=0;
    auto pickup=[&](int id){if(quantity(id))return true;const auto object=std::find_if(world.nativeProperties.objects.begin(),world.nativeProperties.objects.end(),
            [&](const auto& e){return(e.second.flags&1)&&e.second.setup[0]==id;});if(object==world.nativeProperties.objects.end())return false;
        const auto target=object->second.position;ready();const auto p=sample([&](Vec2 at){const float d=length(at-Vec2{target.x,target.z});return d>.35f&&d<1.2f;},
            {{target.x-1.25f,target.z-1.25f},{target.x+1.25f,target.z+1.25f}});
        for(unsigned n=0;n<240&&!quantity(id);++n){if(world.game.mode!=0)ready();if(n%60==0){place(p,target);world.use();}tick();}
        if(!quantity(id)){std::cerr<<"Temple pickup diagnostics id="<<id<<" target="<<target.x<<","<<target.y<<","<<target.z<<" actor="<<world.actor.position[0]<<","<<world.actor.position[1]<<","<<world.actor.position[2]<<" flags="<<world.actors->at({0,0}).flags<<" action="<<vm.field(world.game.ash,11).integer()<<" notice="<<world.notice<<"\n";}require(quantity(id)>0,"source contact did not collect relic"+std::to_string(id));ready();std::cout<<"Earned temple object "<<id<<" in "<<world.scene.id<<'\n';return true;};
    auto intro=[&](unsigned volume,unsigned marker,int flag){if(!vm.staticField(flag).integer()){approach(volume,marker);for(unsigned n=0;n<360&&!vm.staticField(flag).integer();++n)tick();}
        require(vm.staticField(flag).integer()==1,"original temple intro did not finish"+std::to_string(flag));ready();};
    auto earn=[&](int id){for(unsigned n=0;n<6000&&!quantity(id);++n){
            if(world.game.mode!=0)ready();const edactors::Body* enemy=nullptr;
            for(const auto& [key,b]:world.actors->bodies())if(key.type==23&&vm.field(b.script,13).integer()>0&&!vm.field(b.script,1251).integer()){enemy=&b;break;}
            if(!enemy){if(pickup(id))return;tick();continue;}const auto key=enemy->key;const int hp=vm.field(enemy->script,13).integer();const bool hidden=!(enemy->flags&1)||(enemy->flags&8);bool moved=false;
            Vec2 chosen{};float best=-std::numeric_limits<float>::infinity();
            for(float d:{2.35f,2.10f})for(unsigned direction=0;direction<32;++direction){
                const float angle=2*edactormath::Pi*float(direction)/32;
                const Vec2 p{enemy->pose.x+d*std::sin(angle),enemy->pose.z+d*std::cos(angle)};
                if(!floorHeight(world.scene.floor,p)||!clearGround(world.scene.ground,p,world.scene.radius+.02f))continue;
                float safety=10;for(const auto& [otherKey,other]:world.actors->bodies())if(otherKey.type==23&&vm.field(other.script,13).integer()>0)
                    safety=std::min(safety,length(p-Vec2{other.pose.x,other.pose.z}));
                const float rear=std::cos(angle-enemy->pose.yaw-edactormath::Pi);
                const float score=safety+rear*.02f;if(score>best){best=score;chosen=p;moved=true;}}
            if(moved)place(chosen,{enemy->pose.x,enemy->localY,enemy->pose.z});
            if(!moved){tick();continue;}ControlFrame input;if(!hidden){input.down[size_t(ControlAction::Aim)]=true;input.edges[size_t(ControlAction::SecondaryAttack)]=world.sourceTick%6==0;}world.playerControls(input);tick();
            const auto found=world.actors->bodies().find(key);if(found!=world.actors->bodies().end()&&vm.field(found->second.script,13).integer()<hp)++damage;
            if(n%500==0)std::cout<<"Temple combat "<<world.scene.id<<" item"<<id<<" targetHP"<<hp<<" AshHP"<<vm.field(world.game.ash,13).integer()<<" damageSteps"<<damage<<"\n";
        }require(quantity(id)>0,"natural temple encounter did not yield"+std::to_string(id));};
    auto use=[&](unsigned volume,unsigned marker){approach(volume,marker);world.use();};
    auto item=[&](int id,unsigned volume,unsigned marker,int flag){approach(volume,marker);world.openInventory();world.syncInventory();
        const auto selected=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[&](const auto& e){return e.containerField==99&&e.itemID==id;});
        require(selected!=world.inventoryView.items.end(),"earned relic missing from actual UI");world.inventoryView.selected=unsigned(selected-world.inventoryView.items.begin());world.applyInventoryItem();
        require(vm.staticField(flag).integer()==1&&quantity(id)==0,"original relic use/report failed");ready();};
    use(42,25);require(!world.scene.enabledPortals.at(6),"locked key143 door opened before earned item");
    use(44,27);require(!world.scene.enabledPortals.at(8),"locked key144 door opened before earned item");
    use(41,24);passage("bt02",5,"bt04");intro(1,4,554);earn(144);require(vm.staticField(552).integer(),"BT04 original key report552 absent");
    use(16,3);passage("bt04",1,"bt02");item(144,44,27,543);use(44,27);passage("bt02",8,"bt10");intro(1,3,558);earn(145);
    use(10,2);passage("bt10",1,"bt02");item(145,46,28,545);use(43,26);passage("bt02",7,"bt08");intro(0,7,555);earn(143);
    use(17,4);passage("bt08",1,"bt02");item(143,42,25,544);use(42,25);passage("bt02",6,"bt06");intro(12,2,557);earn(82);
    use(8,1);passage("bt06",1,"bt02");item(82,47,29,547);
    for(unsigned n=0;n<30&&world.scene.id=="bt02";++n)tick();require(world.scene.id=="bt11","original two-statue processFrame did not switch toBT11");ready();
    const auto checkpoint=world.saveStateBytes();for(unsigned n=0;n<3;++n)tick();world.restoreStateBytes(checkpoint,nullptr);require(world.saveStateBytes()==checkpoint,"earned temple state failed exact save/load");
    std::cout<<"Earned B temple: actual spokes/keys143+144/statues145+82/source uses543+544+545+547 -> original queued BT11 switch; "<<damage<<" damage steps, "<<fixtures<<" Ash-only approach fixtures, HP"<<vm.field(world.game.ash,13).integer()<<'\n';

}
