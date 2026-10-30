#include "source_vfx.h"
#include "source_animation_event.h"
#include "jpc.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

struct TreeRandom {
    uint32_t seed=12345,index=0;bool ready=false;std::array<uint32_t,64> table{};
    uint32_t step(){seed=seed*0x41c64e6du+0x3039u;return uint32_t(((uint64_t(seed)*0x88888889u)>>35)%0x07ffffffu);}
    int bound(int n){return n?int(step()%uint32_t(std::abs(int64_t(n)))):0;}
    int cached(int n){if(!n)return 0;if(!ready){for(auto& value:table)value=step();ready=true;}
        if(++index>=64){index=0;ready=false;}return int(table[index]%uint32_t(std::abs(int64_t(n))));}
};
template<class F>static bool rejects(F fn){try{fn();return false;}catch(const std::exception&){return true;}}
static void rawEvents(const std::filesystem::path& root) {
    std::array<uint8_t,20> raw{{0xc4,0x2f,0xce,0x0d,255,255,255,255,14,100,24,1,0,0,0,0,0,0,0,0}};
    const auto sound=edanimevent::decodeSound(raw);assert(sound.resource==0x0dce2fc4&&sound.frame==14&&sound.enabled&&sound.frequencyHz()==24000);
    raw[10]=0;assert(edanimevent::decodeSound(raw).frequencyHz()==22000);
    edanimevent::Line event;event.kind=35;event.first={{.5602238774299622f,.005000317469239235f,2.056549310684204f}};
    event.second={{.5165799260139465f,-.6549995541572571f,2.1842997074127197f}};
    const auto args=edanimevent::rumble35(event,5);assert((args==std::array<int32_t,4>{{5,-56,51,0}}));
    assert(edanimevent::skipsVisual(34)&&edanimevent::skipsVisual(35)&&edanimevent::scriptVfxCallback(35)&&!edanimevent::scriptVfxCallback(34));
    jpc::Package package=jpc::Package::load(root/"scripts/levelA.jpc");jpc::Interpreter vm(package);vm.initialize("RumbleCommander");
    auto I=[](int v){return jpc::Value::integer(v);};auto R=[](jpc::Ref v){return jpc::Value::reference(v);};
    std::vector<int> rumble;vm.bind("ed/base/External","rumbleP1","(III)V",[&](auto&,auto,const auto& a){for(auto v:a)rumble.push_back(v.integer());return jpc::Value{};});
    vm.statics[1096]=I(1); // Fixture advertises enabled rumble hardware; no gameplay state is replaced.
    std::vector<jpc::Value> values;for(auto v:args)values.push_back(I(v));vm.invoke(903,0,values);
    assert(vm.staticField(746).integer()==-56&&vm.staticField(747).integer()==255&&vm.staticField(748).integer()==0&&vm.staticField(749).integer()==5);
    const auto npc=vm.makeObject(package.classIndex("TreeBoss"),{{5,I(0)},{6,I(5)}});
    vm.statics[54]=R(vm.makeArray('I',std::vector<jpc::Value>(40,I(0))));vm.statics[53]=R(vm.makeArray('L',{R(npc)}));
    assert(vm.invoke(648,0,{I(5),I(0),I(35),I(0),I(0),I(0),I(0),I(0),I(0),I(0),I(0)}).integer()==0);
    assert(vm.staticField(745).integer()==10&&(rumble==std::vector<int>{0,255,-56}));
    assert(vm.invoke(648,0,{I(5),I(0),I(9),I(0),I(0),I(0),I(0),I(0),I(0),I(0),I(0)}).integer()==0&&rumble.size()==3);
    std::cout<<"source sound20 fields/pitch and raw35 -> original903/648/1525/906/907 rumble ordering: passed\n";
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Expected generated Tree VFX asset root");const std::filesystem::path root=argv[1];rawEvents(root);
    TreeRandom random;edvfx::Host host;host.randomBound=[&](int n){return random.bound(n);};host.cachedBound=[&](int n){return random.cached(n);};
    bool visible=true;host.visible=[&](auto){return visible;};host.coarseCell=[](float,float){return int16_t(0);};host.floorSample=[](float,float,float){return 0.;};host.coarseHeight=[](float,float){return 0.f;};
    edvfx::SourceVfx fx(root,host);assert(fx.supports(15)&&fx.supports(24));
    const auto bytes=edobjects::detail::read(root/"vfx/tree-goldens.bin",16*1024*1024);edobjects::detail::Reader input{bytes};input.magic("EDVFXTG1");
    const auto frames=input.get<uint32_t>(),requests=input.get<uint32_t>();assert(frames==104&&requests==7);
    struct Request {uint32_t tick;std::array<uint8_t,36> raw;};std::vector<Request> events;
    for(unsigned n=0;n<requests;++n){Request r;r.tick=input.get<uint32_t>();for(auto& b:r.raw)b=input.get<uint8_t>();events.push_back(r);}
    uint64_t checked=0;
    for(unsigned tick=0;tick<frames;++tick){for(const auto& request:events)if(request.tick==tick){const auto e=edanimevent::decode(request.raw);
            const auto ends=edanimevent::visualEndpoints(e,{},{{1.25f,.75f,-2}});assert(fx.line(e.kind,ends.first,ends.second,e.mode));}
        if(tick)fx.update(tick);fx.sampleRender();
        assert(input.get<uint32_t>()==tick);const auto count=input.get<uint32_t>();
        const auto seed=input.get<uint32_t>(),index=input.get<uint32_t>(),ready=input.get<uint32_t>();
        if(seed!=random.seed||index!=random.index||ready!=unsigned(random.ready))std::cerr<<"Tree RNG tick="<<tick<<" expected "<<seed<<","<<index<<","<<ready<<" actual "<<random.seed<<","<<random.index<<","<<random.ready<<'\n';
        assert(seed==random.seed&&index==random.index&&ready==unsigned(random.ready));
        for(auto word:random.table)assert(word==input.get<uint32_t>());for(unsigned n=0;n<3;++n)input.get<int32_t>();
        std::vector<edvfx::Particle> particles;for(const auto& [kind,group]:fx.particles()){(void)kind;particles.insert(particles.end(),group.begin(),group.end());}assert(particles.size()==count);
        unsigned batches=0;
        for(const auto& p:particles){assert(input.get<uint32_t>()==p.kind&&input.get<uint64_t>()==p.serial);
            for(float x:p.position)assert(std::abs(input.get<float>()-x)<2e-6f);for(float x:p.velocity)assert(std::abs(input.get<float>()-x)<2e-7f);
            assert(std::abs(input.get<float>()-p.size)<1e-4f&&input.get<int32_t>()==p.age&&input.get<uint32_t>()==p.tile);
            for(auto c:p.color)assert(input.get<uint8_t>()==c);for(auto r:p.rotation)assert(input.get<uint8_t>()==r);
            assert(input.get<uint8_t>()==unsigned(p.stopped)&&input.get<int32_t>()==p.renderMode&&input.get<uint8_t>()==unsigned(p.deferredDraw));
            if(!p.deferredDraw)batches+=p.kind==24?3:1;++checked;}
        const auto before=fx.snapshot();fx.sampleRender();assert(fx.snapshot()==before);const auto draw=fx.draw({{1,0,0}},{{0,1,0}},{{0,2,0}});
        assert(draw.size()==batches&&fx.snapshot()==before);
        if(tick==4){const auto rng=random;fx.update(5);fx.restore(before);random=rng;assert(fx.snapshot()==before);auto bad=before;bad.pop_back();assert(rejects([&]{fx.restore(bad);}));assert(fx.snapshot()==before);}
    }
    assert(input.get<uint32_t>()==4);TreeRandom shapeRandom;auto shapeHost=host;
    shapeHost.randomBound=[&](int n){return shapeRandom.bound(n);};shapeHost.cachedBound=[&](int n){return shapeRandom.cached(n);};
    edvfx::SourceVfx shape(root,shapeHost);assert(shape.line(24,{{.3f,1.4f,-2}},{{.3f,1.4f,-2}},4));
    for(unsigned age=0;age<=16;++age){if(age)shape.update(age);if(age!=0&&age!=1&&age!=8&&age!=16)continue;
        assert(input.get<uint32_t>()==age);const auto draws=shape.draw({{1,0,0}},{{0,1,0}},{});assert(draws.size()==3);
        for(const auto& batch:draws){for(float c:batch.tint)assert(std::abs(input.get<float>()-c)<1e-7f);assert(input.get<uint32_t>()==batch.vertices.size());
            for(const auto& v:batch.vertices)for(float x:{v.x,v.y,v.z,v.u,v.v})assert(std::abs(input.get<float>()-x)<1e-6f);}}
    assert(input.cursor==bytes.size());for(const auto& [kind,group]:fx.particles()){(void)kind;assert(group.empty());}
    assert(fx.line(24,{{0,1,0}},{{0,1,0}},4));fx.update(frames);visible=false;fx.sampleRender();assert(fx.particles().at(22).empty());
    visible=true;fx.update(frames+1);fx.sampleRender();assert(fx.particles().at(22).size()==1&&fx.particles().at(22).front().deferredDraw);
    const auto stable=fx.snapshot();assert(rejects([&]{fx.line(24,{}, {},5);}));assert(fx.snapshot()==stable);
    std::cout<<"source Tree0/9/15/25 variants/full24 with repaired RGB: "<<checked<<" particles, original geometry/UV/alpha, RNG/floor/modes/deferred render/save: passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
