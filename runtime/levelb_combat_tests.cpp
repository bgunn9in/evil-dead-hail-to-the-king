#include "actor_startup_fixture.h"

static void armoryValvePuzzle(const std::filesystem::path& aAssets,const std::filesystem::path& bAssets) {
    StartupCase b(aAssets/"scripts/levelB.jpc",bAssets,true);auto& game=b.game;auto& vm=game.vm;const auto& p=game.package;auto& actors=*b.actors;
    game.enterScene("bg12");vm.invoke(p.methodIndex("SceneCommander","sceneReset","()V"));
    std::ifstream input(bAssets/"bg12/scene.native",std::ios::binary);const std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(input),{});
    const ScriptScene scene(bytes);SourceActorRoutes routes(vm,actors,bytes);routes.bind();
    auto bind=[&](const char* name,jpc::Interpreter::Native native){for(const auto& m:p.methods)
        if(p.classes[size_t(m.owner)].name=="ed/base/External"&&m.name==name){vm.bind("ed/base/External",name,m.signature,std::move(native));return;}
        throw jpc::Error(std::string("Missing puzzle endpoint: ")+name);};
    bind("actorInTrigger",[&](auto&,auto,const auto& a){const auto& body=actors.at({a[0].integer(),a[1].integer()});
        return I(scene.inside(unsigned(a[2].integer()),{body.pose.x,body.localY,body.pose.z}));});
    std::vector<std::pair<int,int>> movies;unsigned particles=0,switched=0;
    bind("rumbleP1",[](auto&,auto,const auto&){return jpc::Value{};});
    bind("moviePlay",[&](auto&,auto,const auto& a){movies.emplace_back(a[0].integer(),a[1].integer());return jpc::Value{};});
    bind("vfxSpawnMarkerOffset",[&](auto&,auto,const auto&){++particles;return jpc::Value{};}); // Presentation has independent decoder/renderer proofs.
    bind("sceneSwitch",[&](auto&,auto,const auto& a){assert(a.size()==4);++switched;return jpc::Value{};});
    const auto boss=actors.createType(29);const edactors::Key key{29,0};routes.actorCreated(key);
    vm.invoke(p.methodIndex("SceneBG12","spawnMonster1","(LGenericNpc;III)V"),game.scene,{jpc::Value::reference(boss),I(0),I(0),I(0)});
    assert(vm.field(game.scene,1605).reference()==boss&&vm.field(boss,13).integer()==100);
    bool park=true;uint64_t tick=0;auto step=[&](){auto& body=actors.at(key);if(body.think&&vm.status(body.think)!=jpc::Interpreter::ThreadStatus::Complete){
            if(park&&vm.status(body.think)!=jpc::Interpreter::ThreadStatus::Held)vm.hold(body.think);
            if(!park&&vm.status(body.think)==jpc::Interpreter::ThreadStatus::Held)vm.awake(body.think);}
        vm.statics[398]=I(int32_t(++tick*33));actors.update(tick);};
    auto center=[&](unsigned n){GroundPoint result{};for(const auto& point:scene.volumes.at(n).points){result.x+=point.x;result.y+=point.y;result.z+=point.z;}
        const float count=float(scene.volumes.at(n).points.size());return GroundPoint{result.x/count,result.y/count,result.z/count};};
    auto place=[&](edactors::Key identity,unsigned volume){const auto pos=center(volume);auto& body=actors.at(identity);body.pose={pos.x,pos.y,pos.z,0};body.localY=pos.y;
        assert(scene.inside(volume,pos));};
    auto valve=[&](unsigned playerVolume,unsigned bossVolume){place({0,0},playerVolume);place(key,bossVolume);assert(game.interact()==117);};
    for(unsigned n=0;n<3;++n)step();
    valve(11,23);assert(movies.size()==1&&vm.field(game.scene,1613).integer()==1&&vm.field(boss,13).integer()==100);
    valve(11,23);for(unsigned n=0;n<100;++n)step();
    assert(vm.field(game.scene,1611).integer()==1&&vm.field(boss,13).integer()==100&&vm.field(boss,1461).integer()==0);
    valve(10,22);assert(movies.size()==2&&vm.field(game.scene,1612).integer()==1&&vm.field(boss,13).integer()==100);
    valve(10,22);const auto savedVm=vm.snapshot(),savedActors=actors.snapshot(),savedRoutes=routes.snapshot();const auto savedTick=tick;
    for(unsigned n=0;n<120;++n)step();assert(vm.field(boss,13).integer()==50&&vm.field(game.scene,1611).integer()==0);
    const auto replayTick=tick;vm.restore(savedVm);actors.restore(savedActors);routes.restore(savedRoutes);tick=savedTick;
    for(unsigned n=0;n<120;++n)step();assert(tick==replayTick&&vm.field(boss,13).integer()==50);
    // Steam without a preceding molten action executes the source non-damaging branch.
    valve(10,22);for(unsigned n=0;n<120;++n)step();assert(vm.field(boss,13).integer()==50);
    valve(11,23);for(unsigned n=0;n<100;++n)step();assert(vm.field(boss,13).integer()==50&&vm.field(game.scene,1611).integer()==1);
    valve(10,22);for(unsigned n=0;n<120&&vm.field(boss,13).integer();++n)step();assert(vm.field(boss,13).integer()==0);
    park=false;for(unsigned n=0;n<750&&vm.field(boss,1251).integer()!=1;++n)step();
    assert(vm.field(boss,1251).integer()==1&&!(actors.at(key).flags&1));
    vm.invoke(p.methodIndex("SceneBG12","processFrame","()V"),game.scene);
    assert(vm.staticField(610).integer()==1&&switched==1&&movies.size()==3&&particles>0);
    const auto finalVm=vm.snapshot(),finalActors=actors.snapshot(),finalRoutes=routes.snapshot();vm.restore(finalVm);actors.restore(finalActors);routes.restore(finalRoutes);
    std::cout<<"ArmoryBoss original BG12 valve geometry/first movies/molten+steam50HP/negative/pending replay/death610/sceneSwitch: passed\n";
}

static void combat(const std::filesystem::path& aAssets,const std::filesystem::path& bAssets,int selector,int type,const char* scene,const char* owner,int damage,bool protectedTarget=false) {
    StartupCase b(aAssets/"scripts/levelB.jpc",bAssets,true);auto& game=b.game;auto& vm=game.vm;const auto& p=game.package;auto& actors=*b.actors;
    game.enterScene(scene);vm.invoke(p.methodIndex("SceneCommander","sceneReset","()V"));
    const auto npc=actors.createType(selector);assert(npc);const edactors::Key key{type,0};
    vm.invoke(p.methodIndex("GenericNpc","resetToBehaviorImmediateBirth","(IZ)V"),npc,{I(1),I(1)});
    actors.at(key).pose={0,0,type==38?2.2f:type==29?1.4f:1.f,edactormath::Pi};actors.at(key).localY=0;
    std::ifstream input(aAssets/"ak09/scene.native",std::ios::binary);const std::vector<uint8_t> sceneBytes(std::istreambuf_iterator<char>(input),{});
    SourceActorRoutes routes(vm,actors,sceneBytes);routes.bind();routes.actorCreated(key);
    auto& body=actors.at(key);body.think=vm.enqueue(p.methodIndex("ActorDB","think","(II)V"),0,{I(type),I(0)});
    vm.invoke(p.methodIndex("ActorDB","setThinkThreadId","(III)V"),0,{I(type),I(0),I(int32_t(body.think))});vm.hold(body.think);
    uint64_t tick=0;bool park=true;b.randomChoice=type==31?3:type==38?1:0;
    auto step=[&](bool press=false){const auto thinker=actors.at(key).think;if(thinker&&vm.status(thinker)!=jpc::Interpreter::ThreadStatus::Complete){
            if(park&&vm.status(thinker)!=jpc::Interpreter::ThreadStatus::Held)vm.hold(thinker);
            if(!park&&vm.status(thinker)==jpc::Interpreter::ThreadStatus::Held)vm.awake(thinker);}
        if(press)actors.playerAction(17);vm.statics[398]=I(int32_t(++tick*33));actors.update(tick);};
    for(unsigned n=0;n<3;++n)step();const int hp=vm.field(game.ash,13).integer();
    const auto attack=vm.start(p.methodIndex(owner,"actionAttack","()V"),npc);const auto beforeVm=vm.snapshot(),beforeActors=actors.snapshot(),beforeRoutes=routes.snapshot();const auto beforeTick=tick;
    for(unsigned n=0;n<240&&vm.status(attack)!=jpc::Interpreter::ThreadStatus::Complete;++n)step();
    if(vm.status(attack)!=jpc::Interpreter::ThreadStatus::Complete||vm.field(game.ash,13).integer()!=hp-damage)
        throw jpc::Error(std::string(owner)+" attack fixture: status="+std::to_string(int(vm.status(attack)))+" HP="+std::to_string(vm.field(game.ash,13).integer())+
            " state="+std::to_string(actors.at(key).actions.layers[0].state)+" guard="+std::to_string(vm.staticField(394).integer())+" invuln="+std::to_string(vm.staticField(60).integer())+
            " distance="+std::to_string(vm.invoke(p.methodIndex("ed/base/EDNpc","distanceToTarget","()I"),npc).integer())+" min="+std::to_string(vm.field(npc,1330).integer())+" max="+std::to_string(vm.field(npc,1328).integer())+
            " facing="+std::to_string(vm.invoke(p.methodIndex("ed/base/EDNpc","facingTarget","(I)Z"),npc,{I(70)}).integer())+" npcHP="+std::to_string(vm.field(npc,13).integer()));
    const auto hitTick=tick;
    vm.restore(beforeVm);actors.restore(beforeActors);routes.restore(beforeRoutes);tick=beforeTick;
    for(unsigned n=0;n<240&&vm.status(attack)!=jpc::Interpreter::ThreadStatus::Complete;++n)step();
    assert(tick==hitTick&&vm.field(game.ash,13).integer()==hp-damage);vm.forget(attack);
    for(unsigned n=0;n<90&&vm.field(game.ash,11).integer();++n)step();
    if(protectedTarget) {
        const int health=vm.field(npc,13).integer();for(unsigned n=0;n<200;++n)step(true);
        assert(vm.field(npc,13).integer()==health);
        std::cout<<owner<<": original attack"<<damage<<"HP and source protected/friendly incoming-hit path, no fabricated damage: passed\n";return;
    }
    for(unsigned n=0;n<5000&&vm.field(npc,13).integer()>0;++n)step(true);
    assert(vm.field(npc,13).integer()==0);park=false;
    for(unsigned n=0;n<750&&vm.field(npc,1251).integer()!=1;++n)step();
    assert(vm.field(npc,1251).integer()==1&&!(actors.at(key).flags&1));
    const auto doneVm=vm.snapshot(),doneActors=actors.snapshot(),doneRoutes=routes.snapshot();vm.restore(doneVm);actors.restore(doneActors);routes.restore(doneRoutes);
    assert(actors.snapshot()==doneActors);
    std::cout<<owner<<" selector"<<selector<<": real attack"<<damage<<"HP/native end/replay, source input17 damage/death/save: passed\n";
}
int main(int argc,char** argv){try{
    if(argc!=3)throw jpc::Error("Pass A and B asset roots");
    combat(argv[1],argv[2],23,23,"bl06","GuildMember",5);
    combat(argv[1],argv[2],28,28,"bg03","ArmorySkeleton",10);
    for(int selector:{31,36,37})combat(argv[1],argv[2],selector,31,"ba11","NetherPet",4);
    combat(argv[1],argv[2],39,39,"ba02","PumpKeeper",15);
    combat(argv[1],argv[2],19,19,"bc01","BadAsh",20);
    combat(argv[1],argv[2],26,26,"bt11","WizardBoss",15);
    combat(argv[1],argv[2],27,27,"ba04","MudGolem",15);
    combat(argv[1],argv[2],38,38,"bg08","Captain",15);
    combat(argv[1],argv[2],29,29,"bg12","ArmoryBoss",15,true);
    combat(argv[1],argv[2],24,24,"bm11","Merchant",0,true);
    combat(argv[1],argv[2],33,33,"bc02","Cthulhu",0,true);
    armoryValvePuzzle(argv[1],argv[2]);
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
