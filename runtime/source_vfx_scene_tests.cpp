#include "source_vfx.h"
#include "jpc.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

static jpc::Value I(int n){return jpc::Value::integer(n);}
template<class F>static bool rejects(F f){try{f();return false;}catch(const std::exception&){return true;}}
struct SceneRandom {
    uint32_t seed=12345,index=0;bool ready=false;std::array<uint32_t,64> table{};
    uint32_t step(){seed=seed*0x41c64e6du+0x3039u;return uint32_t(((uint64_t(seed)*0x88888889u)>>35)%0x07ffffffu);}
    int bound(int n){return n?int(step()%uint32_t(std::abs(int64_t(n)))):0;}
    int cached(int n){if(!n)return 0;if(!ready){for(auto& value:table)value=step();ready=true;}
        if(++index>=64){index=0;ready=false;}return int(table[index]%uint32_t(std::abs(int64_t(n))));}
};
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Expected original scene VFX assets");const std::filesystem::path root=argv[1];
    edvfx::Host host;unsigned calls=0;host.randomBound=host.cachedBound=[&](int){++calls;return 0;};host.visible=[](auto){return true;};
    host.coarseCell=[](float,float){return int16_t(0);};host.coarseHeight=[](float,float){return 0.f;};host.floorSample=[](float,float,float){return 0.;};
    host.actorWorld=[](int type,int index){assert(!type&&!index);return edvfx::Vector{{1.25f,.75f,-2}};};
    host.jointWorld=[](int type,int index,unsigned joint,const auto& cm){assert((type==33&&!index&&!joint&&cm==std::array<int32_t,3>{}));return edvfx::Vector{{3,1.5f,3}};};
    const auto bytes=edobjects::detail::read(root/"vfx/script-goldens.bin",1024*1024);edobjects::detail::Reader input{bytes};input.magic("EDVFXSG2");assert(input.get<uint32_t>()==12);
    for(unsigned n=0;n<12;++n){const auto kind=input.get<uint32_t>(),mode=input.get<uint32_t>(),age=input.get<uint32_t>(),batches=input.get<uint32_t>();
        SceneRandom random;auto shapeHost=host;shapeHost.randomBound=[&](int b){return random.bound(b);};shapeHost.cachedBound=[&](int b){return random.cached(b);};
        edvfx::SourceVfx fx(root,shapeHost);assert(fx.line(kind,{{1.25f,.75f,-2}},{{1.25f,.75f,-2}},int(mode)));
        for(unsigned tick=1;tick<=age;++tick)fx.update(tick);const auto saved=fx.snapshot();const auto draw=fx.draw({{1,0,0}},{{0,1,0}},{});
        assert(draw.size()==batches&&fx.snapshot()==saved);for(const auto& b:draw){for(auto color:b.tint)assert(std::abs(input.get<float>()-color)<1e-7f);
            assert(input.get<uint32_t>()==b.vertices.size());for(const auto& v:b.vertices)for(float c:{v.x,v.y,v.z,v.u,v.v}){const auto expected=input.get<float>();if(std::abs(expected-c)>=1e-5f)std::cerr<<"Geometry kind="<<kind<<" mode="<<mode<<" age="<<age<<" expected="<<expected<<" actual="<<c<<"\n";assert(std::abs(expected-c)<1e-5f);}}
    }assert(input.cursor==bytes.size());
    edvfx::SourceVfx fx(root,host);const auto package=jpc::Package::load(root/"scripts/levelA.jpc");jpc::Interpreter vm(package);
    const auto ash=vm.makeObject(package.classIndex("Ash"),{{5,I(0)},{6,I(0)}});vm.statics[310]=jpc::Value::reference(ash);
    const auto scene=vm.makeObject(package.classIndex("SceneAC05"),{{1355,jpc::Value::reference(ash)}});unsigned requests=0,messages=0;
    vm.bind("ed/base/External","vfxSpawnActorOffset","(IIIIIII)V",[&](auto&,auto,const auto& a){std::vector<int32_t> args;for(auto v:a)args.push_back(v.integer());
        assert(args[0]==15&&args[6]==1&&fx.request("vfxSpawnActorOffset",args));++requests;return jpc::Value{};});
    vm.bind("ed/base/External","sendActorMessage","(IIIIIIII)V",[&](auto&,auto,const auto& a){assert(a[2].integer()==40&&a[5].integer()==3);++messages;return jpc::Value{};});
    vm.invoke(1870,scene);assert(requests==3&&messages==1&&fx.particles().at(15).size()==48);
    const auto& rock=fx.particles().at(15).front();assert(rock.color[0]==255&&rock.velocity[1]<-1.5f); // Special endpointY-1 plus originalmode1 dispersion.
    const auto b=jpc::Package::load(root/"scripts/levelB.jpc");jpc::Interpreter bv(b);const auto cthulhu=bv.makeObject(b.classIndex("Cthulhu"),{{5,I(0)},{6,I(33)}});
    bv.bind("ed/base/External","vfxSpawnActorJointRnd","(IIIIIII)V",[&](auto&,auto,const auto& a){std::vector<int32_t> args;for(auto v:a)args.push_back(v.integer());
        const auto before=calls;assert(fx.request("vfxSpawnActorJointRnd",args));assert(calls-before==4); // Three offsets plus one cached frame.
        return jpc::Value{};});
    bv.invoke(1713,cthulhu,{I(0)});assert(fx.particles().at(24).size()==1&&fx.particles().at(24)[0].renderMode==1);
    fx.clear();assert(fx.line(6,{},{}));fx.update(1);fx.update(100);const auto& paused=fx.particles().at(6)[0];assert(paused.age==2&&paused.size==70&&paused.color[3]==64);
    const auto saved=fx.snapshot();assert(rejects([&]{fx.update(100);})&&rejects([&]{fx.update(2);})&&fx.snapshot()==saved);
    std::cout<<"source SceneAC05 fullJPC rock15/direction/override, Cthulhu1713 genericjoint24/RNG, BG12 original3rows/12geometrycases, monotonicmenu-gap(singleupdate) passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
