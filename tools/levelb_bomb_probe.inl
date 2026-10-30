// Private earned continuation after the frozen BM01 bridge checkpoint.
void continueLevelBBomb(World& world) {
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
    // Continue the same earned World; no scene reload/quest/item fixture.
    if(!vm.staticField(536).integer()){approach(69,44);world.use();tick();}
    require(vm.staticField(536).integer()==1,"original BM11 lever did not earn536");
    approach(66,45);world.use();passage("bm11",5,"bt01");
    auto pickup=[&](int id){ready();const auto found=std::find_if(world.nativeProperties.objects.begin(),world.nativeProperties.objects.end(),
            [&](const auto& e){return (e.second.flags&1)&&e.second.setup[0]==id;});require(found!=world.nativeProperties.objects.end(),"source object not visible: "+std::to_string(id));
        const auto target=found->second.position;const auto point=sample([&](Vec2 p){const float d=length(p-Vec2{target.x,target.z});return d>.35f&&d<1.2f;},
            {{target.x-1.25f,target.z-1.25f},{target.x+1.25f,target.z+1.25f}});
        for(unsigned n=0;n<240&&!quantity(id);++n){if(world.game.mode!=0)ready();if(n%60==0){place(point,target);world.use();}tick();}
        require(quantity(id)>0,"real object contact did not grant"+std::to_string(id));ready();};
    require(!quantity(95)&&!vm.staticField(541).integer(),"BT01 component95 was already present");pickup(95);
    require(vm.staticField(541).integer()==1,"original BT01 reportPickup95 did not set541");
    approach(16,14);world.use();passage("bt01",5,"bm11");
    approach(20,12);world.use();for(unsigned n=0;n<60&&!quantity(94);++n)tick();
    require(quantity(94)==1&&vm.staticField(537).integer()==1,"original merchant did not now grant94 after533+541");ready();
    world.openInventory();world.syncInventory();auto slot=[&](int id){for(const auto& i:world.inventoryView.items)if(i.containerField==99&&i.itemID==id)return i.slot;return -1;};
    require(world.combineInventorySlots(slot(94),slot(95))==180&&quantity(94)==0&&quantity(95)==0&&quantity(180)==1,"source94+95 recipe did not grant180");
    require(world.combineInventorySlots(slot(180),slot(141))==18&&quantity(180)==0&&quantity(141)==0&&quantity(18)==1,"source180+141 bomb recipe failed");
    world.closeInventory();passage("bm11",0,"bm12");require(!vm.staticField(540).integer()&&!world.scene.enabledPortals.at(2),"BM12 bomb gate was already open");
    approach(7,2);world.openInventory();world.syncInventory();const auto bomb=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),
        [](const auto& i){return i.containerField==99&&i.itemID==18;});require(bomb!=world.inventoryView.items.end(),"earned bomb missing from actual inventory UI");
    world.inventoryView.selected=unsigned(bomb-world.inventoryView.items.begin());world.applyInventoryItem();
    require(vm.staticField(540).integer()==1&&quantity(18)==0&&world.scene.enabledPortals.at(2),"original item18 use/report did not earn540/consume bomb/open portal2");
    passage("bm12",2,"bt02");ready();const auto checkpoint=world.saveStateBytes();for(unsigned n=0;n<3;++n)tick();world.restoreStateBytes(checkpoint,nullptr);
    require(world.saveStateBytes()==checkpoint,"earned bomb passage did not roundtrip exactly");
    std::cout<<"Earned B bomb route: source lever536/BT01 object95+541 -> merchant94+537 -> source94+95=180 +rope141=18 -> actual BM12 bomb540 -> BT02; HP"
        <<vm.field(world.game.ash,13).integer()<<"; "<<fixtures<<" Ash-only approach fixtures\n";

}
