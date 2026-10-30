#pragma once
#include "earned_temple_statues_walk_world_tests.inl"
// Original BM01 spawn through authored navigation and ordinary World input only.
struct BM01InputWalk {
    World& world; jpc::Interpreter& vm;
    unsigned frames=0,combat=0,hits=0;float distance=0;bool powered=false;
    explicit BM01InputWalk(World& value):world(value),vm(value.game.vm){}
    void require(bool yes,const std::string& text){if(!yes)throw std::runtime_error("Input-only BM01: "+text);}
    Vec2 at()const{return {world.actor.position[0],world.actor.position[2]};}
    int health()const{return vm.field(world.game.ash,13).integer();}
    int quantity(int id)const{int count=0;for(int container:{98,99,100})for(auto item:world.game.items(container))if(item.itemID==id)count+=item.quantity;return count;}
    void tick(Vec2 stick={},bool aim=false,bool attack=false,bool use=false,float dt=.033f){
        const auto old=at();const int before=health();
        ControlFrame input;input.movement=stick;
        input.down[size_t(ControlAction::Aim)]=aim;input.edges[size_t(powered?ControlAction::PrimaryAttack:ControlAction::SecondaryAttack)]=attack&&world.sourceTick%6==0;
        world.playerControls(input);world.step(stick,true,use,dt);
        require(!world.scriptFault,"native/script exception: "+world.game.diagnostic);
        require(world.notice!="could not perform action"&&world.notice!="action is not connected yet","input fault: "+world.notice);
        require(world.scene.id=="bm01","unexpected source scene "+world.scene.id);
        require(health()>0,"Ash naturally died");
        require(floorHeight(world.scene.floor,at()).has_value(),"input left original floor");
        std::vector<GroundPolygon> statics;for(auto p:world.scene.ground)if(p.boundaryOnly||p.sourceIndex>=world.scene.queries.volumes.size()||
            world.scene.queries.volumes[p.sourceIndex].geometry.kind!=2||(world.scene.queries.volumes[p.sourceIndex].flags&2))statics.push_back(std::move(p));
        require(clearGround(statics,at(),0),"input penetrated an enabled original P1 static");
        if(dt>0){++frames;distance+=length(at()-old);}if(health()<before)std::cout<<"BM01 incoming "<<before<<"->"<<health()<<" tick"<<world.sourceTick<<" at"<<at().x<<','<<at().z<<'\n';

    }
    void ready(){for(unsigned n=0;n<480;++n){if(world.game.mode!=0)world.closeInventory();world.playerControls({});const auto& body=world.actors->at({0,0});
        if(!(body.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&!vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&body.actions.possible(116))return;tick();}
        throw std::runtime_error("Input-only BM01: source Use did not become ready");}
    Vec2 toward(Vec2 goal,float magnitude=1){const auto& camera=world.scene.cameras.at(world.actor.camera);const auto& latch=world.actor.movement;
        const Vec2 right=latch.held?latch.right:camera.right,forward=latch.held?latch.forward:camera.forward,delta=goal-at();
        const float determinant=right.x*forward.z-right.z*forward.x;require(std::abs(determinant)>.001f,"singular movement basis");
        Vec2 stick{(delta.x*forward.z-delta.z*forward.x)/determinant,(right.x*delta.z-right.z*delta.x)/determinant};const auto size=length(stick);
        return size>.000001f?stick*(magnitude/size):Vec2{};}
    Vec2 nav(unsigned index){const auto& raw=world.scene.nativeBytes;uint32_t count,offset;std::memcpy(&count,raw.data()+48,4);std::memcpy(&offset,raw.data()+52,4);
        require(index<count&&offset<=raw.size()&&size_t(index)*44+20<=raw.size()-offset,"missing source navpoint");GroundPoint p;std::memcpy(&p,raw.data()+offset+index*44+8,12);return {p.x,p.z};}
    Vec2 sourceNext(Vec2 target){const auto& raw=world.scene.nativeBytes;uint32_t count,links,offset;std::memcpy(&count,raw.data()+48,4);std::memcpy(&links,raw.data()+56,4);std::memcpy(&offset,raw.data()+60,4);
        auto closest=[&](Vec2 p){unsigned found=0;float best=std::numeric_limits<float>::infinity();for(unsigned i=0;i<count;++i){const auto d=length(nav(i)-p);if(d<best){best=d;found=i;}}return found;};
        const unsigned start=closest(at()),goal=closest(target);if(start==goal)return target;
        std::vector<float> distances(count,std::numeric_limits<float>::infinity());std::vector<int> previous(count,-1);std::vector<bool> used(count,false);distances[start]=0;
        for(unsigned n=0;n<count;++n){unsigned u=count;for(unsigned i=0;i<count;++i)if(!used[i]&&(u==count||distances[i]<distances[u]))u=i;
            if(u==count||!std::isfinite(distances[u]))break;used[u]=true;if(u==goal)break;
            for(unsigned i=0;i<links;++i){uint32_t a,b;std::memcpy(&a,raw.data()+offset+i*32+24,4);std::memcpy(&b,raw.data()+offset+i*32+28,4);if(a!=u&&b!=u)continue;const unsigned v=a==u?b:a;const auto d=distances[u]+length(nav(u)-nav(v));if(d<distances[v]){distances[v]=d;previous[v]=int(u);}}}
        require(previous[goal]>=0,"source navigation graph is disconnected");unsigned next=goal;while(previous[next]>=0&&unsigned(previous[next])!=start)next=unsigned(previous[next]);
        return length(nav(start)-at())>.5f?nav(start):nav(next);}
    bool los(Vec2 target,float localY=0){const auto& ash=world.actors->at({0,0});return world.scene.queries.lineOfSight({ash.pose.x,ash.pose.y,ash.pose.z},{target.x,localY,target.z},4,world.sourceQueryCamera(),world.scene.enabledVolumes,world.visibilityReady);}
    void walk(Vec2 goal){tick({},false,false,false,0);float prior=std::numeric_limits<float>::infinity();unsigned stalled=0;
        for(unsigned n=0;n<2400;++n){const float remaining=length(goal-at());if(remaining<.16f){tick({},false,false,false,0);return;}
            if(world.game.mode!=0){ready();continue;}
            if(world.guard||vm.field(world.game.ash,11).integer()||(world.actors->at({0,0}).flags&0x48)){tick();stalled=0;continue;}
            stalled=remaining>=prior-.00001f?stalled+1:0;prior=remaining;
            if(stalled==179){std::cerr<<"STALLED at"<<at().x<<','<<at().z<<" target"<<goal.x<<','<<goal.z<<" state"<<world.actors->at({0,0}).actions.layers[0].state<<" cam"<<world.actor.camera<<'\n';}
            require(stalled<180,"authored nav walk stalled");tick(toward(goal,std::min(1.f,remaining/(world.scene.speed*2*.033f))));}
        throw std::runtime_error("Input-only BM01: nav input budget exhausted");}
    void intro(){require(world.sourceTick==0&&world.scene.id=="bm01"&&!vm.staticField(526).integer()&&!quantity(20)&&!quantity(21)&&!quantity(22)&&!vm.staticField(531).integer(),"fresh original starting state differs");
        for(unsigned index:{10,90,89,88,87,86,85,84,82,80,79,77,76,75,74,72,71}){walk(nav(index));std::cout<<"BM01 WALK nav"<<index<<" tick"<<world.sourceTick<<" HP"<<health()<<'\n';}
        for(unsigned n=0;n<240&&!vm.staticField(526).integer();++n)tick();require(vm.staticField(526).integer()==1,"walking into source trigger6 did not complete intro526");ready();
        std::cout<<"INPUT_ONLY_BM01_INTRO_PASS frames"<<frames<<" distance"<<distance<<" tick"<<world.sourceTick<<" HP"<<health()<<" no pose/HP/quest/NPC writes\n";

    }
    void fight(){powered=true;require(world.scene.id=="bm01"&&vm.staticField(526).integer()==1&&!vm.staticField(531).integer(),"requires genuine walked intro");
        const auto saw=vm.field(world.game.ash,102).reference();require(saw&&vm.field(saw,812).integer()==101&&vm.field(saw,815).integer()>0,"original owned101/fuel unavailable");
        unsigned idle=0,stalled=0;Vec2 previous=at();
        for(unsigned n=0;n<18000&&!(quantity(20)&&quantity(21)&&quantity(22));++n){
            if(world.game.mode!=0){world.closeInventory();tick();continue;}
            const edactors::Body* enemy=nullptr;float nearest=std::numeric_limits<float>::infinity();
            for(const auto& [key,body]:world.actors->bodies())if(key.type==28&&body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&vm.field(body.script,13).integer()>0&&!vm.field(body.script,world.game.hostField(HostFieldRole::GenericNpc30)).integer()){
                const float d=length(at()-Vec2{body.pose.x,body.pose.z});if(d<nearest){nearest=d;enemy=&body;}}
            if(enemy){idle=0;const auto key=enemy->key;const int before=vm.field(enemy->script,13).integer();const Vec2 target{enemy->pose.x,enemy->pose.z};const bool visible=los(target,enemy->localY);
                const float reach=std::min(3.1f,2.4f+float(enemy->radius)*.01f);
                if(visible&&nearest<reach){
                    tick(toward(target),true,false,false,0);
                    if(!vm.field(saw,157).integer()&&vm.field(saw,815).integer()>0){ControlFrame rev;rev.down[size_t(ControlAction::Aim)]=true;rev.edges[size_t(ControlAction::ChainsawRev)]=true;world.playerControls(rev);}
                    tick({},true,true);++combat;stalled=0;
                }else{const Vec2 goal=visible&&stalled<30?target:sourceNext(target);tick(toward(goal),true);stalled=length(at()-previous)<.00001f?stalled+1:0;}
                const auto found=world.actors->bodies().find(key);if(found!=world.actors->bodies().end()&&vm.field(found->second.script,13).integer()<before){++hits;std::cout<<"BM01 actual contact "<<key.index<<" flavor"<<vm.field(found->second.script,1238).integer()<<" HP"<<before<<"->"<<vm.field(found->second.script,13).integer()<<" Ash"<<health()<<" fuel"<<vm.field(saw,815).integer()<<'\n';}
                if(n%180==0)std::cout<<"BM01 fighting tick"<<world.sourceTick<<" target"<<key.index<<" dist"<<nearest<<" LOS"<<visible<<" HP"<<health()<<" parts"<<quantity(20)<<quantity(21)<<quantity(22)<<" at"<<at().x<<','<<at().z<<'\n';
                require(stalled<180,"source-nav chase stalled");previous=at();continue;}
            bool foundObject=false;
            for(int item:{20,21,22})if(!quantity(item)){
                const auto found=std::find_if(world.nativeProperties.objects.begin(),world.nativeProperties.objects.end(),[&](const auto& row){return (row.second.flags&1)&&row.second.setup[0]==item;});
                if(found==world.nativeProperties.objects.end())continue;foundObject=true;const Vec2 target{found->second.position.x,found->second.position.z};const auto d=length(target-at());
                if(d>1.15f){const auto goal=los(target)?target:sourceNext(target);tick(toward(goal));}
                else{ready();tick(toward(target),false,false,false,0);tick({},false,false,true);for(unsigned wait=0;wait<180&&!quantity(item);++wait){if(world.game.mode!=0)world.closeInventory();tick();}
                    require(quantity(item)>0,"actual visible bridge part pickup failed");ready();std::cout<<"INPUT_ONLY_PICKUP "<<item<<" tick"<<world.sourceTick<<" HP"<<health()<<'\n';}
                break;}
            if(!foundObject){tick();require(++idle<600,"no active wave or genuine parts appeared");}else idle=0;previous=at();
        }
        require(quantity(20)&&quantity(21)&&quantity(22),"free waves did not yield all source parts");
        require(vm.staticField(527).integer()&&vm.staticField(528).integer()&&vm.staticField(529).integer(),"actual pickup reports did not set527/528/529");ready();
        std::cout<<"INPUT_ONLY_BM01_PARTS_PASS "<<frames<<" frames/"<<distance<<"m "<<combat<<" combat/"<<hits<<" actual damage steps HP"<<health()<<'\n';
    }
    void resources(bool takeFuel=false){ready();const auto start=at();std::vector<int> observed;
        for(const auto& [index,object]:world.nativeProperties.objects)if((object.flags&1)&&(object.setup[0]==70||(takeFuel&&object.setup[0]==69)))observed.push_back(index);
        std::reverse(observed.begin(),observed.end());std::cout<<"BM01 OBSERVED source70 count"<<observed.size()<<" owned"<<quantity(70)<<'\n';
        auto approach=[&](Vec2 target,float stop){if(!los(target)){
            const auto& raw=world.scene.nativeBytes;uint32_t count,links,offset;std::memcpy(&count,raw.data()+48,4);std::memcpy(&links,raw.data()+56,4);std::memcpy(&offset,raw.data()+60,4);
            auto closest=[&](Vec2 p){unsigned best=0;for(unsigned i=1;i<count;++i)if(length(nav(i)-p)<length(nav(best)-p))best=i;return best;};const unsigned from=closest(at()),to=closest(target);
            std::vector<float> d(count,std::numeric_limits<float>::infinity());std::vector<int> previous(count,-1);std::vector<bool> used(count,false);d[from]=0;
            for(unsigned n=0;n<count;++n){unsigned u=count;for(unsigned i=0;i<count;++i)if(!used[i]&&(u==count||d[i]<d[u]))u=i;if(u==count||!std::isfinite(d[u]))break;used[u]=true;if(u==to)break;
                for(unsigned i=0;i<links;++i){uint32_t a,b;std::memcpy(&a,raw.data()+offset+i*32+24,4);std::memcpy(&b,raw.data()+offset+i*32+28,4);if(a!=u&&b!=u)continue;const unsigned v=a==u?b:a;const auto next=d[u]+length(nav(u)-nav(v));if(next<d[v]){d[v]=next;previous[v]=int(u);}}}
            require(from==to||previous[to]>=0,"observed resource source route disconnected");std::vector<unsigned> path;for(int i=int(to);i>=0;i=previous[unsigned(i)]){path.push_back(unsigned(i));if(unsigned(i)==from)break;}std::reverse(path.begin(),path.end());
            for(unsigned i:path){if(length(target-at())<=stop)break;std::cout<<"RESOURCE WALK nav"<<i<<'\n';walk(nav(i));}}
            unsigned stalled=0;for(unsigned n=0;n<2400&&length(target-at())>stop;++n){if(world.game.mode!=0){ready();continue;}
                const auto old=at();const float d=length(target-at());tick(toward(target,std::min(1.f,d/(world.scene.speed*2*.033f))));
                stalled=length(old-at())<.00001f?stalled+1:0;require(stalled<180,"observed source70 approach stalled");}require(length(target-at())<=stop,"observed source70 approach budget exhausted");};
        for(int index:observed){const auto live=world.nativeProperties.objects.find(index);if(live==world.nativeProperties.objects.end()||!(live->second.flags&1)||(live->second.setup[0]!=70&&live->second.setup[0]!=69))continue;
            const int item=live->second.setup[0];const Vec2 target{live->second.position.x,live->second.position.z};approach(target,1.15f);ready();const int beforeQty=quantity(item),beforeHP=health();
            tick(toward(target),false,false,false,0);tick({},false,false,true);
            for(unsigned n=0;n<240&&quantity(item)==beforeQty;++n){if(world.game.mode!=0)world.closeInventory();tick();}
            require(quantity(item)==beforeQty+1,"observed source70 did not produce a carried item");require(health()<=beforeHP,"observed resource pickup increased health instead of carrying");ready();
            std::cout<<"OBSERVED_RESOURCE_PICKUP item"<<item<<" index"<<index<<" qty"<<beforeQty<<"->"<<quantity(item)<<" HP"<<beforeHP<<"->"<<health()<<" tick"<<world.sourceTick<<'\n';}
        approach(start,.14f);
    }
    void bridge(){ready();require(quantity(20)&&quantity(21)&&quantity(22)&&!vm.staticField(531).integer(),"requires genuinely earned components");
        auto select=[&](int id){world.syncInventory();const auto it=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[&](const auto& item){return item.containerField==99&&item.itemID==id;});require(it!=world.inventoryView.items.end(),"source inventory selection missing");world.inventoryView.selected=unsigned(it-world.inventoryView.items.begin());};
        world.openInventory();select(20);world.markInventoryCombination();select(21);world.applyInventoryItem();
        require(!quantity(20)&&!quantity(21)&&quantity(154)==1,"original UI20+21 did not produce154");
        select(154);world.markInventoryCombination();select(22);world.applyInventoryItem();
        require(!quantity(154)&&!quantity(22)&&quantity(157)==1,"original UI154+22 did not produce157");world.closeInventory();ready();
        for(unsigned index:{47,48,49,50,51,52,53,54,20,9,8,7,16,5,4,2}){walk(nav(index));std::cout<<"BM01 mechanism WALK nav"<<index<<" tick"<<world.sourceTick<<'\n';}
        ready();const auto marker=world.scene.scripting.markers.at(9);tick(toward({marker.x,marker.z}),false,false,false,0);
        require(world.scene.scripting.inside(3,{at().x,world.actors->at({0,0}).localY,at().z})&&world.scene.scripting.facing(9,70,at(),world.actor.yaw),"input-only mechanism approach missed original trigger3/facing9");
        world.openInventory();select(157);world.applyInventoryItem();world.closeInventory();
        for(unsigned n=0;n<240&&!vm.staticField(531).integer();++n){if(world.game.mode!=0)world.closeInventory();tick();}
        require(vm.staticField(531).integer()==1&&!quantity(157)&&!world.scene.enabledVolumes.at(2),"original157 use did not earn531/open blocker2");ready();
        const auto saved=world.saveStateBytes();auto cold=stageSavedWorld(world,saved);require(cold->saveStateBytes()==saved,"source bridge cold save differs");
        for(unsigned n=0;n<3;++n)tick();const auto future=world.saveStateBytes();world.restoreStateBytes(saved);require(world.saveStateBytes()==saved,"source bridge direct save differs");
        for(unsigned n=0;n<3;++n)tick();require(world.saveStateBytes()==future,"source bridge three-frame replay differs");world.restoreStateBytes(saved);
        std::cout<<"INPUT_ONLY_BM01_BRIDGE_PASS actual intro526/freewaves/20+21+22/UI154+157/use531; "<<frames<<" frames/"<<distance<<"m "<<combat<<" combat/"<<hits<<" observed damage HP"<<health()<<" transitions"<<world.transitions<<" exactdirect/cold/future save; no pose/HP/AI/inventory/quest writes\n";
    }
};

void testEarnedBM01TempleWalkWorld(const fs::path& levelA,
                                 const std::function<void(World&)>& continuation={}) {
    checked(SDL_Init(SDL_INIT_GAMEPAD),"Initialize original input-only LevelB");
    struct Quit{~Quit(){SDL_Quit();}} quit;
    World world(levelA/"levelB",nullptr,presentMovieHeadless,true);
    BM01InputWalk input(world);
    input.intro();input.fight();input.resources(true);input.bridge();
    const auto owned=world.game.items(99);
    if(std::any_of(owned.begin(),owned.end(),[](const auto& item){return item.itemID==69&&item.quantity>0;}))useSourceFuel(world);
    useSourceMushrooms(world);
    continueEarnedBridgeWalkWorld(world);continueEarnedBM02WalkWorld(world);
    useSourceMushrooms(world);continueEarnedMerchantWalkWorld(world);
    useSourceMushrooms(world);continueEarnedBombWalkWorld(world);
    useSourceMushrooms(world);continueEarnedTempleKeyWalkWorld(world);
    useSourceMushrooms(world);continueEarnedTempleStatue145WalkWorld(world);
    useSourceMushrooms(world);continueEarnedTempleSecondKeyWalkWorld(world);
    useSourceMushrooms(world);continueEarnedTempleStatue82WalkWorld(world);
    if(continuation)continuation(world);
}
