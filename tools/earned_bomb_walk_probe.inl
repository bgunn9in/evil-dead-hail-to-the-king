void continueEarnedBombWalkProbe(World& world) {
    auto& vm=world.game.vm;std::cout<<std::unitbuf;
    auto require=[](bool ok,const std::string& text){if(!ok)throw std::runtime_error("Earned bomb walk: "+text);};
    auto quantity=[&](int id){int n=0;for(int field:{98,99,100})for(const auto& item:world.game.items(field))if(item.itemID==id)n+=item.quantity;return n;};
    require(world.scene.id=="bm11"&&vm.staticField(533).integer()&&quantity(141)==1&&!vm.staticField(536).integer()&&!quantity(95),"missing actual walked Merchant141 checkpoint");
    unsigned frames=0,combat=0;float distance=0;
    auto tick=[&](Vec2 stick=Vec2{},bool aim=false,bool use=false,float dt=.033f,bool run=true){const Vec2 old{world.actor.position[0],world.actor.position[2]};const auto source=world.scene.id;
        ControlFrame input;input.movement=stick;input.down[size_t(ControlAction::Aim)]=aim;input.edges[size_t(ControlAction::SecondaryAttack)]=aim&&dt>0&&world.sourceTick%6==0;
        world.playerControls(input);const auto moved=world.step(stick,run,use,dt);require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);
        require(vm.field(world.game.ash,13).integer()>0,"Ash naturally died");require(world.scene.id=="bm12"||world.scene.id=="bm11"||world.scene.id=="bt01"||world.scene.id=="bt02","route entered unexpected scene");
        const Vec2 at{world.actor.position[0],world.actor.position[2]};require(floorHeight(world.scene.floor,at).has_value()&&clearGround(world.scene.ground,at,world.actor.clearanceRadius),"input left source floor or entered an active solid");
        require(moved.moved<=moved.requested+ContactSkin*4,"input exceeded requested displacement");if(dt>0){++frames;distance+=source==world.scene.id?length(at-old):moved.moved;}};
    auto ready=[&](){for(unsigned n=0;n<480;++n){if(world.game.mode!=0)world.closeInventory();world.playerControls({});const auto& ash=world.actors->at({0,0});
        if(!(ash.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&!vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&ash.actions.possible(116))return;tick();}
        throw std::runtime_error("Earned bomb walk: source action did not become ready");};
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
            stalled=remaining>=previous-.00001f?stalled+1:0;
            if(stalled>=120){const auto bytes=world.saveStateBytes();std::ofstream file("build/earned-bomb-walk-stall.world",std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
                throw std::runtime_error("Earned bomb walk: route stalled in "+world.scene.id+" at "+std::to_string(at.x)+","+std::to_string(at.z)+" toward "+std::to_string(goal.x)+","+std::to_string(goal.z));}previous=remaining;
            tick(toward(goal,std::min(1.f,remaining/(world.scene.speed*2*.033f))));}
        throw std::runtime_error("Earned bomb walk: bounded route budget exhausted");};
    auto face=[&](unsigned marker,int angle){ready();const auto p=world.scene.scripting.markers.at(marker);tick({},false,false,0,false);tick(toward({p.x,p.z},1),false,false,0,false);
        require(world.scene.scripting.facing(marker,angle,{world.actor.position[0],world.actor.position[2]},world.actor.yaw),"real facing input missed original marker");};
    auto portalGoal=[&](unsigned index){const auto& points=world.scene.nativePortals.at(index).points;Vec2 p{};for(auto v:points)p=p+v;return p*(1.f/float(points.size()));};

    const unsigned initialTransitions=world.transitions;
    std::cout<<"Bomb WALK actual Merchant141 start "<<world.actor.position[0]<<','<<world.actor.position[2]<<" tick"<<world.sourceTick<<" HP"<<vm.field(world.game.ash,13).integer()<<'\n';
    ready();for(unsigned i:{32,33,34,35,36,37,38,39,40,15})walk(nav(i));walk(sample(99));face(27,50);
    const auto stairMarker=world.scene.scripting.markers.at(5);auto upstairs=[&](){return length(Vec2{world.actor.position[0]-stairMarker.x,world.actor.position[2]-stairMarker.z})<.25f;};
    tick({},false,true,0,false);ready();if(!upstairs())walk(portalGoal(2),upstairs);for(unsigned n=0;n<90&&!upstairs();++n)tick();require(upstairs(),"source stair2 did not reach original upper marker5");
    for(unsigned i:{41,42,60,63,47,48,49,50,59,58,57,55,56})walk(nav(i));walk(sample(69));face(44,65);
    require(world.scene.scripting.inside(69,{world.actor.position[0],world.actors->at({0,0}).localY,world.actor.position[2]}),"walk did not enter original lever69");
    tick({},false,true,0,false);for(unsigned n=0;n<120&&!vm.staticField(536).integer();++n)tick();require(vm.staticField(536).integer()==1,"real lever69 Use did not earn536");ready();
    const auto saved=world.saveStateBytes();for(unsigned n=0;n<3;++n)tick();world.restoreStateBytes(saved,nullptr);require(world.saveStateBytes()==saved,"real lever536 walking boundary did not restore exactly");
    savefile::atomicWrite(fs::absolute("build"),"earned-lever536-walk.eds",saved);
    std::cout<<"Earned bomb WALK first boundary: actual Merchant141 ->lower road/source stair2 ->upper source nav ->lever69/facing44/Use ->536; "<<frames<<" frames/"<<distance<<"m including authored stair, "<<combat<<" combat frames, HP"<<vm.field(world.game.ash,13).integer()<<", exact save; no new pose setters\n";
    // Continue on foot after the same genuinely earned lever; no fixture or reload.
    for(unsigned i:{56,55,57,58,59,50,49,48,47,63,60,42,41})walk(nav(i));walk(sample(98));face(26,50);
    const auto downMarker=world.scene.scripting.markers.at(0);auto down=[&](){return length(Vec2{world.actor.position[0]-downMarker.x,world.actor.position[2]-downMarker.z})<.25f;};
    tick({},false,true,0,false);ready();if(!down())walk(portalGoal(6),down);for(unsigned n=0;n<90&&!down();++n)tick();require(down(),"source return stair6 did not reach original lower marker0");
    for(unsigned i:{15,40,39,38,37,36,35,34,33,32,31,30})walk(nav(i));walk(sample(66));face(45,65);tick({},false,true,0,false);ready();
    if(world.scene.id=="bm11")walk(portalGoal(5),[&](){return world.scene.id=="bt01";});for(unsigned n=0;n<180&&world.scene.id=="bm11";++n)tick();
    require(world.scene.id=="bt01","real enabled BM11 portal5 did not reach BT01");
    const auto entry=world.saveStateBytes();world.restoreStateBytes(entry);require(world.saveStateBytes()==entry,"actual BT01 arrival did not save exactly");
    savefile::atomicWrite(fs::absolute("build"),"earned-bt01-arrival-walk.eds",entry);
    std::cout<<"Bomb WALK reached actual BT01 at("<<world.actor.position[0]<<','<<world.actor.position[2]<<") tick"<<world.sourceTick<<" HP"<<vm.field(world.game.ash,13).integer()<<'\n';
    for(const auto& [index,obj]:world.nativeProperties.objects)if(obj.setup[0]==95)std::cout<<"Source object95 index"<<index<<" flags"<<obj.flags<<" at("<<obj.position.x<<','<<obj.position.z<<")\n";

    for(unsigned i:{2,0,14,13})walk(nav(i));const auto carpart=std::find_if(world.nativeProperties.objects.begin(),world.nativeProperties.objects.end(),
        [](const auto& entry){return (entry.second.flags&1)&&entry.second.setup[0]==95;});require(carpart!=world.nativeProperties.objects.end(),"original visible component95 is absent");
    const auto target95=carpart->second.position;walk({target95.x-.8f,target95.z});ready();tick(toward({target95.x,target95.z},1),false,false,0,false);
    for(unsigned n=0;n<240&&!quantity(95);++n){if(world.game.mode!=0)ready();if(n%60==0)tick({},false,true,0,false);tick();}
    require(quantity(95)==1&&vm.staticField(541).integer()==1,"real source contact did not earn component95/541");ready();
    const auto component=world.saveStateBytes();world.restoreStateBytes(component);require(world.saveStateBytes()==component,"actual95 did not save exactly");
    savefile::atomicWrite(fs::absolute("build"),"earned-component95-walk.eds",component);
    std::cout<<"Bomb WALK component95/541 earned at("<<world.actor.position[0]<<','<<world.actor.position[2]<<") tick"<<world.sourceTick<<" HP"<<vm.field(world.game.ash,13).integer()<<'\n';
    for(unsigned i:{13,14,0,2,3})walk(nav(i));walk(sample(16));face(14,65);tick({},false,true,0,false);ready();
    if(world.scene.id=="bt01")walk(portalGoal(5),[&](){return world.scene.id=="bm11";});for(unsigned n=0;n<180&&world.scene.id=="bt01";++n)tick();require(world.scene.id=="bm11","actual BT01 return portal5 did not enter BM11");
    for(unsigned i:{30,31,32,33,34,35,36,37,38,39,40,15})walk(nav(i));walk(sample(99));face(27,50);
    const auto upper2=world.scene.scripting.markers.at(5);auto up2=[&](){return length(Vec2{world.actor.position[0]-upper2.x,world.actor.position[2]-upper2.z})<.25f;};
    tick({},false,true,0,false);ready();if(!up2())walk(portalGoal(2),up2);for(unsigned n=0;n<90&&!up2();++n)tick();require(up2(),"actual source stair2 did not return to upper Merchant");
    for(unsigned i:{41,42,60,63,47,48})walk(nav(i));walk(sample(20));face(12,65);tick({},false,true,0,false);
    for(unsigned n=0;n<180&&!quantity(94);++n)tick();require(quantity(94)==1&&vm.staticField(537).integer()==1,"original Merchant did not grant94/537 after genuine95/541");ready();
    world.openInventory();world.syncInventory();auto slot=[&](int id){for(const auto& item:world.inventoryView.items)if(item.containerField==99&&item.itemID==id)return item.slot;return -1;};
    require(world.combineInventorySlots(slot(94),slot(95))==180&&quantity(94)==0&&quantity(95)==0&&quantity(180)==1,"source94+95 recipe did not make180");
    require(world.combineInventorySlots(slot(180),slot(141))==18&&quantity(180)==0&&quantity(141)==0&&quantity(18)==1,"source180+141 recipe did not make bomb18");
    world.closeInventory();ready();const auto bomb=world.saveStateBytes();world.restoreStateBytes(bomb);require(world.saveStateBytes()==bomb,"actual crafted bomb18 did not restore exactly");
    savefile::atomicWrite(fs::absolute("build"),"earned-bomb18-walk.eds",bomb);
    std::cout<<"Earned bomb WALK: actual Merchant141 ->lever536 ->BT01/source95+541 ->actualMerchant94+537 ->originalCombine94+95=180+rope141=18; "<<frames<<" frames/"<<distance<<"m including authored stairs, "<<combat<<" combat frames, HP"<<vm.field(world.game.ash,13).integer()<<", exactsave; no new pose setters\n";

    for(unsigned i:{48,47,63,60,42,41})walk(nav(i));walk(sample(98));face(26,50);
    const auto finalDown=world.scene.scripting.markers.at(0);auto descend=[&](){return length(Vec2{world.actor.position[0]-finalDown.x,world.actor.position[2]-finalDown.z})<.25f;};
    tick({},false,true,0,false);ready();if(!descend())walk(portalGoal(6),descend);for(unsigned n=0;n<90&&!descend();++n)tick();require(descend(),"original return stair6 failed after crafted bomb");
    for(unsigned i:{15,8,7,6,4,1,2,0})walk(nav(i));walk(portalGoal(0),[&](){return world.scene.id=="bm12";});
    for(unsigned n=0;n<180&&world.scene.id=="bm11";++n)tick();require(world.scene.id=="bm12"&&!vm.staticField(540).integer()&&!world.scene.enabledPortals.at(2),"original BM12 bomb gate was already open or return failed");
    auto corridor=[&](unsigned index){const float z=nav(index).z;const auto& a=world.scene.scripting.volumes.at(0).points;const auto& b=world.scene.scripting.volumes.at(1).points;
        auto xAt=[&](GroundPoint p,GroundPoint q){require(z>=std::min(p.z,q.z)&&z<=std::max(p.z,q.z)&&p.z!=q.z,"source corridor does not cover navigation Z");return p.x+(q.x-p.x)*(z-p.z)/(q.z-p.z);};
        const Vec2 at{(xAt(a.at(0),a.at(1))+xAt(b.at(2),b.at(3)))*.5f,z};require(clearGround(world.scene.ground,at,0)&&floorHeight(world.scene.floor,at).has_value(),"derived authored corridor is not walkable");return at;};
    for(unsigned i:{6,5,4,3,2,1})walk(corridor(i));walk(sample(7));face(2,65);ready();world.openInventory();world.syncInventory();
    const auto selectedBomb=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[](const auto& item){return item.containerField==99&&item.itemID==18;});require(selectedBomb!=world.inventoryView.items.end(),"real bomb18 is absent from source inventory");
    world.inventoryView.selected=unsigned(selectedBomb-world.inventoryView.items.begin());world.applyInventoryItem();
    require(vm.staticField(540).integer()==1&&quantity(18)==0&&world.scene.enabledPortals.at(2),"original bomb18 use did not consume item/earn540/open portal2");ready();
    if(world.scene.id=="bm12")walk(portalGoal(2),[&](){return world.scene.id=="bt02";});for(unsigned n=0;n<180&&world.scene.id=="bm12";++n)tick();require(world.scene.id=="bt02","real bomb portal2 did not reach BT02");ready();
    const auto completed=world.saveStateBytes();for(unsigned n=0;n<3;++n)tick();world.restoreStateBytes(completed);require(world.saveStateBytes()==completed,"actual walked bomb passage did not restore exactly");
    savefile::atomicWrite(fs::absolute("build"),"earned-bomb-bt02-walk.eds",completed);
    std::cout<<"Earned bomb WALK COMPLETE: actual Merchant141 ->sourcelever536 ->BT01/source95+541 ->sourceMerchant94+537 ->realCombine180/18 ->BM12 actualitem18/540 ->nativeportal2/BT02; "<<frames<<" frames/"<<distance<<"m incl authored stairs, "<<combat<<" combat frames, HP"<<vm.field(world.game.ash,13).integer()<<", exactsave; no new pose setters\n";

}
