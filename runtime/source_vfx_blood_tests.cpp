#include "source_vfx.h"
#include "source_animation_event.h"
#include "jpc.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

struct NpcVfxRandom {
    uint32_t seed=12345,index=0;bool ready=false;std::array<uint32_t,64> table{};
    uint32_t step(){seed=seed*0x41c64e6du+0x3039u;return uint32_t(((uint64_t(seed)*0x88888889u)>>35)%0x07ffffffu);}
    int bound(int n){return n?int(step()%uint32_t(std::abs(int64_t(n)))):0;}
    int cached(int n){if(!n)return 0;if(!ready){for(auto& v:table)v=step();ready=true;}
        if(++index>=64){index=0;ready=false;}return int(table[index]%uint32_t(std::abs(int64_t(n))));}
};
template<class F>static bool rejects(F f){try{f();return false;}catch(const std::exception&){return true;}}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Expected generated native21 assets");const std::filesystem::path root=argv[1];
    const auto bytes=edobjects::detail::read(root/"vfx/blood-goldens.bin",2*1024*1024);edobjects::detail::Reader in{bytes};in.magic("EDVFXBG1");
    const auto frames=in.get<uint32_t>(),count=in.get<uint32_t>();assert(frames==320&&count==2);
    std::vector<std::pair<unsigned,std::array<uint8_t,36>>> requests;
    for(unsigned n=0;n<count;++n){const auto tick=in.get<uint32_t>();std::array<uint8_t,36> raw;for(auto& b:raw)b=in.get<uint8_t>();requests.emplace_back(tick,raw);}
    NpcVfxRandom random;unsigned coarse=0,floor=0;edvfx::Host host;host.randomBound=[&](int n){return random.bound(n);};host.cachedBound=[&](int n){return random.cached(n);};
    host.visible=[](auto){return true;};host.coarseCell=[&](float,float){++coarse;return int16_t(0);};host.coarseHeight=[](float,float){return 0.f;};
    host.floorSample=[&](float,float,float){++floor;return 0.;};edvfx::SourceVfx fx(root,host);assert(fx.supports(21)&&fx.supports(18));unsigned checked=0;
    for(unsigned tick=0;tick<frames;++tick){for(const auto& r:requests)if(r.first==tick){const auto event=edanimevent::decode(r.second);assert(event.kind==21);
            const auto ends=edanimevent::visualEndpoints(event,{},{{1.25f,1.75f,-2}});assert(fx.line(21,ends.first,ends.second,event.mode));}
        if(tick)fx.update(tick);assert(in.get<uint32_t>()==tick);const auto particles=in.get<uint32_t>();
        assert(in.get<uint32_t>()==random.seed&&in.get<uint32_t>()==random.index&&in.get<uint32_t>()==unsigned(random.ready));
        assert(in.get<uint32_t>()==coarse&&in.get<uint32_t>()==floor);for(auto v:random.table)assert(in.get<uint32_t>()==v);
        std::vector<edvfx::Particle> flat;for(const auto& [kind,group]:fx.particles()){(void)kind;flat.insert(flat.end(),group.begin(),group.end());}assert(flat.size()==particles);
        for(const auto& p:flat){assert(in.get<uint32_t>()==p.kind&&in.get<uint64_t>()==p.serial);for(float v:p.position)assert(std::abs(in.get<float>()-v)<1e-5f);
            for(float v:p.velocity)assert(std::abs(in.get<float>()-v)<1e-6f);assert(std::abs(in.get<float>()-p.size)<1e-4f&&in.get<int32_t>()==p.age&&in.get<uint32_t>()==p.tile);
            for(auto v:p.color)assert(in.get<uint8_t>()==v);for(auto v:p.rotation)assert(in.get<uint8_t>()==v);assert(in.get<uint8_t>()==unsigned(p.stopped));++checked;}
        const auto saved=fx.snapshot();fx.restore(saved);assert(fx.snapshot()==saved);
        if(tick==5){auto bad=saved;bad.pop_back();assert(rejects([&]{fx.restore(bad);})&&fx.snapshot()==saved);}
    }assert(in.cursor==bytes.size()&&checked>100);
    fx.clear();assert(fx.line(21,{{1,2,3}},{{1,2,3}},-1));const auto saved=fx.snapshot();const auto draw=fx.draw({{1,0,0}},{{0,1,0}},{});assert(!draw.empty()&&fx.snapshot()==saved);
    const auto& p=fx.particles().at(21).front();const auto& v=draw.front().vertices.front();const float size=p.size*.0010000000474974513f;
    assert(std::abs(v.x-(1-size))<1e-6f&&std::abs(v.y-(2-size))<1e-6f&&v.z==3);assert(draw.front().tint[3]==128.f/255);
    assert(!edanimevent::scriptVfxCallback(21));
    std::cout<<"source21: "<<checked<<" states, actualdirection/count/atlas/sharedRNG/floor/death18, billboard/alpha and opaque replay passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
