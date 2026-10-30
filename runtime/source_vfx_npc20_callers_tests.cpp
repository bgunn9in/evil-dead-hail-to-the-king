#include "actor_startup_fixture.h"

int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("Expected original LevelA and LevelB asset roots");const std::filesystem::path a=argv[1],b=argv[2];
    struct Case{bool levelB;int type,action;const char* scene;uint32_t clip;};
    const std::array<Case,11> cases{{
        {false,2,0,"ak09",0x05482044},{false,4,0,"ak09",0x050c6044},
        {false,7,90,"ac01",0x07b19304},{false,8,90,"ac01",0x07b05304},{false,9,90,"ac01",0x07b01304},
        {false,10,3,"ac05",0x03c96284},{false,11,90,"ah05",0x079cb304},{false,12,90,"ah05",0x079d9304},
        {true,2,0,"bl06",0x05482044},{true,4,0,"bl06",0x050c6044},{true,23,90,"bl06",0x07999304}
    }};
    for(const auto& c:cases){const auto root=c.levelB?b:a;unsigned seen=0,rngCalls=0;uint64_t tick=0;StartupCase* current=nullptr;
        StartupCase source(root/"scripts"/(c.levelB?"levelB.jpc":"levelA.jpc"),root,c.levelB,[&](auto& h){h.random=[&](int){++rngCalls;return 0;};
            h.animationEvent=[&](const auto& body,const auto& raw){const auto e=edanimevent::decode(raw);if(body.key.type!=c.type||e.kind!=20)return;
                assert(e.mode==-1&&e.enabled);bool actual=false;for(const auto& p:body.actions.playbacks)if(p.active&&p.mode!=4)
                    actual|=body.actions.states[size_t(p.state)].animation==c.clip&&p.frame==e.frame;assert(actual);
                auto& vm=current->game.vm;const int health=vm.field(body.script,13).integer();const auto before=rngCalls;
                std::vector<jpc::Value> args(11,I(0));args[0]=I(c.type);args[1]=I(body.key.index);args[2]=I(20);
                for(unsigned n=0;n<4;++n)args[n+3]=I(body.sfxMaterial[n]);const bool consumed=vm.invoke(648,0,args).integer()!=0;
                assert(consumed==bool(health>=100||health>0)&&rngCalls==before+unsigned(health<100)&&vm.field(body.script,13).integer()==health);++seen;
            };});current=&source;auto& vm=source.game.vm;const auto& package=source.game.package;auto& actors=*source.actors;
        source.game.enterScene(c.scene);vm.invoke(package.methodIndex("SceneCommander","sceneReset","()V"));const auto npc=actors.createType(c.type);assert(npc);
        for(const auto& [key,unused]:actors.bodies()){(void)unused;auto& body=actors.at(key);body.think=vm.enqueue(package.methodIndex("ActorDB","think","(II)V"),0,{I(key.type),I(key.index)});
            vm.invoke(package.methodIndex("ActorDB","setThinkThreadId","(III)V"),0,{I(key.type),I(key.index),I(int32_t(body.think))});vm.hold(body.think);}
        for(int mask:{8,32,64})vm.invoke(package.methodIndex("ed/base/EDActor","flagSubEngine","(I)V"),npc,{I(mask)});
        vm.invoke(package.methodIndex("ed/base/EDActor","flagAddEngine","(I)V"),npc,{I(1)});if(c.action)assert(actors.consoleAction({c.type,0},c.action));
        const auto oldVm=vm.snapshot(),oldActors=actors.snapshot();const auto oldCalls=rngCalls;
        auto run=[&](){for(unsigned n=0;n<180&&!seen;++n){vm.statics[398]=I(int32_t(++tick*33));actors.update(tick);}assert(seen>0);return tick;};
        const auto end=run();const auto afterCalls=rngCalls;const auto afterVm=vm.snapshot(),afterActors=actors.snapshot();const auto count=seen;
        vm.restore(oldVm);actors.restore(oldActors);tick=0;seen=0;rngCalls=oldCalls;
        assert(run()==end&&seen==count&&rngCalls==afterCalls&&vm.snapshot()==afterVm&&actors.snapshot()==afterActors);
        std::cout<<"sourceNPC20 "<<(c.levelB?'B':'A')<<" type"<<c.type<<" actualclip"<<std::hex<<c.clip<<std::dec<<"/648/class1525/HPpredicate/RNGreplay passed\n";
    }return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
