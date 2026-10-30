#include "actor_startup_fixture.h"

static void fullProfile(StartupCase& b,const std::filesystem::path& aAssets) {
    auto& vm=b.game.vm;auto& actors=*b.actors;assert(actors.supportedTypes().size()==14);
    std::ifstream input(aAssets/"ak09/scene.native",std::ios::binary);const std::vector<uint8_t> scene(std::istreambuf_iterator<char>(input),{});
    SourceActorRoutes routes(vm,actors,scene);routes.bind(); // Original geometry fixture; these checks use only the five local memory slots.
    unsigned models=0;
    for(const auto& model:edactors::Models)if((model.levels&2)&&model.type!=0&&model.type!=2&&model.type!=4) {
        if(model.type==38)b.game.enterScene("bg08"); // Captain.initialize writes its own reference into actual SceneBG08.field1600.
        const int selector=model.mesh==0x06360000?36:model.mesh==0x06362000?37:model.type;
        const auto ref=actors.createType(selector);assert(ref);const edactors::Key key{model.type,vm.field(ref,5).integer()};
        assert(vm.field(ref,6).integer()==model.type&&vm.objectClass(ref)==b.game.package.classIndex(model.scriptClass));
        const auto& body=actors.at(key);assert(body.meshResource==model.mesh&&body.textureID==model.texture&&!body.vertices.empty());
        routes.actorCreated(key);const auto saved=actors.snapshot();actors.restore(saved);routes.restore(routes.snapshot());
        assert(actors.at(key).meshResource==model.mesh&&actors.snapshot()==saved);++models;
    }
    assert(models==13&&actors.bodies().count({31,0})&&actors.bodies().count({31,1})&&actors.bodies().count({31,2}));
    assert(actors.at({31,0}).meshResource==0x0635e000&&actors.at({31,1}).meshResource==0x06360000&&actors.at({31,2}).meshResource==0x06362000);
    for(int type:actors.supportedTypes())if(type!=0&&type!=2&&type!=4)
        while(!actors.bodies().count({type,edactors::actorCapacity(type)-1}))assert(actors.createType(type));
    assert(actors.bodies().size()==32);const auto savedVm=vm.snapshot(),savedNative=actors.snapshot(),savedRoutes=routes.snapshot();
    vm.restore(savedVm);actors.restore(savedNative);routes.restore(savedRoutes);assert(actors.snapshot()==savedNative);
    auto invalid=actors.cloneState();invalid.bodies[0].meshResource=0x00abcdef;bool rejected=false;
    try{actors.restore(invalid);}catch(const jpc::Error&){rejected=true;}assert(rejected&&actors.snapshot()==savedNative);
    vm.invoke(b.game.package.methodIndex("ActorDB","reinitialize","()V"));const auto removed=actors.unloadScene();routes.forgetActors(removed);
    actors.resetSceneRuntime();assert(removed.size()==23&&actors.bodies().size()==9);
    for(const auto& [key,body]:actors.bodies()){(void)key;assert(body.script);}
    vm.restore(vm.snapshot());actors.restore(actors.snapshot());routes.restore(routes.snapshot());
    std::cout<<"Original B factories:14native types/16models/32bodies, true31variants, mesh-aware save/rejection and source639 arena teardown: passed\n";
}
int main(int argc,char** argv){try {
    if(argc!=3)throw jpc::Error("Pass LevelA asset root and B-startup asset root");
    const std::filesystem::path source=argv[1],target=argv[2];
    StartupCase a(source/"scripts/levelA.jpc",source,false),b(target/"scripts/levelB.jpc",target,true);
    ScriptGame::PlayerTransferBuffer transfer;const auto cursor=a.game.savePlayerTransfer(transfer);
    assert(cursor.bytesTouched()>0);const auto restored=b.game.restorePlayerTransfer(transfer);b.actors->sync();
    assert(restored.fullBytes==cursor.fullBytes&&restored.partialBits==cursor.partialBits&&b.game.vm.field(b.game.ash,13).integer()==100);
    assert(b.actors->bodies().size()==9&&b.actors->at({0,0}).script==b.game.ash);
    const auto native=b.actors->snapshot(),vm=b.game.vm.snapshot();b.game.vm.restore(vm);b.actors->restore(native);
    assert(b.actors->snapshot()==native);
    fullProfile(b,source);
    std::cout<<"Original A/B Main.initialize/createLevel: nine real actors/brains/GUI/parts, B source pool40/0/4, native save and actual player transfer: passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
