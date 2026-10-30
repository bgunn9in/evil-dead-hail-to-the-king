#include "actor_startup_fixture.h"
#include "source_vfx.h"
#include "source_firearm.h"

struct Npc20Random {
    uint32_t seed=12345,index=0;bool ready=false;std::array<uint32_t,64> table{};
    uint32_t step(){seed=seed*0x41c64e6du+0x3039u;return uint32_t(((uint64_t(seed)*0x88888889u)>>35)%0x07ffffffu);}
    int bound(int n){return n?int(step()%uint32_t(std::abs(int64_t(n)))):0;}
    int cached(int n){if(!n)return 0;if(!ready){for(auto& v:table)v=step();ready=true;}
        if(++index>=64){index=0;ready=false;}return int(table[index]%uint32_t(std::abs(int64_t(n))));}
    bool operator==(const Npc20Random& b)const{return seed==b.seed&&index==b.index&&ready==b.ready&&table==b.table;}
};
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Expected original LevelA asset root");const std::filesystem::path root=argv[1];
    Npc20Random random;StartupCase* current=nullptr;uint64_t tick=0;unsigned lines=0,hits=0,fullHealth=0,consumed=0,emitted=0;
    auto sourceActor=[](const auto& b){return edfirearm::Actor{b.key.type,b.key.index,b.nativeSlot,uint32_t(b.flags),{{b.pose.x,b.localY,b.pose.z}},b.pose.yaw,b.radius};};
    edvfx::Host vfxHost;vfxHost.randomBound=[&](int b){return random.bound(b);};vfxHost.cachedBound=[&](int b){return random.cached(b);};
    vfxHost.visible=[](auto){return true;};vfxHost.coarseCell=[](float,float){return int16_t(0);};vfxHost.coarseHeight=[](float,float){return 0.f;};
    vfxHost.floorSample=[](float,float,float){return 0.;};vfxHost.actorWorld=[&](int type,int index){const auto& b=current->actors->at({type,index});return edvfx::Vector{{b.pose.x,b.pose.y,b.pose.z}};};
    vfxHost.repellingActors=[&](){edparticle::PhysicalActors result{};for(const auto& [key,b]:current->actors->bodies()){(void)key;if(b.nativeSlot>=0)
            result.at(size_t(b.nativeSlot))={uint32_t(b.flags),{{b.pose.x,b.localY,b.pose.z}},b.cachedFloorHeight};}return result;};
    edvfx::SourceVfx effects(root,vfxHost);
    StartupCase source(root/"scripts/levelA.jpc",root,false,[&](auto& h){h.random=[&](int b){return random.bound(b);};
        h.spawnPose=[](auto key,int,int,int){return edactors::Pose{0,0,key.type?1.5f:0.f,0};};
        h.animationEvent=[&](const auto& b,const auto& raw){const auto e=edanimevent::decode(raw);auto& vm=current->game.vm;
            if(e.kind==35){std::vector<jpc::Value> args;for(auto a:edanimevent::rumble35(e,b.key.type))args.push_back(I(a));vm.invoke(current->game.package.methodIndex("RumbleCommander","engineReportRumbleVFX","(IIII)V"),0,args);}
            bool skip=false;
            if(edanimevent::scriptVfxCallback(e.kind)){std::vector<jpc::Value> args(11,I(0));args[0]=I(b.key.type);args[1]=I(b.key.index);args[2]=I(e.kind);
                for(unsigned n=0;n<4;++n)args[n+3]=I(b.sfxMaterial[n]);auto expected=random;const int health=vm.field(b.script,13).integer();
                skip=vm.invoke(current->game.package.methodIndex("ActorDB","processVFXAction","(IIIIIIIIIII)I"),0,args).integer()!=0;
                if(b.key.type==4&&e.kind==20){const bool expectedSkip=health>=100||expected.bound(100)<health;
                    assert(skip==expectedSkip&&random==expected);if(health>=100)++fullHealth;else if(skip)++consumed;else ++emitted;}}
            if(skip||edanimevent::skipsVisual(e.kind))return;
            const auto ends=edanimevent::visualEndpoints(e,{{0,b.pose.yaw,0}},{{b.pose.x,b.pose.y,b.pose.z}});effects.line(e.kind,ends.first,ends.second,e.mode);
            if(b.key.type==0&&e.kind==7){++lines;std::vector<edfirearm::Actor> actors;for(const auto& [key,target]:current->actors->bodies()){(void)key;actors.push_back(sourceActor(target));}
                for(const auto& hit:edfirearm::scan(sourceActor(b),actors)){current->actors->post({{hit.targetType,hit.targetIndex},{hit.sourceType,hit.sourceIndex},hit.event,{{hit.selector,0,0,0}}});++hits;}}
        };});current=&source;auto& actors=*source.actors;auto& vm=source.game.vm;const auto& package=source.game.package;
    source.game.enterScene("ak09");vm.invoke(package.methodIndex("SceneCommander","sceneReset","()V"));const auto enemy=actors.createType(4);assert(enemy);
    for(const auto& [key,unused]:actors.bodies()){(void)unused;auto& b=actors.at(key);b.think=vm.enqueue(package.methodIndex("ActorDB","think","(II)V"),0,{I(key.type),I(key.index)});
        vm.invoke(package.methodIndex("ActorDB","setThinkThreadId","(III)V"),0,{I(key.type),I(key.index),I(int32_t(b.think))});vm.hold(b.think);}
    auto bind=[&](const char* name,jpc::Interpreter::Native fn){for(const auto& m:package.methods)if(package.classes[size_t(m.owner)].name=="ed/base/External"&&m.name==name){vm.bind("ed/base/External",name,m.signature,std::move(fn));return;}
        throw jpc::Error(std::string("Missing original sound endpoint ")+name);};
    // These callbacks express unavailable hardware using the original failure result.
    for(const char* name:{"sndMgrGetSNDID","sndMgrActorPlaySound","sndMgrPlaySoundVolLoop","sndMgrPlaySoundVol"})bind(name,[](auto&,auto,const auto&){return I(-1);});
    for(const char* name:{"sndMgrVoiceSetFrequency","sndMgrStopVoice","rumbleP1"})bind(name,[](auto&,auto,const auto&){return jpc::Value{};});
    std::ifstream input(root/"ak09/scene.native",std::ios::binary);std::vector<uint8_t> native(std::istreambuf_iterator<char>(input),{});
    SourceActorRoutes::Host routeHost;routeHost.random=[&](int b){return random.bound(b);};SourceActorRoutes routes(vm,actors,native,routeHost);routes.bind();routes.actorCreated({4,0});
    vm.invoke(package.methodIndex("GenericNpc","resetToBehaviorImmediateBirth","(IZ)V"),enemy,{I(1),I(1)});
    bool parkEnemy=false;auto step=[&](){for(const auto& [key,unused]:actors.bodies()){(void)unused;const auto brain=actors.at(key).think;
            if(brain&&vm.status(brain)!=jpc::Interpreter::ThreadStatus::Complete){if(key.type==4&&key.index==0&&!parkEnemy){if(vm.status(brain)==jpc::Interpreter::ThreadStatus::Held)vm.awake(brain);}
            else if(vm.status(brain)!=jpc::Interpreter::ThreadStatus::Held)vm.hold(brain);}}
        vm.statics[398]=I(int32_t(++tick*33));actors.update(tick);routes.update();effects.update(tick);effects.sampleRender();};
    for(unsigned n=0;n<200&&actors.at({4,0}).actions.layers[0].state!=1;++n)step();assert(actors.at({4,0}).actions.layers[0].state==1);parkEnemy=true;for(unsigned n=0;n<40;++n)step();
    if(!fullHealth)std::cerr<<"full="<<fullHealth<<" HP="<<vm.field(enemy,13).integer()<<" flags="<<actors.at({4,0}).flags<<" state="<<actors.at({4,0}).actions.layers[0].state<<"\n";assert(fullHealth>0&&vm.field(enemy,13).integer()==100);
    const auto ash=source.game.ash;assert(vm.invoke(package.methodIndex("Ash","pickupItem","(II)Z"),ash,{I(3),I(12)}).integer());
    const auto inventory=vm.field(ash,98).reference();const auto gun=vm.invoke(package.methodIndex("Inventory","findItemObj","(I)LInventoryItem;"),inventory,{I(40)}).reference();assert(gun);
    assert(source.game.useSlot(vm.field(gun,34).integer(),98));const auto weapon=vm.field(ash,103).reference();assert(vm.field(weapon,812).integer()==40);
    assert(vm.invoke(package.methodIndex("Weapon","Reload","()I"),weapon).integer()>0);const int ammunition=vm.field(weapon,815).integer();for(unsigned n=0;n<90;++n)step();
    assert(actors.playerAction(22)==22);step();bool fired=false;
    for(unsigned n=0;n<180&&vm.field(enemy,13).integer()==100;++n){if(!fired){const int action=actors.playerAction(17);fired=action>=46&&action<=58;}step();}
    const int injuredHealth=vm.field(enemy,13).integer();assert(fired&&lines==1&&hits==1&&injuredHealth==85&&vm.field(weapon,815).integer()==ammunition-vm.field(weapon,819).integer());
    const auto oldVm=vm.snapshot(),oldActors=actors.snapshot(),oldRoutes=routes.snapshot(),oldEffects=effects.snapshot();const auto oldRandom=random;const auto oldTick=tick;
    consumed=emitted=0;
    auto run=[&](){for(unsigned n=0;n<300&&(!consumed||!emitted);++n)step();assert(consumed&&emitted&&vm.field(enemy,13).integer()==injuredHealth);return tick;};
    const auto end=run();const auto afterVm=vm.snapshot(),afterActors=actors.snapshot(),afterRoutes=routes.snapshot(),afterEffects=effects.snapshot();const auto afterRandom=random;const auto consumeCount=consumed,emitCount=emitted;
    vm.restore(oldVm);actors.restore(oldActors);routes.restore(oldRoutes);effects.restore(oldEffects);random=oldRandom;tick=oldTick;consumed=emitted=0;
    assert(run()==end&&consumed==consumeCount&&emitted==emitCount&&vm.snapshot()==afterVm&&actors.snapshot()==afterActors&&routes.snapshot()==afterRoutes&&effects.snapshot()==afterEffects&&random==afterRandom);
    std::cout<<"sourceNPC20 originalGun/equip/ammo/raw7/realhit HP100->85, fullHPnoRNG +injuredsingleRNG consume="<<consumed<<" emit="<<emitted<<", actual20particles and completeVM/native/VFX/sharedRNGreplay passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
