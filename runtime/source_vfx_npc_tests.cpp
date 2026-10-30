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
static jpc::Value I(int n){return jpc::Value::integer(n);}
static void callbackProof(const std::filesystem::path& root){
    for(const char* level:{"levelA.jpc","levelB.jpc"}){const auto package=jpc::Package::load(root/"scripts"/level);jpc::Interpreter vm(package);
        for(int type:{4,35,31,36,37}){const auto npc=vm.makeObject(package.classIndex("GenericNpc"),{{5,I(0)},{6,I(type)}});
            vm.statics[53]=jpc::Value::reference(vm.makeArray('L',{jpc::Value::reference(npc)}));
            vm.statics[54]=jpc::Value::reference(vm.makeArray('I',std::vector<jpc::Value>(40,I(0))));
            assert(vm.invoke(648,0,{I(type),I(0),I(17),I(0),I(0),I(0),I(0),I(0),I(0),I(0),I(0)}).integer()==int(type==31||type==36||type==37));
        }
    }
}
template<class F>static bool rejects(F f){try{f();return false;}catch(const std::exception&){return true;}}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Expected generated NPC17 assets");const std::filesystem::path root=argv[1];callbackProof(root);
    const auto bytes=edobjects::detail::read(root/"vfx/npc-goldens.bin",1024*1024);edobjects::detail::Reader in{bytes};in.magic("EDVFXNG1");
    const auto frames=in.get<uint32_t>(),count=in.get<uint32_t>();assert(frames==140&&count==2);
    std::vector<std::pair<unsigned,std::array<uint8_t,36>>> requests;
    for(unsigned n=0;n<count;++n){const auto tick=in.get<uint32_t>();std::array<uint8_t,36> raw;for(auto& b:raw)b=in.get<uint8_t>();requests.emplace_back(tick,raw);}
    NpcVfxRandom random;unsigned coarse=0,floor=0;edvfx::Host host;host.randomBound=[&](int n){return random.bound(n);};host.cachedBound=[&](int n){return random.cached(n);};
    host.visible=[](auto){return true;};host.coarseCell=[&](float,float){++coarse;return int16_t(0);};host.coarseHeight=[](float,float){return 0.f;};
    host.floorSample=[&](float,float,float){++floor;return 0.;};edvfx::SourceVfx fx(root,host);assert(fx.supports(17)&&fx.supports(32));unsigned checked=0;
    for(unsigned tick=0;tick<frames;++tick){for(const auto& r:requests)if(r.first==tick){const auto event=edanimevent::decode(r.second);assert(event.kind==17);
            const auto ends=edanimevent::visualEndpoints(event,{},{{1.25f,.75f,-2}});assert(fx.line(17,ends.first,ends.second,event.mode));}
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
    // The renderer17 uses source Y and direct world size; rotation bytes remain
    // state even though426B80 ignores them. Keep source visible-particle offset.
    fx.clear();assert(fx.line(17,{{1,2,3}},{{9,8,7}},-1));assert(fx.line(17,{{4,5,6}},{{9,8,7}},-1));
    const auto before=fx.snapshot();const auto draws=fx.draw({{1,0,0}},{{0,1,0}},{});assert(draws.size()==2&&fx.snapshot()==before);
    const auto& first=draws[0].vertices;assert(std::abs(first[0].x-3.9f)<1e-6f&&first[0].y==5&&std::abs(first[0].z-5.9f)<1e-6f);
    assert(first[0].u==0&&first[0].v==0&&first[2].u==.125f&&first[2].v==.125f);
    assert(std::abs(draws[1].vertices[0].y-2.00002f)<1e-7f);
    std::cout<<"sourceNPC17: "<<checked<<" states, wrapped rotation,75%real32/0/22 descendants/sharedRNG/floor, exactflatXZ/UV/offset and opaque replay passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
