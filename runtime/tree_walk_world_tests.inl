#pragma once
#include "ah06_matriarch_walk_world_tests.inl"
// Continue the same earned AH05 return. All travel, dropped medicine collection,
// inventory use and Tree combat use ControlFrame; no actor poses or quest fields
// are assigned. Local pickup approaches are limited to a clear straight segment.
void continueTreeRouteWalkWorld(World& world) {
    std::cout<<std::unitbuf;auto& vm=world.game.vm;
    auto require=[](bool condition,const std::string& text){if(!condition)throw std::runtime_error("Tree input walk: "+text);};
    auto quantity=[&](int id){int total=0;for(int field:{98,99,100})for(const auto& item:world.game.items(field))if(item.itemID==id)total+=item.quantity;return total;};
    require((world.scene.id=="ah05")&&vm.staticField(604).integer()&&vm.staticField(452).integer()&&quantity(132)==1&&quantity(73)==1&&!vm.staticField(460).integer(),"requires actual Matriarch return/earned132");
    unsigned poweredHits=0;int maximumPoweredHit=0;const auto saw=vm.field(world.game.ash,102).reference();
    require(vm.field(saw,812).integer()==100,"original primary Chainsaw100 is absent");
    const auto isFacing=world.game.package.methodIndex("ed/base/External","actorIsFacingActor","(IIIII)I");const unsigned previousTransitions=world.transitions;unsigned frames=0,combatFrames=0,hits=0,recoveries=0;float distance=0,minRadius=world.actor.clearanceRadius,recoveryDistance=0;
    int previousHealth=vm.field(world.game.ash,13).integer();
    auto tick=[&](Vec2 stick=Vec2{},bool use=false,float dt=.033f,bool attack=false){const std::string area=world.scene.id;const Vec2 old{world.actor.position[0],world.actor.position[2]};
        const bool stopping=!attack&&dt>0&&vm.field(saw,139).integer()!=0;
        ControlFrame input;input.movement=stick;input.down[size_t(ControlAction::Aim)]=attack||stopping;const bool powered=attack&&vm.field(saw,815).integer()>=2;
        input.edges[size_t(powered?ControlAction::PrimaryAttack:ControlAction::SecondaryAttack)]=attack&&dt>0&&world.sourceTick%6==0;
        input.edges[size_t(ControlAction::ChainsawRev)]=powered&&(vm.field(saw,139).integer()==0||vm.field(saw,139).integer()==2);
        input.edges[size_t(ControlAction::ChainsawStop)]=stopping;
        world.playerControls(input);const auto moved=world.step(stick,true,use,dt);require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);
        const auto& body=world.actors->at({0,0});const Vec2 at{body.pose.x,body.pose.z};
        const int health=vm.field(world.game.ash,13).integer();if(health<previousHealth)std::cout<<"Platform route incoming damage "<<previousHealth<<" -> "<<health<<" scene"<<world.scene.id
            <<" tick"<<world.sourceTick<<" at("<<body.pose.x<<','<<body.pose.z<<")\n";
        previousHealth=health;
        require(health>0,"Ash naturally died on the return route");
        require(floorHeight(world.scene.floor,at).has_value()&&clearGround(world.scene.ground,at,world.actor.clearanceRadius),"walking left floor or penetrated enabled source solids");
        require(moved.requested<=std::min(1.f,length(stick))*world.scene.speed*2*frameSeconds(dt)+ContactSkin*4,"requested motion exceeded the input speed bound");
        if(moved.moved>moved.requested+ContactSkin*4){auto combined=world.scene.ground;for(auto obstacle:world.manualActorObstacles()){
                obstacle.circleRadius+=world.scene.radius;combined.push_back(std::move(obstacle));}
            const auto recovered=recoverGround(combined,old,0);const float correction=length(recovered-old);
            // A source-frame spawn can overlap the old point; certify only the
            // existing dynamic separation, including its static-safe segment.
            require(area==world.scene.id&&clearGround(world.scene.ground,old,0)&&!clearGround(combined,old,0)&&clearGround(combined,at,0)&&
                length(moveGround(world.scene.ground,old,recovered-old,0)-recovered)<ContactSkin*4&&moved.moved<=moved.requested+correction+ContactSkin*4,
                "extra movement was not bounded static-safe dynamic recovery");++recoveries;recoveryDistance+=correction;
            if(recoveries==1)std::cout<<"Verified dynamic recovery: scene"<<area<<" tick"<<world.sourceTick<<" correction"<<correction<<" requested"<<moved.requested<<" moved"<<moved.moved<<'\n';}
        if(world.scene.id==area)distance+=length(at-old);
        minRadius=std::min(minRadius,world.actor.clearanceRadius);frames+=dt>0;};
    auto ready=[&](){for(unsigned n=0;n<480;++n){if(world.game.mode!=0)world.closeInventory();world.playerControls({});const auto& ash=world.actors->at({0,0});
            if(!(ash.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&
               !vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&ash.actions.possible(116))return;tick();}
        throw std::runtime_error("Tree input walk: source input did not become ready");};
    auto useOwned=[&](int id){ready();world.openInventory();world.syncInventory();const auto entry=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),
            [&](const auto& item){return item.containerField==99&&item.itemID==id;});require(entry!=world.inventoryView.items.end(),"requested item is not actually owned");
        world.inventoryView.selected=unsigned(entry-world.inventoryView.items.begin());world.applyInventoryItem();require(!world.scriptFault,"original item use failed");};
    auto refuel=[&](int id){if(vm.field(saw,815).integer()>=vm.field(saw,817).integer()||!quantity(id))return;const int count=quantity(id),before=vm.field(saw,815).integer();
        useOwned(id);require(quantity(id)==count-1&&vm.field(saw,815).integer()>before,"owned fuel was not consumed by original inventory use");
        std::cout<<"Original fuel item"<<id<<" "<<before<<" -> "<<vm.field(saw,815).integer()<<'\n';ready();};
    auto toward=[&](Vec2 target,float magnitude){const auto& camera=world.scene.cameras.at(world.actor.camera);const auto& latch=world.actor.movement;
        const Vec2 right=latch.held?latch.right:camera.right,forward=latch.held?latch.forward:camera.forward,delta=target-Vec2{world.actor.position[0],world.actor.position[2]};
        const float determinant=right.x*forward.z-right.z*forward.x;require(std::abs(determinant)>.001f,"camera basis is singular");Vec2 stick{(delta.x*forward.z-delta.z*forward.x)/determinant,(right.x*delta.z-right.z*delta.x)/determinant};
        const float size=length(stick);return size>.000001f?stick*(magnitude/size):Vec2{};};
    auto nav=[&](unsigned index){uint32_t count,offset;std::memcpy(&count,world.scene.nativeBytes.data()+48,4);std::memcpy(&offset,world.scene.nativeBytes.data()+52,4);
        require(index<count,"source nav point missing");GroundPoint p;std::memcpy(&p,world.scene.nativeBytes.data()+offset+index*44+8,12);return Vec2{p.x,p.z};};
    unsigned medicines=0;
    auto healNearby=[&](){if((world.scene.id!="ah50"&&world.scene.id!="al01"&&world.scene.id!="al02")||(world.scene.id=="ah50"&&medicines>0)||vm.field(world.game.ash,13).integer()>65)return false;const Vec2 at{world.actor.position[0],world.actor.position[2]};
        for(const auto&[key,b]:world.actors->bodies())if(key.type!=0&&b.nativeSlot>=0&&(b.flags&1)&&!(b.flags&8)&&vm.field(b.script,13).integer()>0&&length(at-Vec2{b.pose.x,b.pose.z})<4.f)return false;
        std::optional<Vec2> object;float best=world.scene.id=="ah50"?1.3f:3.f;for(const auto&[index,o]:world.nativeProperties.objects)if((o.flags&1)&&o.setup[0]==46){const float d=length(at-Vec2{o.position.x,o.position.z});if(d<best){best=d;object=Vec2{o.position.x,o.position.z};}}
        if(!object)return false;const int before=quantity(46);ready();
        for(unsigned n=0;n<80;++n){const Vec2 here{world.actor.position[0],world.actor.position[2]},away=here-*object;const float d=length(away);if(d<.55f)break;const Vec2 goal=*object+away*(.35f/d);if(!floorHeight(world.scene.floor,goal)||!clearGround(world.scene.ground,goal,0)||length(moveGround(world.scene.ground,here,goal-here,0)-goal)>ContactSkin*4)return false;const float delta=length(goal-here);tick(toward(goal,std::min(1.f,delta/(world.scene.speed*2*.033f))));}
        tick(toward(*object,1),false,0);tick({},true,0);for(unsigned n=0;n<180&&quantity(46)<=before;++n)tick();require(quantity(46)>before,"originalUse didnotcollect actual46 drop");
        while(quantity(46)>0&&vm.field(world.game.ash,13).integer()<=65){if(!world.inventoryView.open)world.openInventory();world.syncInventory();const auto item=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[](const auto&e){return e.containerField==99&&e.itemID==46;});require(item!=world.inventoryView.items.end(),"genuine46 absentfromUI");const int count=quantity(46),hp=vm.field(world.game.ash,13).integer();world.inventoryView.selected=unsigned(item-world.inventoryView.items.begin());world.applyInventoryItem();require(quantity(46)==count-1&&vm.field(world.game.ash,13).integer()>hp,"actual46 use didnotconsume/heal");++medicines;previousHealth=vm.field(world.game.ash,13).integer();std::cout<<"TREE original dropped46 pickup/UI heal "<<hp<<"->"<<previousHealth<<" scene"<<world.scene.id<<" tick"<<world.sourceTick<<std::endl;}
        if(world.inventoryView.open)world.closeInventory();return true;
    };
    auto walk=[&](Vec2 target,const char* destination=""){tick({},false,0);const std::string source=world.scene.id;unsigned stalled=0;float previous=std::numeric_limits<float>::infinity();
        for(unsigned n=0;n<2200;++n){if(world.scene.id!=source){require(world.scene.id==destination,"unexpected portal destination "+source+"->"+world.scene.id+"; expected "+destination);return;}
            if(world.game.mode!=0){ready();continue;}const Vec2 point{world.actor.position[0],world.actor.position[2]};const float remaining=length(target-point);if(remaining<.04f){tick({},false,0);return;}
            const edactors::Body* enemy=nullptr;float nearest=3.1f;const auto& ash=world.actors->at({0,0});
            for(const auto& [key,body]:world.actors->bodies())if(key.type!=0&&body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&vm.field(body.script,13).integer()>0){
                const float d=length(point-Vec2{body.pose.x,body.pose.z});if(d<std::min(nearest,2.4f+float(body.radius)*.01f)&&
                    world.nativeScene().queries.lineOfSight({ash.pose.x,ash.pose.y,ash.pose.z},{body.pose.x,body.localY,body.pose.z},4,world.sourceQueryCamera(),world.nativeScene().enabledVolumes,world.visibilityReady)){enemy=&body;nearest=d;}}
            if(enemy){const auto key=enemy->key;const int health=vm.field(enemy->script,13).integer();const auto I=[](int v){return jpc::Value::integer(v);};
                if(!vm.invoke(isFacing,0,{I(0),I(0),I(key.type),I(key.index),I(30)}).integer())tick(toward({enemy->pose.x,enemy->pose.z},1),false,0,true);
                tick({},false,.033f,true);
                require(++combatFrames<3600,"actual nearby encounters exceeded bounded input budget");const auto current=world.actors->bodies().find(key);
                if(current!=world.actors->bodies().end()){const int change=health-vm.field(current->second.script,13).integer();if(change>0){++hits;std::cout<<"TREE actualhit "<<key.type<<'/'<<key.index<<' '<<health<<"->"<<vm.field(current->second.script,13).integer()<<" tick"<<world.sourceTick<<'\n';
                    if(vm.field(saw,139).integer()!=0&&change>=20){++poweredHits;maximumPoweredHit=std::max(maximumPoweredHit,change);}}}stalled=0;previous=std::numeric_limits<float>::infinity();continue;}
            if(healNearby()){stalled=0;previous=std::numeric_limits<float>::infinity();continue;}
            stalled=remaining>=previous-.00001f?stalled+1:0;
            require(stalled<120,"route stalled in "+source+" at "+std::to_string(point.x)+","+std::to_string(point.z)+" toward "+std::to_string(target.x)+","+std::to_string(target.z));
            previous=remaining;tick(toward(target,std::min(1.0f,remaining/(world.scene.speed*2*.033f))));}
        throw std::runtime_error("Tree input walk: leg exceeded bounded input budget");};

    auto boundary=[&](const char* label){ready();std::cout<<"Tree walk boundary "<<label<<" scene"<<world.scene.id<<" tick"<<world.sourceTick<<" HP"<<vm.field(world.game.ash,13).integer()<<" fuel"<<vm.field(saw,815).integer()<<'\n';};
    ready();
    std::cout<<"ORIGINAL fuel854(34)="<<vm.invoke(854,0,{jpc::Value::integer(34)}).integer()<<" owned34="<<quantity(34)<<" cap"<<vm.field(saw,817).integer()<<'\n';
    refuel(34);boundary("ah05-refuel");
    for(unsigned i:{11,10,9,8,22,0})walk(nav(i));walk({-12.25f,-4.68f},"ah02");require(world.scene.id=="ah02","AH05 portal2 failed");boundary("ah02-return");
    for(unsigned i:{18,20,21,22,23,24,25,26,28,29,11,98,88,36,37,38,92})walk(nav(i));walk({19.1f,-17.8f},"ah50");require(world.scene.id=="ah50","AH02 portal4 failed");boundary("ah50-return");
    for(unsigned i:{33,31,3,2,24})walk(nav(i));walk({20.2f,5.6f},"ak09");require(world.scene.id=="ak09","AH50 portal5 failed");boundary("ak09-return");
    for(unsigned i:{50,5,7,12})walk(nav(i));
    {const int before=quantity(34);walk({1.5959855f,16.1752945f});ready();tick({},true,0);for(unsigned n=0;n<180&&quantity(34)==before;++n)tick();require(quantity(34)==before+1,"actualAK09 authored fuel34 didnotpickup");ready();refuel(34);boundary("ak09-authored-fuel34");}
    walk({-.115413666f,22.5304737f},"al01");require(world.scene.id=="al01","AK09 portal11 failed");boundary("al01-tree-route");
    for(unsigned i:{0,2,1,3})walk(nav(i));walk({9.93685226f,40.4f});walk({7.93685226f,42.f});walk({-2.60694575f,42.f});
    {Vec2 center{};const auto p=world.scene.nativePortals.at(7);for(auto q:p.points)center=center+q;center=center*(1.f/p.points.size());walk(center,"al02");}require(world.scene.id=="al02","AL01 portal7 failed");boundary("al02-tree-route");
    if(!world.scene.scripting.inside(95,{world.actor.position[0],world.actors->at({0,0}).localY,world.actor.position[2]})){for(unsigned i:{0,1,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25})walk(nav(i));walk({-2.132115f,90.2f});}ready();
    const auto marker=world.scene.scripting.markers.at(12);tick(toward({marker.x,marker.z},1),false,0);
    require(world.scene.scripting.inside(95,{world.actor.position[0],world.actors->at({0,0}).localY,world.actor.position[2]})&&world.scene.scripting.facing(12,60,{world.actor.position[0],world.actor.position[2]},world.actor.yaw),"actualwalk missed132 source95/facing12");
    boundary("al02-before132");useOwned(132);require(vm.staticField(460).integer()&&quantity(132)==0&&world.pendingSceneSwitch,"source132 callback failed");
    std::cout<<"SOURCE UI132 queued scene"<<world.scene.id<<" mode"<<world.game.mode<<" applied"<<vm.staticField(844).integer()<<" camera"<<world.actor.camera<<'\n';
    // Match the real frontend: ordinary inventory frame applies mode0 on the
    // source scene before the deferred end-of-loop sceneSwitch is consumed.
    tick();require(world.scene.id=="al02"&&world.pendingSceneSwitch&&vm.staticField(844).integer()==0,"source inventory close didnotapply before queuedswitch");
    std::cout<<"SOURCE UI132 ordinaryframe applied"<<vm.staticField(844).integer()<<" camera"<<world.actor.camera<<'\n';
    const auto request=*world.pendingSceneSwitch;require(request.destinationId==0x61740200u&&request.camera==0&&request.marker==0&&request.rotationXYZ==std::array<float,3>{{0,0,0}},"sourceAT02 tuple differs");world.pendingSceneSwitch.reset();require(world.switchScene(request,nullptr)&&world.scene.id=="at02","actual sourceAT02 switch failed");boundary("at02-earned-herbicide");
    require(world.transitions==previousTransitions+6&&quantity(73)==1,"earned tree branch history differs");require(poweredHits>0&&maximumPoweredHit>=20,"owned powered primary did not produce a source20+ contact");
    std::cout<<"TREE WALK earned132/460 actualAT02 PASS "<<frames<<"positiveframes "<<distance<<"m "<<combatFrames<<"combatframes "<<hits<<"hits "<<medicines<<" real46medicines HP"<<vm.field(world.game.ash,13).integer()<<'\n';
}

void continueTreeBossWalkWorld(World& w) {
    auto require=[](bool b,const std::string& text){if(!b)throw std::runtime_error("Actual Tree walk: "+text);};auto& vm=w.game.vm;
    auto quantity=[&](int id){int count=0;for(int field:{98,99,100})for(const auto& item:w.game.items(field))if(item.itemID==id)count+=item.quantity;return count;};
    auto neutral=[&](){if(w.game.mode!=0)w.closeInventory();w.playerControls({});w.step({},false,false,.033f);require(!w.scriptFault,"source frame: "+w.game.diagnostic);require(vm.field(w.game.ash,13).integer()>0,"natural Ash death");};
    require(w.scene.id=="at02"&&!vm.staticField(605).integer(),"requires original unsolved AT02 arrival");
    for(unsigned n=0;n<180&&(!w.actors->bodies().count({5,0})||!w.actors->bodies().count({34,0})||!w.sourceScreen.fadeInDone());++n)neutral();
    require(w.actors->bodies().count({5,0})&&w.actors->bodies().count({34,0})&&w.sourceScreen.fadeInDone(),"source Tree/Arm birth/fade missing");
    useSourceHealingItem(w);
    const edactors::Key tree{5,0};const auto npc=w.actors->at(tree).script;require(vm.field(npc,13).integer()==100,"Tree HP source default differs");
    const auto weapon=vm.field(w.game.ash,103).reference();require(vm.field(weapon,812).integer()==10,"genuine owned Axe10 absent");
    const auto facing=w.game.package.methodIndex("ed/base/External","actorIsFacingActor","(IIIII)I");auto I=[](int x){return jpc::Value::integer(x);};
    auto toward=[&](Vec2 target,float magnitude){const auto&c=w.scene.cameras.at(w.actor.camera);const auto&l=w.actor.movement;const Vec2 right=l.held?l.right:c.right,forward=l.held?l.forward:c.forward,d=target-Vec2{w.actor.position[0],w.actor.position[2]};const float det=right.x*forward.z-right.z*forward.x;require(std::abs(det)>.001f,"singular camera");const Vec2 stick{(d.x*forward.z-d.z*forward.x)/det,(right.x*d.z-right.z*d.x)/det};return length(stick)>.00001f?stick*(magnitude/length(stick)):Vec2{};};
    unsigned steps=0,hits=0;int previous=100,hero=vm.field(w.game.ash,13).integer();
    auto tick=[&](Vec2 stick,bool attack,float dt){ControlFrame input;input.movement=stick;input.down[size_t(ControlAction::Aim)]=true;input.edges[size_t(ControlAction::SecondaryAttack)]=attack&&dt>0&&w.sourceTick%6==0;w.playerControls(input);const auto moved=w.step(stick,true,false,dt);require(!w.scriptFault,w.game.diagnostic);const Vec2 position{w.actor.position[0],w.actor.position[2]};require(floorHeight(w.scene.floor,position).has_value()&&clearGround(w.scene.ground,position,w.actor.clearanceRadius),"Tree combat left floor or penetrated enabled static geometry");require(moved.requested<=length(stick)*w.scene.speed*2*frameSeconds(dt)+ContactSkin*4,"input speed bound");const int hp=vm.field(npc,13).integer(),h=vm.field(w.game.ash,13).integer();if(h<hero)std::cout<<"TREE incoming "<<hero<<"->"<<h<<" tick"<<w.sourceTick<<" state"<<w.actors->at(tree).actions.layers[0].state<<std::endl;hero=h;if(hp<previous){require(hp==std::max(0,previous-7),"fixed7 source damage differs");++hits;std::cout<<"TREE Axe "<<previous<<"->"<<hp<<" tick"<<w.sourceTick<<" Hero"<<hero<<std::endl;}previous=hp;steps+=dt>0;require(hero>0,"natural Tree combat death");};
    for(unsigned n=0;n<2400&&!vm.staticField(605).integer();++n){if(w.game.mode!=0)w.closeInventory();if(previous<=0){tick({},false,.033f);continue;}const auto& b=w.actors->at(tree);const Vec2 at{w.actor.position[0],w.actor.position[2]},target{b.pose.x,b.pose.z};const float d=length(target-at);if(d>3.4f){const auto goal=target+(at-target)*(3.35f/d);tick(toward(goal,std::min(1.f,length(goal-at)/(w.scene.speed*2*.033f))),false,.033f);}else{if(!vm.invoke(facing,0,{I(0),I(0),I(5),I(0),I(30)}).integer())tick(toward(target,1),false,0);tick({},true,.033f);}}
    require(vm.staticField(605).integer()&&previous==0&&hits==15,"source fixed7/15hits/death605 missing");
    for(unsigned n=0;n<240&&(!vm.staticField(424).integer()||!w.pendingSceneSwitch||vm.staticField(844).integer()!=0);++n)neutral();
    require(vm.staticField(424).integer()&&quantity(74)==1&&w.pendingSceneSwitch&&edpackage::sceneName(w.pendingSceneSwitch->destinationId)=="at01"&&vm.staticField(844).integer()==0,"source page74/424/AT01 queue missing");
    const auto request=*w.pendingSceneSwitch;w.pendingSceneSwitch.reset();require(w.switchScene(request,nullptr)&&w.scene.id=="at01","original queued AT01 exit failed");
    for(unsigned n=0;n<180&&(!w.sourceScreen.fadeInDone()||w.game.mode!=0||vm.staticField(844).integer()!=0);++n)neutral();neutral();
    require(w.sourceScreen.fadeInDone()&&w.lastOverlay.blackAlpha==0,"authored AT01 return fade incomplete");require(quantity(55)==1&&vm.staticField(461).integer(),"original AT01 activation did not award key55/461");
    const auto before=w.saveStateBytes();for(unsigned n=0;n<8;++n)neutral();const auto future=w.saveStateBytes();w.restoreStateBytes(before,nullptr);require(w.saveStateBytes()==before,"direct exact restore differs");for(unsigned n=0;n<8;++n)neutral();require(w.saveStateBytes()==future,"eight-frame direct future differs");w.restoreStateBytes(before,nullptr);auto cold=stageSavedWorld(w,before);require(cold->saveStateBytes()==before&&w.saveStateBytes()==before,"cold exact restore differs");for(unsigned n=0;n<8;++n){if(cold->game.mode!=0)cold->closeInventory();cold->playerControls({});cold->step({},false,false,.033f);require(!cold->scriptFault,"cold future source fault");}require(cold->saveStateBytes()==future,"eight-frame cold future differs");
    std::cout<<"TREE COMPLETE actualinput15Axe/fixed7/death605/page74/424/queuedAT01/visible/direct+cold+8future PASS "<<steps<<" combatframes HeroHP"<<vm.field(w.game.ash,13).integer()<<std::endl;
}
void continueTreeWalkWorld(World& world) { continueTreeRouteWalkWorld(world);continueTreeBossWalkWorld(world); }
void testTreeWalkWorld(const fs::path& root) {
    testEpisode(root,false,640,480,{},[](World& world){continueAH02WalkWorld(world);continueAH02HandleWalkWorld(world);continueAH05WalkWorld(world);continueCrowbarWalkWorld(world);continueAH05PlatformWalkWorld(world);continueAH10KeydoorWalkWorld(world);continueAH10PumpWalkWorld(world);continueAH12AmuletWalkWorld(world);continueAH12ReturnWalkWorld(world);continueAH06ArrivalWalkWorld(world);continueAH06MatriarchWalkWorld(world);continueTreeWalkWorld(world);});
}
