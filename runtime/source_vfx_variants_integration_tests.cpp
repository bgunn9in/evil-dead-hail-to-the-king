#include "actor_startup_fixture.h"
#include "source_vfx.h"

struct VfxRandom {
    uint32_t seed=12345,index=0;bool ready=false;std::array<uint32_t,64> table{};
    uint32_t step(){seed=seed*0x41c64e6du+0x3039u;return uint32_t(((uint64_t(seed)*0x88888889u)>>35)%0x07ffffffu);}
    int bound(int n){return n?int(step()%uint32_t(std::abs(int64_t(n)))):0;}
    int cached(int n){if(!n)return 0;if(!ready){for(auto& value:table)value=step();ready=true;}
        if(++index>=64){index=0;ready=false;}return int(table[index]%uint32_t(std::abs(int64_t(n))));}
};
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Expected original LevelA asset root");const std::filesystem::path root=argv[1];
    struct Case{int type,action,kind,frame;const char* scene;uint32_t clip;};
    for(const auto& c:std::array<Case,4>{{{17,64,25,32,"ag03",0x038c1404},{14,52,24,23,"ah06",0x03b11184},
            {14,54,24,23,"ah06",0x03b111c4},{18,50,24,10,"au04",0x03833184}}}){
        VfxRandom random;StartupCase* source=nullptr;unsigned emitted=0;
        edvfx::Host host;host.randomBound=[&](int n){return random.bound(n);};host.cachedBound=[&](int n){return random.cached(n);};
        host.visible=[](auto){return true;};host.coarseCell=[](float,float){return int16_t(0);};host.coarseHeight=[](float,float){return 0.f;};
        host.floorSample=[](float,float,float){return 0.;};host.actorWorld=[&](int type,int index){const auto& b=source->actors->at({type,index});return edvfx::Vector{{b.pose.x,b.pose.y,b.pose.z}};};
        edvfx::SourceVfx effects(root,host);
        StartupCase fixture(root/"scripts/levelA.jpc",root,false,[&](auto& actorHost){
            actorHost.animationEvent=[&](const auto& body,const auto& raw){if(body.key.type!=c.type)return;
                const auto event=edanimevent::decode(raw);if(event.kind!=c.kind||event.mode!=1)return; // Combined Matriarch16/9/24 RNG has its own source integration proof.
                assert(body.key.index==0&&event.frame==c.frame);
                bool actualFrame=false;for(const auto& playback:body.actions.playbacks)if(playback.active&&playback.mode!=4)
                    actualFrame|=body.actions.states[size_t(playback.state)].animation==c.clip&&playback.frame==unsigned(c.frame);
                assert(actualFrame);const auto endpoints=edanimevent::visualEndpoints(event,{{0,body.pose.yaw,0}},{{body.pose.x,body.pose.y,body.pose.z}});
                assert(effects.line(event.kind,endpoints.first,endpoints.second,event.mode));++emitted;
            };
        });source=&fixture;auto& game=fixture.game;auto& actors=*fixture.actors;auto& vm=game.vm;const auto& p=game.package;
        game.enterScene(c.scene);vm.invoke(p.methodIndex("SceneCommander","sceneReset","()V"));const auto npc=actors.createType(c.type);assert(npc);
        // Park original ActorDB thinkers; the native FSM, event dispatch and
        // original callback records still advance, without unrelated combat AI.
        for(const auto& [key,unused]:actors.bodies()){(void)unused;auto& b=actors.at(key);
            b.think=vm.enqueue(p.methodIndex("ActorDB","think","(II)V"),0,{I(key.type),I(key.index)});
            vm.invoke(p.methodIndex("ActorDB","setThinkThreadId","(III)V"),0,{I(key.type),I(key.index),I(int32_t(b.think))});vm.hold(b.think);}
        for(int mask:{8,32,64})vm.invoke(p.methodIndex("ed/base/EDActor","flagSubEngine","(I)V"),npc,{I(mask)});
        vm.invoke(p.methodIndex("ed/base/EDActor","flagAddEngine","(I)V"),npc,{I(1)});
        assert(actors.consoleAction({c.type,0},c.action));
        const auto beforeVm=vm.snapshot(),beforeActors=actors.snapshot(),beforeEffects=effects.snapshot();const auto beforeRandom=random;
        auto run=[&](){unsigned tick=0;while(!emitted&&tick<100){vm.statics[398]=I(int32_t(++tick*1000/30));actors.update(tick);effects.update(tick);effects.sampleRender();}
            assert(emitted==1&&effects.particles().at(unsigned(c.kind)).size()==1);return tick;};
        const auto tick=run();const auto after=effects.snapshot(),afterActors=actors.snapshot(),afterVm=vm.snapshot();
        assert(effects.draw({{1,0,0}},{{0,1,0}},{}).size()>0);
        vm.restore(beforeVm);actors.restore(beforeActors);effects.restore(beforeEffects);random=beforeRandom;emitted=0;
        assert(run()==tick&&effects.snapshot()==after&&actors.snapshot()==afterActors&&vm.snapshot()==afterVm);
        std::cout<<"source actor"<<c.type<<" consoleAction"<<c.action<<" -> clip"<<std::hex<<c.clip<<std::dec<<"/frame"<<c.frame
            <<" -> raw"<<c.kind<<"mode1 -> real children/render/S5 deterministic replay: passed\n";
    }
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
