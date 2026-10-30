#include "actor_startup_fixture.h"
#include "source_vfx.h"

template<class F>static bool rejects(F f){try{f();return false;}catch(const std::exception&){return true;}}
static edvfx::Host flashHost(){edvfx::Host h;h.visible=[](auto){return true;};
    h.randomBound=h.cachedBound=[](int)->int{throw std::runtime_error("Source6 must not consume RNG");};return h;}
static void goldens(const std::filesystem::path& root){
    edvfx::SourceVfx fx(root,flashHost());assert(fx.supports(6));
    const auto bytes=edobjects::detail::read(root/"vfx/flash-goldens.bin",1024*1024);edobjects::detail::Reader in{bytes};in.magic("EDVFXFG1");
    const auto ticks=in.get<uint32_t>(),count=in.get<uint32_t>();assert(ticks==12&&count==2);
    std::vector<std::pair<unsigned,std::array<uint8_t,36>>> requests;
    for(unsigned n=0;n<count;++n){const auto tick=in.get<uint32_t>();std::array<uint8_t,36> raw;for(auto& b:raw)b=in.get<uint8_t>();requests.emplace_back(tick,raw);}
    unsigned checked=0;for(unsigned tick=0;tick<ticks;++tick){for(const auto& request:requests)if(request.first==tick){const auto raw=edanimevent::decode(request.second);
            assert(raw.kind==6&&raw.mode==-1);const auto ends=edanimevent::visualEndpoints(raw,{},{{1,.75f,-2}});assert(fx.line(6,ends.first,ends.second,raw.mode));}
        if(tick)fx.update(tick);assert(in.get<uint32_t>()==tick);const auto particles=in.get<uint32_t>();const auto& group=fx.particles().at(6);assert(particles==group.size());
        const auto saved=fx.snapshot();const auto batches=fx.draw({{1,0,0}},{{0,1,0}},{});assert(fx.snapshot()==saved&&batches.size()==2*particles);
        for(unsigned n=0;n<particles;++n){const auto& p=group[n];assert(in.get<uint32_t>()==6&&in.get<uint64_t>()==p.serial);
            for(auto v:p.position)assert(std::abs(in.get<float>()-v)<1e-6f);assert(in.get<float>()==p.size&&in.get<int32_t>()==p.age);
            for(auto v:p.color)assert(in.get<uint8_t>()==v);assert(p.velocity==edvfx::Vector{}&&p.tile==0);
            for(unsigned pass=0;pass<2;++pass){const auto& batch=batches[n*2+pass];for(auto v:batch.tint)assert(std::abs(in.get<float>()-v)<1e-7f);assert(batch.vertices.size()==6);
                for(const auto& vertex:batch.vertices)for(float v:{vertex.x,vertex.y,vertex.z,vertex.u,vertex.v})assert(std::abs(in.get<float>()-v)<1e-6f);}++checked;}
        fx.restore(saved);assert(fx.snapshot()==saved);if(tick==3){auto invalid=saved;invalid.pop_back();assert(rejects([&]{fx.restore(invalid);})&&fx.snapshot()==saved);}
    }assert(in.cursor==bytes.size()&&checked>0);std::cout<<"source6: "<<checked<<" no-RNG states, both425CF0 quads/UVs/alpha/lifetime and snapshot rollback passed\n";
}
static void actualAsh(const std::filesystem::path& root,const std::filesystem::path& vfxRoot){
    edvfx::SourceVfx effects(vfxRoot,flashHost());unsigned flashes=0;
    StartupCase source(root/"scripts/levelA.jpc",root,false,[&](auto& host){host.animationEvent=[&](const auto& body,const auto& raw){
        const auto event=edanimevent::decode(raw);if(body.key.type!=0||event.kind!=6)return;
        assert(event.mode==-1&&event.frame==16);bool actual=false;
        for(const auto& p:body.actions.playbacks)if(p.active&&p.mode!=4)actual|=body.actions.states[size_t(p.state)].animation==0x01825108&&p.frame==16;
        assert(actual);const auto ends=edanimevent::visualEndpoints(event,{{0,body.pose.yaw,0}},{{body.pose.x,body.pose.y,body.pose.z}});
        assert(effects.line(6,ends.first,ends.second,event.mode));++flashes;
    };});auto& vm=source.game.vm;const auto& p=source.game.package;auto& actors=*source.actors;
    for(const auto& [key,unused]:actors.bodies()){(void)unused;auto& b=actors.at(key);b.think=vm.enqueue(p.methodIndex("ActorDB","think","(II)V"),0,{I(key.type),I(key.index)});
        vm.invoke(p.methodIndex("ActorDB","setThinkThreadId","(III)V"),0,{I(key.type),I(key.index),I(int32_t(b.think))});vm.hold(b.think);}
    vm.bind("ed/base/External","sndMgrStopVoice","(I)V",[](auto&,auto,const auto& args){assert(args[0].integer()<0);return jpc::Value{};});
    assert(actors.consoleAction({0,0},85));uint64_t tick=0;
    auto step=[&](){vm.statics[398]=I(int32_t(++tick*1000/30));actors.update(tick);effects.update(tick);};
    for(unsigned n=0;n<100&&actors.at({0,0}).actions.layers[0].state!=28;++n)step();assert(actors.at({0,0}).actions.layers[0].state==28);
    assert(actors.consoleAction({0,0},94));for(unsigned n=0;n<100&&actors.at({0,0}).actions.layers[0].state!=42;++n)step();assert(actors.at({0,0}).actions.layers[0].state==42);
    assert(actors.consoleAction({0,0},92));
    // Immediate source92 overwrites a waiting child;4058e0 retains parent+04.
    const auto& ash=actors.at({0,0}).actions;const int child=ash.layers[0].playback,parent=ash.playbacks[size_t(child)].parent;
    assert(parent>=0&&ash.playbacks[size_t(parent)].active&&ash.playbacks[size_t(parent)].child==child);
    const auto oldVm=vm.snapshot(),oldActors=actors.snapshot(),oldEffects=effects.snapshot();const auto oldTick=tick;
    auto run=[&](){for(unsigned n=0;n<150&&!flashes;++n)step();assert(flashes==1&&effects.particles().at(6).size()==1);return tick;};
    const auto emitted=run();const auto after=effects.snapshot();assert(effects.draw({{1,0,0}},{{0,1,0}},{}).size()==2);
    vm.restore(oldVm);actors.restore(oldActors);effects.restore(oldEffects);tick=oldTick;flashes=0;assert(run()==emitted&&effects.snapshot()==after);
    std::cout<<"sourceAsh console85->28/94->42/92->43 clip01825108f16 -> raw6 -> nativeflash and VM/native/VFX replay passed\n";
}
int main(int argc,char** argv){try{if(argc<2||argc>3)throw std::runtime_error("Expected scene assets and optional isolated VFX root");
    const std::filesystem::path root=argv[1],vfxRoot=argc==3?argv[2]:argv[1];goldens(vfxRoot);actualAsh(root,vfxRoot);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
