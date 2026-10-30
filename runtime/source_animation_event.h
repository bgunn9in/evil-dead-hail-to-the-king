#pragma once
#include "source_actor_math.h"
#include <cstring>
#include <utility>

// Native405CC0: 36-byte source animation events, sampled before frame increment.
namespace edanimevent {
struct Line {
    uint16_t frame=0;uint8_t kind=0,enabled=0;int32_t mode=-1;
    edactormath::Vector first{},second{};uint32_t trailing=0;
};
struct Sound {
    uint32_t resource=0;int32_t sourcePointer=-1;uint8_t frame=0,authoredVolume=0,kHz=0,enabled=0;
    uint32_t cachedHandle=0,trailing=0;
    int frequencyHz()const{return int(kHz?kHz:22)*1000;}
};
inline Sound decodeSound(const std::array<uint8_t,20>& bytes) {
    Sound sound;std::memcpy(&sound.resource,bytes.data(),4);std::memcpy(&sound.sourcePointer,bytes.data()+4,4);
    sound.frame=bytes[8];sound.authoredVolume=bytes[9];sound.kHz=bytes[10];sound.enabled=bytes[11];
    std::memcpy(&sound.cachedHandle,bytes.data()+12,4);std::memcpy(&sound.trailing,bytes.data()+16,4);return sound;
}
inline Line decode(const std::array<uint8_t,36>& bytes) {
    Line result;result.frame=uint16_t(bytes[0])|(uint16_t(bytes[1])<<8);result.kind=bytes[2];result.enabled=bytes[3];
    std::memcpy(&result.mode,bytes.data()+4,4);std::memcpy(result.first.data(),bytes.data()+8,12);
    std::memcpy(result.second.data(),bytes.data()+20,12);std::memcpy(&result.trailing,bytes.data()+32,4);
    edactormath::finite(result.first);edactormath::finite(result.second);return result;
}
inline std::pair<edactormath::Vector,edactormath::Vector> visualEndpoints(
        const Line& event,const edactormath::Euler& modelEuler,const edactormath::Vector& actorWorld) {
    const bool ordinary=(event.kind==0||event.kind==6||event.kind==8||event.kind==9||event.kind==16||event.kind==17||event.kind==20||event.kind==21)&&(event.mode==-1||event.mode==0);
    const bool debris=event.kind==15&&event.mode>=-1&&event.mode<=2;
    const bool corona=event.kind==24&&(event.mode==1||event.mode==4);
    const bool ring=event.kind==25&&event.mode>=0&&event.mode<=3;
    if(!ordinary&&!debris&&!corona&&!ring&&!(event.kind==7&&event.mode==-1))throw std::runtime_error("Unrecovered source animation-line kind/mode");
    return {edactormath::jointWorld(modelEuler,event.first,actorWorld),edactormath::jointWorld(modelEuler,event.second,actorWorld)};
}
inline bool scriptVfxCallback(uint8_t kind){return kind==8||kind==9||kind==17||kind==20||kind==35;}
inline bool skipsVisual(uint8_t kind){return kind==34||kind==35||kind==36;} // Native405FEC..405FF6.
inline std::array<int32_t,4> rumble35(const Line& event,int actorType) {
    if(event.kind!=35)throw std::runtime_error("Source rumble requires raw35");
    edactormath::finite(event.first);edactormath::finite(event.second);
    auto integer=[](double value){if(value<std::numeric_limits<int32_t>::min()||value>std::numeric_limits<int32_t>::max())
        throw std::runtime_error("Source rumble exceeds native integer range");return int32_t(value);};
    const double depth=double(std::abs(int64_t(integer(double(event.first[2])-event.second[2]))));
    double left=64,right=64;
    if(event.second[0]>=0&&event.first[0]>0){left=-event.first[0];right=event.second[0];}
    else if(event.second[0]<=0&&event.first[0]>=0){left=-event.second[0];right=event.first[0];}
    return {{actorType,integer(left*100),integer(right*100),integer(double(float(depth))*100)}};
}
} // namespace edanimevent
