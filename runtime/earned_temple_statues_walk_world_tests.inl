#pragma once
#include "earned_temple_key_walk_world_tests.inl"
#include "source_inventory_ui.inl"
// Input-only chapter after the published earned BT10 boundary; no pose or state seeds.
// Genuine input-only continuation; earlier BM01 approach fixtures remain historical.
void continueEarnedTempleStatue145WalkWorld(World& world) {
    auto& vm=world.game.vm;std::cout<<std::unitbuf;
    auto require=[](bool ok,const std::string& text){if(!ok)throw std::runtime_error("Earned temple walk: "+text);};
    auto quantity=[&](int id){int n=0;for(int field:{98,99,100})for(const auto& item:world.game.items(field))if(item.itemID==id)n+=item.quantity;return n;};
    require(world.scene.id=="bt10"&&vm.staticField(543).integer()&&!quantity(144)&&!vm.staticField(545).integer()&&!quantity(145),"missing actual walked BT10/key543 checkpoint");
    // Player-facing route checks use native P1 class4/flag2; validate only that same
    // source-eligible static geometry, preserving raw enable/volume tables.
    auto playerClear=[&](Vec2 point){std::vector<GroundPolygon> active;for(const auto& wall:world.scene.ground) {
        if(wall.boundaryOnly||wall.sourceIndex>=world.scene.queries.volumes.size()||world.scene.queries.volumes[wall.sourceIndex].geometry.kind!=2||
           (world.scene.queries.volumes[wall.sourceIndex].flags&2))active.push_back(wall);}
        return clearGround(active,point,world.actor.clearanceRadius);};
    const unsigned initialTransitions=world.transitions;
    unsigned frames=0,combat=0;float distance=0;int priorHP=vm.field(world.game.ash,13).integer();
    auto tick=[&](Vec2 stick=Vec2{},bool aim=false,bool use=false,float dt=.033f,bool run=true,bool fire=true){const Vec2 old{world.actor.position[0],world.actor.position[2]};const auto source=world.scene.id;
        if(dt>0&&vm.field(world.game.ash,13).integer()<=65)useSourceMushrooms(world);
        ControlFrame input;input.movement=stick;input.down[size_t(ControlAction::Aim)]=aim;const auto saw=vm.field(world.game.ash,102).reference();const bool powered=saw&&vm.field(saw,812).integer()==101&&vm.field(saw,815).integer()>0;input.edges[size_t(powered?ControlAction::PrimaryAttack:ControlAction::SecondaryAttack)]=aim&&fire&&dt>0&&world.sourceTick%6==0;if(aim&&fire&&powered&&!vm.field(saw,157).integer())input.edges[size_t(ControlAction::ChainsawRev)]=true;
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



    ready();std::cout<<"NEXT TEMPLE actual BT10 HP"<<priorHP<<" at"<<world.actor.position[0]<<','<<world.actor.position[2]<<'\n';
    for(int field:{98,99,100})for(const auto& i:world.game.items(field))std::cout<<"OWNED "<<field<<" id"<<i.itemID<<" qty"<<i.quantity<<'\n';
    walk(sample(1));for(unsigned n=0;n<360&&!vm.staticField(558).integer();++n)tick();require(vm.staticField(558).integer(),"original BT10 intro558 did not finish");ready();
    std::cout<<"NEXT TEMPLE intro558 HP"<<priorHP<<'\n';
    unsigned hits=0;
    for(unsigned n=0;n<3500&&!quantity(145);++n){if(world.game.mode!=0)ready();
        const auto loot=std::find_if(world.nativeProperties.objects.begin(),world.nativeProperties.objects.end(),[](const auto& entry){return(entry.second.flags&1)&&entry.second.setup[0]==145;});
        if(loot!=world.nativeProperties.objects.end()&&vm.staticField(685).integer()){
            const auto target=loot->second.position;Vec2 goal{target.x-.8f,target.z};if(!playerClear(goal)||!floorHeight(world.scene.floor,goal))goal={target.x+.8f,target.z};
            walk(goal);ready();tick(toward({target.x,target.z},1),false,false,0,false,false);tick({},false,true,0,false,false);tick();continue;
        }
        const edactors::Body* enemy=nullptr;float nearest=std::numeric_limits<float>::infinity();const Vec2 at{world.actor.position[0],world.actor.position[2]};
        for(const auto& [key,body]:world.actors->bodies())if(key.type==23&&body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&vm.field(body.script,13).integer()>0){
            const float d=length(Vec2{body.pose.x,body.pose.z}-at);if(!enemy||vm.field(body.script,13).integer()<vm.field(enemy->script,13).integer()||(vm.field(body.script,13).integer()==vm.field(enemy->script,13).integer()&&d<nearest)){nearest=d;enemy=&body;}}
        if(!enemy){tick();continue;}const auto key=enemy->key;const auto target=Vec2{enemy->pose.x,enemy->pose.z};const int hp=vm.field(enemy->script,13).integer();
        const float reach=std::min(3.1f,2.4f+float(enemy->radius)*.01f);
        if(n%100==0)std::cout<<"Temple real combat frame"<<n<<" enemyHP"<<hp<<" AshHP"<<priorHP<<" distance"<<nearest<<" reach"<<reach<<" sourceRadius"<<enemy->radius<<" target"<<enemy->key.index<<" alive685="<<vm.staticField(685).integer()<<" at"<<at.x<<','<<at.z<<'\n';
        if(nearest>reach)tick(toward(target,1),true,false,.033f,false,false);
        else {if(!vm.invoke(world.game.package.methodIndex("ed/base/External","actorIsFacingActor","(IIIII)I"),0,{jpc::Value::integer(0),jpc::Value::integer(0),jpc::Value::integer(key.type),jpc::Value::integer(key.index),jpc::Value::integer(30)}).integer())tick(toward(target,1),true,false,0,false,false);tick({},true,false,.033f,false,true);}
        const auto current=world.actors->bodies().find(key);if(current!=world.actors->bodies().end()&&vm.field(current->second.script,13).integer()<hp)++hits;
    }
    require(quantity(145)==1&&vm.staticField(550).integer(),"actual BT10 free GuildMember did not yield key145/550");ready();

    const auto relic=world.saveStateBytes();world.restoreStateBytes(relic);require(world.saveStateBytes()==relic,"actual145 did not restore exactly");
    std::cout<<"NEXT TEMPLE actual145/550 HP"<<priorHP<<" at"<<world.actor.position[0]<<','<<world.actor.position[2]<<'\n';
    auto approach=[&](unsigned volume,unsigned marker){walk(sample(volume),[&](){const auto& b=world.actors->at({0,0});return world.scene.scripting.inside(volume,{b.pose.x,b.localY,b.pose.z});});face(marker,65);};
    auto use=[&](unsigned volume,unsigned marker){approach(volume,marker);tick({},false,true,0,false);ready();};
    auto passage=[&](const char* source,unsigned index,const char* destination){if(world.scene.id==destination)return;require(world.scene.id==source,"unexpected route scene before passage");
        walk(portalGoal(index),[&](){return world.scene.id==destination;});for(unsigned n=0;n<180&&world.scene.id==source;++n)tick();require(world.scene.id==destination,"native door did not transition");std::cout<<"TEMPLE WALK passage "<<source<<"->"<<destination<<" HP"<<priorHP<<'\n';};
    auto item=[&](int id,unsigned volume,unsigned marker,int flag){approach(volume,marker);world.openInventory();world.syncInventory();
        const auto selected=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[&](const auto& i){return i.containerField==99&&i.itemID==id;});
        require(selected!=world.inventoryView.items.end(),"genuine earned item missing from actual UI");world.inventoryView.selected=unsigned(selected-world.inventoryView.items.begin());world.applyInventoryItem();
        require(!world.scriptFault&&vm.staticField(flag).integer()&&quantity(id)==0,"original item use did not report/consume");ready();std::cout<<"TEMPLE WALK item"<<id<<" flag"<<flag<<" HP"<<priorHP<<'\n';};
    auto checkpoint=[&](const char* name){const auto bytes=world.saveStateBytes();world.restoreStateBytes(bytes);require(world.saveStateBytes()==bytes,"full World save did not restore exactly");(void)name;};

    for(unsigned i:{2,1,0})walk(nav(i));use(10,2);passage("bt10",1,"bt02");
    for(unsigned i:{27,26,25,24,23,22,21,20,19,18,17,16,15,2,0})walk(nav(i));
    {const auto central=world.scene.scripting.markers.at(14);walk({central.x,central.z});}item(145,46,28,545);
    const auto done=world.saveStateBytes();world.restoreStateBytes(done);require(world.saveStateBytes()==done,"actual statue545 did not restore exactly");
    std::cout<<"NEXT TEMPLE statue145/550 actualUI->545 COMPLETE HP"<<priorHP<<" frames"<<frames<<" displacement"<<distance<<" observedFinalFightHits"<<hits<<" exactsave/no poses\n";
}
// Genuine input-only continuation; earlier BM01 approach fixtures remain historical.
void continueEarnedTempleSecondKeyWalkWorld(World& world) {
    auto& vm=world.game.vm;std::cout<<std::unitbuf;
    auto require=[](bool ok,const std::string& text){if(!ok)throw std::runtime_error("Earned temple walk: "+text);};
    auto quantity=[&](int id){int n=0;for(int field:{98,99,100})for(const auto& item:world.game.items(field))if(item.itemID==id)n+=item.quantity;return n;};
    require(world.scene.id=="bt02"&&vm.staticField(545).integer()&&!quantity(145)&&!vm.staticField(544).integer()&&!quantity(143),"missing actual statue545 checkpoint");
    // Player-facing route checks use native P1 class4/flag2; validate only that same
    // source-eligible static geometry, preserving raw enable/volume tables.
    auto playerClear=[&](Vec2 point){std::vector<GroundPolygon> active;for(const auto& wall:world.scene.ground) {
        if(wall.boundaryOnly||wall.sourceIndex>=world.scene.queries.volumes.size()||world.scene.queries.volumes[wall.sourceIndex].geometry.kind!=2||
           (world.scene.queries.volumes[wall.sourceIndex].flags&2))active.push_back(wall);}
        return clearGround(active,point,world.actor.clearanceRadius);};
    const unsigned initialTransitions=world.transitions;
    unsigned frames=0,combat=0;float distance=0;int priorHP=vm.field(world.game.ash,13).integer();
    auto tick=[&](Vec2 stick=Vec2{},bool aim=false,bool use=false,float dt=.033f,bool run=true,bool fire=true){const Vec2 old{world.actor.position[0],world.actor.position[2]};const auto source=world.scene.id;
        if(dt>0&&vm.field(world.game.ash,13).integer()<=65)useSourceMushrooms(world);
        ControlFrame input;input.movement=stick;input.down[size_t(ControlAction::Aim)]=aim;const auto saw=vm.field(world.game.ash,102).reference();const bool powered=saw&&vm.field(saw,812).integer()==101&&vm.field(saw,815).integer()>0;input.edges[size_t(powered?ControlAction::PrimaryAttack:ControlAction::SecondaryAttack)]=aim&&fire&&dt>0&&world.sourceTick%6==0;if(aim&&fire&&powered&&!vm.field(saw,157).integer())input.edges[size_t(ControlAction::ChainsawRev)]=true;
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



    auto approach=[&](unsigned volume,unsigned marker){walk(sample(volume),[&](){const auto& b=world.actors->at({0,0});return world.scene.scripting.inside(volume,{b.pose.x,b.localY,b.pose.z});});face(marker,65);};
    auto use=[&](unsigned volume,unsigned marker){approach(volume,marker);tick({},false,true,0,false);ready();};
    auto passage=[&](const char* source,unsigned index,const char* destination){if(world.scene.id==destination)return;require(world.scene.id==source,"unexpected route scene before passage");
        walk(portalGoal(index),[&](){return world.scene.id==destination;});for(unsigned n=0;n<180&&world.scene.id==source;++n)tick();require(world.scene.id==destination,"native door did not transition");std::cout<<"TEMPLE WALK passage "<<source<<"->"<<destination<<" HP"<<priorHP<<'\n';};
    auto item=[&](int id,unsigned volume,unsigned marker,int flag){approach(volume,marker);world.openInventory();world.syncInventory();
        const auto selected=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[&](const auto& i){return i.containerField==99&&i.itemID==id;});
        require(selected!=world.inventoryView.items.end(),"genuine earned item missing from actual UI");world.inventoryView.selected=unsigned(selected-world.inventoryView.items.begin());world.applyInventoryItem();
        require(!world.scriptFault&&vm.staticField(flag).integer()&&quantity(id)==0,"original item use did not report/consume");ready();std::cout<<"TEMPLE WALK item"<<id<<" flag"<<flag<<" HP"<<priorHP<<'\n';};
    auto checkpoint=[&](const char* name){const auto bytes=world.saveStateBytes();world.restoreStateBytes(bytes);require(world.saveStateBytes()==bytes,"full World save did not restore exactly");(void)name;};


    ready();std::cout<<"SECOND KEY genuine statue545 start HP"<<priorHP<<'\n';
    {const auto p=world.scene.scripting.markers.at(14);walk({p.x,p.z});}for(unsigned i:{0,2,15,16,17,18,19,20,21,22,23,39})walk(nav(i));
    use(43,26);passage("bt02",7,"bt08");
    for(unsigned i:{0,1,2})walk(nav(i));walk(sample(0));for(unsigned n=0;n<360&&!vm.staticField(555).integer();++n)tick();require(vm.staticField(555).integer(),"original BT08 intro555 did not finish");ready();
    unsigned hits=0;
    for(unsigned n=0;n<3500&&!quantity(143);++n){if(world.game.mode!=0)ready();
        const auto loot=std::find_if(world.nativeProperties.objects.begin(),world.nativeProperties.objects.end(),[](const auto& entry){return(entry.second.flags&1)&&entry.second.setup[0]==143;});
        if(loot!=world.nativeProperties.objects.end()){
            const auto target=loot->second.position;Vec2 goal{target.x-.8f,target.z};if(!playerClear(goal)||!floorHeight(world.scene.floor,goal))goal={target.x+.8f,target.z};
            walk(goal);ready();tick(toward({target.x,target.z},1),false,false,0,false,false);tick({},false,true,0,false,false);tick();continue;
        }
        const edactors::Body* enemy=nullptr;float nearest=std::numeric_limits<float>::infinity();const Vec2 at{world.actor.position[0],world.actor.position[2]};
        for(const auto& [key,body]:world.actors->bodies())if(key.type==23&&body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&vm.field(body.script,13).integer()>0){
            const float d=length(Vec2{body.pose.x,body.pose.z}-at);if(!enemy||vm.field(body.script,13).integer()<vm.field(enemy->script,13).integer()||(vm.field(body.script,13).integer()==vm.field(enemy->script,13).integer()&&d<nearest)){nearest=d;enemy=&body;}}
        if(!enemy){tick();continue;}const auto key=enemy->key;const auto target=Vec2{enemy->pose.x,enemy->pose.z};const int hp=vm.field(enemy->script,13).integer();
        const float reach=std::min(3.1f,2.4f+float(enemy->radius)*.01f);
        if(n%100==0)std::cout<<"Temple real combat frame"<<n<<" enemyHP"<<hp<<" AshHP"<<priorHP<<" distance"<<nearest<<" reach"<<reach<<" sourceRadius"<<enemy->radius<<" target"<<enemy->key.index<<" alive685="<<vm.staticField(685).integer()<<" at"<<at.x<<','<<at.z<<'\n';
        if(nearest>reach)tick(toward(target,1),true,false,.033f,false,false);
        else {if(!vm.invoke(world.game.package.methodIndex("ed/base/External","actorIsFacingActor","(IIIII)I"),0,{jpc::Value::integer(0),jpc::Value::integer(0),jpc::Value::integer(key.type),jpc::Value::integer(key.index),jpc::Value::integer(30)}).integer())tick(toward(target,1),true,false,0,false,false);tick({},true,false,.033f,false,true);}
        const auto current=world.actors->bodies().find(key);if(current!=world.actors->bodies().end()&&vm.field(current->second.script,13).integer()<hp)++hits;
    }
    require(quantity(143)==1&&vm.staticField(553).integer(),"actual BT08 free GuildMember did not yield key143/553");ready();


    const auto key=world.saveStateBytes();world.restoreStateBytes(key);require(world.saveStateBytes()==key,"actual143 did not restore exactly");
    std::cout<<"SECOND KEY real143/553 HP"<<priorHP<<'\n';
    for(unsigned i:{3,2,1,0})walk(nav(i));use(17,4);passage("bt08",1,"bt02");
    for(unsigned i:{39,23,22,21,20,19,18,17,16,15,2,0,3,4,5,6,7,8,9,10,11})walk(nav(i));item(143,42,25,544);
    const auto done=world.saveStateBytes();world.restoreStateBytes(done);require(world.saveStateBytes()==done,"actual key544 did not restore exactly");
    std::cout<<"SECOND KEY real143/553 actualUI->544 COMPLETE HP"<<priorHP<<" frames"<<frames<<" displacement"<<distance<<" observedFinalFightHits"<<hits<<" exactsave/no poses\n";
}
// Genuine input-only continuation; earlier BM01 approach fixtures remain historical.
void continueEarnedTempleStatue82WalkWorld(World& world) {
    auto& vm=world.game.vm;std::cout<<std::unitbuf;
    auto require=[](bool ok,const std::string& text){if(!ok)throw std::runtime_error("Earned temple walk: "+text);};
    auto quantity=[&](int id){int n=0;for(int field:{98,99,100})for(const auto& item:world.game.items(field))if(item.itemID==id)n+=item.quantity;return n;};
    require(world.scene.id=="bt02"&&vm.staticField(544).integer()&&vm.staticField(545).integer()&&!quantity(143)&&!vm.staticField(547).integer()&&!quantity(82),"missing actual key544/statue545 checkpoint");
    // Player-facing route checks use native P1 class4/flag2; validate only that same
    // source-eligible static geometry, preserving raw enable/volume tables.
    auto playerClear=[&](Vec2 point){std::vector<GroundPolygon> active;for(const auto& wall:world.scene.ground) {
        if(wall.boundaryOnly||wall.sourceIndex>=world.scene.queries.volumes.size()||world.scene.queries.volumes[wall.sourceIndex].geometry.kind!=2||
           (world.scene.queries.volumes[wall.sourceIndex].flags&2))active.push_back(wall);}
        return clearGround(active,point,world.actor.clearanceRadius);};
    const unsigned initialTransitions=world.transitions;
    unsigned frames=0,combat=0;float distance=0;int priorHP=vm.field(world.game.ash,13).integer();
    auto tick=[&](Vec2 stick=Vec2{},bool aim=false,bool use=false,float dt=.033f,bool run=true,bool fire=true){const Vec2 old{world.actor.position[0],world.actor.position[2]};const auto source=world.scene.id;
        if(dt>0&&vm.field(world.game.ash,13).integer()<=65)useSourceMushrooms(world);
        ControlFrame input;input.movement=stick;input.down[size_t(ControlAction::Aim)]=aim;const auto saw=vm.field(world.game.ash,102).reference();const bool powered=saw&&vm.field(saw,812).integer()==101&&vm.field(saw,815).integer()>0;input.edges[size_t(powered?ControlAction::PrimaryAttack:ControlAction::SecondaryAttack)]=aim&&fire&&dt>0&&world.sourceTick%6==0;if(aim&&fire&&powered&&!vm.field(saw,157).integer())input.edges[size_t(ControlAction::ChainsawRev)]=true;
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



    auto approach=[&](unsigned volume,unsigned marker){walk(sample(volume),[&](){const auto& b=world.actors->at({0,0});return world.scene.scripting.inside(volume,{b.pose.x,b.localY,b.pose.z});});face(marker,65);};
    auto use=[&](unsigned volume,unsigned marker){approach(volume,marker);tick({},false,true,0,false);ready();};
    auto passage=[&](const char* source,unsigned index,const char* destination){if(world.scene.id==destination)return;require(world.scene.id==source,"unexpected route scene before passage");
        walk(portalGoal(index),[&](){return world.scene.id==destination;});for(unsigned n=0;n<180&&world.scene.id==source;++n)tick();require(world.scene.id==destination,"native door did not transition");std::cout<<"TEMPLE WALK passage "<<source<<"->"<<destination<<" HP"<<priorHP<<'\n';};
    auto item=[&](int id,unsigned volume,unsigned marker,int flag){approach(volume,marker);world.openInventory();world.syncInventory();
        const auto selected=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[&](const auto& i){return i.containerField==99&&i.itemID==id;});
        require(selected!=world.inventoryView.items.end(),"genuine earned item missing from actual UI");world.inventoryView.selected=unsigned(selected-world.inventoryView.items.begin());world.applyInventoryItem();
        require(!world.scriptFault&&vm.staticField(flag).integer()&&quantity(id)==0,"original item use did not report/consume");ready();std::cout<<"TEMPLE WALK item"<<id<<" flag"<<flag<<" HP"<<priorHP<<'\n';};
    auto checkpoint=[&](const char* name){const auto bytes=world.saveStateBytes();world.restoreStateBytes(bytes);require(world.saveStateBytes()==bytes,"full World save did not restore exactly");(void)name;};


    ready();std::cout<<"FINAL STATUE genuine key544 start HP"<<priorHP<<'\n';
    for(int field:{98,99,100})for(const auto& i:world.game.items(field))std::cout<<"OWNED "<<field<<" id"<<i.itemID<<" qty"<<i.quantity<<'\n';
    while(quantity(46)&&vm.field(world.game.ash,13).integer()<70) {
        ready();const int beforeHP=vm.field(world.game.ash,13).integer(),beforeQty=quantity(46);
        const int value=vm.invoke(world.game.package.methodIndex("ObjDB","getObjectValueHealth","(I)I"),0,{jpc::Value::integer(46)}).integer();
        world.openInventory();world.syncInventory();const auto selected=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[](const auto& i){return i.containerField==99&&i.itemID==46;});
        require(selected!=world.inventoryView.items.end()&&value>0,"earned medical item is absent");world.inventoryView.selected=unsigned(selected-world.inventoryView.items.begin());world.applyInventoryItem();
        require(quantity(46)==beforeQty-1&&vm.field(world.game.ash,13).integer()==std::min(vm.field(world.game.ash,95).integer(),beforeHP+value),"original medicine UI did not consume/heal");
        std::cout<<"GENUINE MEDICINE46 originalUI "<<beforeHP<<"->"<<vm.field(world.game.ash,13).integer()<<" qty"<<beforeQty<<"->"<<quantity(46)<<'\n';world.closeInventory();ready();priorHP=vm.field(world.game.ash,13).integer();
    }
    use(42,25);passage("bt02",6,"bt06");
    walk(sample(12));for(unsigned n=0;n<360&&!vm.staticField(557).integer();++n)tick();require(vm.staticField(557).integer(),"original BT06 intro557 did not finish");ready();
    unsigned hits=0;
    for(unsigned n=0;n<3500&&!quantity(82);++n){if(world.game.mode!=0)ready();
        const auto loot=std::find_if(world.nativeProperties.objects.begin(),world.nativeProperties.objects.end(),[](const auto& entry){return(entry.second.flags&1)&&entry.second.setup[0]==82;});
        if(loot!=world.nativeProperties.objects.end()&&vm.staticField(684).integer()){
            const auto target=loot->second.position;Vec2 goal{target.x-.8f,target.z};if(!playerClear(goal)||!floorHeight(world.scene.floor,goal))goal={target.x+.8f,target.z};
            walk(goal);ready();tick(toward({target.x,target.z},1),false,false,0,false,false);tick({},false,true,0,false,false);tick();continue;
        }
        const edactors::Body* enemy=nullptr;float nearest=std::numeric_limits<float>::infinity();const Vec2 at{world.actor.position[0],world.actor.position[2]};
        for(const auto& [key,body]:world.actors->bodies())if(key.type==23&&body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&vm.field(body.script,13).integer()>0){
            const float d=length(Vec2{body.pose.x,body.pose.z}-at);if(!enemy||vm.field(body.script,13).integer()<vm.field(enemy->script,13).integer()||(vm.field(body.script,13).integer()==vm.field(enemy->script,13).integer()&&d<nearest)){nearest=d;enemy=&body;}}
        if(!enemy){tick();continue;}const auto key=enemy->key;const auto target=Vec2{enemy->pose.x,enemy->pose.z};const int hp=vm.field(enemy->script,13).integer();
        const float reach=std::min(3.1f,2.4f+float(enemy->radius)*.01f);
        if(n%100==0)std::cout<<"Temple real combat frame"<<n<<" enemyHP"<<hp<<" AshHP"<<priorHP<<" distance"<<nearest<<" reach"<<reach<<" sourceRadius"<<enemy->radius<<" target"<<enemy->key.index<<" alive684="<<vm.staticField(684).integer()<<" at"<<at.x<<','<<at.z<<'\n';
        if(nearest>reach)tick(toward(target,1),true,false,.033f,false,false);
        else {if(!vm.invoke(world.game.package.methodIndex("ed/base/External","actorIsFacingActor","(IIIII)I"),0,{jpc::Value::integer(0),jpc::Value::integer(0),jpc::Value::integer(key.type),jpc::Value::integer(key.index),jpc::Value::integer(30)}).integer())tick(toward(target,1),true,false,0,false,false);tick({},true,false,.033f,false,true);}
        const auto current=world.actors->bodies().find(key);if(current!=world.actors->bodies().end()&&vm.field(current->second.script,13).integer()<hp)++hits;
    }
    require(quantity(82)==1&&vm.staticField(551).integer(),"actual BT06 free GuildMember did not yield key82/551");ready();


    const auto relic=world.saveStateBytes();world.restoreStateBytes(relic);require(world.saveStateBytes()==relic,"actual82 did not restore exactly");
    std::cout<<"FINAL STATUE actual82/551 HP"<<priorHP<<'\n';
    for(unsigned i:{4,5,6,0})walk(nav(i));use(8,1);passage("bt06",1,"bt02");
    for(unsigned i:{11,10,9,8,7,6,5,4,3,0})walk(nav(i));{const auto p=world.scene.scripting.markers.at(14);walk({p.x,p.z});}item(82,47,29,547);
    for(unsigned n=0;n<60&&world.scene.id=="bt02";++n)tick();require(world.scene.id=="bt11","original two-statue queued switch did not reach BT11");
    const auto done=world.saveStateBytes();world.restoreStateBytes(done);require(world.saveStateBytes()==done,"actual queued BT11 did not restore exactly");
    std::cout<<"FINAL STATUE 82/551 actualUI547+prior545->SOURCE BT11 COMPLETE HP"<<priorHP<<" frames"<<frames<<" displacement"<<distance<<" observedFinalFightHits"<<hits<<" exactsave/no poses\n";
}

void continueEarnedTempleStatuesWalkWorld(World& world) {
    continueEarnedTempleStatue145WalkWorld(world);continueEarnedTempleSecondKeyWalkWorld(world);continueEarnedTempleStatue82WalkWorld(world);
}
void testEarnedTempleStatuesWalkWorld(const fs::path& levelA,const std::function<void(World&)>& continuation={}) {
    testEarnedTempleKeyWalkWorld(levelA,[&](World& world){continueEarnedTempleStatuesWalkWorld(world);if(continuation)continuation(world);});
}
