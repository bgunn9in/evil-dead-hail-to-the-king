#include "script_game.h"
#include "actor_actions.h"
#undef NDEBUG
#include <cassert>
#include <iostream>
#include <memory>

static jpc::Value I(int32_t n){return jpc::Value::integer(n);}
static jpc::Value R(jpc::Ref n){return jpc::Value::reference(n);}
struct Message {int type=0,index=0,sender=-1,senderIndex=-1,event=0,value=0,a=0,b=0;};
struct NativeEnemy {
    jpc::Ref script=0;
    edactions::ActorActions actions;
    int flags=0,radius=0;
    explicit NativeEnemy(const std::vector<edactions::State>& states):actions(states){}
};
struct CombatCase {
    ScriptGame game;
    std::vector<edactions::State> source;
    std::vector<std::unique_ptr<NativeEnemy>> enemies;
    std::vector<Message> messages;
    std::map<std::string,unsigned> nativeCalls;
    std::map<jpc::Interpreter::ThreadID,unsigned> scheduled;
    edactions::ActorActions ashActions;
    explicit CombatCase(const std::filesystem::path& script,const std::filesystem::path& actions,const std::filesystem::path& ash)
        :game(script),source(edactions::load(actions)),ashActions(edactions::load(ash)) {
        auto& vm=game.vm;const auto& p=game.package;
        game.enterScene("ak09");vm.invoke(p.methodIndex("ActorDB","initialize","()V"));
        vm.setField(game.ash,6,I(0));vm.invoke(p.methodIndex("ed/base/EDActor","initialize","()V"),game.ash);
        const int ashOffset=vm.arrayValues(vm.staticField(54).reference())[0].integer();
        vm.arrayValues(vm.staticField(53).reference())[size_t(ashOffset)]=R(game.ash);
        const auto axe=vm.makeObject(p.classIndex("Axe"));
        vm.invoke(p.methodIndex("Axe","<init>","(Led/base/EDNpc;)V"),axe,{R(game.ash)});vm.setField(game.ash,102,R(axe));
        const std::set<std::string> names={"actorCreate","registerActorMessageInterest","actorFlagsSet","actorFlagsAdd","actorFlagsSub","actorFlagsGet",
            "actorSetAttrFOV","actorSetAttrSight","actorSetBRadius","actorSetPositionFloorHeight","actorDoConsoleAction","actorSetVisible",
            "actorHaveLOSToActor","sceneIsNPCInVisArea","actorDistanceToActor","actorIsFacingActor","stateTransitionIsPossible","random",
            "actorFaceActor","actorEffectFadeInfo","actorEffectFadeStart","vfxSpawnActorJoint","vfxSpawnActorOffset","sendActorMessage"};
        for(const auto& method:p.methods)if(p.classes[size_t(method.owner)].name=="ed/base/External"&&names.count(method.name)) {
            const auto name=method.name,signature=method.signature;
            vm.bind("ed/base/External",name,signature,[this,name,signature](auto&,auto,const auto& values) {
                ++nativeCalls[name];std::vector<int> args;for(auto value:values)args.push_back(value.integer());
                if(name=="actorCreate") {assert(args[0]==4);const auto index=enemies.size();enemies.push_back(std::make_unique<NativeEnemy>(source));return I(int(index));}
                if(name=="random")return I(0);
                if(name=="actorHaveLOSToActor"||name=="sceneIsNPCInVisArea"||name=="actorIsFacingActor")return I(1);
                // These are explicit native contact inputs, not a replacement for source damage/AI logic.
                if(name=="actorDistanceToActor")return I(100);
                if(name=="sendActorMessage") {messages.push_back({args[3],args[4],args[0],args[1],args[2],args[5],args[6],args[7]});return jpc::Value{};}
                if(args.size()>=2&&args[0]==4) {
                    auto& enemy=*enemies.at(size_t(args[1]));
                    if(name=="actorFlagsSet")enemy.flags=args[2];
                    if(name=="actorFlagsAdd")enemy.flags|=args[2];
                    if(name=="actorFlagsSub")enemy.flags&=~args[2];
                    if(name=="actorFlagsGet"||name=="actorFlagsSet"||name=="actorFlagsAdd"||name=="actorFlagsSub")return I(enemy.flags);
                    if(name=="actorSetBRadius")enemy.radius=args[2];
                    if(name=="actorSetVisible")enemy.flags=args[2]?enemy.flags|1:enemy.flags&~1;
                    if(name=="actorDoConsoleAction")return I(enemy.actions.command(args[2])?1:0);
                    if(name=="stateTransitionIsPossible")return I(enemy.actions.possible(args[2])?1:0);
                }
                if(args.size()>=2&&args[0]==0) {
                    if(name=="actorDoConsoleAction")return I(ashActions.command(args[2])?1:0);
                    if(name=="stateTransitionIsPossible")return I(ashActions.possible(args[2])?1:0);
                }
                return signature.back()=='V'?jpc::Value{}:I(0);
            });
        }
    }
    jpc::Ref create() {
        const auto ref=game.vm.invoke(game.package.methodIndex("ActorDB","createDeadite","(III)LDeadite;"),0,{I(0x040c6000),I(0x048c6000),I(0x080c6000)}).reference();
        const int index=game.vm.field(ref,5).integer();enemies.at(size_t(index))->script=ref;
        assert(game.vm.field(ref,13).integer()==100&&game.vm.field(ref,6).integer()==4);return ref;
    }
    jpc::Interpreter::ThreadID dispatch(Message m) {
        const auto id=game.vm.start(game.package.methodIndex("ActorDB","processMessage","(IIIIIIIIIII)I"),0,
            {I(m.type),I(m.index),I(m.type),I(m.index),I(m.sender),I(m.senderIndex),I(m.event),I(m.value),I(m.a),I(m.b),I(0)});
        game.vm.notify(m.type,m.index,m.event);return id;
    }
    void pump() {
        game.vm.advanceFrame();
        for(unsigned i=0;i<enemies.size();++i) {
            auto& enemy=*enemies[i];if(!(enemy.flags&1))continue;enemy.actions.tick();
            for(auto event:enemy.actions.events)messages.push_back({4,int(i),-1,-1,event.event,0,0,0});enemy.actions.events.clear();
        }
        ashActions.tick();for(auto event:ashActions.events)messages.push_back({0,0,-1,-1,event.event,0,0,0});ashActions.events.clear();
        while(!messages.empty()) {const auto pending=std::move(messages);messages.clear();for(auto message:pending){const auto id=dispatch(message);if(game.vm.status(id)==jpc::Interpreter::ThreadStatus::Complete)game.vm.forget(id);}}
        for(const auto id:game.vm.runReady())++scheduled[id];
    }
    void until(jpc::Interpreter::ThreadID thread,unsigned maximum=300) {
        for(unsigned n=0;n<maximum&&game.vm.status(thread)!=jpc::Interpreter::ThreadStatus::Complete;++n)pump();
        assert(game.vm.status(thread)==jpc::Interpreter::ThreadStatus::Complete);game.vm.forget(thread);
    }
    jpc::Interpreter::ThreadID startThink(int index) {
        // Native4018b0 creates one ActorDB.think thread per actor, then calls647.
        const auto thread=game.vm.enqueue(game.package.methodIndex("ActorDB","think","(II)V"),0,{I(4),I(index)});
        game.vm.invoke(game.package.methodIndex("ActorDB","setThinkThreadId","(III)V"),0,{I(4),I(index),I(int32_t(thread))});
        return thread;
    }
};
int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::runtime_error("Pass levelA.jpc,deadite/actions.bin,actions.bin");CombatCase test(argv[1],argv[2],argv[3]);
        const auto enemy=test.create();assert(test.enemies[0]->radius==30);
        auto& vm=test.game.vm;const auto& p=test.game.package;
        const auto dead=test.game.hostField(HostFieldRole::GenericNpc30);
        vm.invoke(p.methodIndex("GenericNpc","resetToBehaviorImmediateBirth","(IZ)V"),enemy,{I(1),I(1)});
        const auto birth=vm.start(p.methodIndex("GenericNpc","actionDiveUp","()Z"),enemy);
        test.until(birth);assert(test.enemies[0]->actions.layers[0].state==1);
        const auto attack=vm.start(p.methodIndex("Deadite","actionAttack","()V"),enemy);test.until(attack);
        for(unsigned frame=0;frame<30&&vm.field(test.game.ash,114).integer();++frame)test.pump();
        assert(vm.field(test.game.ash,13).integer()==92&&test.nativeCalls["sendActorMessage"]==1);
        for(int hit=0;hit<13;++hit) {const auto event=test.dispatch({4,0,0,0,5,-2,0,0});test.until(event);assert(vm.field(enemy,13).integer()==std::max(0,100-8*(hit+1)));}
        const auto buddy=test.create();
        vm.invoke(p.methodIndex("GenericNpc","resetToBehaviorImmediateBirth","(IZ)V"),buddy,{I(1),I(1)});
        test.until(vm.start(p.methodIndex("GenericNpc","actionDiveUp","()Z"),buddy));
        const auto brain=test.startThink(0),buddyBrain=test.startThink(1);
        assert(brain!=buddyBrain&&vm.field(enemy,10).integer()==int32_t(brain)&&vm.field(buddy,10).integer()==int32_t(buddyBrain));
        vm.runReady();
        unsigned buddyRunsWhileDying=0;
        for(unsigned frame=0;frame<300&&vm.field(enemy,dead).integer()!=1;++frame) {
            const auto before=test.scheduled[buddyBrain];
            test.pump();
            if(vm.field(enemy,dead).integer()!=1)buddyRunsWhileDying+=test.scheduled[buddyBrain]-before;
        }
        assert(vm.field(enemy,dead).integer()==1&&vm.field(enemy,13).integer()==0);
        // Its own original think651 continues despite the first NPC's death wait.
        // SceneCommander permissions remain source-controlled; no attack is forced.
        assert(buddyRunsWhileDying>20&&vm.field(buddy,13).integer()==100&&vm.field(buddy,dead).integer()==0);
        assert(vm.status(brain)==jpc::Interpreter::ThreadStatus::Waiting&&vm.status(buddyBrain)==jpc::Interpreter::ThreadStatus::Waiting);
        const auto hit=test.dispatch({4,1,0,0,5,-2,0,0});
        assert(vm.field(buddy,13).integer()==92&&vm.status(buddyBrain)==jpc::Interpreter::ThreadStatus::Held);
        const auto heldCombat=vm.snapshot();vm.restore(heldCombat);
        assert(vm.field(buddy,13).integer()==92&&vm.status(buddyBrain)==jpc::Interpreter::ThreadStatus::Held);
        test.until(hit);
        assert(vm.field(buddy,13).integer()==92&&vm.field(buddy,11).integer()==0&&vm.status(buddyBrain)!=jpc::Interpreter::ThreadStatus::Held);
        std::cout<<"Original Deadite100HP/attack/Axe13hits/AIdeath/end24/two-NPC threads: passed\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
