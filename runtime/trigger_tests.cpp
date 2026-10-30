#include "triggers.h"
#undef NDEBUG
#include <cassert>
#include <fstream>
#include <iterator>

namespace {
void integer(std::vector<uint8_t>& data,uint32_t value) {
    for(unsigned byte=0;byte<4;++byte)data.push_back(uint8_t(value>>(byte*8)));
}
void number(std::vector<uint8_t>& data,float value) {
    uint32_t bits;std::memcpy(&bits,&value,4);integer(data,bits);
}
void patch(std::vector<uint8_t>& data,size_t offset,uint32_t value) {
    for(unsigned byte=0;byte<4;++byte)data[offset+byte]=uint8_t(value>>(byte*8));
}
std::vector<uint8_t> fixture(unsigned version=3) {
    std::vector<uint8_t> data={'E','D','T','R','I','G',uint8_t('0'+version),0};integer(data,1);
    for(float value:{-10.f,-10.f,20.f,20.f})number(data,value);
    for(unsigned value:{0u,2u,version==3?2u:1u})integer(data,value);
    if(version==2)integer(data,1);
    integer(data,0xffffffffu);
    for(float value:{0.f,0.f,2.f,2.f})number(data,value);
    if(version==3) {
        for(unsigned value:{1u,1u,1u,1u,0u,1u,1u,0u,1u,6u})integer(data,value);
    } else {integer(data,1);if(version==2)integer(data,2);}
    return data;
}
void rejects(const std::vector<uint8_t>& data,unsigned cameras=7) {
    bool failed=false;try{(void)readCameraTriggers(data,cameras);}catch(const std::runtime_error&){failed=true;}assert(failed);
}
}

int main(int argc,char** argv) {
    auto source=readCameraTriggers(fixture(),7);assert(source.triggers.size()==1);
    const auto& trigger=source.triggers[0];assert(trigger.cameras.empty()&&trigger.rules.size()==2);
    assert(trigger.rules[0].cameraPayload==1&&trigger.rules[1].cameraPayload==0);
    assert(cameraRuleActions(trigger,1,1)==0);
    for(unsigned camera:{0u,2u,6u})assert(cameraRuleActions(trigger,1,camera)==camera);
    assert(cameraRuleActions(trigger,2,1)==1);
    std::vector<bool> membership(1,false);
    assert(enterTriggers(source.triggers,{1,1},membership,1,source.grid)==0&&membership[0]);
    assert(enterTriggers(source.triggers,{1,1},membership,1,source.grid)==1); // No sticky enter predicate.
    assert(enterTriggers(source.triggers,{-1,-1},membership,0,source.grid)==0&&!membership[0]);
    assert(enterTriggers(source.triggers,{1,1},membership,0,source.grid)==0);
    auto twoRules=trigger;twoRules.rules[1].cameraPayload=1;
    assert(cameraRuleActions(twoRules,1,1)==6); // Both see camera1, even after first writes0.
    CameraTrigger first;first.shape=0;first.points={{0,0},{2,2}};first.cameras={1};
    std::vector<CameraTrigger> zones={first,trigger};membership.assign(2,false);
    assert(enterTriggers(zones,{1,1},membership,6,source.grid)==0); // Next zone sees prior zone's camera.
    source.triggers[0].mask=0;membership.assign(1,true);
    assert(enterTriggers(source.triggers,{-1,-1},membership,1,source.grid)==1&&membership[0]);
    for(unsigned version:{1u,2u}) {
        auto old=readCameraTriggers(fixture(version),7);membership.assign(1,false);
        assert(enterTriggers(old.triggers,{1,1},membership,0,old.grid)==1);
        assert(enterTriggers(old.triggers,{-1,-1},membership,0,old.grid)==(version==2?2u:0u));
    }
    const auto valid=fixture();
    for(size_t length=0;length<valid.size();++length)rejects({valid.begin(),valid.begin()+length});
    auto bad=valid;bad.push_back(0);rejects(bad);rejects(valid,0);rejects(valid,257);
    for(auto [offset,value]:std::vector<std::pair<size_t,uint32_t>>{{8,10001},{20,0},{28,3},{32,3},
            {36,257},{44,0x7fc00000},{60,3},{64,2},{72,0},{76,7}}) {
        bad=valid;patch(bad,offset,value);rejects(bad);
    }
    bad=valid;patch(bad,64,0);rejects(bad); // Unconditional rule cannot smuggle an unused payload.
    if(argc>1) {
        std::ifstream input(argv[1],std::ios::binary);assert(input);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
        auto at01=readCameraTriggers(bytes,7);assert(at01.triggers.size()==7);
        assert(at01.triggers.back().rules.size()==2);
        const Vec2 point{.47781842947006226f,-3.5444799661636353f};
        membership.assign(at01.triggers.size(),false);
        assert(enterTriggers(at01.triggers,point,membership,1,at01.grid)==0);
        membership.assign(at01.triggers.size(),false);
        assert(enterTriggers(at01.triggers,point,membership,0,at01.grid)==0);
        membership.assign(at01.triggers.size(),false);
        assert(enterTriggers(at01.triggers,point,membership,6,at01.grid)==6);
    }
}
