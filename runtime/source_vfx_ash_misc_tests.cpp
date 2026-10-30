#include "source_vfx.h"
#include "source_animation_event.h"
#include "jpc.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

static jpc::Value I(int n){return jpc::Value::integer(n);}
static std::array<uint8_t,36> originalEvent(const std::filesystem::path& root,uint32_t clip,unsigned frame,unsigned kind) {
    const auto path=root/"actors/ash/animation-events.bin";assert(std::filesystem::file_size(path)<1024*1024);
    std::ifstream file(path,std::ios::binary);std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(file),{});assert(bytes.size()>=12&&std::memcmp(bytes.data(),"EDANEV1\0",8)==0);
    size_t cursor=8;auto u32=[&]{assert(cursor<=bytes.size()&&4<=bytes.size()-cursor);uint32_t n;std::memcpy(&n,bytes.data()+cursor,4);cursor+=4;return n;};
    const auto clips=u32();assert(clips<=4096);
    for(unsigned n=0;n<clips;++n){const auto id=u32(),count36=u32(),count20=u32();assert(count36<=4096&&count20<=4096);
        for(unsigned k=0;k<count36;++k){assert(cursor<=bytes.size()&&36<=bytes.size()-cursor);std::array<uint8_t,36> raw;
            std::memcpy(raw.data(),bytes.data()+cursor,36);cursor+=36;const auto e=edanimevent::decode(raw);if(id==clip&&e.frame==frame&&e.kind==kind&&e.enabled)return raw;}
        assert(cursor<=bytes.size()&&size_t(count20)*20<=bytes.size()-cursor);cursor+=size_t(count20)*20;}
    throw std::runtime_error("Expected original Ash presentation event missing");
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Expected generated episode root");const std::filesystem::path root=argv[1];
    const auto smoke=originalEvent(root,0x01824944,34,0),dust=originalEvent(root,0x01824284,12,9),rumble=originalEvent(root,0x01825108,20,35);
    const auto package=jpc::Package::load(root/"scripts/levelA.jpc");jpc::Interpreter vm(package);vm.initialize("Ash");vm.initialize("RumbleCommander");
    const auto ash=vm.makeObject(package.classIndex("Ash"),{{5,I(0)},{6,I(0)}});
    vm.statics[53]=jpc::Value::reference(vm.makeArray('L',{jpc::Value::reference(ash)}));vm.statics[54]=jpc::Value::reference(vm.makeArray('I',std::vector<jpc::Value>(40,I(0))));
    unsigned hardwareRumble=0;vm.bind("ed/base/External","rumbleP1","(III)V",[&](auto&,auto,const auto&){++hardwareRumble;return jpc::Value{};});
    auto callback=[&](unsigned kind,unsigned material){return vm.invoke(package.methodIndex("ActorDB","processVFXAction","(IIIIIIIIIII)I"),0,{I(0),I(0),I(int(kind)),I(int(material)),I(2),I(3),I(4),I(0),I(0),I(0),I(0)}).integer();};
    for(int classification:{0,1,2,3})for(int material:{0,1,6,8}){vm.statics[762]=I(classification);
        assert(callback(9,unsigned(material))==int(classification==2&&material==0));}
    unsigned rng=0;edvfx::Host host;host.randomBound=[&](int){++rng;return 0;};host.cachedBound=[&](int){++rng;return 0;};host.visible=[](auto){return true;};
    edvfx::SourceVfx effects(root,host);vm.statics[762]=I(2);const auto before=effects.snapshot();assert(callback(9,0)==1&&effects.snapshot()==before&&rng==0);
    assert(callback(9,6)==0);const auto dustLine=edanimevent::decode(dust);auto endpoints=edanimevent::visualEndpoints(dustLine,{{0,.4f,0}},{{3,.7f,-2}});
    assert(effects.line(9,endpoints.first,endpoints.second,dustLine.mode)&&effects.particles().at(9).size()==1);
    const auto smokeLine=edanimevent::decode(smoke);assert(!edanimevent::scriptVfxCallback(0));endpoints=edanimevent::visualEndpoints(smokeLine,{},{});
    assert(effects.line(0,endpoints.first,endpoints.second,smokeLine.mode)&&effects.particles().at(0).size()==1);
    const auto rumbleLine=edanimevent::decode(rumble);const auto args=edanimevent::rumble35(rumbleLine,0);std::vector<jpc::Value> values;for(auto value:args)values.push_back(I(value));
    const auto visual=effects.snapshot();vm.invoke(package.methodIndex("RumbleCommander","engineReportRumbleVFX","(IIII)V"),0,values);assert(callback(35,6)==0&&edanimevent::skipsVisual(35));
    assert(vm.staticField(746).integer()==args[1]&&vm.staticField(747).integer()==(args[2]>0?255:args[2])&&vm.staticField(748).integer()==args[3]&&vm.staticField(749).integer()==0);
    assert(hardwareRumble==0&&effects.snapshot()==visual); // Reporting35 alone does not invent a startRumble request.
    assert(effects.draw({{1,0,0}},{{0,1,0}},{}).size()==2);
    std::cout<<"source Ash0/9/35 actual raw records: material/classification648/670 predicate, original emitters,903 reporting order/no invented rumble passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
