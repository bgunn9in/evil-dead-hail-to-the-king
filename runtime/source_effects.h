#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

// Shared PC41F630/660/6E0/700/720 and visible-model sample41F3D0.
namespace edeffects {
constexpr float DefaultTint=128.0f/255.0f;
struct State {
    uint32_t flags=0;
    std::array<uint8_t,3> from{{255,255,255}},to{},rgb{};
    uint8_t fromMode=255,toMode=255,duration=0,elapsed=0;
    bool targetKnown=false,sampled=false;
    static uint8_t color(int value){return value>=127?255:uint8_t(uint32_t(value)*2u);}
    void info(int r,int g,int b,int mode,int channel) {
        const std::array<uint8_t,3> next{{color(r),color(g),color(b)}};
        if(!channel){from=next;fromMode=uint8_t(mode);flags&=~3u;duration=elapsed=0;}
        else {to=next;toMode=uint8_t(mode);targetKnown=true;}
    }
    void start(int mask,int ticks){flags|=uint32_t(mask);elapsed=0;duration=uint8_t(ticks);}
    bool done()const{return elapsed==duration;}
    void setFlag(int mask,bool enabled){if(enabled)flags|=uint32_t(mask);else flags&=~uint32_t(mask);}
    static uint8_t truncateByte(double value) {
        // FTOL451F70 returns the x87 indefinite INT64_MIN for NaN/overflow.
        if(!std::isfinite(value)||value>=9223372036854775808.0||value< -9223372036854775808.0)return 0;
        return uint8_t(uint64_t(int64_t(value)));
    }
    uint32_t sample(const std::array<float,3>& tint={{DefaultTint,DefaultTint,DefaultTint}}) {
        for(float gain:tint)if(!std::isfinite(gain))throw std::runtime_error("Non-finite source effect tint");
        if((flags&3)&&duration&&!targetKnown)throw std::runtime_error("Source fade target contains uninitialized native bytes");
        std::array<uint8_t,3> current=from;
        if(flags&3)for(unsigned i=0;i<3;++i) {
            // shortcut: binary64 follows recovered x87 math; rare arbitrary RGB/period precision ties remain unverified.
            current[i]=duration?truncateByte((int(to[i])-int(from[i]))*(double(elapsed)/duration)+from[i]):0;
        }
        for(unsigned i=0;i<3;++i)rgb[i]=truncateByte(double(current[i])*tint[i]);sampled=true;
        const uint32_t packed=0xff000000u|(uint32_t(rgb[0])<<16)|(uint32_t(rgb[1])<<8)|rgb[2];
        if(flags&&elapsed!=duration) {
            elapsed=uint8_t(unsigned(elapsed)+1);
            if(elapsed==duration) {
                if(flags&1){flags&=~1u;duration=elapsed=0;from=to;fromMode=toMode;}
                else if(flags&2){const auto previous=from;from=to;to=previous;elapsed=0;}
            }
        }
        return packed;
    }
    std::vector<uint8_t> snapshot()const {
        std::vector<uint8_t> bytes{'E','D','F','A','D','E','1',0};
        for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(flags>>(8*i)));
        for(const auto* values:{&from,&to,&rgb})bytes.insert(bytes.end(),values->begin(),values->end());
        for(uint8_t value:{fromMode,toMode,duration,elapsed,uint8_t(targetKnown),uint8_t(sampled)})bytes.push_back(value);
        return bytes;
    }
    void restore(const std::vector<uint8_t>& bytes) {
        if(bytes.size()!=27||std::memcmp(bytes.data(),"EDFADE1\0",8))throw std::runtime_error("Invalid source fade snapshot");
        State next;size_t at=8;next.flags=0;for(unsigned i=0;i<4;++i)next.flags|=uint32_t(bytes[at++])<<(8*i);
        for(auto* values:{&next.from,&next.to,&next.rgb})for(auto& value:*values)value=bytes[at++];
        next.fromMode=bytes[at++];next.toMode=bytes[at++];next.duration=bytes[at++];next.elapsed=bytes[at++];
        if(bytes[at]>1||bytes[at+1]>1)throw std::runtime_error("Invalid source fade initialization state");
        next.targetKnown=bytes[at++]!=0;next.sampled=bytes[at++]!=0;
        if((!next.targetKnown&&(next.to!=std::array<uint8_t,3>{}||next.toMode!=255))||
           (!next.sampled&&next.rgb!=std::array<uint8_t,3>{}))throw std::runtime_error("Noncanonical source fade snapshot");
        *this=next;
    }
};
} // namespace edeffects
