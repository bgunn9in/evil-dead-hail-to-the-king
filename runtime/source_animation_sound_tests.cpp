#include "source_animation_sound.h"
#include "audio_bank.h"
#include "jpc.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

static jpc::Value I(int n){return jpc::Value::integer(n);}
static jpc::Value R(jpc::Ref n){return jpc::Value::reference(n);}
template<class F>static bool rejects(F fn){try{fn();return false;}catch(const std::exception&){return true;}}
static std::map<uint32_t,edanimevent::Sound> ashRecords(const std::filesystem::path& root) {
    const auto file=root/"actors/ash/animation-events.bin";assert(std::filesystem::file_size(file)<1024*1024);
    std::ifstream input(file,std::ios::binary);std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(input),{});
    assert(bytes.size()>=12&&std::memcmp(bytes.data(),"EDANEV1\0",8)==0);size_t cursor=8;
    auto u32=[&]{assert(cursor<=bytes.size()&&bytes.size()-cursor>=4);uint32_t value;std::memcpy(&value,bytes.data()+cursor,4);cursor+=4;return value;};
    const auto clips=u32();assert(clips<=4096);std::map<uint32_t,edanimevent::Sound> result;
    for(unsigned n=0;n<clips;++n){u32();const auto count36=u32(),count20=u32();assert(count36<=4096&&count20<=4096);
        assert(cursor<=bytes.size()&&size_t(count36)*36<=bytes.size()-cursor);cursor+=size_t(count36)*36;
        for(unsigned k=0;k<count20;++k){assert(cursor<=bytes.size()&&20<=bytes.size()-cursor);std::array<uint8_t,20> raw;
            std::memcpy(raw.data(),bytes.data()+cursor,20);cursor+=20;const auto sound=edanimevent::decodeSound(raw);
            if(sound.enabled)result.emplace(sound.resource,sound);}}
    assert(cursor==bytes.size());return result;
}
static void materialQuery(const std::filesystem::path& root) {
    SourceSceneQueries scene;scene.grid={-10,-10,20,20};
    auto add=[&](uint32_t codes,uint32_t mask=0xffffffff){SourceSceneQueries::Volume v;
        v.geometry.kind=7;v.geometry.shape=0;v.geometry.points={{-5,0,-5},{5,0,5}};v.mask=mask;v.flags=codes;scene.volumes.push_back(v);};
    add(0x06000100);add(0x02030004);add(0x08000000);
    assert((edanimsound::surfaceCodes(scene,{0,0,0},{true,true,true})==edanimsound::Materials{{6,1,2,3}}));
    assert((edanimsound::surfaceCodes(scene,{0,0,0},{false,true,true})==edanimsound::Materials{{2,3,4,8}}));
    assert((edanimsound::surfaceCodes(scene,{9,0,9},{true,true,true})==edanimsound::Materials{}));
    scene.volumes[0].mask=0xffff;assert((edanimsound::surfaceCodes(scene,{0,0,0},{true,false,false})==edanimsound::Materials{}));
    assert(rejects([&]{edanimsound::surfaceCodes(scene,{0,0,0},{true});}));
    const auto file=root/"ak01/scene.native";std::ifstream input(file,std::ios::binary);
    std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(input),{});SourceSceneQueries original(bytes);
    assert(original.volumes.at(20).geometry.kind==7&&original.volumes[20].flags==0x06000000);
    const auto& points=original.volumes[20].geometry.points;GroundPoint center{};
    for(const auto& p:points){center.x+=p.x/float(points.size());center.z+=p.z/float(points.size());}
    auto enabled=std::vector<bool>(original.volumes.size(),true);const auto codes=edanimsound::surfaceCodes(original,center,enabled);
    assert(codes[0]==6);enabled[20]=false;assert(edanimsound::surfaceCodes(original,center,enabled)[0]!=6);
    assert(edanimsound::actorFrequencyHz(44001)==22000&&edanimsound::actorFrequencyHz(44003)==22001);
    assert(edanimsound::actorFrequencyHz(-1)==0&&edanimsound::actorFrequencyHz(std::numeric_limits<int>::min())==0);
    assert(edanimsound::verifiedActor(0)&&edanimsound::verifiedActor(18)&&edanimsound::verifiedActor(19)&&edanimsound::verifiedActor(31)&&!edanimsound::verifiedActor(99));
}
static void sourceAsh(const std::filesystem::path& root) {
    const auto records=ashRecords(root);AudioBank bank(root);const auto package=jpc::Package::load(root/"scripts/levelA.jpc");jpc::Interpreter vm(package);vm.initialize("Ash");
    const auto ash=vm.makeObject(package.classIndex("Ash"),{{5,I(0)},{6,I(0)}});
    vm.statics[53]=R(vm.makeArray('L',{R(ash)}));vm.statics[54]=R(vm.makeArray('I',std::vector<jpc::Value>(40,I(0))));
    const auto weapon=vm.makeObject(package.classIndex("Gun"),{{812,I(40)},{815,I(1)}});vm.setField(ash,103,R(weapon));
    struct Play{int clip,volume,frequency;};std::vector<Play> played;unsigned randomCalls=0;
    vm.bind("ed/base/External","random","(I)I",[&](auto&,auto,const auto& a){assert(a[0].integer()>0);++randomCalls;return I(0);});
    vm.bind("ed/base/External","sndMgrGetSNDID","(I)I",[&](auto&,auto,const auto& a){return I(bank.resolve("ak01",uint32_t(a[0].integer())));});
    vm.bind("ed/base/External","sndMgrActorPlaySound","(IIIII)I",[&](auto&,auto,const auto& a){
        assert(a[0].integer()==0&&a[1].integer()==0);const int hz=edanimsound::actorFrequencyHz(a[4].integer());
        played.push_back({a[2].integer(),a[3].integer(),hz});return I(bank.playAtFrequency(a[2].integer(),a[3].integer(),hz));});
    const auto method=package.methodIndex("ActorDB","processSFXAction","(IIIIIIIIIII)I");
    auto dispatch=[&](uint32_t id,edanimsound::Materials material={}){
        const auto& sound=records.at(id);
        const auto raw=edanimsound::ashArguments(sound,0,bank.resolve("ak01",id),material);assert(raw[2]==100&&raw[3]==sound.frequencyHz()*2);
        std::vector<jpc::Value> args;for(auto value:raw)args.push_back(I(value));return vm.invoke(method,0,args).integer();};
    for(const auto& row:std::vector<std::array<int,4>>{{40,0x10004,30,0},{199,0x10004,70,51000},{41,0x10002,32,0},{200,0x10002,50,40000},{42,0x10005,50,0},{201,0x10005,65,38000}}) {
        bank.stopAll();played.clear();randomCalls=0;vm.setField(weapon,812,I(row[0]));assert(dispatch(uint32_t(row[1]))==1);
        assert(randomCalls==1&&played.size()==(row[3]?2u:1u));const auto& main=played.back();
        assert(main.clip==bank.resolve("ak01",uint32_t(row[1]))&&main.volume==row[2]&&main.frequency==records.at(uint32_t(row[1])).frequencyHz()-1250);
        if(row[3])assert(played[0].clip==bank.resolve("ak01",0x0e991226)&&played[0].volume==100&&played[0].frequency==row[3]/2);
    }
    vm.invoke(package.methodIndex("Ash","clearSoundFootSteps","()V"),ash);
    // Actual AK01.activateScene2024 literals: slot6 is the second source surface.
    vm.invoke(package.methodIndex("Ash","setSoundFootStep","(III)V"),ash,{I(244910242),I(781781154),I(6)});
    played.clear();randomCalls=0;bank.stopAll();assert(dispatch(0x10000,{{6,0,0,0}})==1);
    assert(played.size()==1&&played[0].clip==bank.resolve("ak01",244910242)&&played[0].frequency==records.at(0x10000).frequencyHz()-5000&&played[0].volume==100);
    assert(vm.staticField(128).integer()==-1&&randomCalls==1);assert(dispatch(0x10000,{{6,0,0,0}})==1);
    assert(played.size()==2&&played[1].clip==bank.resolve("ak01",781781154)&&vm.staticField(128).integer()==1);
    played.clear();randomCalls=0;assert(dispatch(0x10000,{{8,0,0,0}})==0&&played.empty()&&randomCalls==0);
    vm.setField(weapon,815,I(0));assert(dispatch(0x10007)==1&&played.empty());vm.setField(weapon,815,I(1));assert(dispatch(0x10007)==0);
    vm.setField(ash,103,R(0));assert(dispatch(0x10007)==0);assert(dispatch(0x10006)==0);
    std::cout<<"source Ash637/658 footsteps, six weapon variants, SAR pitch, empty-ammo suppression and NPC material ordering: passed\n";
}
int main(int argc,char** argv){try{if(argc!=2)throw std::runtime_error("Expected generated episode asset root");
    const std::filesystem::path root=argv[1];materialQuery(root);sourceAsh(root);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
