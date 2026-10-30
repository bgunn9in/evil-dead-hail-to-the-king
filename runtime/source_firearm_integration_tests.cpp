#include "gameplay_actors.h"
#include "source_firearm.h"
#include "source_actor_routes.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

static jpc::Value I(int n){return jpc::Value::integer(n);}
static void sourceWeapon(const std::filesystem::path& root,int item,int ammo,int damage,unsigned lineCount) {
    ScriptGame game(root/"scripts/levelA.jpc");game.enterScene("ak09");auto& vm=game.vm;const auto& p=game.package;
    auto bind=[&](const char* name,jpc::Interpreter::Native fn){for(const auto& m:p.methods)
        if(p.classes[size_t(m.owner)].name=="ed/base/External"&&m.name==name){vm.bind("ed/base/External",name,m.signature,std::move(fn));return;}
        throw jpc::Error(std::string("Missing firearm fixture endpoint: ")+name);};
    bind("guiItemSetPadObj",[](auto&,auto,const auto&){return jpc::Value{};});
    bind("rumbleP1",[](auto&,auto,const auto&){return jpc::Value{};});
    // Missing hardware audio uses its real failure return. No gameplay native is stubbed.
    for(const char* name:{"sndMgrGetSNDID","sndMgrActorPlaySound","sndMgrPlaySoundVolLoop","sndMgrPlaySoundVol"})
        bind(name,[](auto&,auto,const auto&){return I(-1);});
    for(const char* name:{"sndMgrVoiceSetFrequency","sndMgrStopVoice"})bind(name,[](auto&,auto,const auto&){return jpc::Value{};});
    edactors::GameplayActors* registry=nullptr;unsigned lines=0,posted=0;
    edactors::Host host;
    host.spawnPose=[](auto key,int,int,int){return edactors::Pose{0,0,key.type==0?0.f:1.5f,0};};
    host.floorHeight=[](float,float){return 0.f;};host.visibleArea=[](auto){return true;};
    host.facing=[](auto,auto,int){return true;};host.lineOfSight=[](auto,auto){return true;};
    host.lineOfSightDirection=[](auto,int,int){return false;};host.random=[](int){return 0;};
    host.rootMotionAllowed=[](auto,const auto&){return false;};host.effect=[](const auto&,const auto&){return 0;};
    host.soundMaterials=[](const auto&){return edanimsound::Materials{};};
    host.animationSound=[](const auto&,const auto&){}; // This HP/ammo fixture has no audio device; source SFX callback has its own PCM/JPC proof.
    host.pickupObjects=[](){return std::vector<edactors::PickupObject>{};};
    host.move=[](auto& body,int forward,int strafe,int vertical){edactormath::appendMovement(body.pendingMove,forward,strafe,vertical,{{0,body.pose.yaw,0}},body.motionFlags);return 1;};
    host.applyMovement=[](auto& body){const auto d=edactormath::pendingToWorld(body.pendingMove,{{0,body.pose.yaw,0}},body.motionFlags);
        body.pose.x+=d[0];body.pose.y+=d[1];body.pose.z+=d[2];body.localY+=d[1];};
    auto sourceActor=[](const edactors::Body& body){return edfirearm::Actor{body.key.type,body.key.index,body.nativeSlot,
        uint32_t(body.flags),{{body.pose.x,body.localY,body.pose.z}},body.pose.yaw,body.radius};};
    host.animationEvent=[&](const auto& body,const auto& raw){const auto event=edanimevent::decode(raw);
        if(event.kind==0||event.kind==6||event.kind==8||event.kind==9||event.kind==17||event.kind==20||event.kind==35)return; // Presentation has separate original raw/JPC/VFX proofs.
        assert(body.key.type==0&&event.kind==7&&event.frame==1&&event.enabled&&event.mode==-1);++lines;
        std::vector<edfirearm::Actor> source;for(const auto& [key,target]:registry->bodies()){(void)key;source.push_back(sourceActor(target));}
        for(const auto& hit:edfirearm::scan(sourceActor(body),source)){registry->post({{hit.targetType,hit.targetIndex},{hit.sourceType,hit.sourceIndex},hit.event,{{hit.selector,0,0,0}}});++posted;}
    };
    edactors::GameplayActors actors(game,root,host);registry=&actors;
    const auto ash=actors.initializeAsh(),enemy=actors.createDeadite();
    assert(vm.field(ash,13).integer()==100&&vm.field(enemy,13).integer()==100);
    assert(actors.at({0,0}).nativeSlot==0&&actors.at({4,0}).nativeSlot==1);
    assert(actors.at({0,0}).radius==80&&actors.at({4,0}).radius==30);
    for(const auto& [name,mask]:std::vector<std::pair<const char*,int>>{{"flagSubEngine",8},{"flagAddEngine",1},{"flagSubEngine",32}})
        vm.invoke(p.methodIndex("ed/base/EDActor",name,"(I)V"),ash,{I(mask)});
    std::ifstream file(root/"ak09/scene.native",std::ios::binary);std::vector<uint8_t> payload(std::istreambuf_iterator<char>(file),{});
    std::vector<bool> enabled(ScriptScene(payload).volumes.size(),true);
    SourceActorRoutes::Host routeHost;routeHost.camera=[](){return 0u;};routeHost.enabledVolumes=[&]()->const std::vector<bool>&{return enabled;};
    routeHost.random=[](int){return 0;};routeHost.collisionStatus=[](auto){return uint8_t(0);};
    SourceActorRoutes routes(vm,actors,payload,routeHost);routes.bind();routes.actorCreated({4,0});
    vm.invoke(p.methodIndex("GenericNpc","resetToBehaviorImmediateBirth","(IZ)V"),enemy,{I(1),I(1)});
    uint64_t tick=0;auto step=[&](){const auto thinker=actors.at({4,0}).think;
        // Park only this independent AI producer. Hit callbacks still execute the complete original dispatcher.
        if(thinker&&vm.status(thinker)!=jpc::Interpreter::ThreadStatus::Held)vm.hold(thinker);
        ++tick;vm.statics[398]=I(int(tick*1000/30));actors.update(tick);routes.update();};
    for(unsigned n=0;n<100&&actors.at({4,0}).actions.layers[0].state!=1;++n)step();
    assert(actors.at({4,0}).actions.layers[0].state==1);
    if(item!=40)assert(vm.invoke(p.methodIndex("Ash","pickupItem","(II)Z"),ash,{I(item),I(1)}).integer());
    assert(vm.invoke(p.methodIndex("Ash","pickupItem","(II)Z"),ash,{I(ammo),I(12)}).integer());
    const auto inventory=vm.field(ash,98).reference();const auto entry=vm.invoke(p.methodIndex("Inventory","findItemObj","(I)LInventoryItem;"),inventory,{I(item)}).reference();assert(entry);
    // InvWeapon.itemUse1292@44..52 passes false: its weapon items carry64,
    // for which the one-argument convenience method intentionally returns early.
    assert(vm.field(entry,35).integer()==0x56);
    assert(vm.invoke(p.methodIndex("Inventory","useItemSlot","(I)Z"),inventory,{vm.field(entry,34)}).integer());
    assert(vm.field(vm.field(ash,103).reference(),812).integer()==10); // The true overload intentionally did not equip.
    const auto rows=game.items(98);assert(std::any_of(rows.begin(),rows.end(),[&](const auto& row){return row.itemID==item&&row.slot==vm.field(entry,34).integer();}));
    assert(game.useSlot(vm.field(entry,34).integer(),98));
    const auto weapon=vm.field(ash,103).reference();assert(vm.field(weapon,812).integer()==item);
    const char* expectedClass=item==40?"Gun":item==41?"Shotgun":item==42?"Rifle":item==199?"PistolUpgrade":item==200?"ShotgunUpgrade":"RifleUpgrade";
    assert(vm.objectClass(weapon)==p.classIndex(expectedClass));
    assert(vm.invoke(p.methodIndex("Weapon","Reload","()I"),weapon).integer()>0);const int ammunition=vm.field(weapon,815).integer();
    for(unsigned n=0;n<70;++n)step(); // Original weapon switch tail + real cooldown clock.
    assert(actors.playerAction(22)==22);step();
    const auto savedVm=vm.snapshot(),savedActors=actors.snapshot(),savedRoutes=routes.snapshot();const auto savedTick=tick;
    unsigned accepted=0;uint64_t hitTick=0;
    for(unsigned replay=0;replay<2;++replay) {
        if(replay){vm.restore(savedVm);actors.restore(savedActors);routes.restore(savedRoutes);tick=savedTick;lines=posted=accepted=0;}
        for(unsigned n=0;n<160&&!lines;++n){if(!accepted){const int action=actors.playerAction(17);if(action>=46&&action<=58)accepted=unsigned(action);}step();}
        assert(accepted&&lines==lineCount&&posted==lineCount);
        assert(vm.field(weapon,815).integer()==ammunition-vm.field(weapon,819).integer());
        for(unsigned n=0;n<120&&vm.field(enemy,13).integer()==100;++n)step();
        assert(vm.field(enemy,13).integer()==100-damage);assert(vm.field(ash,13).integer()==100);
        if(!replay)hitTick=tick;else assert(tick==hitTick);
    }
    std::cout<<"source weapon "<<item<<" action "<<accepted<<" raw7="<<lines<<" HP100->"<<vm.field(enemy,13).integer()<<" ammo "<<ammunition<<"->"<<vm.field(weapon,815).integer()<<" hitTick="<<hitTick<<" save/replay: passed\n";
}
int main(int argc,char** argv){try{if(argc!=2)throw std::runtime_error("Expected generated asset root");
    sourceWeapon(argv[1],40,3,15,1);sourceWeapon(argv[1],41,5,30,3);sourceWeapon(argv[1],42,7,20,1);
    sourceWeapon(argv[1],199,3,20,1);sourceWeapon(argv[1],200,5,40,3);sourceWeapon(argv[1],201,7,25,1);
    return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
