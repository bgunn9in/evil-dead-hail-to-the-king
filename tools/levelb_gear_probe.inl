// Private earned source water-control item88 continuation.
void continueLevelBGear(World& world) {
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

    require(world.scene.id=="bg13"&&vm.staticField(563).integer()&&vm.staticField(562).integer()&&vm.staticField(585).integer()&&vm.staticField(610).integer()&&!vm.staticField(565).integer()&&!vm.staticField(566).integer()&&!quantity(88),"missing earned Armory checkpoint or seeded water gear state");
    require(!world.scene.enabledPortals.at(1),"BG13 source return opened before actual88/report565");
    auto pickup=[&](int id){if(quantity(id))return true;const auto object=std::find_if(world.nativeProperties.objects.begin(),world.nativeProperties.objects.end(),
            [&](const auto& e){return(e.second.flags&1)&&e.second.setup[0]==id;});if(object==world.nativeProperties.objects.end())return false;
        const auto target=object->second.position;ready();const auto p=sample([&](Vec2 at){const float d=length(at-Vec2{target.x,target.z});return d>.35f&&d<1.2f;},
            {{target.x-1.25f,target.z-1.25f},{target.x+1.25f,target.z+1.25f}});
        for(unsigned n=0;n<240&&!quantity(id);++n){if(world.game.mode!=0)ready();if(n%60==0){place(p,target);world.use();}tick();}
        if(!quantity(id)){std::cerr<<"Temple pickup diagnostics id="<<id<<" target="<<target.x<<","<<target.y<<","<<target.z<<" actor="<<world.actor.position[0]<<","<<world.actor.position[1]<<","<<world.actor.position[2]<<" flags="<<world.actors->at({0,0}).flags<<" action="<<vm.field(world.game.ash,11).integer()<<" notice="<<world.notice<<"\n";}require(quantity(id)>0,"source contact did not collect relic"+std::to_string(id));ready();std::cout<<"Earned temple object "<<id<<" in "<<world.scene.id<<'\n';return true;};

    require(pickup(88)&&vm.staticField(565).integer()==1,"actual BG13 object88 pickup did not report565");
    for(unsigned n=0;n<30&&!world.scene.enabledPortals.at(1);++n)tick();passage("bg13",1,"bm11");
    approach(66,45);world.use();passage("bm11",5,"bt01");
    approach(15,13);world.use();passage("bt01",4,"ba11");
    approach(24,9);world.use();passage("ba11",2,"ba02");
    approach(8,6);world.openInventory();world.syncInventory();const auto selected=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),
        [&](const auto& e){return e.containerField==99&&e.itemID==88;});require(selected!=world.inventoryView.items.end(),"earned88 missing from actual inventory UI");
    world.inventoryView.selected=unsigned(selected-world.inventoryView.items.begin());world.applyInventoryItem();
    require(vm.staticField(566).integer()==1&&!quantity(88),"actual source item88 use did not consume gear/earn566");ready();
    require(world.scene.id=="ba02","earned gear checkpoint unexpectedly left BA02");
    const auto checkpoint=world.saveStateBytes();for(unsigned n=0;n<3;++n)tick();world.restoreStateBytes(checkpoint,nullptr);require(world.saveStateBytes()==checkpoint,"earned water gear state failed exact save/load");
    std::cout<<"Earned B gear: actual earned Armory610 -> source BG13 switch -> source object88/report565 -> original returns -> BA02 item88/use566 (before BA04 interaction); "<<fixtures<<" Ash-only approach fixtures, HP"<<vm.field(world.game.ash,13).integer()<<'\n';
}
