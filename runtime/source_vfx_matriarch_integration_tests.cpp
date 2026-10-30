#include "actor_startup_fixture.h"
#include "source_vfx.h"

struct MatriarchRng {
    uint32_t seed=12345,index=0;bool ready=false;std::array<uint32_t,64> table{};
    uint32_t step(){seed=seed*0x41c64e6du+0x3039u;return uint32_t(((uint64_t(seed)*0x88888889u)>>35)%0x07ffffffu);}
    int bound(int n){return n?int(step()%uint32_t(std::abs(int64_t(n)))):0;}
    int cached(int n){if(!n)return 0;if(!ready){for(auto& v:table)v=step();ready=true;}
        if(++index>=64){index=0;ready=false;}return int(table[index]%uint32_t(std::abs(int64_t(n))));}
    bool operator==(const MatriarchRng& b)const{return seed==b.seed&&index==b.index&&ready==b.ready&&table==b.table;}
};
static std::vector<std::array<uint8_t,36>> originalFrame(const std::filesystem::path& root,uint32_t clip,unsigned frame){
    const auto bytes=edobjects::detail::read(root/"actors/matriarchboss/animation-events.bin",1024*1024);edobjects::detail::Reader input{bytes};input.magic("EDANEV1\0");
    const auto clips=input.get<uint32_t>();assert(clips<=4096);std::vector<std::array<uint8_t,36>> result;
    for(unsigned n=0;n<clips;++n){const auto id=input.get<uint32_t>(),lines=input.get<uint32_t>(),sounds=input.get<uint32_t>();assert(lines<=4096&&sounds<=4096);
        for(unsigned k=0;k<lines;++k){std::array<uint8_t,36> raw;for(auto& b:raw)b=input.get<uint8_t>();const auto e=edanimevent::decode(raw);
            if(id==clip&&e.frame==frame&&e.enabled)result.push_back(raw);}
        for(unsigned k=0;k<sounds*20;++k)(void)input.get<uint8_t>();}
    assert(input.cursor==bytes.size());return result;
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Expected original LevelA assets");const std::filesystem::path root=argv[1];
    for(int action:{52,54}){const uint32_t clip=action==52?0x03b11184:0x03b111c4;const auto expected=originalFrame(root,clip,23);
        std::vector<unsigned> kinds;for(const auto& raw:expected)kinds.push_back(edanimevent::decode(raw).kind);
        assert(kinds==(action==52?std::vector<unsigned>{16,9,9,24}:std::vector<unsigned>{16,24}));
        MatriarchRng random,referenceRandom;StartupCase* current=nullptr;bool testing=false,verified=false;uint64_t tick=0;unsigned earlierDust=0;
        std::vector<std::array<uint8_t,36>> observed;
        auto environment=[](){edvfx::Host h;h.visible=[](auto){return true;};h.coarseCell=[](float,float){return int16_t(0);};
            h.coarseHeight=[](float,float){return 0.f;};h.floorSample=[](float,float,float){return 0.;};return h;};
        auto host=environment();host.randomBound=[&](int b){return random.bound(b);};host.cachedBound=[&](int b){return random.cached(b);};
        host.actorWorld=[&](int type,int index){const auto& b=current->actors->at({type,index});return edvfx::Vector{{b.pose.x,b.pose.y,b.pose.z}};};
        auto referenceHost=host;referenceHost.randomBound=[&](int b){return referenceRandom.bound(b);};referenceHost.cachedBound=[&](int b){return referenceRandom.cached(b);};
        edvfx::SourceVfx effects(root,host),reference(root,referenceHost);
        StartupCase source(root/"scripts/levelA.jpc",root,false,[&](auto& actorHost){actorHost.animationEvent=[&](const auto& body,const auto& raw){
            if(body.key.type!=14||!testing)return;const auto event=edanimevent::decode(raw);assert(event.kind==9||event.kind==16||event.kind==24);
            if(event.kind==9){std::vector<jpc::Value> args(11,I(0));args[0]=I(14);args[1]=I(body.key.index);args[2]=I(9);
                for(unsigned n=0;n<4;++n)args[n+3]=I(body.sfxMaterial[n]);assert(current->game.vm.invoke(current->game.package.methodIndex("ActorDB","processVFXAction","(IIIIIIIIIII)I"),0,args).integer()==0);if(event.frame<23)++earlierDust;}
            const edvfx::Vector position{{body.pose.x,body.pose.y,body.pose.z}};const edactormath::Euler euler{{0,body.pose.yaw,0}};
            if(event.frame==23){bool actual=false;for(const auto& p:body.actions.playbacks)if(p.active&&p.mode!=4)
                    actual|=body.actions.states[size_t(p.state)].animation==clip&&p.frame==23;assert(actual);
                if(observed.empty()){reference.restore(effects.snapshot());referenceRandom=random;
                    for(const auto& original:expected){const auto e=edanimevent::decode(original);const auto ends=edanimevent::visualEndpoints(e,euler,position);
                        assert(reference.line(e.kind,ends.first,ends.second,e.mode));}}
                observed.push_back(raw);}
            const auto ends=edanimevent::visualEndpoints(event,euler,position);assert(effects.line(event.kind,ends.first,ends.second,event.mode));
            if(observed.size()==expected.size()){assert(observed==expected&&effects.snapshot()==reference.snapshot()&&random==referenceRandom);verified=true;}
        };});current=&source;auto& actors=*source.actors;auto& vm=source.game.vm;const auto& package=source.game.package;
        source.game.enterScene("ah06");vm.invoke(package.methodIndex("SceneCommander","sceneReset","()V"));const auto npc=actors.createType(14);assert(npc);
        for(const auto& [key,unused]:actors.bodies()){(void)unused;auto& b=actors.at(key);b.think=vm.enqueue(package.methodIndex("ActorDB","think","(II)V"),0,{I(key.type),I(key.index)});
            vm.invoke(package.methodIndex("ActorDB","setThinkThreadId","(III)V"),0,{I(key.type),I(key.index),I(int32_t(b.think))});vm.hold(b.think);}
        for(int mask:{8,32,64})vm.invoke(package.methodIndex("ed/base/EDActor","flagSubEngine","(I)V"),npc,{I(mask)});
        vm.invoke(package.methodIndex("ed/base/EDActor","flagAddEngine","(I)V"),npc,{I(1)});assert(actors.consoleAction({14,0},action));testing=true;
        const auto oldVm=vm.snapshot(),oldActors=actors.snapshot(),oldEffects=effects.snapshot();const auto oldRandom=random;
        auto run=[&](){for(unsigned n=0;n<120&&!verified;++n){vm.statics[398]=I(int32_t(++tick*1000/30));actors.update(tick);effects.update(tick);effects.sampleRender();}
            assert(verified&&observed==expected&&earlierDust>0);return tick;};const auto end=run();const auto afterVm=vm.snapshot(),afterActors=actors.snapshot(),afterEffects=effects.snapshot();const auto afterRandom=random;
        assert(!effects.draw({{1,0,0}},{{0,1,0}},{}).empty());
        vm.restore(oldVm);actors.restore(oldActors);effects.restore(oldEffects);random=oldRandom;tick=0;verified=false;observed.clear();earlierDust=0;
        assert(run()==end&&vm.snapshot()==afterVm&&actors.snapshot()==afterActors&&effects.snapshot()==afterEffects&&random==afterRandom);
        std::cout<<"sourceMatriarch console"<<action<<" clip"<<std::hex<<clip<<std::dec<<"/f23 raworder";for(auto kind:kinds)std::cout<<' '<<kind;
        std::cout<<" original648(9)/earlierdust/sharedRNG/nativeparticles exactphase+wholeVMreplay: passed\n";
    }return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
