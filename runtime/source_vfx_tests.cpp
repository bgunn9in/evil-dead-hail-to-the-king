#include "source_vfx.h"
#include "script_scene.h"
#include "source_animation_event.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

struct Random {
    uint32_t seed=12345,index=0;bool ready=false;std::array<uint32_t,64> table{};unsigned singles=0,cachedCalls=0;
    uint32_t step(){seed=seed*0x41c64e6du+0x3039u;return uint32_t(((uint64_t(seed)*0x88888889u)>>35)%0x07ffffffu);}
    int bound(int n){++singles;return n?int(step()%uint32_t(std::abs(int64_t(n)))):0;}
    int cached(int n){++cachedCalls;if(!n)return 0;if(!ready){for(auto& v:table)v=step();ready=true;}
        if(++index>=64){index=0;ready=false;}return int(table[index]%uint32_t(std::abs(int64_t(n))));}
};
template<class F>bool rejects(F f){try{f();return false;}catch(const std::exception&){return true;}}
static void rangedTests(const std::filesystem::path& root) {
    if(!std::filesystem::exists(root/"vfx/ranged-effects.bin"))return;
    Random random;unsigned coarseQueries=0;edvfx::Host host;host.visible=[](auto){return true;};
    host.randomBound=[&](int n){return random.bound(n);};host.cachedBound=[&](int n){return random.cached(n);};
    host.coarseHeight=[&](float,float){++coarseQueries;return 1.375f;};
    edvfx::SourceVfx fx(root,host);assert(fx.supports(7)&&fx.supports(22));
    const auto bytes=edobjects::detail::read(root/"vfx/ranged-goldens.bin",1024*1024);edobjects::detail::Reader input{bytes};input.magic("EDVFXRG1");
    const auto frames=input.get<uint32_t>();assert(frames==42);std::array<uint8_t,36> raw{};for(auto& b:raw)b=input.get<uint8_t>();
    const auto event=edanimevent::decode(raw);assert(event.frame==12&&event.kind==7&&event.enabled==1&&event.mode==-1&&!event.trailing);
    const auto placed=edanimevent::visualEndpoints(event,{{0,edactormath::HalfPi,0}},{{2,3,4}});
    for(unsigned endpoint=0;endpoint<2;++endpoint){const auto& original=endpoint?event.second:event.first;const auto& actual=endpoint?placed.second:placed.first;
        assert(std::abs(actual[0]-(2+original[2]))<1e-6f&&std::abs(actual[1]-(3+original[1]))<1e-6f&&std::abs(actual[2]-(4+original[0]))<1e-6f);}
    auto unsupported=event;unsupported.mode=1;assert(rejects([&]{edanimevent::visualEndpoints(unsupported,{},{});}));
    assert(fx.line(7,{{1,2,3}},{{1.25f,2,3.5f}}));unsigned checked=0;
    for(unsigned tick=0;tick<frames;++tick){if(tick==3)assert(fx.line(7,{{-1,1,2}},{{-.5f,1.25f,2}}));if(tick)fx.update(tick);
        assert(input.get<uint32_t>()==tick);const auto count=input.get<uint32_t>();
        assert(input.get<uint32_t>()==random.seed&&input.get<uint32_t>()==random.index&&input.get<uint32_t>()==unsigned(random.ready));
        assert(input.get<uint32_t>()==fx.childPhase(7));for(auto word:random.table)assert(word==input.get<uint32_t>());
        std::vector<edvfx::Particle> actual;for(const auto& [kind,group]:fx.particles()){(void)kind;actual.insert(actual.end(),group.begin(),group.end());}assert(actual.size()==count);
        for(const auto& p:actual){assert(p.kind==input.get<uint32_t>()&&p.serial==input.get<uint64_t>());edvfx::Vector position,velocity;
            for(auto& n:position)n=input.get<float>();for(auto& n:velocity)n=input.get<float>();const auto size=input.get<float>();
            assert(p.age==input.get<int32_t>()&&p.tile==input.get<uint32_t>());for(auto c:p.color)assert(c==input.get<uint8_t>());
            for(unsigned axis=0;axis<3;++axis)assert(std::abs(p.position[axis]-position[axis])<1e-6f&&std::abs(p.velocity[axis]-velocity[axis])<1e-7f);
            assert(std::abs(p.size-size)<1e-6f);++checked;}
        if(tick==4){const auto saved=fx.snapshot();const auto phase=fx.childPhase(7);fx.update(5);fx.restore(saved);assert(fx.snapshot()==saved&&fx.childPhase(7)==phase);
            auto bad=saved;bad.pop_back();assert(rejects([&]{fx.restore(bad);}));assert(fx.snapshot()==saved);
            const auto draw=fx.draw({{1,0,0}},{{0,1,0}},{{0,3,0}});assert(draw.size()==count&&coarseQueries>0);
            const auto& shot=fx.particles().at(7).front();assert(draw.front().vertices.size()==6);
            assert(draw.front().vertices[0].x==shot.position[0]&&draw.front().vertices[1].x==shot.position[0]-shot.velocity[0]);
            assert(std::abs(draw.front().vertices[2].x-shot.position[0]-.02f)<1e-6f&&std::abs(draw.front().vertices[5].x-shot.position[0]+.02f)<1e-6f);
            const auto firstTrail=fx.particles().at(7).size();assert(draw.at(firstTrail).vertices[0].y==1.375f);
            if(fx.particles().at(22).size()>1)assert(std::abs(draw.at(firstTrail+1).vertices[0].y-1.375f-.00002f)<1e-7f);}
    }
    assert(input.cursor==bytes.size()&&random.singles==0&&random.cachedCalls==2);
    for(const auto& [kind,group]:fx.particles()){(void)kind;assert(group.empty());}
    fx.clear();assert(fx.line(7,{{0,0,0}},{{0,0,1}}));const auto fresh=fx.snapshot();
    statebytes::Reader reader{fresh};const auto version=reader.text();assert(version=="EDVFXS3"||version=="EDVFXS4"||version=="EDVFXS5");reader.get<uint32_t>();reader.get<uint64_t>();reader.flag();reader.get<uint64_t>();
    for(unsigned i=reader.count(35);i;--i){reader.get<uint32_t>();reader.get<uint32_t>();}
    for(unsigned i=reader.count(35);i;--i){reader.get<uint32_t>();reader.get<uint32_t>();}
    if(version=="EDVFXS4"||version=="EDVFXS5"){for(unsigned i=reader.count(3);i;--i){reader.get<uint32_t>();reader.get<int32_t>();}reader.flag();reader.get<uint64_t>();}
    assert(reader.count(1500)==1);reader.get<uint32_t>();reader.get<uint64_t>();auto invalid=fresh;
    const uint32_t nan=0x7fc00000u;std::memcpy(invalid.data()+reader.cursor,&nan,4);assert(rejects([&]{fx.restore(invalid);}));assert(fx.snapshot()==fresh);
    std::cout<<"Original ranged visual7/trail22: "<<checked<<" source states, rawframe12 event, RNG, shared child timer, crossed triangles and float-grid decals: passed\n";
}
static void hitTests(const std::filesystem::path& root) {
    Random random;uint32_t coarseCalls=0,floorCalls=0;edvfx::Vector joint{{1,1,2}};
    edvfx::Host host;host.randomBound=[&](int n){return random.bound(n);};host.cachedBound=[&](int n){return random.cached(n);};
    host.visible=[](auto){return true;};host.coarseCell=[&](float,float){++coarseCalls;return int16_t(0);};
    host.floorSample=[&](float,float,float){++floorCalls;return 0.0;};
    host.jointWorld=[&](int type,int index,unsigned bone,const std::array<int32_t,3>& offset){
        assert(type==4&&index==0&&bone==1&&(offset==std::array<int32_t,3>{}));return joint;};
    edvfx::SourceVfx fx(root,host);assert(fx.supports(2)&&fx.supports(18)&&fx.supports(19));
    const auto bytes=edobjects::detail::read(root/"vfx/hit-goldens.bin",4*1024*1024);edobjects::detail::Reader input{bytes};input.magic("EDVFXJ1\0");
    const auto requests=input.get<uint32_t>();assert(requests==1);std::vector<std::vector<int32_t>> calls;
    for(unsigned i=0;i<requests;++i){std::vector<int32_t> args;for(unsigned n=0;n<7;++n)args.push_back(input.get<int32_t>());calls.push_back(args);}
    for(auto x:joint)assert(x==input.get<float>());for(const auto& args:calls)assert(fx.request("vfxSpawnActorJoint",args));
    assert(fx.spawn(19,{{.25f,.8f,.75f}}));const auto frames=input.get<uint32_t>();assert(frames==305);unsigned checked=0;
    for(unsigned n=0;n<frames;++n) {
        const auto tick=input.get<uint32_t>(),count=input.get<uint32_t>();assert(tick==n);if(tick)fx.update(tick);
        assert(random.seed==input.get<uint32_t>()&&random.index==input.get<uint32_t>()&&unsigned(random.ready)==input.get<uint32_t>());
        assert(coarseCalls==input.get<uint32_t>()&&floorCalls==input.get<uint32_t>());for(auto word:random.table)assert(word==input.get<uint32_t>());
        std::vector<edvfx::Particle> actual;for(const auto& [kind,group]:fx.particles()){(void)kind;actual.insert(actual.end(),group.begin(),group.end());}assert(actual.size()==count);
        for(const auto& p:actual) {
            assert(p.kind==input.get<uint32_t>()&&p.serial==input.get<uint64_t>());edvfx::Vector position,velocity;
            for(auto& x:position)x=input.get<float>();for(auto& x:velocity)x=input.get<float>();const auto size=input.get<float>();
            assert(p.age==input.get<int32_t>()&&p.tile==input.get<uint32_t>());for(auto c:p.color)assert(c==input.get<uint8_t>());
            for(auto r:p.rotation)assert(r==input.get<uint8_t>());assert(unsigned(p.stopped)==input.get<uint8_t>());
            for(unsigned axis=0;axis<3;++axis){assert(std::abs(p.position[axis]-position[axis])<1e-6f);assert(std::abs(p.velocity[axis]-velocity[axis])<1e-7f);}
            assert(std::abs(p.size-size)<1e-5f);++checked;
        }
        if(tick==20) {
            const auto saved=fx.snapshot();const auto rng=random;const auto coarse=coarseCalls,floor=floorCalls;
            fx.update(21);fx.restore(saved);random=rng;coarseCalls=coarse;floorCalls=floor;assert(fx.snapshot()==saved);
            auto bad=saved;bad.back()=2;assert(rejects([&]{fx.restore(bad);}));assert(fx.snapshot()==saved);
            const auto batches=fx.draw({{1,0,0}},{{0,1,0}},{{0,2,0}});assert(!batches.empty());
            const auto& decal=fx.particles().at(18);assert(decal.size()>1);size_t at=fx.particles().at(2).size();
            assert(std::abs(batches.at(at).vertices[0].y-decal[0].position[1])<1e-8f);
            assert(std::abs(batches.at(at+1).vertices[0].y-decal[1].position[1]-.00002f)<1e-8f);
            assert(std::abs((batches.at(at).vertices[1].x-batches.at(at).vertices[0].x)-2*decal[0].size)<1e-6f);
        }
    }
    assert(input.cursor==bytes.size());for(const auto& [kind,group]:fx.particles()){(void)kind;assert(group.empty());}
    // Native sprite node phase advances with no active droplets and survives scene clear.
    fx.clear();const auto phase=fx.snapshot();fx.restore(phase);assert(fx.snapshot()==phase);
    for(unsigned i=0;i<200;++i)assert(fx.spawn(18,{{0,0,0}}));const auto before=random.singles;
    assert(!fx.spawn(18,{{0,0,0}})&&random.singles==before);
    assert(fx.spawn(2,{{0,-1,0}}));const auto allocated=random.singles;fx.update(frames);
    assert(random.singles==allocated&&fx.particles().at(18).size()==200&&fx.particles().at(2).empty());
    assert(rejects([&]{fx.request("vfxSpawnActorJoint",{2,4,0,-1,0,0,0});}));
    std::cout<<"Original actor hit2/recursive18/rotating19: "<<checked<<" source states, RNG/floor queries/atlas phase/continuation restore: passed\n";
}
static void floorNegativeTests(const std::filesystem::path& root) {
    edvfx::Host host;unsigned floorCalls=0;int16_t coarse=2;double actual=-4;
    host.randomBound=[](int bound){return bound==32767?16384:0;};host.cachedBound=[](int){return 0;};host.visible=[](auto){return true;};
    host.coarseCell=[&](float,float){return coarse;};host.floorSample=[&](float,float,float){++floorCalls;return actual;};
    edvfx::SourceVfx fx(root,host);assert(fx.spawn(2,{{0,1,0}}));fx.update(1);
    assert(fx.particles().at(2).empty()&&fx.particles().at(18).size()==20&&floorCalls==20);
    for(const auto& p:fx.particles().at(18))assert(p.position[1]==-4); // Coarse gate kills even above the exact floor.
    fx.clear();actual=1.9;assert(fx.spawn(19,{{0,.8f,0}}));fx.update(2);
    for(const auto& p:fx.particles().at(19))assert((p.position[1]==1&&p.stopped&&p.velocity[1]==0&&p.rotation==std::array<uint8_t,3>({2,8,5})));
    const auto saved=fx.snapshot();fx.update(3);fx.restore(saved);assert(fx.snapshot()==saved);
    fx.clear();coarse=32767;const auto before=floorCalls;assert(fx.spawn(19,{{0,-5,0}}));fx.update(3);assert(floorCalls==before);
    fx.clear();coarse=2;actual=32767;assert(fx.spawn(19,{{0,.8f,0}}));fx.update(4);assert(fx.particles().at(19).empty());
    assert(rejects([&]{fx.update(4);}));
    std::cout<<"source VFX floor negatives: coarse kill / LOW16 bounce / stop flag / missing-floor sentinel: passed\n";
}
static void actorEmitterTests(const std::filesystem::path& root) {
    Random random;std::vector<std::array<int32_t,3>> offsets;edvfx::Vector joint{{1,1,2}};
    edvfx::Host host;host.randomBound=[&](int n){return random.bound(n);};host.cachedBound=[&](int n){return random.cached(n);};host.visible=[](auto){return true;};
    host.jointWorld=[&](int type,int index,unsigned bone,const std::array<int32_t,3>& cm){assert(type==0&&index==0&&bone==10);offsets.push_back(cm);
        return edvfx::Vector{{float(double(joint[0])-double(cm[0])*edactormath::Centimeter),float(double(joint[1])+double(cm[1])*edactormath::Centimeter),float(double(joint[2])+double(cm[2])*edactormath::Centimeter)}};};
    host.actorWorld=[](int type,int index){assert(type==4&&index==0);return edvfx::Vector{{.25f,0,.75f}};};
    edvfx::SourceVfx fx(root,host);assert(fx.supports(0)&&fx.supports(25));
    const auto bytes=edobjects::detail::read(root/"vfx/emitter-goldens.bin",1024*1024);edobjects::detail::Reader input{bytes};input.magic("EDVFXK1\0");
    const auto count=input.get<uint32_t>();assert(count==1);std::vector<int32_t> args;
    for(unsigned n=0;n<7;++n)args.push_back(input.get<int32_t>());assert(fx.request("vfxSpawnActorJointRnd",args));
    assert(input.get<uint32_t>()==1);std::vector<int32_t> birth;
    for(unsigned n=0;n<7;++n)birth.push_back(input.get<int32_t>());assert(fx.request("vfxSpawnActorOffset",birth));
    for(auto x:joint)assert(x==input.get<float>());const auto jitter=input.get<uint32_t>();assert(offsets.size()==jitter+1);
    for(unsigned n=0;n<jitter;++n)for(auto cm:offsets[n+1])assert(cm==input.get<int32_t>());
    const auto frames=input.get<uint32_t>();assert(frames==40);std::map<unsigned,std::vector<uint8_t>> captures;unsigned checked=0;
    for(unsigned n=0;n<frames;++n) {
        assert(input.get<uint32_t>()==n);const auto particles=input.get<uint32_t>();if(n)fx.update(n);
        assert(random.seed==input.get<uint32_t>()&&random.index==input.get<uint32_t>()&&unsigned(random.ready)==input.get<uint32_t>());
        for(auto word:random.table)assert(word==input.get<uint32_t>());unsigned actual=0;
        for(const auto& [kind,group]:fx.particles())for(const auto& p:group) {
            assert(kind==input.get<uint32_t>()&&p.serial==input.get<uint64_t>());edvfx::Vector point,velocity;
            for(auto& x:point)x=input.get<float>();for(auto& x:velocity)x=input.get<float>();const auto size=input.get<float>();
            assert(p.age==input.get<int32_t>()&&p.tile==input.get<uint32_t>());for(auto c:p.color)assert(c==input.get<uint8_t>());
            for(auto r:p.rotation)assert(r==input.get<uint8_t>());assert(unsigned(p.stopped)==input.get<uint8_t>());
            for(unsigned axis=0;axis<3;++axis)assert(std::abs(p.position[axis]-point[axis])<1e-6f&&std::abs(p.velocity[axis]-velocity[axis])<1e-8f);
            assert(std::abs(p.size-size)<1e-5f);++actual;++checked;
        }
        assert(actual==particles);if(n==0||n==1||n==8||n==16)captures[n]=fx.snapshot();
    }
    const auto renderFrames=input.get<uint32_t>();assert(renderFrames==4);
    for(unsigned n=0;n<renderFrames;++n) {
        const auto age=input.get<uint32_t>();fx.restore(captures.at(age));std::array<float,4> tint;for(auto& c:tint)c=input.get<float>();
        const auto vertices=input.get<uint32_t>();const auto batches=fx.draw({{1,0,0}},{{0,1,0}},{{0,0,0}});
        const auto& ring=batches.back();assert(vertices==96&&ring.vertices.size()==96);
        for(unsigned c=0;c<4;++c)assert(std::abs(tint[c]-ring.tint[c])<1e-7f);
        for(const auto& vertex:ring.vertices)for(float actual:{vertex.x,vertex.y,vertex.z,vertex.u,vertex.v})assert(std::abs(actual-input.get<float>())<1e-6f);
    }
    assert(input.cursor==bytes.size());const auto calls=random.singles;
    assert(!fx.request("vfxSpawnActorJointRnd",{0,0,0,10,0,26,0})&&random.singles==calls);
    assert(rejects([&]{fx.request("vfxSpawnActorOffset",{25,4,0,0,0,0,4});})); // Variant4 remains outside the verified slice.
    // Native cap is checked once before the whole emission; count1..2 can leave101 in cap100.
    bool first=true;host.randomBound=[&](int bound){if(bound==2){const int result=first?0:1;first=false;return result;}return bound?bound/2:0;};
    host.cachedBound=[](int){return 0;};edvfx::SourceVfx capped(root,host);
    for(unsigned n=0;n<51;++n)assert(capped.spawn(0,{{0,0,0}}));assert(capped.particles().at(0).size()==101&&!capped.spawn(0,{{0,0,0}}));
    const auto saved=capped.snapshot();capped.restore(saved);assert(capped.snapshot()==saved);
    std::cout<<"source chainsaw747/birth1628 emitters: "<<checked<<" source states, joint jitter RNG,16segment ring384 vertices, cap overshoot/save: passed\n";
}
int main(int argc,char**argv){try{
    if(argc!=2)throw std::runtime_error("Pass exported episode asset root");const std::filesystem::path root=argv[1];Random random;bool visible=true;
    const ScriptScene scene(edobjects::detail::read(root/"ak09/scene.native",16*1024*1024));
    edvfx::Host host;host.marker=[&](int index){if(index<0||size_t(index)>=scene.markers.size())throw std::runtime_error("Missing source marker");
        const auto& p=scene.markers[size_t(index)];return edvfx::Vector{{p.x,p.y,p.z}};};
    host.randomBound=[&](int n){return random.bound(n);};host.cachedBound=[&](int n){return random.cached(n);};host.visible=[&](auto){return visible;};
    edvfx::SourceVfx fx(root,host);const auto bytes=edobjects::detail::read(root/"vfx/state-goldens.bin",1024*1024);edobjects::detail::Reader input{bytes};input.magic("EDVFXG1\0");
    const auto requests=input.get<uint32_t>();assert(requests==2);
    for(unsigned i=0;i<requests;++i){std::vector<int32_t> args;for(unsigned j=0;j<7;++j)args.push_back(input.get<int32_t>());
        assert(random.bound(200)==args[4]&&random.bound(100)-50==args[5]);assert(fx.request("vfxSpawnMarkerOffset",args));}
    edvfx::Vector point;for(auto& x:point)x=input.get<float>();assert(fx.spawn(23,point));
    assert(random.seed==input.get<uint32_t>()&&random.index==input.get<uint32_t>()&&unsigned(random.ready)==input.get<uint32_t>());
    for(auto n:random.table)assert(n==input.get<uint32_t>());assert(random.singles==7&&random.cachedCalls==2);
    const auto initial=fx.snapshot();const auto batches=fx.draw({{1,0,0}},{{0,1,0}},{{0,0,0}});assert(batches.size()==3&&batches[0].vertices.size()==6);
    for(const auto& batch:batches)assert(batch.texture==0x0816e044&&batch.tint[3]<=1);
    const auto& spark=batches[2].vertices;edvfx::Vector center{{(spark[0].x+spark[2].x)/2,(spark[0].y+spark[2].y)/2,(spark[0].z+spark[2].z)/2}};
    double shift=0;for(unsigned axis=0;axis<3;++axis)shift+=std::pow(double(center[axis])-point[axis],2);assert(std::abs(std::sqrt(shift)-.5)<1e-6);

    visible=false;assert(fx.draw({{1,0,0}},{{0,1,0}},{{0,0,0}}).empty());visible=true;
    const auto frames=input.get<uint32_t>();assert(frames==44);unsigned checked=0;
    for(unsigned n=0;n<frames;++n){const auto tick=input.get<uint32_t>(),count=input.get<uint32_t>();assert(tick==n);if(tick)fx.update(tick);
        std::vector<edvfx::Particle> actual;for(const auto& [kind,particles]:fx.particles()){(void)kind;actual.insert(actual.end(),particles.begin(),particles.end());}assert(actual.size()==count);
        for(const auto& a:actual){const auto kind=input.get<uint32_t>();const auto serial=input.get<uint64_t>();edvfx::Vector position,velocity;
            for(auto& x:position)x=input.get<float>();for(auto& x:velocity)x=input.get<float>();const auto size=input.get<float>();const auto age=input.get<int32_t>();const auto tile=input.get<uint32_t>();
            std::array<uint8_t,4> color;for(auto& x:color)x=input.get<uint8_t>();assert(a.kind==kind&&a.serial==serial&&a.age==age&&a.tile==tile&&a.color==color);
            for(unsigned axis=0;axis<3;++axis)assert(std::abs(a.position[axis]-position[axis])<1e-6f&&std::abs(a.velocity[axis]-velocity[axis])<1e-8f);
            assert(std::abs(a.size-size)<1e-5f);++checked;}
        if(tick==5){const auto saved=fx.snapshot();fx.update(6);fx.restore(saved);assert(fx.snapshot()==saved);
            auto bad=saved;bad.pop_back();assert(rejects([&]{fx.restore(bad);}));assert(fx.snapshot()==saved);}
    }
    assert(input.cursor==bytes.size());fx.restore(initial);assert(fx.snapshot()==initial);
    const auto beforeSingles=random.singles;for(unsigned i=0;i<98;++i)assert(fx.spawn(9,{{0,0,0}}));
    assert(!fx.spawn(9,{{0,0,0}})&&random.singles==beforeSingles+98);
    assert(rejects([&]{fx.spawn(34,{{0,0,0}});}));assert(rejects([&]{fx.spawn(9,{{0,0,0}},10);}));
    assert(rejects([&]{fx.request("vfxSpawnMarkerOffset",{9,-1,0,0,0,0,0});}));
    const auto atlas=edobjects::Texture::load(root/"vfx/atlas.rgba");assert(atlas.identity==0x0816e044&&atlas.width==512&&atlas.height==512);
    hitTests(root);floorNegativeTests(root);actorEmitterTests(root);rangedTests(root);
    std::cout<<"Original marker9/23 particles: "<<checked<<" source states, complete JPC2051 RNG interleave, atlas/billboards/capacity/continuation restore: passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
