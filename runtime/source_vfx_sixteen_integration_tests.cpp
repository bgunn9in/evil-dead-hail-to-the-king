#include "actor_startup_fixture.h"
#include "source_vfx.h"

int main(int argc,char** argv){try{
    if(argc<2||argc>3)throw std::runtime_error("Expected LevelA assets and optional isolated VFX root");
    const std::filesystem::path root=argv[1],vfxRoot=argc==3?argv[2]:argv[1];
    for(int type:{4}){unsigned emitted=0;uint64_t tick=0;bool testing=false;
        edvfx::Host host;host.randomBound=host.cachedBound=[](int){return 0;};host.visible=[](auto){return true;};
        host.coarseCell=[](float,float){return int16_t(0);};host.coarseHeight=[](float,float){return 0.f;};host.floorSample=[](float,float,float){return 0.;};
        edvfx::SourceVfx effects(vfxRoot,host);
        StartupCase source(root/"scripts/levelA.jpc",root,false,[&](auto& input){input.animationEvent=[&](const auto& body,const auto& raw){
            if(body.key.type!=type||!testing||edanimevent::decode(raw).kind!=16)return;const auto event=edanimevent::decode(raw);assert(event.kind==16&&event.mode==-1);
            const uint32_t clip=0x058c7184;const unsigned frame=27;assert(event.frame==frame);
            bool actual=false;for(const auto& playback:body.actions.playbacks)if(playback.active&&playback.mode!=4)
                actual|=body.actions.states[size_t(playback.state)].animation==clip&&playback.frame==frame;assert(actual);
            assert(!edanimevent::scriptVfxCallback(16));
            const auto ends=edanimevent::visualEndpoints(event,{{0,body.pose.yaw,0}},{{body.pose.x,body.pose.y,body.pose.z}});
            assert(effects.line(16,ends.first,ends.second,event.mode));++emitted;
        };});auto& actors=*source.actors;auto& vm=source.game.vm;const auto& p=source.game.package;
        source.game.enterScene("ak09");vm.invoke(p.methodIndex("SceneCommander","sceneReset","()V"));const auto npc=actors.createType(type);assert(npc);
        for(const auto& [key,unused]:actors.bodies()){(void)unused;auto& body=actors.at(key);body.think=vm.enqueue(p.methodIndex("ActorDB","think","(II)V"),0,{I(key.type),I(key.index)});
            vm.invoke(p.methodIndex("ActorDB","setThinkThreadId","(III)V"),0,{I(key.type),I(key.index),I(int32_t(body.think))});vm.hold(body.think);}
        for(int mask:{8,32,64})vm.invoke(p.methodIndex("ed/base/EDActor","flagSubEngine","(I)V"),npc,{I(mask)});
        vm.invoke(p.methodIndex("ed/base/EDActor","flagAddEngine","(I)V"),npc,{I(1)});assert(actors.consoleAction({type,0},70));
        auto step=[&](){vm.statics[398]=I(int32_t(++tick*1000/30));actors.update(tick);effects.update(tick);};
        for(unsigned n=0;n<100&&actors.at({type,0}).actions.layers[0].state!=1;++n)step();
        assert(actors.at({type,0}).actions.layers[0].state==1&&actors.consoleAction({type,0},52));
        testing=true;const auto oldTick=tick;const auto oldVm=vm.snapshot(),oldActors=actors.snapshot(),oldEffects=effects.snapshot();
        auto run=[&](){for(unsigned n=0;n<100&&!emitted;++n)step();
            assert(emitted==1&&effects.globals().nextSerial>8&&effects.particles().at(16).size()==8);return tick;};const auto end=run();
        const auto after=effects.snapshot(),afterActors=actors.snapshot(),afterVm=vm.snapshot();assert(!effects.draw({{1,0,0}},{{0,1,0}},{}).empty());
        vm.restore(oldVm);actors.restore(oldActors);effects.restore(oldEffects);tick=oldTick;emitted=0;
        assert(run()==end&&effects.snapshot()==after&&actors.snapshot()==afterActors&&vm.snapshot()==afterVm);
        std::cout<<"source NPC"<<type<<" realstartup/console70/52 -> original raw16 -> nativeparticles/jitter/orderedchildren and VM/native/VFX replay passed\n";
    }return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
