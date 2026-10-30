#include "source_vfx.h"
#include "source_animation_event.h"
#include "jpc.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

struct ShellRandom {
    uint32_t seed=12345,index=0;bool ready=false;std::array<uint32_t,64> table{};
    uint32_t step(){seed=seed*0x41c64e6du+0x3039u;return uint32_t(((uint64_t(seed)*0x88888889u)>>35)%0x07ffffffu);}
    int bound(int n){return n?int(step()%uint32_t(std::abs(int64_t(n)))):0;}
    int cached(int n){if(!n)return 0;if(!ready){for(auto& value:table)value=step();ready=true;}
        if(++index>=64){index=0;ready=false;}return int(table[index]%uint32_t(std::abs(int64_t(n))));}
};
static jpc::Value I(int n){return jpc::Value::integer(n);}
static void originalAmmoGate(const std::filesystem::path& root){
    const auto package=jpc::Package::load(root/"scripts/levelA.jpc");jpc::Interpreter vm(package);vm.initialize("Ash");
    const auto weapon=vm.makeObject(package.classIndex("Gun"),{{815,I(0)}});
    const auto ash=vm.makeObject(package.classIndex("Ash"),{{5,I(0)},{6,I(0)},{103,jpc::Value::reference(weapon)}});
    vm.statics[53]=jpc::Value::reference(vm.makeArray('L',{jpc::Value::reference(ash)}));
    vm.statics[54]=jpc::Value::reference(vm.makeArray('I',std::vector<jpc::Value>(40,I(0))));
    auto call=[&]{return vm.invoke(648,0,{I(0),I(0),I(8),I(0),I(0),I(0),I(0),I(0),I(0),I(0),I(0)}).integer();};
    assert(call()==1);vm.setField(weapon,815,I(1));assert(call()==0);vm.setField(ash,103,jpc::Value::reference(0));assert(call()==0);
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Expected generated shell VFX asset root");const std::filesystem::path root=argv[1];originalAmmoGate(root);
    const auto bytes=edobjects::detail::read(root/"vfx/shell-goldens.bin",2*1024*1024);edobjects::detail::Reader input{bytes};input.magic("EDVFXSG1");
    const auto ticks=input.get<uint32_t>();assert(ticks==240);std::array<uint8_t,36> raw;for(auto& b:raw)b=input.get<uint8_t>();
    const auto event=edanimevent::decode(raw);assert(event.kind==8&&event.enabled&&event.mode==-1);
    edparticle::PhysicalActors physical{};for(auto& actor:physical){actor.flags=input.get<uint32_t>();for(auto& value:actor.local)value=input.get<float>();actor.cachedFloor=input.get<float>();}
    ShellRandom random;unsigned coarse=0,floor=0,repel=0;edvfx::Host host;
    host.randomBound=[&](int n){return random.bound(n);};host.cachedBound=[&](int n){return random.cached(n);};host.visible=[](auto){return true;};
    host.coarseCell=[&](float,float){++coarse;return int16_t(0);};host.floorSample=[&](float,float,float){++floor;return 0.;};
    host.repellingActors=[&]{++repel;return physical;};edvfx::SourceVfx fx(root,host);assert(fx.supports(8));
    const auto ends=edanimevent::visualEndpoints(event,{},{});unsigned checked=0;
    for(unsigned tick=0;tick<ticks;++tick){if(tick==0||tick==3)assert(fx.line(8,ends.first,ends.second,-1));if(tick)fx.update(tick);
        assert(input.get<uint32_t>()==tick);const auto count=input.get<uint32_t>();assert(count==fx.particles().at(8).size());
        assert(input.get<uint32_t>()==random.seed&&input.get<uint32_t>()==random.index&&input.get<uint32_t>()==unsigned(random.ready));
        assert(input.get<uint32_t>()==coarse&&input.get<uint32_t>()==floor);for(auto value:random.table)assert(input.get<uint32_t>()==value);
        for(const auto& p:fx.particles().at(8)){assert(input.get<uint32_t>()==8&&input.get<uint64_t>()==p.serial);
            for(float value:p.position)assert(std::abs(input.get<float>()-value)<1e-5f);for(float value:p.velocity)assert(std::abs(input.get<float>()-value)<1e-5f);
            assert(input.get<float>()==p.size&&input.get<int32_t>()==p.age&&input.get<uint32_t>()==p.tile);for(auto color:p.color)assert(input.get<uint8_t>()==color);
            assert(input.get<uint8_t>()==unsigned(p.stopped)&&!p.stopped);++checked;}
        const auto saved=fx.snapshot();const auto batches=fx.draw({{1,0,0}},{{0,1,0}},{});assert(batches.size()==count&&fx.snapshot()==saved);
        for(const auto& b:batches)assert(b.vertices.size()==6&&b.tint[0]==64.f/255&&b.tint[3]==1);
        if(tick==5){const auto rng=random;const auto c=coarse,f=floor,r=repel;fx.update(6);fx.restore(saved);random=rng;coarse=c;floor=f;repel=r;assert(fx.snapshot()==saved);}
    }
    assert(input.cursor==bytes.size()&&repel==239&&floor>0&&checked==477);
    std::cout<<"source raw8/648/670 shells: "<<checked<<" states, physical13 forces/gravity/motion/bounce/RNG/atlas/save passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
