#pragma once
// Original lamp/book/relic chain; only Ash floor approaches are fixtures.
void continueLevelBLibrary(World& world) {
    auto& vm=world.game.vm;unsigned fixtures=0;
    auto require=[](bool value,const std::string& message){if(!value)throw std::runtime_error("Original library: "+message);};
    auto quantity=[&](int id){int count=0;for(int container:{98,99,100})for(const auto& item:world.game.items(container))if(item.itemID==id)count+=item.quantity;return count;};
    auto tick=[&](){world.step({},false,false,.033f);if(world.pendingSceneSwitch){const auto request=*world.pendingSceneSwitch;world.pendingSceneSwitch.reset();require(!edpackage::reload(request.sourceSceneId,request.destinationId),"unexpected package request in library");require(world.switchScene(request,nullptr),"original queued scene switch failed");}require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);
        require(world.notice!="action is not connected yet"&&world.notice!="could not perform action","source input failed");
        require(vm.field(world.game.ash,13).integer()>0,"Ash naturally died");};
    auto ready=[&](){for(unsigned n=0;n<480;++n){if(world.game.mode!=0)world.closeInventory();world.playerControls({});const auto& ash=world.actors->at({0,0});
            if(!(ash.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&
                !vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&ash.actions.possible(116))return;tick();}
        throw std::runtime_error("Original library: input did not become ready");};
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

    require(world.scene.id=="bl03"&&!vm.staticField(591).integer()&&!vm.staticField(589).integer(),"requires unsolved original library arrival");
    auto intro=[&](int flag){if(!vm.staticField(flag).integer()){approach(1,0);for(unsigned n=0;n<360&&!vm.staticField(flag).integer();++n)tick();}
        require(vm.staticField(flag).integer()==1,"original library intro did not finish");ready();};
    auto roundtrip=[&](){ready();const auto saved=world.saveStateBytes();for(unsigned n=0;n<3;++n)tick();world.restoreStateBytes(saved,nullptr);const auto restored=world.saveStateBytes();
        if(restored!=saved){auto write=[](const char* path,const std::vector<uint8_t>& bytes){std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());};
            write("build/root-postgear/library-before.world",saved);write("build/root-postgear/library-after.world",restored);
            size_t first=0;while(first<std::min(saved.size(),restored.size())&&saved[first]==restored[first])++first;
            std::cerr<<"Library save difference at"<<first<<" sizes"<<saved.size()<<'/'<<restored.size()<<" scene"<<world.scene.id<<" lamp"<<vm.staticField(590).integer()<<" books"<<vm.staticField(591).integer()<<'\n';}
        require(restored==saved,"library state did not restore exactly");};
    auto pickup=[&](int id){ready();const auto object=std::find_if(world.nativeProperties.objects.begin(),world.nativeProperties.objects.end(),
            [&](const auto& entry){return(entry.second.flags&1)&&entry.second.setup[0]==id;});require(object!=world.nativeProperties.objects.end(),"source object absent: "+std::to_string(id));
        const auto target=object->second.position;const auto p=sample([&](Vec2 at){const float d=length(at-Vec2{target.x,target.z});return d>.35f&&d<1.2f;},
            {{target.x-1.25f,target.z-1.25f},{target.x+1.25f,target.z+1.25f}});
        for(unsigned n=0;n<240&&!quantity(id);++n){if(world.game.mode!=0)ready();if(n%60==0){place(p,target);world.use();}tick();}
        require(quantity(id)>0,"source contact did not collect "+std::to_string(id));ready();};
    auto item=[&](int id,unsigned volume,unsigned marker){approach(volume,marker);world.openInventory();world.syncInventory();
        const auto selected=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[&](const auto& value){return value.containerField==99&&value.itemID==id;});
        require(selected!=world.inventoryView.items.end(),"source item missing from inventory UI");world.inventoryView.selected=unsigned(selected-world.inventoryView.items.begin());world.applyInventoryItem();ready();};
    auto books=[&](std::initializer_list<unsigned> order){for(unsigned book:order){approach(20+book,20+book);world.use();ready();tick();}};
    intro(603);unsigned damage=0;
    for(unsigned n=0;n<8000&&!vm.staticField(680).integer();++n){
        const edactors::Body* enemy=nullptr;for(const auto& [key,body]:world.actors->bodies())if(key.type==31&&vm.field(body.script,13).integer()>0&&!vm.field(body.script,world.game.hostField(HostFieldRole::GenericNpc30)).integer()){enemy=&body;break;}
        if(!enemy){tick();continue;}const auto key=enemy->key;const int hp=vm.field(enemy->script,13).integer();
        Vec2 chosen{};float best=-std::numeric_limits<float>::infinity();
        for(float d:{2.35f,2.10f})for(unsigned direction=0;direction<32;++direction){const float angle=2*edactormath::Pi*float(direction)/32;
            const Vec2 p{enemy->pose.x+d*std::sin(angle),enemy->pose.z+d*std::cos(angle)};
            if(!floorHeight(world.scene.floor,p)||!clearGround(world.scene.ground,p,world.scene.radius+.02f))continue;
            float safety=10;bool clear=true;for(const auto& [otherKey,other]:world.actors->bodies())if(otherKey.type==31&&vm.field(other.script,13).integer()>0){
                const float separation=length(p-Vec2{other.pose.x,other.pose.z});safety=std::min(safety,separation);
                if(separation<float(world.actors->at({0,0}).radius+other.radius)*.01f+.02f)clear=false;}
            const float score=safety+std::cos(angle-enemy->pose.yaw-edactormath::Pi)*.02f;if(clear&&score>best){best=score;chosen=p;}}
        if(!std::isfinite(best)){tick();continue;}place(chosen,{enemy->pose.x,enemy->localY,enemy->pose.z});
        ControlFrame input;input.down[size_t(ControlAction::Aim)]=true;input.edges[size_t(ControlAction::SecondaryAttack)]=world.sourceTick%6==0;world.playerControls(input);tick();
        const auto found=world.actors->bodies().find(key);if(found!=world.actors->bodies().end()&&vm.field(found->second.script,13).integer()<hp)++damage;
        if(n%500==0)std::cout<<"Library NetherPet targetHP"<<hp<<" AshHP"<<vm.field(world.game.ash,13).integer()<<" damageSteps"<<damage<<'\n';
    }
    require(vm.staticField(680).integer()==1&&damage>0,"original two-wave NetherPet encounter did not reveal book47");ready();
    pickup(47);require(vm.staticField(602).integer()==1,"original book pickup did not earn602");
    passage("bl03",1,"bl06");intro(593);
    require(!vm.staticField(590).integer(),"library lamp was already lit");
    books({2,4,1,3});for(unsigned n=0;n<10;++n)tick();
    require(!vm.staticField(591).integer(),"correct order bypassed the original lighting requirement");
    for(int field:{594,595,596,597,598,599,600,601})require(vm.staticField(field).integer()==0,"unlit puzzle did not reset source books");
    pickup(115);require(vm.staticField(592).integer()==1,"oil pickup did not report592");
    item(115,29,27);require(quantity(115)==0&&quantity(176)==1,"original oil use did not transform115 to176");
    item(176,28,26);require(vm.staticField(590).integer()==1,"original lamp use did not earn590");roundtrip();
    books({2,4,1,3});for(unsigned n=0;n<10&&!vm.staticField(591).integer();++n)tick();
    require(vm.staticField(591).integer()==1,"lit original book order2,4,1,3 did not solve591");roundtrip();
    pickup(151);require(vm.staticField(589).integer()==1&&world.scene.enabledPortals.at(1),"original relic pickup did not report589/open exit");
    passage("bl06",1,"bc01");roundtrip();
    std::cout<<"Original library: BL03 intro603 ->BL06 intro593 ->unlit book rejection/reset ->115/592 ->176 ->lamp590 ->books2,4,1,3/591 ->relic151/589 ->portal1 BC01; "<<damage<<" damage steps, "<<fixtures<<" Ash-only approach fixtures, HP"<<vm.field(world.game.ash,13).integer()<<'\n';
}
