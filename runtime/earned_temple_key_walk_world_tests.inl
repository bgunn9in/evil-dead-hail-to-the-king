#pragma once
#include "earned_bomb_walk_world_tests.inl"
// Genuine input-only continuation; earlier BM01 approach fixtures remain historical.
void continueEarnedTempleKeyWalkWorld(World& world) {
    auto& vm=world.game.vm;std::cout<<std::unitbuf;
    auto require=[](bool ok,const std::string& text){if(!ok)throw std::runtime_error("Earned temple walk: "+text);};
    auto quantity=[&](int id){int n=0;for(int field:{98,99,100})for(const auto& item:world.game.items(field))if(item.itemID==id)n+=item.quantity;return n;};
    require(world.scene.id=="bt02"&&vm.staticField(540).integer()&&!vm.staticField(543).integer()&&!vm.staticField(544).integer()&&!vm.staticField(545).integer()&&!vm.staticField(547).integer(),"missing genuine walked bomb540 checkpoint");
    // Player-facing route checks use native P1 class4/flag2; validate only that same
    // source-eligible static geometry, preserving raw enable/volume tables.
    auto playerClear=[&](Vec2 point){std::vector<GroundPolygon> active;for(const auto& wall:world.scene.ground) {
        if(wall.boundaryOnly||wall.sourceIndex>=world.scene.queries.volumes.size()||world.scene.queries.volumes[wall.sourceIndex].geometry.kind!=2||
           (world.scene.queries.volumes[wall.sourceIndex].flags&2))active.push_back(wall);}
        return clearGround(active,point,world.actor.clearanceRadius);};
    const unsigned initialTransitions=world.transitions;
    unsigned frames=0,combat=0;float distance=0;int priorHP=vm.field(world.game.ash,13).integer();
    auto tick=[&](Vec2 stick=Vec2{},bool aim=false,bool use=false,float dt=.033f,bool run=true,bool fire=true){const Vec2 old{world.actor.position[0],world.actor.position[2]};const auto source=world.scene.id;
        ControlFrame input;input.movement=stick;input.down[size_t(ControlAction::Aim)]=aim;input.edges[size_t(ControlAction::SecondaryAttack)]=aim&&fire&&dt>0&&world.sourceTick%6==0;
        world.playerControls(input);const auto moved=world.step(stick,run,use,dt);require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);
        const int health=vm.field(world.game.ash,13).integer();if(health<priorHP)std::cout<<"Temple actual incoming damage "<<priorHP<<"->"<<health<<" at"<<world.actor.position[0]<<','<<world.actor.position[2]<<" tick"<<world.sourceTick<<'\n';priorHP=health;require(health>0,"Ash naturally died");if(world.pendingSceneSwitch){const auto request=*world.pendingSceneSwitch;world.pendingSceneSwitch.reset();require(!edpackage::reload(request.sourceSceneId,request.destinationId),"unexpected package request in temple");require(world.switchScene(request,nullptr),"actual source queued temple switch failed");}
        require(world.scene.id=="bt02"||world.scene.id=="bt04"||world.scene.id=="bt10"||world.scene.id=="bt08"||world.scene.id=="bt06"||world.scene.id=="bt11","route entered unexpected scene");
        const Vec2 at{world.actor.position[0],world.actor.position[2]};require(floorHeight(world.scene.floor,at).has_value()&&playerClear(at),"input left source floor or entered an active solid");
        require(moved.moved<=moved.requested+ContactSkin*4,"input exceeded requested displacement");if(dt>0){++frames;distance+=source==world.scene.id?length(at-old):moved.moved;}};
    auto ready=[&](){for(unsigned n=0;n<480;++n){if(world.game.mode!=0)world.closeInventory();world.playerControls({});const auto& ash=world.actors->at({0,0});
        if(!(ash.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&!vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&ash.actions.possible(116))return;tick();}
        throw std::runtime_error("Earned temple walk: source action did not become ready");};
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
            if(volume.contains({p.x,0,p.z})&&floorHeight(world.scene.floor,p)&&playerClear(p)&&length(p-center)<best){best=length(p-center);chosen=p;}}
        require(std::isfinite(best),"source interaction has no floor goal");return chosen;};
    auto walk=[&](Vec2 goal,const std::function<bool()>& done=std::function<bool()>{}){tick({},false,false,0);float previous=std::numeric_limits<float>::infinity();unsigned stalled=0;
        for(unsigned n=0;n<2500;++n){if(done&&done())return;if(world.game.mode!=0){ready();continue;}const auto& ash=world.actors->at({0,0});
            const Vec2 at{ash.pose.x,ash.pose.z};const float remaining=length(goal-at);if(remaining<.15f){tick({},false,false,0);return;}
            const edactors::Body* enemy=nullptr;float nearest=3.1f;
            for(const auto& [key,body]:world.actors->bodies())if(key.type==23&&body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&vm.field(body.script,13).integer()>0){
                const float d=length(Vec2{body.pose.x,body.pose.z}-at),reach=std::min(3.1f,2.4f+float(body.radius)*.01f);
                if(d<nearest&&d<reach&&world.scene.queries.lineOfSight({ash.pose.x,ash.pose.y,ash.pose.z},{body.pose.x,body.localY,body.pose.z},4,world.sourceQueryCamera(),world.scene.enabledVolumes,world.visibilityReady)){nearest=d;enemy=&body;}}
            if(enemy){tick(toward({enemy->pose.x,enemy->pose.z},1),true,false,0,false);tick({},true,false,.033f,false);require(++combat<4000,"source nearby combat budget exceeded");stalled=0;previous=std::numeric_limits<float>::infinity();continue;}
            stalled=remaining>=previous-.00001f?stalled+1:0;
            if(stalled>=120){
                throw std::runtime_error("Earned temple walk: stalled in "+world.scene.id+" at "+std::to_string(at.x)+","+std::to_string(at.z)+" toward "+std::to_string(goal.x)+","+std::to_string(goal.z));}previous=remaining;
            tick(toward(goal,std::min(1.f,remaining/(world.scene.speed*2*.033f))));}
        throw std::runtime_error("Earned temple walk: bounded route budget exhausted");};
    auto face=[&](unsigned marker,int angle){ready();const auto p=world.scene.scripting.markers.at(marker);tick({},false,false,0,false);tick(toward({p.x,p.z},1),false,false,0,false);
        require(world.scene.scripting.facing(marker,angle,{world.actor.position[0],world.actor.position[2]},world.actor.yaw),"real facing input missed original marker");};
    auto portalGoal=[&](unsigned index){const auto& points=world.scene.nativePortals.at(index).points;Vec2 p{};for(auto v:points)p=p+v;return p*(1.f/float(points.size()));};


    ready();std::cout<<"TEMPLE WALK genuine BT02 start "<<world.actor.position[0]<<','<<world.actor.position[2]<<" HP"<<vm.field(world.game.ash,13).integer()<<'\n';
    for(unsigned i:{1,0,3,4,5,6,7,8,9,10,11,12,14,33})walk(nav(i));walk(sample(41),[&](){return world.scene.scripting.inside(41,{world.actor.position[0],world.actors->at({0,0}).localY,world.actor.position[2]});});face(24,65);tick({},false,true,0,false);ready();
    if(world.scene.id=="bt02")walk(portalGoal(5),[&](){return world.scene.id=="bt04";});for(unsigned n=0;n<180&&world.scene.id=="bt02";++n)tick();require(world.scene.id=="bt04","original unlocked portal5 did not enter BT04");
    std::cout<<"TEMPLE WALK actual BT04 entry "<<world.actor.position[0]<<','<<world.actor.position[2]<<" HP"<<vm.field(world.game.ash,13).integer()<<'\n';
    for(const auto& [index,obj]:world.nativeProperties.objects)if(obj.flags&1)std::cout<<"BT04 object"<<obj.setup[0]<<" flags"<<obj.flags<<" at"<<obj.position.x<<','<<obj.position.z<<'\n';
    for(unsigned i:{0,2,3})walk(nav(i));walk(sample(1));for(unsigned n=0;n<360&&!vm.staticField(554).integer();++n)tick();require(vm.staticField(554).integer(),"actual BT04 intro1 did not earn554");ready();
    const auto first=world.saveStateBytes();world.restoreStateBytes(first);require(world.saveStateBytes()==first,"actual temple intro did not restore exactly");
    std::cout<<"TEMPLE WALK actual BT04 intro554 "<<world.actor.position[0]<<','<<world.actor.position[2]<<" HP"<<vm.field(world.game.ash,13).integer()<<" frames"<<frames<<" displacement"<<distance<<'\n';
    for(const auto& [key,body]:world.actors->bodies())if(key.type==23)std::cout<<"BT04 NPC23:"<<key.index<<" slot"<<body.nativeSlot<<" flags"<<body.flags<<" R"<<body.radius<<" HP"<<vm.field(body.script,13).integer()<<" xyz"<<body.pose.x<<','<<body.pose.z<<'\n';
    unsigned hits=0;
    for(unsigned n=0;n<3500&&!quantity(144);++n){if(world.game.mode!=0)ready();
        const auto loot=std::find_if(world.nativeProperties.objects.begin(),world.nativeProperties.objects.end(),[](const auto& entry){return(entry.second.flags&1)&&entry.second.setup[0]==144;});
        if(loot!=world.nativeProperties.objects.end()){
            const auto target=loot->second.position;Vec2 goal{target.x-.8f,target.z};if(!playerClear(goal)||!floorHeight(world.scene.floor,goal))goal={target.x+.8f,target.z};
            walk(goal);ready();tick(toward({target.x,target.z},1),false,false,0,false,false);tick({},false,true,0,false,false);tick();continue;
        }
        const edactors::Body* enemy=nullptr;float nearest=std::numeric_limits<float>::infinity();const Vec2 at{world.actor.position[0],world.actor.position[2]};
        for(const auto& [key,body]:world.actors->bodies())if(key.type==23&&body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&vm.field(body.script,13).integer()>0){
            const float d=length(Vec2{body.pose.x,body.pose.z}-at);if(d<nearest){nearest=d;enemy=&body;}}
        if(!enemy){tick();continue;}const auto key=enemy->key;const auto target=Vec2{enemy->pose.x,enemy->pose.z};const int hp=vm.field(enemy->script,13).integer();
        const float reach=std::min(3.1f,2.4f+float(enemy->radius)*.01f),spacing=reach-.06f;
        if(n%100==0)std::cout<<"Temple real combat frame"<<n<<" enemyHP"<<hp<<" AshHP"<<priorHP<<" distance"<<nearest<<" reach"<<reach<<" sourceRadius"<<enemy->radius<<" at"<<at.x<<','<<at.z<<'\n';
        if(nearest>reach)tick(toward(target,1),true,false,.033f,false,false);
        else if(nearest<spacing-.1f){const Vec2 away=at+(at-target)*(1.f/std::max(.001f,nearest));tick(toward(away,1),true,false,.033f,false,false);}
        else {tick(toward(target,1),true,false,0,false,false);tick({},true,false,.033f,false,true);}
        const auto current=world.actors->bodies().find(key);if(current!=world.actors->bodies().end()&&vm.field(current->second.script,13).integer()<hp)++hits;
    }
    require(quantity(144)==1&&vm.staticField(552).integer(),"actual BT04 free GuildMember did not yield key144/552");ready();
    const auto keyState=world.saveStateBytes();world.restoreStateBytes(keyState);require(world.saveStateBytes()==keyState,"genuine first temple key144 failed exactsave");
    std::cout<<"TEMPLE WALK firstKEY PASS BT02/bomb540 ->actualdoor5/BT04 ->intro554 ->free GuildMember23/realAxe17 ->key144/552; frames"<<frames<<" displacement"<<distance<<" observedHits"<<hits<<" HP"<<priorHP<<" exactsave/no new poses\n";


    auto approach=[&](unsigned volume,unsigned marker){walk(sample(volume),[&](){const auto& b=world.actors->at({0,0});return world.scene.scripting.inside(volume,{b.pose.x,b.localY,b.pose.z});});face(marker,65);};
    auto use=[&](unsigned volume,unsigned marker){approach(volume,marker);tick({},false,true,0,false);ready();};
    auto passage=[&](const char* source,unsigned index,const char* destination){if(world.scene.id==destination)return;require(world.scene.id==source,"unexpected route scene before passage");
        walk(portalGoal(index),[&](){return world.scene.id==destination;});for(unsigned n=0;n<180&&world.scene.id==source;++n)tick();require(world.scene.id==destination,"native door did not transition");std::cout<<"TEMPLE WALK passage "<<source<<"->"<<destination<<" HP"<<priorHP<<'\n';};
    auto item=[&](int id,unsigned volume,unsigned marker,int flag){approach(volume,marker);world.openInventory();world.syncInventory();
        const auto selected=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[&](const auto& i){return i.containerField==99&&i.itemID==id;});
        require(selected!=world.inventoryView.items.end(),"genuine earned item missing from actual UI");world.inventoryView.selected=unsigned(selected-world.inventoryView.items.begin());world.applyInventoryItem();
        require(!world.scriptFault&&vm.staticField(flag).integer()&&quantity(id)==0,"original item use did not report/consume");ready();std::cout<<"TEMPLE WALK item"<<id<<" flag"<<flag<<" HP"<<priorHP<<'\n';};
    auto checkpoint=[&](const char* name){const auto bytes=world.saveStateBytes();world.restoreStateBytes(bytes);require(world.saveStateBytes()==bytes,"full World save did not restore exactly");(void)name;};
    for(unsigned i:{4,3,2,0})walk(nav(i));use(16,3);passage("bt04",1,"bt02");
    for(unsigned i:{14,12,11,10,9,8,7,6,5,4,3,0,2,15,16,17,18,19,20,21,22,23,24,25,26,27})walk(nav(i));
    item(144,44,27,543);checkpoint("earned-key144-applied543.eds");use(44,27);passage("bt02",8,"bt10");
    require(world.transitions==initialTransitions+3,"temple key path did not use exactly three native scene transitions");checkpoint("actual-bt10-arrival");
    std::cout<<"Earned temple key WALK: genuineBT02Bomb->BT04key144->originaldoor1/BT02->actualUI144/543->originalportal8/BT10; frames"<<frames<<" displacement"<<distance<<" HP"<<priorHP<<" exactsave/no new poses\n";
}

void testEarnedTempleKeyWalkWorld(const fs::path& levelA,const std::function<void(World&)>& continuation={}) {
    testEarnedBombWalkWorld(levelA,[&](World& world){continueEarnedTempleKeyWalkWorld(world);if(continuation)continuation(world);});
}
