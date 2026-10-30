#include "actor_startup_fixture.h"
#include "source_vfx.h"

int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("Expected original LevelA and LevelB asset roots");const std::filesystem::path a=argv[1],b=argv[2];
    struct Case{bool levelB;int selector,type;const char* scene;uint32_t clip;unsigned frame;std::vector<std::pair<int,int>> steps;};
    const std::array<Case,9> cases{{
        {false,12,12,"ah05",0x079d9404,18,{{64,14}}},
        {false,7,7,"ac01",0x07b1930c,1,{{90,18},{94,19},{64,20}}},
        {false,8,8,"ac01",0x07b05444,1,{{118,17}}},
        {true,19,19,"bc01",0x07841484,4,{{41,17}}},
        {true,23,23,"bl06",0x07999404,5,{{64,15}}},
        {true,26,26,"bt11",0x03db3544,7,{{41,10}}},
        {true,31,31,"ba11",0x07b5f404,2,{{64,16}}},
        {true,36,31,"ba11",0x07b5f404,2,{{64,16}}},
        {true,37,31,"ba11",0x07b5f404,2,{{64,16}}}
    }};
    for(const auto& c:cases){const auto root=c.levelB?b:a;unsigned emitted=0;uint64_t tick=0;bool testing=false;
        edvfx::Host h;h.randomBound=h.cachedBound=[](int){return 0;};h.visible=[](auto){return true;};
        h.coarseCell=[](float,float){return int16_t(0);};h.coarseHeight=[](float,float){return 0.f;};h.floorSample=[](float,float,float){return 0.;};
        edvfx::SourceVfx effects(root,h);
        StartupCase source(root/"scripts"/(c.levelB?"levelB.jpc":"levelA.jpc"),root,c.levelB,[&](auto& host){host.animationEvent=[&](const auto& body,const auto& raw){
            const auto e=edanimevent::decode(raw);if(body.key.type!=c.type||!testing||e.kind!=21)return;
            assert(e.mode==-1&&e.frame==c.frame&&e.enabled);bool actual=false;for(const auto& p:body.actions.playbacks)if(p.active&&p.mode!=4)
                actual|=body.actions.states[size_t(p.state)].animation==c.clip&&p.frame==c.frame;assert(actual&&!edanimevent::scriptVfxCallback(21));
            const auto ends=edanimevent::visualEndpoints(e,{{0,body.pose.yaw,0}},{{body.pose.x,body.pose.y,body.pose.z}});
            assert(effects.line(21,ends.first,ends.second,e.mode));++emitted;
        };});auto& vm=source.game.vm;const auto& package=source.game.package;auto& actors=*source.actors;
        source.game.enterScene(c.scene);vm.invoke(package.methodIndex("SceneCommander","sceneReset","()V"));const auto npc=actors.createType(c.selector);assert(npc);
        for(const auto& [key,unused]:actors.bodies()){(void)unused;auto& body=actors.at(key);body.think=vm.enqueue(package.methodIndex("ActorDB","think","(II)V"),0,{I(key.type),I(key.index)});
            vm.invoke(package.methodIndex("ActorDB","setThinkThreadId","(III)V"),0,{I(key.type),I(key.index),I(int32_t(body.think))});vm.hold(body.think);}
        for(int mask:{8,32,64})vm.invoke(package.methodIndex("ed/base/EDActor","flagSubEngine","(I)V"),npc,{I(mask)});
        vm.invoke(package.methodIndex("ed/base/EDActor","flagAddEngine","(I)V"),npc,{I(1)});
        auto step=[&](){vm.statics[398]=I(int32_t(++tick*1000/30));actors.update(tick);effects.update(tick);};
        for(size_t n=0;n<c.steps.size();++n){const auto [action,target]=c.steps[n];assert(actors.consoleAction({c.type,0},action));
            if(n+1<c.steps.size()){auto active=[&](){for(const auto& p:actors.at({c.type,0}).actions.playbacks)if(p.active&&p.mode!=4&&p.state==target)return true;return false;};
                for(unsigned frame=0;frame<300&&!active();++frame)step();assert(active());}}
        testing=true;const auto oldTick=tick,oldSerial=effects.globals().nextSerial;const auto oldVm=vm.snapshot(),oldActors=actors.snapshot(),oldEffects=effects.snapshot();
        auto run=[&](){for(unsigned n=0;n<180&&!emitted;++n)step();assert(emitted==1&&effects.globals().nextSerial>oldSerial+29);return tick;};
        const auto end=run();const auto afterVm=vm.snapshot(),afterActors=actors.snapshot(),afterEffects=effects.snapshot();assert(!effects.draw({{1,0,0}},{{0,1,0}},{}).empty());
        vm.restore(oldVm);actors.restore(oldActors);effects.restore(oldEffects);tick=oldTick;emitted=0;
        assert(run()==end&&vm.snapshot()==afterVm&&actors.snapshot()==afterActors&&effects.snapshot()==afterEffects);
        std::cout<<"sourceNPC21 "<<(c.levelB?'B':'A')<<" prototype"<<c.selector<<"/native"<<c.type<<" clip"<<std::hex<<c.clip<<std::dec<<"/frame"<<c.frame<<" realFSM/sourceparticles/replay passed\n";
    }return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
