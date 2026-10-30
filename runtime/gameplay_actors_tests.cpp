#include "actor_startup_fixture.h"
#include "source_actor_math.h"
#include "source_actor_routes.h"
#include "source_gui.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

template<class Fn> static void rejects(Fn fn,const char* expected) {
    bool rejected=false;try{fn();}catch(const jpc::Error& error){rejected=true;assert(std::string(error.what()).find(expected)!=std::string::npos);}assert(rejected);
}
static jpc::Ref visibleAsh(edactors::GameplayActors& actors,ScriptGame& game) {
    const auto ash=actors.initializeAsh();
    for(const auto& [name,mask]:std::vector<std::pair<const char*,int>>{{"flagSubEngine",8},{"flagAddEngine",1},{"flagSubEngine",32}})
        game.vm.invoke(game.package.methodIndex("ed/base/EDActor",name,"(I)V"),ash,{I(mask)});
    return ash;
}
static void rootMotionTests(const std::filesystem::path& root,edactors::Host host) {
    ScriptGame game(root/"scripts"/"levelA.jpc");std::array<float,3> moved{};bool enabled=true;unsigned floorQueries=0;
    host.floorHeightForActor=[&](auto key,float,float,int32_t& cache){assert(key.type==0);++floorQueries;cache=7;return 0.0f;};
    host.rootMotionAllowed=[&](auto,const auto&){return enabled;};
    host.applyMovement=[&](auto& body){moved=edactormath::pendingToWorld(body.pendingMove,{{0,body.pose.yaw,0}},body.motionFlags);};
    edactors::GameplayActors actors(game,root,host);visibleAsh(actors,game);
    assert(actors.at({0,0}).floorTriangle==-1&&floorQueries==0);
    actors.sampleFloors();assert(actors.at({0,0}).floorTriangle==7&&floorQueries==1);
    assert(actors.consoleAction({0,0},3));uint64_t tick=0;auto& body=actors.at({0,0});
    for(unsigned n=0;n<100&&body.actions.playbacks[size_t(body.actions.layers[0].playback)].mode==4;++n)actors.update(++tick);
    auto& playback=body.actions.playbacks[size_t(body.actions.layers[0].playback)];assert(playback.mode!=4);playback.frame=0;playback.mode=1;
    actors.update(++tick);
    // Independent original curve samples: root[0] and root[1], source40199d X sign.
    assert(std::abs(moved[0]-(-0.008721612393856049f))<1e-6f&&std::abs(moved[2]-0.008739763870835304f)<1e-6f);
    playback.frame=0;playback.mode=1;
    body.pose.yaw=1.5707963267948966f;actors.update(++tick);
    assert(std::abs(moved[0]-0.008739763870835304f)<1e-6f&&std::abs(moved[2]-0.008721612393856049f)<1e-6f);
    playback.frame=0;playback.mode=1;body.motionFlags=2;actors.update(++tick);
    assert(std::abs(moved[0]-(-0.008721612393856049f))<1e-6f&&std::abs(moved[2]-0.008739763870835304f)<1e-6f);
    body.motionFlags=6;
    enabled=false;actors.update(++tick);assert((moved==std::array<float,3>{}));enabled=true;body.pose.yaw=0;
    playback.frame=32;playback.mode=2;actors.update(++tick);assert((moved==std::array<float,3>{}));
    assert(actors.consoleAction({0,0},4));actors.update(++tick);
    assert(std::abs(moved[0]-0.005048274993896484f)<1e-6f&&std::abs(moved[2]-(-0.006720423698425293f))<1e-6f);
    body.motionFlags=2;body.motionFlags2=6;const auto saved=actors.snapshot();body.motionFlags=6;body.motionFlags2=0;actors.restore(saved);
    assert(actors.at({0,0}).motionFlags==2&&actors.at({0,0}).motionFlags2==6&&actors.at({0,0}).floorTriangle==7);
    assert(floorQueries==1); // Physics/VM never performs the source render-phase BV query.
    auto badFloor=actors.cloneState();badFloor.bodies[0].floorTriangle=-2;rejects([&]{actors.restore(badFloor);},"actor identity");
    std::cout<<"source root-XZ curves/yaw/loop wrap/outgoing tail/modern-locomotion policy: passed\n";
}
static void turnCallbackTests(const std::filesystem::path& root,edactors::Host host) {
    for(bool global:{false,true}) {
        ScriptGame game(root/"scripts/levelA.jpc");int amount=8;unsigned reads=0,rootChecks=0;float yawAtPhysics=-1;bool checkRoot=false,callbackSeen=false;
        host.turnAmount=[&]{++reads;callbackSeen=true;return amount;};
        host.rootMotionAllowed=[&](auto,const auto&){if(checkRoot){++rootChecks;assert(callbackSeen);}return true;};
        host.applyMovement=[&](auto& body){if(body.key.type==0)yawAtPhysics=body.pose.yaw;};
        edactors::GameplayActors actors(game,root,host,global);visibleAsh(actors,game);auto& body=actors.at({0,0});
        assert(body.actions.states.at(140).callback==1&&!body.actions.states.at(140).animation);
        assert(body.actions.states.at(141).callback==2&&!body.actions.states.at(141).animation);
        assert(actors.consoleAction({0,0},9));assert(std::any_of(body.actions.layers.begin(),body.actions.layers.end(),[](const auto& layer){return layer.state==140;}));
        uint64_t tick=0;actors.update(++tick);const float eight=8.f*.01745329238474369f;
        assert(body.pose.yaw==eight&&yawAtPhysics==eight&&reads==1); //One callback even without animation playback.
        auto before=actors.snapshot();const auto vm=game.vm.snapshot();amount=3;actors.update(++tick);
        assert(body.pose.yaw==float(eight+3.f*.01745329238474369f));const auto future=actors.snapshot();
        game.vm.restore(vm);actors.restore(before);actors.update(tick);assert(actors.snapshot()==future);
        assert(actors.consoleAction({0,0},10));const auto stopped=actors.at({0,0}).pose.yaw;const auto stoppedReads=reads;actors.update(++tick);
        assert(actors.at({0,0}).pose.yaw==stopped&&reads==stoppedReads); //Idle callback10 remains a no-op.
        assert(actors.consoleAction({0,0},7));assert(std::any_of(actors.at({0,0}).actions.layers.begin(),actors.at({0,0}).actions.layers.end(),[](const auto& layer){return layer.state==141;}));
        amount=8;actors.at({0,0}).pose.yaw=0;actors.update(++tick);
        assert(actors.at({0,0}).pose.yaw==edactormath::normalizeAngle(-eight));
        const auto frozen=actors.at({0,0}).pose.yaw;const auto readsBefore=reads;
        actors.update(++tick,false);assert(actors.at({0,0}).pose.yaw==frozen&&reads==readsBefore);
        actors.at({0,0}).flags|=0x40;actors.update(++tick);
        assert(actors.at({0,0}).pose.yaw==frozen&&reads==readsBefore);
        actors.at({0,0}).flags&=~0x40;assert(actors.consoleAction({0,0},8));
        assert(actors.consoleAction({0,0},3));assert(actors.consoleAction({0,0},9));
        checkRoot=true;callbackSeen=false;actors.at({0,0}).pose.yaw=edactormath::Tau-.05f;actors.update(++tick);
        assert(actors.at({0,0}).pose.yaw>0&&actors.at({0,0}).pose.yaw<.1f);
        for(unsigned n=0;n<5;++n){callbackSeen=false;actors.update(++tick);}assert(rootChecks>0);
        amount=0;const auto zero=actors.at({0,0}).pose.yaw;actors.update(++tick);assert(actors.at({0,0}).pose.yaw==zero);
        for(int invalid:{-1,13}){amount=invalid;rejects([&]{actors.update(++tick);},"Invalid native turn amount");assert(actors.at({0,0}).pose.yaw==zero);}
    }
    //B's two independently allocated NPC classes share the same original callback dispatcher.
    const auto b=root/"levelB";int amount=5;unsigned reads=0;
    StartupCase fixture(b/"scripts/levelB.jpc",b,true,[&](edactors::Host& sourceHost){
        sourceHost.turnAmount=[&]{++reads;return amount;};sourceHost.applyMovement=[](auto&){};
    });
    auto& actors=*fixture.actors;auto& game=fixture.game;
    for(int type:{28,29}) {
        const auto npc=actors.createType(type);assert(npc);auto& body=actors.at({type,0});
        game.vm.invoke(game.package.methodIndex("ed/base/EDActor","flagAddEngine","(I)V"),npc,{I(1)});
        const auto found=std::find_if(body.actions.states.begin(),body.actions.states.end(),[](const auto& state){return state.callback==1;});
        assert(found!=body.actions.states.end());body.actions.layers[0].state=int(found-body.actions.states.begin());
        body.actions.layers[0].playback=-1;body.pose.yaw=0;
    }
    actors.update(1,true,1);const auto five=5.f*.01745329238474369f;
    assert(actors.at({28,0}).pose.yaw==five&&actors.at({29,0}).pose.yaw==five&&reads==2);
    for(int type:{28,29}) {auto& body=actors.at({type,0});
        const auto found=std::find_if(body.actions.states.begin(),body.actions.states.end(),[](const auto& state){return state.callback==2;});
        assert(found!=body.actions.states.end());body.actions.layers[0].state=int(found-body.actions.states.begin());body.pose.yaw=0;}
    actors.update(2,true,1);
    assert(actors.at({28,0}).pose.yaw==edactormath::normalizeAngle(-five)&&actors.at({29,0}).pose.yaw==edactormath::normalizeAngle(-five)&&reads==4);
    std::cout<<"native callback1/2: exact sign/f32 yaw, active no-clip layers, phase/pause/freeze, A-save future and B NPCs: passed\n";
}
static void historicalActiveChildTests(const std::filesystem::path& root,edactors::Host host) {
    ScriptGame game(root/"scripts/levelA.jpc");edactors::GameplayActors actors(game,root,host);visibleAsh(actors,game);
    uint64_t tick=0;
    // Original Ash0/13 table, one positive native tick per genuine Run/Stop command.
    // No World dt0/facing fixture or manually edited playback creates this alias.
    for(int action:{5,6,5,6}){assert(actors.consoleAction({0,0},action));actors.update(++tick);}
    const auto state=actors.cloneState();const auto& ash=state.bodies.front();
    const unsigned waiting=unsigned(ash.layers[0].playback);const auto& child=ash.playbacks[waiting];
    assert(child.active&&child.mode==4&&child.parent>=0&&ash.playbacks[size_t(child.parent)].child==int(waiting));
    int historical=-1;
    for(unsigned n=0;n<50;++n)if(ash.playbacks[n].active&&ash.playbacks[n].child==int(waiting)&&int(n)!=child.parent)historical=int(n);
    assert(historical>=0&&(state.playbackPool[waiting].owner==edactions::PlaybackOwner{0,0}));
    const auto savedVM=game.vm.snapshot(),saved=actors.snapshot();actors.restore(saved);assert(actors.snapshot()==saved);
    for(unsigned n=0;n<24;++n)actors.update(++tick);const auto afterVM=game.vm.snapshot(),after=actors.snapshot();
    game.vm.restore(savedVM);actors.restore(saved);tick=state.sourceTick;
    for(unsigned n=0;n<24;++n)actors.update(++tick);assert(game.vm.snapshot()==afterVM&&actors.snapshot()==after);
    game.vm.restore(savedVM);actors.restore(saved);
    auto bad=state;bad.bodies.front().playbacks[waiting].parent=-1;
    rejects([&]{actors.restore(bad);},"waiting playback parent");assert(actors.snapshot()==saved);
    bad=state;bad.bodies.front().playbacks[size_t(historical)].child=50;
    rejects([&]{actors.restore(bad);},"actor playback");assert(actors.snapshot()==saved);
    bad=state;bad.playbackPool[waiting].owner={4,0};
    rejects([&]{actors.restore(bad);},"owner");assert(actors.snapshot()==saved);
    bad=state;bad.bodies.front().playbacks[waiting].child=historical;
    rejects([&]{actors.restore(bad);},"cyclic");assert(actors.snapshot()==saved);
    std::cout<<"Original Run/Stop four-positive-tick active historical-child alias, exact VM replay and waiting/bounds/owner/cycle atomic rejection: passed\n";
}
static void floorPhaseTests(const std::filesystem::path& root,edactors::Host host) {
    ScriptGame game(root/"scripts"/"levelA.jpc");unsigned queries=0;float floor=1.25f;
    host.floorHeightForActor=[&](auto,float,float,int32_t& cache){++queries;cache=9;return floor;};
    edactors::GameplayActors actors(game,root,host);visibleAsh(actors,game);auto& body=actors.at({0,0});
    assert(queries==0&&body.cachedFloorHeight==0&&body.localY==0&&body.pose.y==0);
    actors.sampleFloors();assert(queries==1&&body.cachedFloorHeight==1.25f&&body.pose.y==1.25f);
    floor=2.5f;game.vm.invoke(game.package.methodIndex("ed/base/EDActor","setPositionFloorHeight","(I)V"),game.ash,{I(50)});
    assert(queries==1&&body.localY==.5f&&body.pose.y==1.75f);
    body.flags|=0x40;actors.sampleFloors();assert(queries==2&&body.pose.y==3&&body.localY==.5f);
    body.flags|=8;actors.sampleFloors();assert(queries==2);body.flags&=~9;actors.sampleFloors();assert(queries==2);
    body.flags|=1;body.flags&=~8;const auto saved=actors.snapshot();body.cachedFloorHeight=17;actors.restore(saved);
    assert(actors.at({0,0}).cachedFloorHeight==2.5f&&actors.at({0,0}).floorTriangle==9);
    auto bad=actors.cloneState();bad.bodies[0].cachedFloorHeight=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{actors.restore(bad);},"Non-finite");assert(actors.snapshot()==saved);
    bad=actors.cloneState();bad.bodies[0].cachedFloorHeight+=.125f;
    rejects([&]{actors.restore(bad);},"cached floor/world");assert(actors.snapshot()==saved);
    std::cout<<"source render-phase actor BV cache/localY/visibility/freeze/save semantics: passed\n";
}
static void menuPhaseTests(const std::filesystem::path& root,edactors::Host host) {
    ScriptGame game(root/"scripts/levelA.jpc");game.enterScene("ak09");unsigned physics=0,materials=0,floors=0;
    game.vm.bind("ed/base/External","guiItemSetPadObj","(ILed/base/EDBase;)V",[](auto&,auto,const auto&){return jpc::Value{};});
    const auto movement=host.applyMovement;host.applyMovement=[&](auto& body){++physics;movement(body);};
    host.soundMaterials=[&](const auto&){++materials;return edanimsound::Materials{};};
    host.floorHeightForActor=[&](auto,float,float,int32_t&){++floors;return .25f;};
    edactors::GameplayActors actors(game,root,host);const auto ash=visibleAsh(actors,game);auto& body=actors.at({0,0});
    actors.sampleFloors();assert(floors==1);
    const auto timer=game.vm.start(game.package.methodIndex("ed/base/External","threadSleepFrame","(I)V"),0,{I(0)});
    game.vm.invoke(game.package.methodIndex("ed/base/External","actorMoveForward","(III)I"),0,{I(0),I(0),I(10)});
    const auto before=body.pose;const auto pending=body.pendingMove;
    // Message callbacks still start immediately while animation/physics and their continuations are paused.
    actors.post({{0,0},{4,0},36,{{20,0,0,0}}});actors.update(1,false,1);
    assert(game.vm.field(ash,13).integer()==80&&game.vm.field(ash,11).integer()==1);
    const auto pausedUpdates=body.actions.updates;
    for(uint64_t tick=2;tick<=5;++tick)actors.update(tick,false,1);
    assert(body.actions.updates==pausedUpdates&&body.pendingMove==pending&&body.pose.x==before.x&&body.pose.z==before.z);
    assert(!physics&&!materials&&!body.think&&game.vm.status(timer)==jpc::Interpreter::ThreadStatus::Waiting);
    actors.sampleFloors();assert(floors==2); // Render-phase BV remains active in a menu.
    const auto vmSave=game.vm.snapshot(),actorSave=actors.snapshot();
    actors.update(6);assert(physics==1&&materials==1&&body.actions.updates==pausedUpdates+1);
    assert((game.vm.status(timer)==jpc::Interpreter::ThreadStatus::Complete&&body.pendingMove==std::array<float,3>{}));
    const auto resumedVm=game.vm.snapshot(),resumedActors=actors.snapshot();
    game.vm.restore(vmSave);actors.restore(actorSave);actors.update(6);
    assert(game.vm.snapshot()==resumedVm&&actors.snapshot()==resumedActors&&game.vm.field(ash,13).integer()==80);
    auto wrongOwner=actors.cloneState();const auto gui=game.vm.enqueue(game.package.methodIndex("ActorDB","think","(II)V"),0,{I(0),I(0)},0);
    game.vm.setField(ash,10,I(int(gui)));wrongOwner.bodies[0].think=gui;
    rejects([&]{actors.restore(wrongOwner);},"think thread flags");
    game.vm.restore(resumedVm);actors.restore(resumedActors);
    game.vm.forget(timer);
    std::cout<<"source menu phases: message dispatch/HP once, masked continuations, preserved movement/animation, live BV, exact save-resume: passed\n";
}
static void rotationSaveTests(const std::filesystem::path& root,edactors::Host host) {
    ScriptGame game(root/"scripts/levelA.jpc");edactors::GameplayActors actors(game,root,host);const auto ash=visibleAsh(actors,game);
    auto& vm=game.vm;const auto& p=game.package;
    // Existing World owns this native setter; fixture reproduces405370/401BF0 without replacing its binding in production.
    vm.bind("ed/base/External","actorFace","(III)V",[&](auto&,auto,const auto& a){
        actors.at({a[0].integer(),a[1].integer()}).pose.yaw=edactormath::modelYawFromHeading(float(double(a[2].integer())*.01745329238474369f));return jpc::Value{};});
    const auto stream=vm.makeArray('B',std::vector<jpc::Value>(8,I(0)));
    const auto getter=p.methodIndex("ed/base/EDActor","getRotY","()I"),init=p.methodIndex("ed/base/EDBase","initBitStream","([B)V");
    for(const auto& [yaw,degrees]:std::vector<std::pair<float,int>>{{0.f,0},{edactormath::HalfPi,90},{edactormath::Pi,180},{-edactormath::HalfPi,270},{-.5f,331}}) {
        actors.at({0,0}).pose.yaw=yaw;assert(vm.invoke(getter,ash).integer()==degrees);
        vm.invoke(init,0,{jpc::Value::reference(stream)});vm.invoke(p.methodIndex("ed/base/EDBase","writeB16_s","(I)V"),0,{I(degrees)});
        vm.invoke(init,0,{jpc::Value::reference(stream)});const int saved=vm.invoke(p.methodIndex("ed/base/EDBase","readB16_s","()I")).integer();assert(saved==degrees);
        vm.invoke(p.methodIndex("ed/base/EDNpc","face","(I)V"),ash,{I(saved)});
        const float expected=edactormath::normalizeAngle(double(edactormath::HalfPi)-double(degrees)*.01745329238474369f);
        assert(std::abs(actors.at({0,0}).pose.yaw-expected)<1e-6f);
    }
    actors.at({0,0}).pose.yaw=0;assert(vm.invoke(getter,ash).integer()==0);
    assert(vm.invoke(p.methodIndex("ed/base/External","actorGetAngle","(II)I"),0,{I(0),I(0)}).integer()==90);
    vm.invoke(p.methodIndex("ed/base/EDNpc","turn","(I)V"),ash,{I(90)});assert(actors.at({0,0}).pose.yaw==edactormath::HalfPi);
    vm.invoke(p.methodIndex("ed/base/EDNpc","turn","(I)V"),ash,{I(-90)});assert(actors.at({0,0}).pose.yaw==0);
    const auto saved=actors.snapshot();actors.at({0,0}).pose.yaw=2;actors.restore(saved);assert(actors.at({0,0}).pose.yaw==0);
    vm.invoke(p.methodIndex("ed/base/EDActor","setTexture","(I)V"),ash,{I(int(edactors::defaultTexture(0)))});
    rejects([&]{vm.invoke(p.methodIndex("ed/base/EDActor","setTexture","(I)V"),ash,{I(0x08024001)});},"texture variant");
    assert(actors.snapshot()==saved);
    std::cout<<"source modelRotY/heading distinction, negative yaw, bitstream/face restore and opaque rotation save: passed\n";
}
static void textureStateTests(const std::filesystem::path& root,edactors::Host host) {
    ScriptGame game(root/"scripts/levelA.jpc");edactors::GameplayActors actors(game,root,host);const auto ash=visibleAsh(actors,game);auto& vm=game.vm;
    const auto setter=game.package.methodIndex("ed/base/EDActor","setTexture","(I)V");
    for(uint32_t id:{0x080c6004u,0x083cc004u,0x081ca004u,0x081d8004u,0x08318004u,0x08198004u,0x08198008u}) {
        vm.invoke(setter,ash,{I(int32_t(id|0xe0000000u))});assert(actors.at({0,0}).textureID==id);
        const auto bytes=actors.snapshot();actors.at({0,0}).textureID=edactors::defaultTexture(0);actors.restore(bytes);assert(actors.at({0,0}).textureID==id);
        auto invalid=actors.cloneState();invalid.bodies[0].textureID=0x08024001;
        rejects([&]{actors.restore(invalid);},"texture");assert(actors.snapshot()==bytes);
        rejects([&]{vm.invoke(setter,ash,{I(0x08024001)});},"texture variant");assert(actors.snapshot()==bytes);
    }
    vm.invoke(setter,ash,{I(int(edactors::defaultTexture(0)))});const auto current=actors.snapshot();
    // Global ownership was absent in V1..10: fail closed without changing state.
    statebytes::Reader in{current};assert(in.text()=="EDACTORS13");const auto header=in.cursor;
    statebytes::Writer legacy;legacy.text("EDACTORS10");
    legacy.bytes.insert(legacy.bytes.end(),current.begin()+header+501,current.end()-127);
    rejects([&]{actors.restore(legacy.bytes);},"ownership");assert(actors.snapshot()==current);
    std::cout<<"source texture variants, atomic rejection and global-ownership saveV11: passed\n";
}
static void soundPhaseTests(const std::filesystem::path& root,edactors::Host host) {
    ScriptGame game(root/"scripts/levelA.jpc");std::string order;bool armed=false;
    edanimsound::Materials old{{1,2,3,4}},next{{5,6,7,8}};
    host.animationSound=[&](const auto& body,const auto& raw){if(armed){const auto sound=edanimevent::decodeSound(raw);
        assert(body.key.type==0&&sound.frame==11&&sound.resource==0x00010000&&body.sfxMaterial==old);order+='s';}};
    host.soundMaterials=[&](const auto& body){if(armed){assert(body.sfxMaterial==old);order+='q';}return next;};
    const auto movement=host.applyMovement;host.applyMovement=[&](auto& body){if(armed){assert(body.sfxMaterial==next);order+='p';}movement(body);};
    edactors::GameplayActors actors(game,root,host);visibleAsh(actors,game);assert(actors.at({0,0}).sfxMaterial==edanimsound::Materials{});
    assert(actors.consoleAction({0,0},3));uint64_t tick=0;
    for(unsigned n=0;n<100&&actors.at({0,0}).actions.playbacks[size_t(actors.at({0,0}).actions.layers[0].playback)].mode==4;++n)actors.update(++tick);
    const auto phaseVm=game.vm.snapshot(),phaseActors=actors.snapshot();
    auto prepare=[&](int flags,int mode=1,bool active=true){
        // Each literal gate fixture starts from the same coherent global pool.
        game.vm.restore(phaseVm);actors.restore(phaseActors);auto phase=actors.cloneState();phase.sourceTick=tick;phase.updated=tick!=0;actors.restore(phase);
        auto& b=actors.at({0,0});b.flags=flags;b.sfxMaterial=old;
        auto& p=b.actions.playbacks[size_t(b.actions.layers[0].playback)];assert(b.actions.states[size_t(p.state)].animation==0x01824284);
        p.frame=11;p.mode=mode;p.active=active;p.reusable=true;order.clear();armed=true;};
    prepare(1);actors.update(++tick);assert(order=="sqp"&&actors.at({0,0}).sfxMaterial==next);
    prepare(9);actors.update(++tick);assert(order=="sq"); // Hidden8 still queries source materials.
    prepare(0);actors.update(++tick);assert(order=="qp"); // Visible1 gates sound, not physical material sampling.
    prepare(0x41);actors.update(++tick);assert(order.empty()&&actors.at({0,0}).sfxMaterial==old);
    prepare(1,4);actors.update(++tick);assert(order=="qp");
    prepare(1,1,false);actors.update(++tick);assert(order=="qp");
    prepare(1);actors.unregister({0,0});actors.update(++tick);assert(order=="s"&&actors.at({0,0}).sfxMaterial==old);
    armed=false;const auto bytes=actors.snapshot();actors.at({0,0}).sfxMaterial={};actors.restore(bytes);assert(actors.at({0,0}).sfxMaterial==old);
    auto truncated=bytes;truncated.pop_back();bool rejected=false;try{actors.restore(truncated);}catch(const std::exception&){rejected=true;}
    assert(rejected&&actors.snapshot()==bytes);
    std::cout<<"source sound20 preincrement/old-material -> physical material query -> physics, exact flags/slot gates and nativeV9 cache save: passed\n";
}
static void contactNegativeTests(const std::filesystem::path& root,edactors::Host host) {
    ScriptGame game(root/"scripts"/"levelA.jpc");game.enterScene("ak09");bool facing=true;
    game.vm.bind("ed/base/External","guiItemSetPadObj","(ILed/base/EDBase;)V",[](auto&,auto,const auto&){return jpc::Value{};});
    host.facing=[&](auto,auto,int){return facing;};host.lineOfSight=[](auto,auto){return false;};
    host.rootMotionAllowed=[](auto,const auto&){return false;};
    edactors::GameplayActors actors(game,root,host);visibleAsh(actors,game);
    const auto enemy=actors.createDeadite();auto& vm=game.vm;
    vm.invoke(game.package.methodIndex("GenericNpc","resetToBehaviorImmediateBirth","(IZ)V"),enemy,{I(1),I(1)});
    uint64_t tick=0;
    const auto step=[&](){const auto t=actors.at({4,0}).think;
        if(t&&vm.status(t)!=jpc::Interpreter::ThreadStatus::Held)vm.hold(t);
        ++tick;vm.statics[398]=I(int32_t(tick*1000/30));actors.update(tick);};
    for(unsigned n=0;n<100&&actors.at({4,0}).actions.layers[0].state!=1;++n)step();
    assert(actors.playerAction(17)==22);for(unsigned n=0;n<40;++n)step();
    const auto vmSave=vm.snapshot(),nativeSave=actors.snapshot();const auto baseTick=tick;
    const auto restore=[&](){vm.restore(vmSave);actors.restore(nativeSave);tick=baseTick;facing=true;};
    for(int test=0;test<5;++test) {
        restore();auto& victim=actors.at({4,0});
        if(test==0)victim.pose.z=4;else if(test==1)facing=false;else if(test==2)victim.flags&=~1;
        bool attacked=false;
        for(unsigned n=0;n<80;++n) {if(test!=3&&!attacked){const auto action=actors.playerAction(17);attacked=action>=46&&action<=58;}step();}
        assert(vm.field(enemy,13).integer()==(test==4?92:100));
        if(test!=3)assert(attacked);
    }
    std::cout<<"contact negatives: range/facing/hidden/aim; native melee intentionally has no LOS gate: passed\n";
}
static void objectContactTests(const std::filesystem::path& root,edactors::Host host) {
    ScriptGame game(root/"scripts"/"levelA.jpc");std::vector<edactors::PickupObject> objects;
    host.pickupObjects=[&](){return objects;};host.rootMotionAllowed=[](auto,const auto&){return false;};
    edactors::GameplayActors actors(game,root,host);const auto ash=visibleAsh(actors,game);auto& vm=game.vm;
    vm.invoke(game.package.methodIndex("ObjDB","initialize","()V"));
    const auto object=vm.arrayValues(vm.staticField(700).reference())[0].reference();vm.setField(object,43,I(140));
    const auto vmSave=vm.snapshot(),nativeSave=actors.snapshot();
    const auto query=game.package.methodIndex("ed/base/EDActor","getPickupObject","(I)I");
    auto candidate=[&](int ordinal){return vm.invoke(query,ash,{I(ordinal)}).integer();};
    auto& queryBody=actors.at({0,0});queryBody.pose={0,0,0,0};
    objects={{7,true,0,1.499f},{1,false,0,.3f},{2,true,0,1},{31,true,0,1.5f},{4,true,0,-1}};
    const auto beforeQuery=vm.snapshot(),beforeNative=actors.snapshot();
    assert(candidate(0)==2&&candidate(1)==7&&candidate(2)==-1&&candidate(-1)==-1&&candidate(32)==-1);
    assert(vm.snapshot()==beforeQuery&&actors.snapshot()==beforeNative); // Enumeration has no pickup/message effect.
    objects={{0,true,0,-.78f},{1,true,0,-.795f}};
    assert(candidate(0)==0&&candidate(1)==(game.profile.dreamcast?1:-1)); // DC compares79cm with79.999992f, without integer-radius truncation.
    objects={{0,true,0,-.805f}};assert(candidate(0)==-1); //80cm behind is outside the native radius bypass.
    objects={{0,true,0,1.499f}};assert(candidate(0)==0); //149cm and facing.
    objects={{0,true,0,1.5f}};assert(candidate(0)==-1);
    queryBody.pose.yaw=edactormath::HalfPi;
    objects={{0,true,1.4995481967926025f,.036811839789152145f}};
    assert(candidate(0)==(game.profile.dreamcast?-1:0)); // Original05ef20 opcodes: float32 length rounds to150cm; old double path149.
    queryBody.pose.yaw=0;
    objects={{0,true,0,0}};assert(candidate(0)==-1);queryBody.pose.yaw=edactormath::HalfPi;assert(candidate(0)==0);
    objects={{0,true,0,1},{0,true,0,.5f}};rejects([&]{candidate(0);},"pickup object");
    objects={{32,true,0,1}};rejects([&]{candidate(0);},"pickup object");
    // Item140 is the real save-object proximity branch of Ash672, no inventory mock.
    for(int test=0;test<10;++test) {
        vm.restore(vmSave);actors.restore(nativeSave);auto& body=actors.at({0,0});body.pose={0,0,0,0};
        objects={{0,true,0,1}};
        if(test==1)objects[0].active=false;
        if(test==2)objects[0].z=1.5f;
        if(test==3)objects[0].z=-1;
        if(test==5||test==6)objects[0].z=0;
        if(test==6)body.pose.yaw=edactormath::HalfPi;
        if(test==4)objects[0].z=-0.5f; // Source radius80 bypasses facing for close objects.
        if(test==7)objects[0].z=-.795f; // Same float-radius predicate in original07a4a0 animation kind4.
        if(test==8)objects[0].z=-.805f;
        if(test==9){objects[0].x=1.4995481967926025f;objects[0].z=.036811839789152145f;body.pose.yaw=edactormath::HalfPi;}
        assert(actors.consoleAction({0,0},116));
        bool seen=false;for(uint64_t tick=1;tick<=35;++tick){actors.update(tick);seen=seen||vm.field(ash,70).integer()==1;}
        assert(seen==(test==0||test==4||test==6||(test==7&&game.profile.dreamcast)||(test==9&&!game.profile.dreamcast)));
    }
    std::cout<<"source Use116 midpoint -> object17 -> Ash672, active/range/facing/radius bypass: passed\n";
}
static void pickupReservationTests(const std::filesystem::path& root,edactors::Host host) {
    ScriptGame game(root/"scripts"/"levelA.jpc");if(!game.profile.dreamcast)return;
    std::vector<edactors::PickupObject> objects;host.pickupObjects=[&](){return objects;};host.rootMotionAllowed=[](auto,const auto&){return true;};
    edactors::GameplayActors* clearActors=nullptr; bool clearAtContact=false, clearedQueuedReceipt=false, cancelQueuedReceipt=false;
    host.soundFrame=[&]{if(clearAtContact&&clearActors){const auto state=clearActors->cloneState();
        if(state.pickup.receiptPhase&&std::any_of(state.messages.begin(),state.messages.end(),[](const auto& m){return m.event==17;})){
            clearActors->post({{4,0},{-1,-1},17,{{3,0,0,0}}});if(cancelQueuedReceipt)clearActors->cancelPickup();clearActors->clearMessages();clearAtContact=false;clearedQueuedReceipt=true;
        }}};
    edactors::GameplayActors actors(game,root,host);clearActors=&actors;const auto ash=visibleAsh(actors,game);auto& vm=game.vm;
    vm.invoke(game.package.methodIndex("ObjDB","initialize","()V"));
    const auto pool=vm.arrayValues(vm.staticField(700).reference());
    vm.setField(pool[0].reference(),43,I(140));vm.setField(pool[1].reference(),43,I(112));
    auto objectBindings=[&](ScriptGame& g){for(const auto& method:g.package.methods)if(g.package.classes[size_t(method.owner)].name=="ed/base/External") {
        if(method.name=="objSetType")g.vm.bind("ed/base/External",method.name,method.signature,[&](auto&,auto,const auto& args){
            for(auto& object:objects)if(object.index==args[0].integer())object.setup[0]=args[1].integer();return jpc::Value{};});
        if(method.name=="objSetVisible")g.vm.bind("ed/base/External",method.name,method.signature,[&](auto&,auto,const auto& args){
            for(auto& object:objects)if(object.index==args[0].integer())object.active=args[1].integer()!=0;return jpc::Value{};});
    }};
    objectBindings(game);actors.captureLevelBoundary();
    const auto baseVM=vm.snapshot(),baseActors=actors.snapshot();uint64_t tick=0;
    const auto query=game.package.methodIndex("ed/base/EDActor","getPickupObject","(I)I");
    auto quantity=[&](ScriptGame& g){int n=0;for(const auto& item:g.items())if(item.itemID==112)n+=item.quantity;return n;};
    auto reset=[&](float distance=1.44f){vm.restore(baseVM);actors.restore(baseActors);tick=0;
        objects={{0,false,0,.6f,{{140,2,3,4}},0},{1,true,0,distance,{{112,5,6,7}},0}};};
    auto choose=[&](){assert(vm.invoke(query,ash,{I(0)}).integer()==1);assert(actors.cloneState().pickup.query.index==1);};
    auto step=[&](unsigned count){while(count--)actors.update(++tick);};
    const int initial=quantity(game);
    // The unchanged direct/native contact loses an outer-range target after the low clip retreats.
    reset();assert(actors.consoleAction({0,0},115));step(17);assert(quantity(game)==initial);
    for(int action:{115,116,117}) {
        reset(1.49f);choose();assert(actors.consoleAction({0,0},action));const auto armed=actors.cloneState().pickup;
        assert(armed.target.index==1&&armed.query.index==-1&&armed.playback>=0);
        const auto& clip=actors.at({0,0}).actions.states[size_t(armed.state)];step(unsigned((clip.start+clip.earlyEnd)/2));
        assert(quantity(game)==initial+1&&actors.cloneState().pickup.playback==-1);
        if(action==115)assert(std::hypot(objects[1].x-actors.at({0,0}).pose.x,objects[1].z-actors.at({0,0}).pose.z)>1.5f);
    }
    // Invalidating a reserved slot never falls back to another eligible object.
    for(int negative=0;negative<7;++negative) {
        reset();choose();assert(actors.consoleAction({0,0},115));objects[0].active=true;
        if(negative==0)objects[1].active=false;
        if(negative==1)objects[1].setup[0]=46;
        if(negative==2)objects[1].setup[3]++;
        if(negative==3)objects[1].x+=.01f;
        if(negative==4)objects[1].y+=.01f;
        if(negative==5)actors.invalidatePickup(1);
        if(negative==6)actors.cancelPickup();
        step(17);assert(quantity(game)==initial&&vm.field(ash,70).integer()==0&&actors.cloneState().pickup.playback==-1);
    }
    reset();choose();actors.invalidatePickup(1);objects[0].active=true;assert(actors.consoleAction({0,0},115));
    assert(actors.cloneState().pickup.target.index==-1&&actors.cloneState().pickup.playback>=0);step(17);
    assert(quantity(game)==initial&&vm.field(ash,70).integer()==0);
    reset();choose();actors.update(++tick,false);assert(actors.cloneState().pickup.query.index==-1); // Stale query cannot arm a later command.
    assert(actors.consoleAction({0,0},115));assert(actors.cloneState().pickup.playback==-1);step(17);assert(quantity(game)==initial);
    reset();assert(actors.consoleAction({0,0},115));choose();assert(!actors.consoleAction({0,0},115));
    assert(actors.cloneState().pickup.query.index==-1&&actors.cloneState().pickup.playback==-1);step(17);assert(quantity(game)==initial);
    reset();choose();assert(actors.consoleAction({0,0},115));assert(actors.consoleAction({0,0},64));
    assert(actors.cloneState().pickup.playback==-1);step(17);assert(quantity(game)==initial);
    // No new persistent target is invented when an older mid-pickup save is read.
    reset();choose();assert(actors.consoleAction({0,0},115));step(16);
    const auto midVM=vm.snapshot(),midActors=actors.snapshot();const auto midTick=tick;const auto midState=actors.cloneState();const auto midObjects=objects;
    statebytes::Reader reader{midActors};assert(reader.text()=="EDACTORS13");statebytes::Writer old;old.text("EDACTORS12");
    old.bytes.insert(old.bytes.end(),midActors.begin()+reader.cursor,midActors.end()-114);
    actors.restore(old.bytes);assert(actors.cloneState().pickup.playback==-1);step(1);assert(quantity(game)==initial);
    vm.restore(midVM);actors.restore(midActors);tick=midTick;
    // Actor rollback restores the invalidated target; cold/future execution reaches the same original receive call.
    actors.invalidatePickup(1);actors.restore(midState);assert(actors.snapshot()==midActors);step(1);
    assert(quantity(game)==initial+1);const auto afterActors=actors.snapshot(),afterVM=vm.snapshot();
    objects=midObjects;ScriptGame coldGame(root/"scripts"/"levelA.jpc");edactors::GameplayActors cold(coldGame,root,host);objectBindings(coldGame);coldGame.vm.restore(midVM);cold.restore(midActors);
    cold.update(midTick+1);assert(cold.snapshot()==afterActors&&coldGame.vm.snapshot()==afterVM&&quantity(coldGame)==initial+1);
    vm.restore(midVM);actors.restore(midActors);const auto before=actors.snapshot();auto bad=midState;
    bad.pickup.playback=50;rejects([&]{actors.restore(bad);},"pickup reservation");assert(actors.snapshot()==before);
    bad=midState;bad.pickup.state=0;rejects([&]{actors.restore(bad);},"pickup reservation playback");assert(actors.snapshot()==before);
    bad=midState;bad.pickup.target.index=32;rejects([&]{actors.restore(bad);},"pickup target slot");assert(actors.snapshot()==before);
    bad=midState;bad.pickup.target.position[1]=std::numeric_limits<float>::quiet_NaN();rejects([&]{actors.restore(bad);},"pickup target position");assert(actors.snapshot()==before);
    bad=midState;bad.pickup.query=bad.pickup.target;bad.pickup.queryTick=bad.sourceTick+1;rejects([&]{actors.restore(bad);},"pickup reservation");assert(actors.snapshot()==before);
    bad=midState;bad.bodies.front().playbacks[size_t(bad.pickup.playback)].frame=17;rejects([&]{actors.restore(bad);},"past contact");assert(actors.snapshot()==before);
    auto truncated=midActors;truncated.pop_back();bool truncatedRejected=false;try{actors.restore(truncated);}catch(const std::runtime_error&){truncatedRejected=true;}
    assert(truncatedRejected&&actors.snapshot()==before);
    objects=midObjects;actors.unloadScene();assert(actors.cloneState().pickup.target.index==-1&&actors.cloneState().pickup.playback>=0);
    actors.update(midTick+1);assert(quantity(game)==initial&&actors.cloneState().pickup.playback==-1);
    vm.restore(midVM);actors.restore(midActors);objects=midObjects;actors.resetSceneRuntime();assert(actors.cloneState().pickup.target.index==-1&&actors.cloneState().pickup.playback>=0);
    actors.update(midTick+1);assert(quantity(game)==initial&&actors.cloneState().pickup.playback==-1);
    // Source soundFrame is before the raw queue dispatch. Clearing there must leave a self-restorable Actor state.
    for(bool cancel:{false,true}) {
        reset();choose();assert(actors.consoleAction({0,0},115));cancelQueuedReceipt=cancel;clearedQueuedReceipt=false;clearAtContact=true;step(17);
        assert(clearedQueuedReceipt);const auto cleared=actors.cloneState();
        std::cout<<"CLEAR_MESSAGE cancel="<<cancel<<" receiptPhase="<<unsigned(cleared.pickup.receiptPhase)<<" messages="<<cleared.messages.size()<<" callbacks="<<cleared.callbacks.size()<<'\n';
        const auto clearSnapshot=actors.snapshot(),clearVM=vm.snapshot();const auto clearTick=tick;const auto clearObjects=objects;
        actors.restore(clearSnapshot);assert(actors.snapshot()==clearSnapshot&&cleared.pickup.receiptPhase==0);
        step(22);assert(quantity(game)==initial);const auto expectedActors=actors.snapshot(),expectedVM=vm.snapshot();
        objects=clearObjects;vm.restore(clearVM);actors.restore(clearSnapshot);tick=clearTick;step(22);
        assert(actors.snapshot()==expectedActors&&vm.snapshot()==expectedVM);
    }
    // Original648 returns0 while another source reaction owns Ash11; keep identity through its retry.
    for(int protocol=0;protocol<5;++protocol) {
        reset();choose();assert(actors.consoleAction({0,0},115));step(16);vm.setField(ash,11,I(1));step(3);
        auto queued=actors.cloneState();assert(queued.pickup.playback==-1&&queued.pickup.receiptPhase==1&&queued.pickup.receipt.index==1&&quantity(game)==initial);
        assert(std::any_of(queued.callbacks.begin(),queued.callbacks.end(),[](const auto& callback){return callback.listener==edactors::Key{0,0}&&callback.message.event==17&&callback.message.data[0]==1&&callback.thread==0;}));
        if(protocol==1)actors.invalidatePickup(1); // Same setup/position reset must still cancel the old receipt.
        if(protocol==2)objects[1].x+=.01f;
        if(protocol==3)actors.cancelPickup();
        if(protocol==4)objects[1].active=false;
        const auto callbackCount=actors.cloneState().callbacks.size();actors.post({{4,0},{-1,-1},17,{{3,0,0,0}}});
        actors.clearMessages();assert(actors.cloneState().callbacks.size()==callbackCount&&actors.cloneState().pickup.receiptPhase==(protocol==1||protocol==3?2:1));
        const auto deferredVM=vm.snapshot(),deferredActors=actors.snapshot();const auto deferredObjects=objects;const auto deferredTick=tick;
        queued=actors.cloneState();auto corrupt=queued;corrupt.pickup.receiptPhase=3;
        rejects([&]{actors.restore(corrupt);},"pickup receipt");assert(actors.snapshot()==deferredActors);
        corrupt=queued;corrupt.callbacks.clear();corrupt.messages.clear();
        rejects([&]{actors.restore(corrupt);},"pickup receipt has no message");assert(actors.snapshot()==deferredActors);
        corrupt=queued;corrupt.pickup.receipt.index=0;
        rejects([&]{actors.restore(corrupt);},"pickup receipt has no message");assert(actors.snapshot()==deferredActors);
        vm.setField(ash,11,I(0));step(22);assert(quantity(game)==initial+(protocol==0)&&actors.cloneState().pickup.receiptPhase==0);
        const auto completedVM=vm.snapshot(),completedActors=actors.snapshot();
        objects=deferredObjects;ScriptGame pendingGame(root/"scripts"/"levelA.jpc");edactors::GameplayActors pending(pendingGame,root,host);
        objectBindings(pendingGame);pendingGame.vm.restore(deferredVM);pending.restore(deferredActors);assert(pending.snapshot()==deferredActors);
        pendingGame.vm.setField(ash,11,I(0));for(unsigned n=1;n<=22;++n)pending.update(deferredTick+n);
        assert(pending.snapshot()==completedActors&&pendingGame.vm.snapshot()==completedVM);
        vm.restore(deferredVM);actors.restore(deferredActors);objects=deferredObjects;tick=deferredTick;
        actors.invalidatePickup(1);vm.restore(deferredVM);actors.restore(deferredActors);assert(actors.snapshot()==deferredActors);
        vm.setField(ash,11,I(0));step(22);assert(actors.snapshot()==completedActors&&vm.snapshot()==completedVM);
    }
    // A dispatched original callback is retained, even when unrelated raw messages are cleared.
    reset();choose();assert(actors.consoleAction({0,0},115));step(16);vm.setField(ash,11,I(1));step(3);
    auto active=actors.cloneState();assert(active.callbacks.size()==1&&active.callbacks.front().thread==0);vm.setField(ash,11,I(0));
    const auto message=active.callbacks.front().message;
    active.callbacks.front().thread=vm.start(game.package.methodIndex("ActorDB","processMessage","(IIIIIIIIIII)I"),0,
        {I(0),I(0),I(message.first.type),I(message.first.index),I(message.second.type),I(message.second.index),I(message.event),I(message.data[0]),I(message.data[1]),I(message.data[2]),I(message.data[3])});
    actors.restore(active);actors.post({{4,0},{-1,-1},17,{{3,0,0,0}}});actors.clearMessages();
    assert(actors.cloneState().callbacks.front().thread==active.callbacks.front().thread&&actors.cloneState().pickup.receiptPhase==1);
    const auto activeActors=actors.snapshot(),activeVM=vm.snapshot();const auto activeTick=tick;const auto activeObjects=objects;
    actors.restore(activeActors);step(22);assert(quantity(game)==initial+1&&actors.cloneState().pickup.receiptPhase==0);
    const auto activeEnd=actors.snapshot(),activeEndVM=vm.snapshot();objects=activeObjects;vm.restore(activeVM);actors.restore(activeActors);tick=activeTick;step(22);
    assert(actors.snapshot()==activeEnd&&vm.snapshot()==activeEndVM);
    // An old deferred receipt and a newly accepted animation keep independent fingerprints.
    reset();choose();assert(actors.consoleAction({0,0},115));step(16);vm.setField(ash,11,I(1));step(20);
    objects.push_back({2,true,actors.at({0,0}).pose.x,actors.at({0,0}).pose.z+.9f,{{112,8,9,10}},0});vm.setField(pool[2].reference(),43,I(112));
    int next=vm.invoke(query,ash,{I(0)}).integer();if(next==1)next=vm.invoke(query,ash,{I(1)}).integer();assert(next==2);assert(actors.consoleAction({0,0},115));
    assert(actors.cloneState().pickup.target.index==2&&actors.cloneState().pickup.receipt.index==1);
    vm.setField(ash,11,I(0));step(18);assert(quantity(game)==initial+2&&actors.cloneState().pickup.receiptPhase==0);
    actors.clearPickup();assert(actors.cloneState().pickup.query.index==-1&&actors.cloneState().pickup.playback==-1);
    std::cout<<"DC accepted pickup115/116/117 target, source root motion, no replacement/inactive/moved/rejected/interrupted, deferred648 identity, V12/cold/rollback/invalid13: passed\n";
}
static void inputCombat(const std::filesystem::path& root,edactors::Host host,int logical,int enemyType=4) {
    ScriptGame game(root/"scripts"/"levelA.jpc");game.enterScene("ak09");
    bool levelArena=false;unsigned arenaStarts=0,arenaStops=0;
    game.vm.bind("ed/base/External","memLevelStart","()V",[&](auto&,auto,const auto&){levelArena=true;++arenaStarts;return jpc::Value{};});
    game.vm.bind("ed/base/External","memLevelStop","()V",[&](auto&,auto,const auto&){levelArena=false;++arenaStops;return jpc::Value{};});
    game.vm.bind("ed/base/External","guiItemSetPadObj","(ILed/base/EDBase;)V",[](auto&,auto,const auto&){return jpc::Value{};});
    for(const auto& method:game.package.methods)if(game.package.classes[size_t(method.owner)].name=="ed/base/External"&&
        std::set<std::string>{"sndMgrGetSNDID","sndMgrActorPlaySound","sndMgrPlaySoundVolLoop","sndMgrPlaySoundVol","sndMgrVoiceSetFrequency","sndMgrVoiceStop"}.count(method.name)) {
        game.vm.bind("ed/base/External",method.name,method.signature,[signature=method.signature](auto&,auto,const auto&){return signature.back()=='V'?jpc::Value{}:I(1);}); // Headless audio endpoint.
    }
    // Producer fixture keeps the separate NPC thinker parked while alive; hit
    // callbacks and original damage run, then the real death thinker resumes.
    host.facing=[](auto from,auto,int){return from.type==0;};
    host.lineOfSightDirection=[](auto,int,int){return false;}; // Native obstacle input: no dodge direction is clear.
    edactors::GameplayActors actors(game,root,host);const auto ash=actors.initializeAsh();auto& vm=game.vm;const auto& p=game.package;
    vm.invoke(p.methodIndex("ed/base/EDActor","flagSubEngine","(I)V"),ash,{I(8)});
    vm.invoke(p.methodIndex("ed/base/EDActor","flagAddEngine","(I)V"),ash,{I(1)});
    vm.invoke(p.methodIndex("ed/base/EDActor","flagSubEngine","(I)V"),ash,{I(32)});
    const auto enemy=enemyType==4?actors.createDeadite():actors.createType(enemyType);const edactors::Key enemyKey{enemyType,0};
    assert(!levelArena&&arenaStarts==(enemyType==4?0u:1u)&&arenaStops==arenaStarts);
    assert(vm.field(enemy,13).integer()==100);
    if(enemyType==35)assert(vm.field(enemy,1203).integer()==0&&vm.invoke(p.methodIndex("Axe","CalcDamage","(Led/base/EDNpc;)I"),vm.field(ash,103).reference(),{jpc::Value::reference(enemy)}).integer()==8);
    vm.invoke(p.methodIndex("GenericNpc","resetToBehaviorImmediateBirth","(IZ)V"),enemy,{I(1),I(1)});
    uint64_t tick=0;std::vector<int> damageSteps;
    const auto step=[&](){
        const auto thread=actors.at(enemyKey).think;
        if(thread&&vm.field(enemy,13).integer()>0&&vm.status(thread)!=jpc::Interpreter::ThreadStatus::Held)vm.hold(thread);
        if(thread&&vm.field(enemy,13).integer()<=0&&vm.status(thread)==jpc::Interpreter::ThreadStatus::Held)vm.awake(thread);
        const int before=vm.field(enemy,13).integer();++tick;vm.statics[398]=I(int32_t(tick*1000/30));actors.update(tick);
        if(vm.field(enemy,13).integer()!=before)damageSteps.push_back(vm.field(enemy,13).integer());
    };
    const int idle=enemyType==6?3:enemyType==35?2:enemyType==4?1:0;
    for(unsigned n=0;n<100&&actors.at(enemyKey).actions.layers[0].state!=idle;++n)step();
    assert(actors.at(enemyKey).actions.layers[0].state==idle&&vm.field(enemy,13).integer()==100);
    assert(actors.playerAction(logical)==22);step();
    unsigned swings=0;
    for(unsigned frame=0;frame<1000&&vm.field(enemy,13).integer()>(logical==15?99:0);++frame) {
        const int chosen=actors.playerAction(logical);if(chosen>=46&&chosen<=58)++swings;
        step();
    }
    if(logical==15) {assert(vm.field(enemy,13).integer()==94&&swings==1);std::cout<<"source input15 -> primary animation contact -> original Chainsaw6 damage: passed\n";return;}
    assert(vm.field(enemy,13).integer()==0&&swings>=1&&vm.field(ash,13).integer()==100);
    for(unsigned n=0;n<300&&vm.field(enemy,game.hostField(HostFieldRole::GenericNpc30)).integer()!=1;++n)step();
    assert(vm.field(enemy,game.hostField(HostFieldRole::GenericNpc30)).integer()==1);
    if(enemyType==6)assert(swings==15);
    if(enemyType==11||enemyType==12)assert(swings==13);
    if(enemyType==13)assert(swings==10);
    if(enemyType==35){assert(swings==25&&damageSteps.size()==13);for(unsigned n=0;n<damageSteps.size();++n)assert(damageSteps[n]==std::max(0,100-8*int(n+1)));}
    if(enemyType==7||enemyType==8||enemyType==9)assert(swings==13);
    std::cout<<"source input"<<logical<<" -> animation contact -> "<<edactors::actorName(enemyType)<<" original damage/death: "<<swings<<" swings\n";
}
static void skeletonLifecycleTests(const std::filesystem::path& root,edactors::Host host) {
    ScriptGame game(root/"scripts/levelA.jpc");game.enterScene("ak09");auto& vm=game.vm;const auto& p=game.package;
    bool arena=false;unsigned arenaStarts=0,arenaStops=0;
    vm.bind("ed/base/External","memLevelStart","()V",[&](auto&,auto,const auto&){arena=true;++arenaStarts;return jpc::Value{};});
    vm.bind("ed/base/External","memLevelStop","()V",[&](auto&,auto,const auto&){arena=false;++arenaStops;return jpc::Value{};});
    vm.bind("ed/base/External","guiItemSetPadObj","(ILed/base/EDBase;)V",[](auto&,auto,const auto&){return jpc::Value{};});
    for(const auto& m:p.methods)if(p.classes[size_t(m.owner)].name=="ed/base/External"&&
        std::set<std::string>{"sndMgrGetSNDID","sndMgrActorPlaySound","sndMgrPlaySoundVolLoop","sndMgrPlaySoundVol","sndMgrVoiceSetFrequency","sndMgrVoiceStop"}.count(m.name))
        vm.bind("ed/base/External",m.name,m.signature,[signature=m.signature](auto&,auto,const auto&){return signature.back()=='V'?jpc::Value{}:I(1);});
    host.spawnPose=[](auto key,int,int,int){return edactors::Pose{0,0,key.type==6?1.f:0,0};};
    host.facing=[](auto,auto,int){return true;};host.rootMotionAllowed=[](auto,const auto&){return false;};
    edactors::GameplayActors actors(game,root,host);const auto ash=visibleAsh(actors,game);const auto skeleton=actors.createSkeleton();
    assert(skeleton&&!arena&&arenaStarts==1&&arenaStops==1&&vm.field(skeleton,6).integer()==6&&vm.field(skeleton,5).integer()==0);
    assert(vm.objectClass(skeleton)==p.classIndex("Skeleton")&&vm.objectClass(vm.field(skeleton,4).reference())==p.classIndex("ed/base/EDBrain"));
    assert(vm.field(skeleton,13).integer()==100&&vm.field(skeleton,1212).integer()==100&&vm.field(skeleton,1206).integer()==10);
    assert(vm.field(skeleton,1260).integer()==120&&vm.field(skeleton,1261).integer()==145&&(vm.field(skeleton,1272).integer()&0x4000));
    assert(vm.arrayValues(vm.staticField(54).reference())[6].integer()==10);
    const auto& body=actors.at({6,0});assert(body.radius==30&&body.fov==120&&body.sight==400&&body.parts.size()==1&&body.parts[0]);
    assert(body.vertices.size()==1098&&body.localPose.rotationsDegrees.size()==16&&body.actions.states.size()==37);
    vm.invoke(p.methodIndex("GenericNpc","resetToBehaviorImmediateBirth","(IZ)V"),skeleton,{I(1),I(1)});
    uint64_t tick=0;const auto step=[&](){const auto thinker=actors.at({6,0}).think;
        if(thinker&&vm.status(thinker)!=jpc::Interpreter::ThreadStatus::Held)vm.hold(thinker);
        vm.statics[398]=I(int32_t(++tick*1000/30));actors.update(tick);};
    for(unsigned n=0;n<100&&actors.at({6,0}).actions.layers[0].state!=3;++n)step();
    assert(actors.at({6,0}).actions.layers[0].state==3);
    // Original1740 chooses action50, waits five source frames, then sends source directional event40/damage10.
    const auto attack=vm.start(p.methodIndex("Skeleton","actionAttack","()V"),skeleton);
    assert(vm.status(attack)!=jpc::Interpreter::ThreadStatus::Complete);
    const auto savedVm=vm.snapshot(),savedActors=actors.snapshot();const auto savedTick=tick;
    for(unsigned n=0;n<100&&vm.status(attack)!=jpc::Interpreter::ThreadStatus::Complete;++n)step();
    assert(vm.status(attack)==jpc::Interpreter::ThreadStatus::Complete&&vm.field(ash,13).integer()==90);
    vm.restore(savedVm);actors.restore(savedActors);tick=savedTick;
    assert(vm.field(ash,13).integer()==100);
    for(unsigned n=0;n<100&&vm.status(attack)!=jpc::Interpreter::ThreadStatus::Complete;++n)step();
    assert(vm.field(ash,13).integer()==90);vm.forget(attack);
    for(unsigned n=1;n<4;++n){const auto next=actors.createSkeleton();assert(next&&vm.field(next,5).integer()==int(n));}
    assert(!actors.createSkeleton());for(unsigned n=0;n<4;++n){assert(actors.createDeadite());assert(actors.createSkullBat());}
    for(int type:{11,12,13})for(unsigned n=0;n<4;++n)assert(actors.createType(type));
    assert(actors.createType(35)&&!actors.createType(35));
    for(int type:{7,8,9})for(unsigned n=0;n<4;++n)assert(actors.createType(type));
    assert(actors.bodies().size()==38); // Bosses have their full Main/GUI fixture below.
    const auto allVm=vm.snapshot(),allBodies=actors.snapshot();vm.restore(allVm);actors.restore(allBodies);assert(actors.snapshot()==allBodies);
    auto invalid=actors.cloneState();for(auto& b:invalid.bodies)if(b.key.type==6){b.script=ash;break;}
    rejects([&]{actors.restore(invalid);},"actor identity");assert(actors.snapshot()==allBodies);
    std::cout<<"source Skeleton657/1739/1738 brain100HP, attack1740->Ash10damage, pending save/replay and native38body pool: passed\n";
}
static void ah05LifecycleTests(const std::filesystem::path& root,edactors::Host host) {
    struct Case {int type;const char* owner;unsigned bones,vertices,states;int radius,damage;};
    for(const auto c:std::array<Case,7>{{{11,"HellbillyFat",16,1020,17,40,20},{12,"HellbillyThin",16,834,27,30,12},{13,"DemonPig",25,894,17,65,6},{35,"HeadlessSkeleton",16,1158,30,30,10},
            {7,"TroopKnife",16,762,23,30,5},{8,"TroopClaw",16,822,23,30,5},{9,"TroopRange",16,834,21,30,2}}}) {
        ScriptGame game(root/"scripts/levelA.jpc");game.enterScene("ak09");auto& vm=game.vm;const auto& p=game.package;
        bool arena=false;unsigned starts=0,stops=0;
        vm.bind("ed/base/External","memLevelStart","()V",[&](auto&,auto,const auto&){arena=true;++starts;return jpc::Value{};});
        vm.bind("ed/base/External","memLevelStop","()V",[&](auto&,auto,const auto&){arena=false;++stops;return jpc::Value{};});
        vm.bind("ed/base/External","guiItemSetPadObj","(ILed/base/EDBase;)V",[](auto&,auto,const auto&){return jpc::Value{};});
        for(const auto& m:p.methods)if(p.classes[size_t(m.owner)].name=="ed/base/External"&&
            std::set<std::string>{"sndMgrGetSNDID","sndMgrActorPlaySound","sndMgrPlaySoundVolLoop","sndMgrPlaySoundVol","sndMgrVoiceSetFrequency","sndMgrVoiceStop"}.count(m.name))
            vm.bind("ed/base/External",m.name,m.signature,[signature=m.signature](auto&,auto,const auto&){return signature.back()=='V'?jpc::Value{}:I(1);});
        host.spawnPose=[&](auto key,int,int,int){return edactors::Pose{0,0,key.type==c.type?1.f:0,0};};
        host.facing=[](auto,auto,int){return true;};host.rootMotionAllowed=[](auto,const auto&){return false;};
        edactors::GameplayActors actors(game,root,host);const auto ash=visibleAsh(actors,game),npc=actors.createType(c.type);const edactors::Key key{c.type,0};
        assert(npc&&!arena&&starts==1&&stops==1&&vm.objectClass(npc)==p.classIndex(c.owner));
        assert(vm.objectClass(vm.field(npc,4).reference())==p.classIndex("ed/base/EDBrain")&&vm.field(npc,13).integer()==100&&vm.field(npc,1212).integer()==100);
        assert(vm.field(npc,1207).integer()==c.radius&&actors.at(key).radius==30); // Source overrides1275 AFTER its first resetToDefault.
        assert(actors.at(key).vertices.size()==c.vertices&&actors.at(key).localPose.rotationsDegrees.size()==c.bones&&actors.at(key).actions.states.size()==c.states);
        assert(actors.at(key).parts.size()==1&&actors.at(key).parts[0]);
        const std::map<int,int> bases{{7,15},{8,19},{9,23},{11,28},{12,32},{13,36},{35,14}};
        assert(vm.arrayValues(vm.staticField(54).reference())[size_t(c.type)].integer()==bases.at(c.type));
        vm.invoke(p.methodIndex("GenericNpc","resetToBehaviorImmediateBirth","(IZ)V"),npc,{I(1),I(1)});assert(actors.at(key).radius==c.radius);
        uint64_t tick=0;const auto step=[&](){const auto thinker=actors.at(key).think;
            if(thinker&&vm.status(thinker)!=jpc::Interpreter::ThreadStatus::Held)vm.hold(thinker);
            vm.statics[398]=I(int32_t(++tick*1000/30));actors.update(tick);};
        const int idle=c.type==35?2:0;
        for(unsigned n=0;n<100&&(n<3||actors.at(key).actions.layers[0].state!=idle);++n)step();assert(actors.at(key).actions.layers[0].state==idle);
        const auto attack=vm.start(p.methodIndex(c.type==7||c.type==8?"TroopKid":c.owner,"actionAttack","()V"),npc);
        assert(vm.status(attack)!=jpc::Interpreter::ThreadStatus::Complete);
        const auto beforeVm=vm.snapshot(),beforeNative=actors.snapshot();const auto beforeTick=tick;
        for(unsigned n=0;n<150&&vm.status(attack)!=jpc::Interpreter::ThreadStatus::Complete;++n)step();
        assert(vm.status(attack)==jpc::Interpreter::ThreadStatus::Complete&&vm.field(ash,13).integer()==100-c.damage);
        vm.restore(beforeVm);actors.restore(beforeNative);tick=beforeTick;assert(vm.field(ash,13).integer()==100);
        for(unsigned n=0;n<150&&vm.status(attack)!=jpc::Interpreter::ThreadStatus::Complete;++n)step();
        assert(vm.field(ash,13).integer()==100-c.damage);vm.forget(attack);
        for(int index=1;index<edactors::actorCapacity(c.type);++index){const auto next=actors.createType(c.type);assert(next&&vm.field(next,5).integer()==index);}
        assert(!actors.createType(c.type));const auto saved=actors.snapshot();actors.restore(saved);assert(actors.snapshot()==saved);
        auto invalid=actors.cloneState();for(auto& body:invalid.bodies)if(body.key==key){body.script=ash;break;}
        rejects([&]{actors.restore(invalid);},"actor identity");assert(actors.snapshot()==saved);
        std::cout<<c.owner<<": source657 factory/brain/attack -> Ash"<<c.damage<<"damage, pending save/replay/cap"<<edactors::actorCapacity(c.type)<<": passed\n";
    }
}
static void rangedTroopTests(const std::filesystem::path& root,edactors::Host host) {
    ScriptGame game(root/"scripts/levelA.jpc");game.enterScene("ak09");auto& vm=game.vm;const auto& p=game.package;
    bool arena=false;vm.bind("ed/base/External","memLevelStart","()V",[&](auto&,auto,const auto&){arena=true;return jpc::Value{};});
    vm.bind("ed/base/External","memLevelStop","()V",[&](auto&,auto,const auto&){arena=false;return jpc::Value{};});
    vm.bind("ed/base/External","guiItemSetPadObj","(ILed/base/EDBase;)V",[](auto&,auto,const auto&){return jpc::Value{};});
    for(const auto& m:p.methods)if(p.classes[size_t(m.owner)].name=="ed/base/External"&&
        std::set<std::string>{"sndMgrGetSNDID","sndMgrActorPlaySound","sndMgrPlaySoundVolLoop","sndMgrPlaySoundVol","sndMgrVoiceSetFrequency","sndMgrVoiceStop"}.count(m.name))
        vm.bind("ed/base/External",m.name,m.signature,[signature=m.signature](auto&,auto,const auto&){return signature.back()=='V'?jpc::Value{}:I(1);});
    host.spawnPose=[](auto key,int,int,int){return edactors::Pose{0,0,key.type==9?1.f:0,0};};
    bool facing=true;host.facing=[&](auto,auto,int){return facing;};bool allowLos=true;host.lineOfSight=[&](auto,auto){return allowLos;};
    unsigned visuals=0;host.animationEvent=[&](const auto& body,const auto& raw){const auto event=edanimevent::decode(raw);
        if(body.key.type==0&&event.kind!=7)return;
        if(body.key.type==9&&event.kind==20&&event.mode==-1)return; // NPC20 shared-RNG composition has a separate source proof.
        assert(body.key.type==9&&event.kind==7&&event.frame==12&&event.enabled&&event.mode==-1);++visuals;
        const auto endpoints=edanimevent::visualEndpoints(event,{{0,body.pose.yaw,0}},{{body.pose.x,body.pose.y,body.pose.z}});edactormath::finite(endpoints.first);edactormath::finite(endpoints.second);};
    edactors::GameplayActors actors(game,root,host);
    std::ifstream input(root/"ak09/scene.native",std::ios::binary);std::vector<uint8_t> payload(std::istreambuf_iterator<char>(input),{});
    std::vector<bool> enabled(ScriptScene(payload).volumes.size(),true);SourceActorRoutes::Host routeHost;
    routeHost.camera=[](){return 0u;};routeHost.enabledVolumes=[&]()->const std::vector<bool>&{return enabled;};
    routeHost.collisionStatus=[&](auto key){return actors.at(key).motionFlags2;};routeHost.random=[](int){return 0;};
    SourceActorRoutes routes(vm,actors,payload,routeHost);routes.bind();
    const auto ash=visibleAsh(actors,game),npc=actors.createType(9);
    assert(!arena&&vm.field(npc,1216).reference()==ash);
    vm.invoke(p.methodIndex("GenericNpc","resetToBehaviorImmediateBirth","(IZ)V"),npc,{I(1),I(1)});
    auto& body=actors.at({9,0});body.think=vm.enqueue(p.methodIndex("ActorDB","think","(II)V"),0,{I(9),I(0)});
    vm.invoke(p.methodIndex("ActorDB","setThinkThreadId","(III)V"),0,{I(9),I(0),I(int(body.think))});vm.hold(body.think);
    uint64_t tick=0;std::vector<int> health;std::vector<int> shotEvents;
    const auto step=[&](){const auto thinker=actors.at({9,0}).think;
        if(thinker&&vm.status(thinker)!=jpc::Interpreter::ThreadStatus::Held)vm.hold(thinker);
        const int before=vm.field(ash,13).integer();vm.statics[398]=I(int32_t(++tick*1000/30));actors.update(tick);
        for(const auto& message:actors.cloneState().messages)if(message.first==edactors::Key{9,0}&&message.event>=36&&message.event<=42)shotEvents.push_back(message.event);
        if(vm.field(ash,13).integer()!=before)health.push_back(vm.field(ash,13).integer());};
    for(unsigned n=0;n<3;++n)step();
    // Park the independent AI producer before placing the attack fixture at8m;
    // this measures original1777, not an unbound route scheduler.
    actors.at({9,0}).pose.z=8;
    allowLos=false;
    const auto shoot=vm.start(p.methodIndex("TroopRange","actionWailOnAsh","()V"),npc);
    const auto sourceVm=vm.snapshot(),native=actors.snapshot(),routeState=routes.snapshot();const auto sourceTick=tick;
    for(unsigned n=0;n<150&&vm.status(shoot)!=jpc::Interpreter::ThreadStatus::Complete;++n)step();
    assert(vm.status(shoot)==jpc::Interpreter::ThreadStatus::Complete);
    assert(vm.field(ash,13).integer()==80&&health==std::vector<int>({90,80})&&shotEvents.size()==3&&visuals==3);
    vm.restore(sourceVm);actors.restore(native);routes.restore(routeState);tick=sourceTick;health.clear();shotEvents.clear();visuals=0;
    for(unsigned n=0;n<150&&vm.status(shoot)!=jpc::Interpreter::ThreadStatus::Complete;++n)step();
    assert(vm.field(ash,13).integer()==80&&health==std::vector<int>({90,80})&&shotEvents.size()==3&&visuals==3);vm.forget(shoot);
    for(int negative=0;negative<2;++negative) {
        vm.restore(sourceVm);actors.restore(native);routes.restore(routeState);tick=sourceTick;health.clear();shotEvents.clear();visuals=0;
        facing=negative==0;if(negative==0)actors.at({9,0}).pose.z=14;
        for(unsigned n=0;n<150&&vm.status(shoot)!=jpc::Interpreter::ThreadStatus::Complete;++n)step();
        assert(health.empty()&&shotEvents.empty());
        if(vm.status(shoot)==jpc::Interpreter::ThreadStatus::Complete)vm.forget(shoot);
    }
    std::cout<<"TroopRange original1777/1531 three directional shots at8m (LOSfalse), source Ash busy rejection and pending save/replay: passed\n";
}
#include "levela_boss_tests.inl"
static void teardownTests(const std::filesystem::path& root,edactors::Host host) {
    ScriptGame game(root/"scripts/levelA.jpc");auto& vm=game.vm;const auto& p=game.package;
    auto bind=[&](const char* name,jpc::Interpreter::Native fn){for(const auto& m:p.methods)
        if(p.classes[size_t(m.owner)].name=="ed/base/External"&&m.name==name){vm.bind("ed/base/External",name,m.signature,std::move(fn));return;}throw jpc::Error("Missing teardown fixture endpoint");};
    bind("guiItemSetPadObj",[](auto&,auto,const auto&){return jpc::Value{};});
    for(const char* name:{"memLevelStart","memLevelStop","actorEffectSetFlag","rumbleP1"})bind(name,[](auto&,auto,const auto&){return jpc::Value{};});
    SourceGui gui;for(const auto* name:SourceGui::Setters)bind(name,[&,name](auto& interpreter,auto,const auto& args){return gui.native(interpreter,name,args);});
    for(const auto* name:{"guiSysCreate","guiItemCreate","guiSysGetFlag","guiItemGetDataID","guiSysAtOffset","guiSysSet"})
        bind(name,[&,name](auto& interpreter,auto,const auto& args){return gui.native(interpreter,name,args);});
    for(const char* name:{"platform","checkRumbleAbility"})bind(name,[](auto&,auto,const auto&){return I(0);});
    int milliseconds=0;bind("getMilliseconds",[&](auto&,auto,const auto&){return I(milliseconds);});
    bind("sndMgrGetStereo",[&](auto&,auto,const auto&){return I(gui.stereo);});
    bind("sndMgrGetMasterVolume",[&](auto&,auto,const auto&){return I(gui.masterVolume);});
    bind("sndMgrSetStereo",[&](auto&,auto,const auto& args){gui.stereo=args[0].integer();return jpc::Value{};});
    bind("sndMgrSetMasterVolume",[&](auto&,auto,const auto& args){gui.masterVolume=args[0].integer();return jpc::Value{};});
    for(const char* name:{"sndMgrGetSNDID","sndMgrPlaySoundVol","sndMgrActorPlaySound"})bind(name,[](auto&,auto,const auto&){return I(-1);}); // Real unavailable-audio path in this CPU fixture.
    std::vector<std::string> diagnostics;bind("debugPrintStringInt",[&](auto&,auto,const auto& args){diagnostics.push_back(game.string(args[1].reference())+std::to_string(args[0].integer()));return jpc::Value{};});
    vm.invoke(p.methodIndex("Main","initialize","()V"));
    host.spawnPose=[](auto,int,int,int){return edactors::Pose{};};host.rootMotionAllowed=[](auto,const auto&){return false;};
    std::vector<edactors::Key> applied;const auto originalMovement=host.applyMovement;host.applyMovement=[&](auto& body){applied.push_back(body.key);originalMovement(body);};
    unsigned floorQueries=0,unregisteredVisuals=0;const auto originalFloor=host.floorHeight;host.floorHeight=[&](float x,float z){++floorQueries;return originalFloor(x,z);};
    using Presentation=std::tuple<int,char,uint32_t,int>;std::vector<Presentation> presentation;
    host.animationEvent=[&](const auto& body,const auto& raw){const auto e=edanimevent::decode(raw);
        if(body.key.type==5||body.key.type==34)presentation.emplace_back(body.key.type,'v',e.kind,e.mode);
        else if(body.key.type==14||body.key.type==17||body.key.type==18)return; // Original particle composition has a separate integration proof.
        else if(body.key.type==0&&e.kind!=7)return;
        else if(e.kind==20&&e.mode==-1&&(body.key.type==2||body.key.type==4||body.key.type==7||body.key.type==8||
                body.key.type==9||body.key.type==10||body.key.type==11||body.key.type==12||body.key.type==23))return;
        else {assert(body.nativeSlot==-1&&e.kind==7);++unregisteredVisuals;}};
    host.animationSound=[&](const auto& body,const auto& raw){const auto e=edanimevent::decodeSound(raw);
        if(body.key.type==5||body.key.type==34)presentation.emplace_back(body.key.type,'s',e.resource,e.frequencyHz());};
    edactors::GameplayActors actors(game,root,host);actors.prepareBootstrap();
    bind("actorSetPosition",[&](auto&,auto,const auto& args){auto& body=actors.at({args[0].integer(),args[1].integer()});
        body.pose.x=float(double(args[2].integer())*edactormath::Centimeter);body.localY=float(double(args[3].integer())*edactormath::Centimeter);
        body.pose.z=float(double(args[4].integer())*edactormath::Centimeter);body.pose.y=body.localY+body.cachedFloorHeight;return jpc::Value{};});
    bind("actorFace",[&](auto&,auto,const auto& args){actors.at({args[0].integer(),args[1].integer()}).pose.yaw=
        edactormath::modelYawFromHeading(float(double(args[2].integer())*.01745329238474369f));return jpc::Value{};});
    bind("actorSetPositionActor",[&](auto&,auto,const auto& args){auto& target=actors.at({args[0].integer(),args[1].integer()});const auto& from=actors.at({args[2].integer(),args[3].integer()});
        target.pose.x=from.pose.x;target.localY=from.localY;target.pose.z=from.pose.z;target.pose.y=target.cachedFloorHeight+target.localY;return jpc::Value{};});
    vm.invoke(p.methodIndex("ActorDB","initialize","()V"));vm.invoke(p.methodIndex("LevelA","create","()V"),game.level);actors.sync();
    assert(actors.bodies().size()==9);actors.captureLevelBoundary();
    rejects([&]{actors.captureLevelBoundary();},"boundary capture");
    std::ifstream file(root/"ak09/scene.native",std::ios::binary);const std::vector<uint8_t> payload(std::istreambuf_iterator<char>(file),{});
    std::vector<bool> routeVolumes(ScriptScene(payload).volumes.size(),true);SourceActorRoutes::Host routeHost;
    routeHost.camera=[](){return 0u;};routeHost.enabledVolumes=[&]()->const std::vector<bool>&{return routeVolumes;};
    routeHost.collisionStatus=[&](auto key){return actors.at(key).motionFlags2;};routeHost.random=[](int){return 0;};
    SourceActorRoutes routes(vm,actors,payload,routeHost);routes.bind();
    const auto initialScopeVm=vm.snapshot(),initialScopeActors=actors.snapshot(),initialScopeRoutes=routes.snapshot();
    auto call=[&](const char* name,std::vector<jpc::Value> args){for(const auto& m:p.methods)if(p.classes[size_t(m.owner)].name=="ed/base/External"&&m.name==name)
        return vm.invoke(p.methodIndex("ed/base/External",name,m.signature),0,std::move(args));throw jpc::Error("Missing teardown test native");};
    call("actorRecordActorSpot",{I(4),I(0),I(0),I(0),I(2)});
    actors.consoleAction({0,0},116); // Native player FSM is deliberately preserved by scene unload.
    for(const auto& c:std::vector<std::pair<const char*,int>>{{"al02",6},{"ac01",7},{"ah05",11},{"at02",5},{"at02",34},{"ac05",10},
            {"ah06",14},{"ag03",17},{"au04",18}}) {
        game.enterScene(c.first);vm.invoke(p.methodIndex("SceneCommander","sceneReset","()V"));
        const auto npc=actors.createType(c.second);const edactors::Key key{c.second,0};assert(npc);
        routes.actorCreated(key);call("actorRecordActorSpot",{I(c.second),I(0),I(0),I(0),I(0)});
        const auto sourceBrain=vm.field(npc,4).reference();
        if(c.second==5){assert(actors.at(key).parts==std::vector<bool>({true,true,true,true}));assert(actors.at(key).localPose.rotationsDegrees.size()==45&&actors.at(key).vertices.size()==4050);}
        if(c.second==34){assert(!(actors.at(key).flags&1)&&actors.at(key).vertices.size()==222&&actors.at(key).localPose.rotationsDegrees.size()==4);}
        vm.invoke(p.methodIndex("ed/base/EDActor","setPosition","(III)V"),npc,{I(100),I(0),I(200)});
        const auto npcStream=vm.makeArray('B',std::vector<jpc::Value>(64,I(0)));
        vm.invoke(p.methodIndex("ed/base/EDBase","initBitStream","([B)V"),0,{jpc::Value::reference(npcStream)});
        vm.invoke(p.methodIndex("GenericNpc","save","()V"),npc);
        const auto waiting=vm.start(p.methodIndex("ed/base/EDThread","sleepFrame","(I)V"),0,{I(20)});vm.hold(waiting);
        auto& body=actors.at(key);body.think=vm.enqueue(p.methodIndex("ActorDB","think","(II)V"),0,{I(c.second),I(0)});
        vm.invoke(p.methodIndex("ActorDB","setThinkThreadId","(III)V"),0,{I(c.second),I(0),I(int(body.think))});
        const auto beforeVm=vm.snapshot(),beforeActors=actors.snapshot(),beforeRoutes=routes.snapshot();const auto ashAction=actors.at({0,0}).actions.layers;
        vm.invoke(p.methodIndex("LevelA","endScene","()V"),game.level);
        const auto removed=actors.unloadScene();assert(removed==std::vector<edactors::Key>{key});routes.forgetActors(removed);
        routes.setScene(payload);actors.resetSceneRuntime();
        assert(actors.bodies().size()==9&&!vm.pendingThreads());
        for(const auto& b:actors.bodies())assert(b.second.script&&b.second.think==0);
        for(unsigned n=0;n<7;++n)assert(actors.at({0,0}).actions.layers[n].state==ashAction[n].state);
        assert(call("actorDistanceToRecordedPosition",{I(4),I(0),I(2)}).integer()==100); // Resident native memory survives.
        const auto unloadedVm=vm.snapshot(),unloadedActors=actors.snapshot(),unloadedRoutes=routes.snapshot();
        vm.restore(unloadedVm);actors.restore(unloadedActors);routes.restore(unloadedRoutes);assert(actors.snapshot()==unloadedActors);
        // Roll back a failed transaction to the exact source wait, native scope and memory.
        vm.restore(beforeVm);actors.restore(beforeActors);routes.restore(beforeRoutes);assert(vm.status(waiting)==jpc::Interpreter::ThreadStatus::Held);
        assert(actors.at(key).script==npc&&vm.field(npc,4).reference()==sourceBrain&&actors.snapshot()==beforeActors);
        vm.invoke(p.methodIndex("LevelA","endScene","()V"),game.level);routes.forgetActors(actors.unloadScene());routes.setScene(payload);actors.resetSceneRuntime();
        game.enterScene("ak09");vm.invoke(p.methodIndex("SceneCommander","sceneReset","()V")); // Destination source reset, with no fabricated actor refs.
        vm.invoke(p.methodIndex("GenericNpc","registerDefaultActorMessages","()V"),actors.at({4,0}).script);
        vm.invoke(p.methodIndex("GenericNpc","registerDefaultActorMessages","()V"),actors.at({2,0}).script);
        game.enterScene(c.first);vm.invoke(p.methodIndex("SceneCommander","sceneReset","()V"));
        const auto again=actors.createType(c.second);assert(again&&again!=npc&&vm.field(again,4).reference()!=sourceBrain);
        vm.invoke(p.methodIndex("ed/base/EDBase","initBitStream","([B)V"),0,{jpc::Value::reference(npcStream)});
        vm.invoke(p.methodIndex("GenericNpc","restore","()V"),again);
        assert(actors.at(key).pose.x==1&&actors.at(key).pose.z==2&&vm.field(again,13).integer()==100);
        routes.actorCreated(key);const auto saved=actors.snapshot();actors.restore(saved);routes.restore(routes.snapshot());
        // Finish this scene before advancing to the next special archetype.
        vm.invoke(p.methodIndex("LevelA","endScene","()V"),game.level);routes.forgetActors(actors.unloadScene());routes.setScene(payload);actors.resetSceneRuntime();
    }
    {
        game.enterScene("at02");vm.invoke(p.methodIndex("SceneCommander","sceneReset","()V"));const auto boss=actors.createType(5),arm=actors.createType(34);
        assert(boss&&arm&&!actors.createType(5)&&!actors.createType(34));
        assert(vm.objectClass(vm.field(boss,4).reference())==p.classIndex("ed/base/EDBrain")&&vm.field(boss,13).integer()==100);
        assert(vm.arrayValues(vm.staticField(54).reference())[5].integer()==8&&vm.arrayValues(vm.staticField(54).reference())[34].integer()==9);
        vm.invoke(p.methodIndex("SceneAT02","spawnMonster1","(LGenericNpc;III)V"),game.scene,{jpc::Value::reference(boss),I(0),I(0),I(0)});
        vm.invoke(p.methodIndex("SceneAT02","spawnMonster2","(LGenericNpc;III)V"),game.scene,{jpc::Value::reference(arm),I(0),I(0),I(0)});
        assert(vm.field(boss,1417).reference()==arm&&vm.field(arm,1216).reference()==game.ash&&!(actors.at({34,0}).flags&1));
        assert(actors.at({5,0}).radius==100&&actors.at({5,0}).parts==std::vector<bool>({true,true,true,true}));
        {
            const auto savedVm=vm.snapshot(),savedActors=actors.snapshot();
            auto sample=[&](edactors::Key key,int state,int frame,int flags=1,int mode=0,bool active=true) {
                vm.restore(savedVm);actors.restore(savedActors);presentation.clear();
                for(const auto& [identity,unused]:actors.bodies()){(void)unused;actors.at(identity).flags=0x40;}
                auto& b=actors.at(key);b.flags=flags;b.nativeSlot=-1;
                const int globalSlot=b.actions.layers[0].playback;
                for(auto& layer:b.actions.layers)layer=edactions::Layer{};
                b.actions.layers[0].state=state;b.actions.layers[0].playback=globalSlot;
                auto& playback=b.actions.playbacks[size_t(globalSlot)];playback.state=state;playback.frame=frame;playback.mode=mode;
                playback.desiredMode=mode;playback.active=active;playback.reusable=true;b.actions.events.clear();
                actors.update(actors.cloneState().sourceTick+1);return presentation;
            };
            // Original AT02 records: both same-frame20B sounds precede all36B
            // events; raw34 is an actual PC no-op between two raw35 records.
            const std::vector<Presentation> armFrame{{34,'s',0x0d9a4f40,22000},{34,'s',0x00800001,15000},
                {34,'v',9,-1},{34,'v',15,0},{34,'v',25,2}};
            assert(sample({34,0},1,1)==armFrame);
            assert(sample({34,0},1,1,9)==armFrame); // Hidden8 and physical registration do not gate animation.
            assert(sample({34,0},1,1,0).empty()&&sample({34,0},1,1,0x41).empty());
            assert(sample({34,0},1,1,1,4).empty()&&sample({34,0},1,1,1,0,false).empty());
            assert(sample({34,0},1,1)==armFrame); // Visiting a frame again must emit again.
            assert((sample({5,0},1,14)==std::vector<Presentation>{{5,'s',0x0dce2fc4,24000},{5,'v',15,0}}));
            assert((sample({5,0},3,16)==std::vector<Presentation>{{5,'v',35,-1},{5,'v',35,-1},
                {5,'v',9,-1},{5,'v',9,-1},{5,'v',9,-1},{5,'v',15,0},{5,'v',25,2}}));
            vm.restore(savedVm);actors.restore(savedActors);presentation.clear();
            std::cout<<"Tree original20B sound before36B, source order/frequency, raw34 no-op, preincrement and hidden/wait/freeze gates: passed\n";
        }
        actors.at({5,0}).pose.z=1.25f;bool parkBoss=true;uint64_t tick=actors.cloneState().sourceTick;
        std::vector<int> bossHealth;std::vector<std::pair<int,int>> attacks;
        const auto step=[&](){const auto thinker=actors.at({5,0}).think;
            if(thinker&&vm.status(thinker)!=jpc::Interpreter::ThreadStatus::Complete){
                if(parkBoss&&vm.status(thinker)!=jpc::Interpreter::ThreadStatus::Held)vm.hold(thinker);
                if(!parkBoss&&vm.status(thinker)==jpc::Interpreter::ThreadStatus::Held)vm.awake(thinker);}
            const int before=vm.field(boss,13).integer();milliseconds=int(++tick*1000/30);vm.statics[398]=I(milliseconds);actors.update(tick);
            if(vm.field(boss,13).integer()!=before)bossHealth.push_back(vm.field(boss,13).integer());
            for(const auto& msg:actors.cloneState().messages)if((msg.first.type==5||msg.first.type==34)&&msg.event>=36&&msg.event<=42)attacks.emplace_back(msg.first.type,msg.data[0]);};
        // Real Ash input17 produces a native animation contact; original1751
        // waits hurt26 before applying its fixed7 Axe/Chainsaw damage.
        for(unsigned n=0;n<200&&actors.cloneState().callbacks.empty();++n){actors.playerAction(17);step();}
        assert(vm.field(boss,13).integer()==100&&!actors.cloneState().callbacks.empty());
        const auto callbackVm=vm.snapshot();const auto retained=actors.cloneState();auto cleared=retained;cleared.messages.clear();
        actors.restore(cleared);const auto expectedClear=actors.snapshot();actors.restore(retained);
        actors.post({{0,0},{5,0},36,{{1,0,0,0}}});actors.clearMessages();
        assert(actors.snapshot()==expectedClear&&vm.snapshot()==callbackVm);actors.restore(retained);
        const auto pendingVm=vm.snapshot(),pendingActors=actors.snapshot(),pendingRoutes=routes.snapshot();const auto pendingTick=tick;const int pendingMS=milliseconds;
        for(unsigned n=0;n<100&&vm.field(boss,13).integer()==100;++n)step();assert(vm.field(boss,13).integer()==93&&bossHealth==std::vector<int>{93});
        const auto hitTick=tick;vm.restore(pendingVm);actors.restore(pendingActors);routes.restore(pendingRoutes);tick=pendingTick;milliseconds=pendingMS;bossHealth.clear();
        for(unsigned n=0;n<100&&vm.field(boss,13).integer()==100;++n)step();assert(vm.field(boss,13).integer()==93&&tick==hitTick&&bossHealth==std::vector<int>{93});
        assert(vm.field(boss,1419).integer()==1);parkBoss=false;
        // Full ActorDB.think651 -> TreeBoss.behaviorWait1749 produces both source attacks.
        for(unsigned n=0;n<250&&vm.field(game.ash,13).integer()==100;++n)step();
        assert(vm.field(game.ash,13).integer()==90&&std::find(attacks.begin(),attacks.end(),std::pair<int,int>{5,10})!=attacks.end());
        actors.at({5,0}).pose={-10,0,0,edactormath::HalfPi};
        for(unsigned n=0;n<300&&vm.field(game.ash,13).integer()==90;++n)step();
        assert(vm.field(game.ash,13).integer()==70&&std::find(attacks.begin(),attacks.end(),std::pair<int,int>{5,20})!=attacks.end());
        // An off-axis target selects the actual linked TreeArm action.1757
        // copies LOCAL target XYZ, waits7, shows/attacks5HP and hides again.
        actors.at({5,0}).pose={-5,0,5,edactormath::HalfPi};
        for(unsigned n=0;n<400&&vm.field(game.ash,13).integer()==70;++n)step();
        assert(vm.field(game.ash,13).integer()==65&&std::find(attacks.begin(),attacks.end(),std::pair<int,int>{34,5})!=attacks.end());
        assert(actors.at({34,0}).pose.x==actors.at({0,0}).pose.x&&actors.at({34,0}).localY==actors.at({0,0}).localY&&actors.at({34,0}).pose.z==actors.at({0,0}).pose.z);
        for(unsigned n=0;n<100&&(actors.at({34,0}).flags&1);++n)step();assert(!(actors.at({34,0}).flags&1));
        parkBoss=true;actors.at({5,0}).pose={0,0,1.25f,edactormath::HalfPi};
        for(unsigned n=0;n<2500&&vm.field(boss,13).integer()>0;++n){actors.playerAction(17);step();}
        assert(vm.field(boss,13).integer()==0&&bossHealth.size()==15);for(unsigned n=0;n<15;++n)assert(bossHealth[n]==std::max(0,100-7*int(n+1)));
        parkBoss=false;for(unsigned n=0;n<350&&vm.field(boss,game.hostField(HostFieldRole::GenericNpc30)).integer()!=1;++n)step();assert(vm.field(boss,game.hostField(HostFieldRole::GenericNpc30)).integer()==1&&!(actors.at({5,0}).flags&1));
        vm.invoke(p.methodIndex("LevelA","endScene","()V"),game.level);routes.forgetActors(actors.unloadScene());routes.setScene(payload);actors.resetSceneRuntime();
        std::cout<<"TreeBoss source5/34 link, real input17->fixed7 hurt26/save replay, AI10/20HP/Arm5HP,15hits/death24: passed\n";
    }
    {
        game.enterScene("ac05");vm.invoke(p.methodIndex("SceneCommander","sceneReset","()V"));
        std::ifstream sceneFile(root/"actors/troopleaderboss/source-scene.native",std::ios::binary);
        const ScriptScene geometry(std::vector<uint8_t>(std::istreambuf_iterator<char>(sceneFile),{}));
        bind("actorInTrigger",[&](auto&,auto,const auto& a){const auto& b=actors.at({a[0].integer(),a[1].integer()});
            return I(geometry.inside(unsigned(a[2].integer()),{b.pose.x,b.localY,b.pose.z}));});
        bind("actorIsFacingMarker",[&](auto&,auto,const auto& a){const auto& b=actors.at({a[0].integer(),a[1].integer()});
            return I(geometry.facing(unsigned(a[2].integer()),float(a[3].integer()),{b.pose.x,b.pose.z},b.pose.yaw));});
        unsigned shake=0,light=0;bind("scrFxShake",[&](auto&,auto,const auto& a){assert(a[0].integer()==45);++shake;return jpc::Value{};});
        bind("lightSetColor",[&](auto&,auto,const auto& a){assert(a.size()==5);++light;return jpc::Value{};});
        const auto boss=actors.createType(10);assert(boss&&!actors.createType(10));const edactors::Key key{10,0};
        assert(vm.objectClass(boss)==p.classIndex("TroopLeaderBoss")&&vm.objectClass(vm.field(boss,4).reference())==p.classIndex("ed/base/EDBrain"));
        assert(vm.field(boss,13).integer()==100&&vm.field(boss,1212).integer()==100&&vm.field(boss,1424).integer()==0);
        assert(vm.arrayValues(vm.staticField(54).reference())[10].integer()==27&&actors.at(key).parts==std::vector<bool>{true});
        assert(actors.at(key).actions.states.size()==22&&actors.at(key).localPose.rotationsDegrees.size()==18&&actors.at(key).vertices.size()==1422);
        vm.invoke(p.methodIndex("SceneAC05","spawnMonster1","(LGenericNpc;III)V"),game.scene,{jpc::Value::reference(boss),I(0),I(0),I(0)});
        assert(vm.field(game.scene,1502).reference()==boss);
        auto place=[&](edactors::Key identity,GroundPoint point,float yaw){auto& b=actors.at(identity);b.pose={point.x,point.y,point.z,yaw};b.localY=point.y-b.cachedFloorHeight;};
        uint64_t tick=actors.cloneState().sourceTick;bool park=true;
        auto step=[&](){const auto thinker=actors.at(key).think;if(thinker&&vm.status(thinker)!=jpc::Interpreter::ThreadStatus::Complete){
                if(park&&vm.status(thinker)!=jpc::Interpreter::ThreadStatus::Held)vm.hold(thinker);
                if(!park&&vm.status(thinker)==jpc::Interpreter::ThreadStatus::Held)vm.awake(thinker);}
            milliseconds=int(++tick*1000/30);vm.statics[398]=I(milliseconds);actors.update(tick);};
        place({0,0},{0,0,0},0);place(key,{0,0,1},edactormath::Pi);
        for(unsigned n=0;n<100&&(n<3||actors.at(key).actions.layers[0].state!=0);++n)step();
        const int ashHealth=vm.field(game.ash,13).integer();
        const auto attack=vm.start(p.methodIndex("TroopLeaderBoss","actionAttack","()V"),boss);
        const auto attackVm=vm.snapshot(),attackActors=actors.snapshot(),attackRoutes=routes.snapshot();const auto attackTick=tick;
        for(unsigned n=0;n<180&&vm.status(attack)!=jpc::Interpreter::ThreadStatus::Complete;++n)step();
        assert(vm.status(attack)==jpc::Interpreter::ThreadStatus::Complete&&vm.field(game.ash,13).integer()==ashHealth-15);
        const auto hitTick=tick;vm.restore(attackVm);actors.restore(attackActors);routes.restore(attackRoutes);tick=attackTick;
        for(unsigned n=0;n<180&&vm.status(attack)!=jpc::Interpreter::ThreadStatus::Complete;++n)step();
        assert(tick==hitTick&&vm.field(game.ash,13).integer()==ashHealth-15);vm.forget(attack);
        // Ordinary actual melee contacts cause the source dodge/end34 reaction,
        // never damage this boss:1770 subtracts the literal zero.
        for(unsigned n=0;n<180;++n){actors.playerAction(17);step();}assert(vm.field(boss,13).integer()==100);
        auto pillar=[&](int n,bool facing=true){const auto player=geometry.volumes[size_t(n-1)].points[0];
            const auto monster=geometry.volumes[size_t(n+2)].points[0],marker=geometry.markers[size_t(n)];
            const float heading=float(std::atan2(double(marker.z)-player.z,double(marker.x)-player.x));
            place({0,0},player,edactormath::modelYawFromHeading(heading)+(facing?0:edactormath::Pi));place(key,monster,0);
            assert(geometry.inside(unsigned(n-1),player)&&geometry.inside(unsigned(n+2),monster));
            actors.playerAction(17);};
        pillar(1,false);assert(vm.field(boss,13).integer()==100&&shake==0&&light==0);
        pillar(1);assert(vm.field(boss,13).integer()==89&&vm.field(game.scene,1508).integer()==1&&shake==1&&light==1);
        const auto puzzleVm=vm.snapshot(),puzzleActors=actors.snapshot(),puzzleRoutes=routes.snapshot();const auto puzzleTick=tick;
        for(unsigned n=0;n<100;++n)step();const auto replayTick=tick;
        vm.restore(puzzleVm);actors.restore(puzzleActors);routes.restore(puzzleRoutes);tick=puzzleTick;
        for(unsigned n=0;n<100;++n)step();assert(tick==replayTick&&vm.field(boss,13).integer()==89);
        pillar(1);assert(vm.field(boss,13).integer()==89&&vm.field(game.scene,1508).integer()==1); // Consecutive pillar rejected by original1877.
        for(int n:{2,3,1,2,3,1,2,3,1}) {
            const int before=vm.field(boss,13).integer();pillar(n);assert(vm.field(boss,13).integer()==std::max(0,before-11));
            for(unsigned wait=0;wait<110&&vm.field(boss,13).integer()>0;++wait)step();
        }
        assert(vm.field(boss,13).integer()==0&&vm.field(game.scene,1508).integer()==5&&vm.field(game.scene,1509).integer()==4&&vm.field(game.scene,1510).integer()==4);
        park=false;for(unsigned n=0;n<650&&vm.field(boss,game.hostField(HostFieldRole::GenericNpc30)).integer()!=1;++n)step();
        assert(vm.field(boss,game.hostField(HostFieldRole::GenericNpc30)).integer()==1&&!(actors.at(key).flags&1));
        const auto finishedVm=vm.snapshot(),finishedActors=actors.snapshot(),finishedRoutes=routes.snapshot();
        vm.restore(finishedVm);actors.restore(finishedActors);routes.restore(finishedRoutes);assert(actors.snapshot()==finishedActors);
        vm.invoke(p.methodIndex("LevelA","endScene","()V"),game.level);routes.forgetActors(actors.unloadScene());routes.setScene(payload);actors.resetSceneRuntime();
        std::cout<<"TroopLeader10 original100HP/brain/cap1, attack15/save replay, meleeimmune, actualAC05pillars input17->10x11HP/death24/save: passed\n";
    }
    levelaBossTests(game,actors,routes,milliseconds,initialScopeVm,initialScopeActors,initialScopeRoutes);
    // Native403830 fills the FIRST empty physical slot, independent of arena order.
    actors.unregister({4,0});assert(actors.at({4,0}).nativeSlot==-1);assert(actors.createType(6));
    assert(actors.at({6,0}).nativeSlot==1&&actors.at({6,0}).allocationOrder==9);
    for(int type:{7,11,8})assert(actors.createType(type));assert(actors.createType(9)&&actors.createType(9));
    assert(actors.at({9,0}).nativeSlot==12&&actors.at({9,1}).nativeSlot==-1); // Full table still returns a valid per-type index.
    for(const auto& [key,unused]:actors.bodies()){(void)unused;actors.at(key).flags=0x40;}
    auto& overflow=actors.at({9,1});overflow.flags=1;assert(actors.consoleAction({9,1},54));
    uint64_t tick=actors.cloneState().sourceTick;for(unsigned n=0;n<100&&overflow.actions.playbacks[size_t(overflow.actions.layers[0].playback)].mode==4;++n)actors.update(++tick);
    assert(overflow.actions.playbacks[size_t(overflow.actions.layers[0].playback)].mode!=4);
    overflow.actions.playbacks[size_t(overflow.actions.layers[0].playback)].frame=12;applied.clear();actors.update(++tick);
    assert(unregisteredVisuals==1&&overflow.think==0&&applied.empty()); // Global animation cadence is independent of physical update slots.
    for(const auto& [key,unused]:actors.bodies()){(void)unused;actors.at(key).flags=1;}
    floorQueries=0;actors.sampleFloors();assert(floorQueries==13);
    const auto physicalState=actors.snapshot();auto corrupt=actors.cloneState();corrupt.bodies[1].nativeSlot=0;
    rejects([&]{actors.restore(corrupt);},"physical actor slot");assert(actors.snapshot()==physicalState);
    actors.restore(physicalState);assert(actors.at({9,1}).nativeSlot==-1&&actors.at({6,0}).nativeSlot==1);
    for(int type:edactors::SupportedTypes)if(type)for(int index=0;index<edactors::actorCapacity(type);++index)
        if(!actors.bodies().count({type,index})){const auto before=actors.bodies().size();assert(actors.createType(type)&&actors.bodies().size()==before+1);}
    assert(actors.bodies().size()==edactors::MaxBodies);const auto fullVM=vm.snapshot(),fullPool=actors.snapshot();
    vm.restore(fullVM);actors.restore(fullPool);assert(actors.snapshot()==fullPool);
    std::cout<<"Original LevelA1778 arena prefix/endScene639, source special actor teardown/return/new brain, wait rollback and44body memory/save: passed\n";
    std::cout<<"Native first-null13 registration/hole reuse/full-table unregistered data, physical update/BV gates and independent raw animation: passed\n";
}
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("Pass exported asset directory");const std::filesystem::path root=argv[1];
        ScriptGame game(root/"scripts"/"levelA.jpc");game.enterScene("ak09");
        unsigned padBindings=0,effects=0,randomCalls=0;
        game.vm.bind("ed/base/External","guiItemSetPadObj","(ILed/base/EDBase;)V",[&](auto&,auto,const auto&){++padBindings;return jpc::Value{};});
        edactors::Host host;
        host.spawnPose=[](auto key,int,int,int){return edactors::Pose{0,0,key.type==4?1.0f:0,0};};
        host.floorHeight=[](float,float){return 0.0f;};
        host.lineOfSight=[](auto,auto){return true;};host.visibleArea=[](auto){return true;};
        host.facing=[](auto,auto,int){return true;};host.random=[&](int){++randomCalls;return 0;};
        host.move=[](auto& body,int forward,int strafe,int vertical){
            edactormath::appendMovement(body.pendingMove,forward,strafe,vertical,{{0,body.pose.yaw,0}},body.motionFlags);return 1;
        };
        host.applyMovement=[](auto& body){
            const auto movement=edactormath::pendingToWorld(body.pendingMove,{{0,body.pose.yaw,0}},body.motionFlags);
            body.pose.x+=movement[0];body.pose.y+=movement[1];body.pose.z+=movement[2];
        };
        host.pickupObjects=[](){return std::vector<edactors::PickupObject>{};};
        host.effect=[&](const std::string&,const std::vector<int>&){++effects;return 0;};
        host.animationEvent=[](const auto& body,const auto& raw){const auto event=edanimevent::decode(raw);
            assert((body.key.type==5||body.key.type==34)||(event.kind==7&&event.mode==-1)||
                (event.mode==1&&((body.key.type==17&&event.kind==25)||((body.key.type==14||body.key.type==18)&&event.kind==24)))||
                (body.key.type==14&&event.mode==-1&&(event.kind==9||event.kind==16))||
                (event.mode==-1&&event.kind==20&&(body.key.type==2||body.key.type==4||body.key.type==7||body.key.type==8||
                    body.key.type==9||body.key.type==10||body.key.type==11||body.key.type==12||body.key.type==23))||
                (body.key.type==0&&(event.kind==0||event.kind==6||event.kind==8||event.kind==9||event.kind==20||event.kind==35))||
                (event.mode==-1&&(((body.key.type==4||body.key.type==35)&&event.kind==17)||
                    (body.key.type==4&&event.kind==16)||((body.key.type==7||body.key.type==8||body.key.type==11||body.key.type==12||
                    body.key.type==19||body.key.type==23||body.key.type==26||body.key.type==31)&&event.kind==21))));};
        host.animationSound=[](const auto& body,const auto&){assert(edanimsound::verifiedActor(body.key.type));};
        host.soundMaterials=[](const auto&){return edanimsound::Materials{};}; // This flat CPU fixture has no source material volumes.
        edactors::GameplayActors actors(game,root,host);
        const auto ash=actors.initializeAsh();auto& vm=game.vm;
        assert(ash==game.ash&&vm.field(ash,6).integer()==0&&vm.field(ash,13).integer()==100);
        assert(game.package.classes[size_t(vm.objectClass(vm.field(ash,102).reference()))].name=="Chainsaw");
        assert(game.package.classes[size_t(vm.objectClass(vm.field(ash,103).reference()))].name=="Axe");
        const auto& ashBody=actors.at({0,0});assert(ashBody.radius==80);
        assert(ashBody.vertices.size()==1710);
        const auto joint=actors.jointWorld({0,0},0),offsetJoint=actors.jointWorld({0,0},0,{{100,0,0}});
        assert(std::abs(offsetJoint[0]-joint[0]+1)<1e-6f&&offsetJoint[1]==joint[1]&&offsetJoint[2]==joint[2]);

        for(size_t part=0;part<ashBody.parts.size();++part)assert(ashBody.parts[part]==(part==0||part==6||part==12));
        // Exact helper sequence after640 in original LevelA.create1778.
        vm.invoke(game.package.methodIndex("ed/base/EDActor","flagSubEngine","(I)V"),ash,{I(8)});
        vm.invoke(game.package.methodIndex("ed/base/EDActor","flagAddEngine","(I)V"),ash,{I(1)});
        vm.invoke(game.package.methodIndex("ed/base/EDActor","flagSubEngine","(I)V"),ash,{I(32)});
        assert(actors.at({0,0}).flags==23);
        const auto randomBefore=randomCalls;const auto nativeRandom=game.package.methodIndex("ed/base/External","random","(I)I");
        assert(vm.invoke(nativeRandom,0,{I(0)}).integer()==0&&randomCalls==randomBefore);
        assert(vm.invoke(nativeRandom,0,{I(-5)}).integer()==0&&vm.invoke(nativeRandom,0,{I(std::numeric_limits<int32_t>::min())}).integer()==0&&randomCalls==randomBefore+2);
        vm.invoke(game.package.methodIndex("ed/base/EDActor","flagSubEngine","(I)V"),ash,{I(10)});
        assert(actors.at({0,0}).flags==(23^10));actors.at({0,0}).flags=23;

        assert(actors.consoleAction({0,0},116));
        for(uint64_t tick=1;tick<=35;++tick)actors.update(tick);
        assert(actors.at({0,0}).actions.layers[0].state==0&&!actors.at({0,0}).vertices.empty());
        const auto before=actors.at({0,0}).pose;vm.invoke(game.package.methodIndex("ed/base/External","actorMoveForward","(III)I"),0,{I(0),I(0),I(10)});
        assert(actors.at({0,0}).pose.z==before.z&&std::abs(actors.at({0,0}).pendingMove[2]-0.1f)<1e-7f);
        const auto movementVm=vm.snapshot(),movementActors=actors.snapshot();actors.update(36);
        const auto applied=actors.at({0,0}).pose;
        assert(std::abs(applied.z-before.z-0.1f)<0.005f&&actors.at({0,0}).pendingMove[2]==0);
        vm.restore(movementVm);actors.restore(movementActors);assert(actors.at({0,0}).pose.z==before.z&&std::abs(actors.at({0,0}).pendingMove[2]-0.1f)<1e-7f);
        actors.update(36);assert(actors.at({0,0}).pose.z==applied.z&&actors.at({0,0}).pose.x==applied.x);
        const auto enemy=actors.createDeadite();assert(enemy&&vm.field(enemy,6).integer()==4&&vm.field(enemy,13).integer()==100);
        assert(actors.at({4,0}).radius==30);
        const auto originalPose=actors.at({4,0}).pose;actors.at({4,0}).pose=actors.at({0,0}).pose;
        const auto distance=game.package.methodIndex("ed/base/External","actorDistanceToActor","(IIII)I");
        assert(vm.invoke(distance,0,{I(0),I(0),I(4),I(0)}).integer()==70);
        vm.invoke(game.package.methodIndex("ed/base/External","actorFaceActor","(IIII)V"),0,{I(0),I(0),I(4),I(0)});
        assert(actors.at({0,0}).pose.yaw==edactormath::HalfPi);
        assert(vm.invoke(game.package.methodIndex("ed/base/External","actorGetAngle","(II)I"),0,{I(0),I(0)}).integer()==0);
        actors.at({0,0}).pose.yaw=0;actors.at({4,0}).pose=originalPose;

        vm.invoke(game.package.methodIndex("GenericNpc","resetToBehaviorImmediateBirth","(IZ)V"),enemy,{I(1),I(1)});
        const auto birth=vm.start(game.package.methodIndex("GenericNpc","actionDiveUp","()Z"),enemy);uint64_t tick=36;
        for(unsigned n=0;n<100&&vm.status(birth)!=jpc::Interpreter::ThreadStatus::Complete;++n)actors.update(++tick);
        assert(vm.status(birth)==jpc::Interpreter::ThreadStatus::Complete);vm.forget(birth);
        assert(actors.at({4,0}).think&&actors.at({4,0}).actions.layers[0].state==1);
        for(int hit=1;hit<=13;++hit) {
            // Original Axe is secondary after659: source selector-1, no weapon-field patch.
            actors.post({{4,0},{0,0},5,{{-1,0,0,0}}});const int expected=std::max(0,100-8*hit);
            for(unsigned n=0;n<100;++n) {
                actors.update(++tick);
                if(hit==1&&n==0) {
                    const auto vmSave=vm.snapshot();const auto actorSave=actors.cloneState();const auto actorBytes=actors.snapshot();
                    assert(vm.field(enemy,13).integer()==92&&vm.status(actors.at({4,0}).think)==jpc::Interpreter::ThreadStatus::Held);
                    for(unsigned skip=0;skip<3;++skip)actors.update(++tick);
                    vm.restore(vmSave);actors.restore(actorBytes);tick=actorSave.sourceTick;
                    assert(vm.field(enemy,13).integer()==92&&actors.at({4,0}).actions.updates==actorSave.bodies[1].updates);
                    auto bad=actorSave;bad.bodies[1].layers[0].historyIndex=10;
                    rejects([&]{actors.restore(bad);},"actor layer");assert(vm.field(enemy,13).integer()==92&&actors.cloneState().sourceTick==tick);
                    bad=actorSave;bad.bodies[1].pose.y=std::numeric_limits<float>::quiet_NaN();
                    rejects([&]{actors.restore(bad);},"Non-finite");assert(actors.cloneState().sourceTick==tick);
                    auto truncated=actorBytes;truncated.pop_back();bool rejected=false;
                    try{actors.restore(truncated);}catch(const std::runtime_error&){rejected=true;}assert(rejected&&actors.snapshot()==actorBytes);
                }
                if(vm.field(enemy,13).integer()==expected&&vm.field(enemy,11).integer()==0)break;
            }
            assert(vm.field(enemy,13).integer()==expected&&vm.field(enemy,11).integer()==0);
        }
        for(unsigned n=0;n<300&&vm.field(enemy,game.hostField(HostFieldRole::GenericNpc30)).integer()!=1;++n)actors.update(++tick);
        assert(vm.field(enemy,game.hostField(HostFieldRole::GenericNpc30)).integer()==1&&!(actors.at({4,0}).flags&1));
        assert(!actors.at({0,0}).vertices.empty());
        // Source643 reuses the finished script/native slot; only four native type4 slots exist.
        assert(actors.createDeadite()==enemy);
        for(unsigned index=1;index<4;++index)assert(actors.createDeadite());
        assert(!actors.createDeadite()&&actors.bodies().size()==5);
        const auto bat=actors.createSkullBat();assert(bat&&vm.field(bat,6).integer()==2);
        assert(actors.at({2,0}).localPose.rotationsDegrees.size()==5&&actors.at({2,0}).actions.states.size()==9);
        const auto beforeReset=actors.snapshot();vm.invoke(game.package.methodIndex("ActorDB","reinitialize","()V"));
        assert(actors.snapshot()==beforeReset); // Real639 resets Deadite/SkullBat to their exported default texture.
        const auto poolVm=vm.snapshot(),poolActors=actors.snapshot();vm.restore(poolVm);actors.restore(poolActors);
        ScriptGame missing(root/"scripts"/"levelA.jpc");edactors::GameplayActors unsupported(missing,root,{});
        rejects([&]{unsupported.initializeAsh();},"spawnPose");assert(unsupported.bodies().empty());
        turnCallbackTests(root,host);rootMotionTests(root,host);historicalActiveChildTests(root,host);floorPhaseTests(root,host);menuPhaseTests(root,host);rotationSaveTests(root,host);textureStateTests(root,host);soundPhaseTests(root,host);contactNegativeTests(root,host);objectContactTests(root,host);pickupReservationTests(root,host);
        inputCombat(root,host,17);inputCombat(root,host,15);inputCombat(root,host,17,6);
        for(int type:{11,12,13,35,7,8,9})inputCombat(root,host,17,type);skeletonLifecycleTests(root,host);ah05LifecycleTests(root,host);rangedTroopTests(root,host);teardownTests(root,host);
        std::cout<<"Original Ash659/Use116/Deadite641/Axe13hits/AIdeath24/pending native-state restore/pool4: passed\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}


