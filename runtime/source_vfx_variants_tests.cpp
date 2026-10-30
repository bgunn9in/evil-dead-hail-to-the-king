#include "source_vfx.h"
#include "source_animation_event.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

struct VariantRandom {
    uint32_t seed=12345,index=0;bool ready=false;std::array<uint32_t,64> table{};
    uint32_t step(){seed=seed*0x41c64e6du+0x3039u;return uint32_t(((uint64_t(seed)*0x88888889u)>>35)%0x07ffffffu);}
    int bound(int n){return n?int(step()%uint32_t(std::abs(int64_t(n)))):0;}
    int cached(int n){if(!n)return 0;if(!ready){for(auto& value:table)value=step();ready=true;}
        if(++index>=64){index=0;ready=false;}return int(table[index]%uint32_t(std::abs(int64_t(n))));}
};
template<class F>static bool rejects(F fn){try{fn();return false;}catch(const std::exception&){return true;}}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Expected generated variant VFX asset root");const std::filesystem::path root=argv[1];
    const auto bytes=edobjects::detail::read(root/"vfx/variant-goldens.bin",16*1024*1024);edobjects::detail::Reader input{bytes};input.magic("EDVFXVG1");
    const auto ticks=input.get<uint32_t>(),requests=input.get<uint32_t>();assert(ticks==140&&requests==4&&edvfx::MaxVerticesPerParticle==444&&edvfx::MaxVertices==666000);
    struct Request {uint32_t tick;std::array<uint8_t,36> raw;edvfx::Vector world;};std::vector<Request> events;
    for(unsigned n=0;n<requests;++n){Request r;r.tick=input.get<uint32_t>();for(auto& b:r.raw)b=input.get<uint8_t>();for(auto& p:r.world)p=input.get<float>();events.push_back(r);}
    VariantRandom random;unsigned coarse=0,floor=0;edvfx::Host host;
    host.randomBound=[&](int n){return random.bound(n);};host.cachedBound=[&](int n){return random.cached(n);};host.visible=[](auto){return true;};
    host.coarseCell=[&](float,float){++coarse;return int16_t(0);};host.coarseHeight=[](float,float){return 0.f;};
    host.floorSample=[&](float,float,float){++floor;return 0.;};host.actorWorld=[](int type,int index){assert(type==0&&index==0);return edvfx::Vector{};};
    edvfx::SourceVfx fx(root,host);assert(fx.supports(3)&&fx.supports(32));unsigned checked=0,renderOnly=0,geometries=0;
    for(unsigned tick=0;tick<ticks;++tick){for(const auto& request:events)if(request.tick==tick){const auto event=edanimevent::decode(request.raw);
            const auto ends=edanimevent::visualEndpoints(event,{},request.world);assert(fx.line(event.kind,ends.first,ends.second,event.mode));}
        if(tick)fx.update(tick);fx.sampleRender();assert(input.get<uint32_t>()==tick);const auto extra=input.get<uint32_t>();
        for(unsigned n=0;n<extra;++n){fx.sampleRender(true);++renderOnly;}
        const auto count=input.get<uint32_t>(),seed=input.get<uint32_t>(),index=input.get<uint32_t>(),ready=input.get<uint32_t>(),coarseCount=input.get<uint32_t>(),floorCount=input.get<uint32_t>();
        if(seed!=random.seed||index!=random.index||ready!=unsigned(random.ready)||coarseCount!=coarse||floorCount!=floor)
            std::cerr<<"Variant tick="<<tick<<" expected RNG/floor "<<seed<<'/'<<index<<'/'<<ready<<'/'<<coarseCount<<'/'<<floorCount<<" actual "<<random.seed<<'/'<<random.index<<'/'<<random.ready<<'/'<<coarse<<'/'<<floor<<'\n';
        assert(seed==random.seed&&index==random.index&&ready==unsigned(random.ready)&&coarseCount==coarse&&floorCount==floor);
        for(auto value:random.table)assert(input.get<uint32_t>()==value);for(unsigned n=0;n<3;++n)input.get<int32_t>();
        std::vector<edvfx::Particle> particles;for(const auto& [kind,group]:fx.particles()){(void)kind;particles.insert(particles.end(),group.begin(),group.end());}assert(count==particles.size());
        for(const auto& p:particles){assert(input.get<uint32_t>()==p.kind&&input.get<uint64_t>()==p.serial);
            for(float value:p.position)assert(std::abs(input.get<float>()-value)<1e-5f);for(float value:p.velocity)assert(std::abs(input.get<float>()-value)<1e-6f);
            assert(std::abs(input.get<float>()-p.size)<1e-4f&&input.get<int32_t>()==p.age&&input.get<uint32_t>()==p.tile);
            for(auto color:p.color)assert(input.get<uint8_t>()==color);for(auto rotation:p.rotation)assert(input.get<uint8_t>()==rotation);
            assert(input.get<uint8_t>()==unsigned(p.stopped)&&input.get<int32_t>()==p.renderMode&&input.get<uint8_t>()==unsigned(p.deferredDraw));++checked;}
        const auto saved=fx.snapshot();fx.sampleRender();assert(fx.snapshot()==saved);const auto geometry=input.get<uint32_t>();
        if(geometry){const auto batches=fx.draw({{1,0,0}},{{0,1,0}},{});assert(batches.size()==geometry&&fx.snapshot()==saved);
            for(const auto& batch:batches){for(float color:batch.tint)assert(std::abs(input.get<float>()-color)<1e-7f);const auto vertices=input.get<uint32_t>();assert(vertices==batch.vertices.size());
                for(const auto& vertex:batch.vertices)for(float value:{vertex.x,vertex.y,vertex.z,vertex.u,vertex.v}){const auto expected=input.get<float>();
                    if(std::abs(expected-value)>=1e-5f)std::cerr<<"Geometry tick="<<tick<<" expected="<<expected<<" actual="<<value<<'\n';assert(std::abs(expected-value)<1e-5f);}++geometries;}}
        if(tick==6||tick==70){const auto rng=random;const auto c=coarse,f=floor;fx.update(tick+1);fx.restore(saved);random=rng;coarse=c;floor=f;assert(fx.snapshot()==saved);
            auto broken=saved;broken.pop_back();assert(rejects([&]{fx.restore(broken);})&&fx.snapshot()==saved);}
    }
    assert(input.cursor==bytes.size()&&renderOnly==4&&checked>100&&geometries>10);
    // Pending homing edits are data from save files, validated before any swap.
    fx.clear();assert(fx.line(24,{{100,2,100}},{{100,2,100}},1));fx.sampleRender(true);
    const auto pending=fx.snapshot();assert(pending.size()>=28);uint32_t pendingCount;std::memcpy(&pendingCount,pending.data()+pending.size()-28,4);assert(pendingCount==1);
    for(unsigned test=0;test<4;++test){auto bad=pending;const auto start=bad.size()-24;
        if(test==0){uint64_t absent=0;std::memcpy(bad.data()+start,&absent,8);}
        if(test==1){float changedY=99;std::memcpy(bad.data()+start+12,&changedY,4);}
        if(test==2){int32_t invalidAge=123;std::memcpy(bad.data()+start+20,&invalidAge,4);}
        if(test==3){uint32_t nan=0x7fc00000;std::memcpy(bad.data()+start+8,&nan,4);}
        assert(rejects([&]{fx.restore(bad);})&&fx.snapshot()==pending);}
    // A full source pool must also fit the bounded opaque S5 snapshot with homing edits.
    fx.clear();for(unsigned n=0;n<1500;++n)assert(fx.line(24,{{100,2,100}},{{100,2,100}},1));fx.sampleRender(true);
    const auto maximum=fx.snapshot();assert(maximum.size()<=128*1024);fx.restore(maximum);assert(fx.snapshot()==maximum);
    std::cout<<"source24/25 mode1 + real3/32: "<<checked<<" states, "<<geometries<<" geometry batches, render-only cadence/RNG/children/S5rollback/fullpool passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
