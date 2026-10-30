#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace ednativeaudio {
inline unsigned volumeLevel(int percent) {
    percent=std::clamp(percent,0,100);if(!percent)return 0;
    const float ratio=float(percent)*.009999999776482582f;
    const auto attenuation=uint8_t(int(std::log(float(ratio*ratio))*-23.083120346069336f));
    return (255u-attenuation)>>1; //8c099220, then amSound/amStream reduce this to four bits.
}
inline float gain(int percent) {
    constexpr std::array<unsigned,16> q15{{0,256,362,512,724,1024,1448,2048,2896,4096,5792,8192,11585,16384,23170,32768}};
    return float(q15[volumeLevel(percent)>>3])*(1.f/32768.f);
}
inline int masterPercent(int requested,int master) {
    requested=std::clamp(requested,0,100);const auto value=uint8_t(master);
    return value<100?int(float(float(requested)*float(value))*.009999999776482582f):requested;
}
inline float soundGain(int requested,int master=100){return gain(masterPercent(requested,master));}
inline float streamGain(int master){return gain(uint8_t(master));}
inline std::vector<uint8_t> centeredMoviePcm(const std::vector<uint8_t>& stereo) {
    if(stereo.size()%4)throw std::runtime_error("Incomplete stereo movie PCM");
    std::vector<uint8_t> mono(stereo.size()/2);
    const auto sample=[](const uint8_t* p){const unsigned value=unsigned(p[0])|(unsigned(p[1])<<8);return value<32768?int(value):int(value)-65536;};
    //8c080ee0 centers both SFD channels in mono mode; each native pan0 reaches both outputs.
    for(std::size_t in=0,out=0;in<stereo.size();in+=4,out+=2){
        const int value=std::clamp(sample(stereo.data()+in)+sample(stereo.data()+in+2),-32768,32767);
        mono[out]=uint8_t(value);mono[out+1]=uint8_t(unsigned(value)>>8);
    }
    return mono;
}
} // namespace ednativeaudio
