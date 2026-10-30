#include "actor_startup_fixture.h"
#include "source_vfx.h"

int main(int argc,char** argv){try{
    if(argc<2||argc>3)throw std::runtime_error("Expected LevelA assets and optional isolated VFX root");
    const std::filesystem::path root=argv[1],vfxRoot=argc==3?argv[2]:argv[1];
    for(int type:{11}){unsigned emitted=0;uint64_t tick=0;bool testing=false;
        edvfx::Host host;host.randomBound=host.cachedBound=[](int){return 0;};host.visible=[](auto){return true;};
        host.coarseCell=[](float,float){return int16_t(0);};host.coarseHeight=[](float,float){return 0.f;};host.floorSample=[](float,float,float){return 0.;};
        edvfx::SourceVfx effects(vfxRoot,host);
        StartupCase source(root/"scripts/levelA.jpc",root,false,[&](auto& input){input.animationEvent=[&](const auto& body,const auto& raw){
            if(body.key.type!=type||!testing)return;const auto event=edanimevent::decode(raw);if(event.kind!=21)return; // Other presentation has dedicated source proofs.
            assert(event.mode==-1);
            const uint32_t clip=0x079cb404;const unsigned frame=22;assert(event.frame==frame);
            bool actual=false;for(const auto& playback:body.actions.playbacks)if(playback.active&&playback.mode!=4)
                actual|=body.actions.states[size_t(playback.state)].animation==clip&&playback.frame==frame;assert(actual);
            assert(!edanimevent::scriptVfxCallback(21));
            const auto ends=edanimevent::visualEndpoints(event,{{0,body.pose.yaw,0}},{{body.pose.x,body.pose.y,body.pose.z}});
            assert(effects.line(21,ends.first,ends.second,event.mode));++emitted;
        };});auto& actors=*source.actors;auto& vm=source.game.vm;const auto& p=source.game.package;
        source.game.enterScene("ah05");vm.invoke(p.methodIndex("SceneCommander","sceneReset","()V"));const auto npc=actors.createType(type);assert(npc);
        for(const auto& [key,unused]:actors.bodies()){(void)unused;auto& body=actors.at(key);body.think=vm.enqueue(p.methodIndex("ActorDB","think","(II)V"),0,{I(key.type),I(key.index)});
            vm.invoke(p.methodIndex("ActorDB","setThinkThreadId","(III)V"),0,{I(key.type),I(key.index),I(int32_t(body.think))});vm.hold(body.think);}
        for(int mask:{8,32,64})vm.invoke(p.methodIndex("ed/base/EDActor","flagSubEngine","(I)V"),npc,{I(mask)});
        vm.invoke(p.methodIndex("ed/base/EDActor","flagAddEngine","(I)V"),npc,{I(1)});assert(actors.consoleAction({type,0},64));
        auto step=[&](){vm.statics[398]=I(int32_t(++tick*1000/30));actors.update(tick);effects.update(tick);};
        testing=true;const auto oldTick=tick;const auto oldVm=vm.snapshot(),oldActors=actors.snapshot(),oldEffects=effects.snapshot();
        auto run=[&](){for(unsigned n=0;n<100&&!emitted;++n)step();
            assert(emitted==1&&effects.globals().nextSerial>30);return tick;};const auto end=run();
        const auto after=effects.snapshot(),afterActors=actors.snapshot(),afterVm=vm.snapshot();assert(!effects.draw({{1,0,0}},{{0,1,0}},{}).empty());
        vm.restore(oldVm);actors.restore(oldActors);effects.restore(oldEffects);tick=oldTick;emitted=0;
        assert(run()==end&&effects.snapshot()==after&&actors.snapshot()==afterActors&&vm.snapshot()==afterVm);
        std::cout<<"source NPC"<<type<<" realstartup/console64 -> original raw21 -> nativeparticles/death18 and VM/native/VFX replay passed\n";
    }return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
