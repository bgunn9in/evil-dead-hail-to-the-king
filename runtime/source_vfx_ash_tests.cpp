#include "source_vfx.h"
#include "source_animation_event.h"
#include "jpc.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

struct AshVfxRandom {
    uint32_t seed=12345,index=0;bool ready=false;std::array<uint32_t,64> table{};
    uint32_t step(){seed=seed*0x41c64e6du+0x3039u;return uint32_t(((uint64_t(seed)*0x88888889u)>>35)%0x07ffffffu);}
    int bound(int n){return n?int(step()%uint32_t(std::abs(int64_t(n)))):0;}
    int cached(int n){if(!n)return 0;if(!ready){for(auto& value:table)value=step();ready=true;}
        if(++index>=64){index=0;ready=false;}return int(table[index]%uint32_t(std::abs(int64_t(n))));}
};
static jpc::Value I(int n){return jpc::Value::integer(n);}
static void actualCombatCounter(const std::filesystem::path& root) {
    const auto package=jpc::Package::load(root/"scripts/levelA.jpc");jpc::Interpreter vm(package);vm.initialize("Ash");
    const auto ash=vm.makeObject(package.classIndex("Ash"),{{5,I(0)},{6,I(0)}});
    vm.statics[53]=jpc::Value::reference(vm.makeArray('L',{jpc::Value::reference(ash)}));
    vm.statics[54]=jpc::Value::reference(vm.makeArray('I',std::vector<jpc::Value>(40,I(0))));
    const auto process=package.methodIndex("ActorDB","processVFXAction","(IIIIIIIIIII)I");
    auto allowed=[&]{return vm.invoke(process,0,{I(0),I(0),I(20),I(0),I(0),I(0),I(0),I(0),I(0),I(0),I(0)}).integer()==0;};
    assert(!allowed()&&vm.staticField(72).integer()==0);
    vm.invoke(package.methodIndex("Ash","eventPlayerComboBegin","()V"),ash);assert(vm.staticField(72).integer()==100);
    for(unsigned n=0;n<100;++n)assert(allowed());assert(vm.staticField(72).integer()==0&&!allowed());
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Expected generated Ash VFX asset root");const std::filesystem::path root=argv[1];actualCombatCounter(root);
    AshVfxRandom random;edvfx::Host host;host.randomBound=[&](int n){return random.bound(n);};host.cachedBound=[&](int n){return random.cached(n);};
    unsigned coarse=0,floor=0;host.coarseCell=[&](float,float){++coarse;return int16_t(0);};
    host.floorSample=[&](float,float,float){++floor;return 0.;};host.visible=[](auto){return true;};
    edvfx::SourceVfx fx(root,host);assert(fx.supports(20)&&fx.supports(18));
    const auto bytes=edobjects::detail::read(root/"vfx/ash-goldens.bin",2*1024*1024);edobjects::detail::Reader input{bytes};input.magic("EDVFXAG1");
    const auto ticks=input.get<uint32_t>();assert(ticks==140);std::array<uint8_t,36> raw{};for(auto& b:raw)b=input.get<uint8_t>();
    const auto event=edanimevent::decode(raw);assert(event.kind==20&&event.enabled&&event.mode==-1);
    const auto ends=edanimevent::visualEndpoints(event,{},{{1,1.25f,-2}});unsigned checked=0,deathCount=0;
    for(unsigned tick=0;tick<ticks;++tick){if(tick==0||tick==3)assert(fx.line(20,ends.first,ends.second,-1));if(tick)fx.update(tick);
        assert(input.get<uint32_t>()==tick);const auto count=input.get<uint32_t>();assert(input.get<uint32_t>()==random.seed&&input.get<uint32_t>()==random.index&&input.get<uint32_t>()==unsigned(random.ready));
        assert(input.get<uint32_t>()==coarse&&input.get<uint32_t>()==floor);for(auto value:random.table)assert(input.get<uint32_t>()==value);
        std::vector<edvfx::Particle> particles;for(const auto& [kind,group]:fx.particles()){(void)kind;particles.insert(particles.end(),group.begin(),group.end());}assert(count==particles.size());
        for(const auto& p:particles){assert(input.get<uint32_t>()==p.kind&&input.get<uint64_t>()==p.serial);
            for(float value:p.position)assert(std::abs(input.get<float>()-value)<1e-6f);for(float value:p.velocity)assert(std::abs(input.get<float>()-value)<1e-7f);
            const auto size=input.get<float>();const auto age=input.get<int32_t>();const auto tile=input.get<uint32_t>();
            if(std::abs(size-p.size)>=1e-5f||age!=p.age||tile!=p.tile)std::cerr<<"Ash VFX tick="<<tick<<" kind="<<p.kind<<" serial="<<p.serial<<" expected size/age/tile "<<size<<'/'<<age<<'/'<<tile<<" actual "<<p.size<<'/'<<p.age<<'/'<<p.tile<<'\n';
            assert(std::abs(size-p.size)<1e-5f&&age==p.age&&tile==p.tile);
            for(auto color:p.color)assert(input.get<uint8_t>()==color);++checked;if(p.kind==18&&p.age==1)++deathCount;}
        const auto before=fx.snapshot();assert(fx.draw({{1,0,0}},{{0,1,0}},{}).size()==particles.size()&&fx.snapshot()==before);
        if(tick==5){const auto rng=random;const auto c=coarse,f=floor;fx.update(6);fx.restore(before);random=rng;coarse=c;floor=f;assert(fx.snapshot()==before);}
    }
    assert(input.cursor==bytes.size()&&floor>0&&deathCount>0&&checked>100);const auto saved=fx.snapshot();bool rejected=false;
    try{fx.line(20,{},{},1);}catch(const std::exception&){rejected=true;}assert(rejected&&fx.snapshot()==saved);
    std::cout<<"source Ash648/670/684 combat counter and native20/death18/random18: "<<checked<<" states, RNG/atlas/floor/priority/save/draw passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
