#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>

namespace edrumble {
struct Command {int port=0,a=0,b=0;};
struct Chainsaw {
    bool enabled=true,gameplay=true,running=false,revving=false;
    uint32_t weapon=0;
    int phase=0,revTimer=0,attackPitch=0;
};
struct Motor {uint16_t amplitude=0;int power=0,frequency=0;};
// Modern motor strength only: Puru Puru waveform frequency/sign have no exact two-motor equivalent.
inline Motor modernMotor(Command command) {
    if(command.a==0&&command.b==0)return {}; // Original RumbleCommander.stopRumble intent.
    const auto raw=uint32_t(command.a);
    const int bits=int((raw>>5)&255);
    int power=command.a?std::clamp(bits<128?bits:bits-256,-7,7):7;
    const int frequency=command.a?std::clamp(int((raw>>2)&255),7,59):59;
    if(power==1||power==-1)power*=2; // Native SDK8c043efe promotes +/-1 before encoding.
    const int strength=power<0?-power:power;
    return {uint16_t(strength*65535/7),power,frequency};
}
struct Host {
    std::function<bool(int)> available;
    std::function<void(Command)> rumble;
    bool supports(int port)const noexcept {try{return available&&available(port);}catch(...){return false;}}
    void send(Command command)const noexcept {
        // Physical output failure cannot roll back an already committed source frame.
        try{if(rumble)rumble(command);}catch(...){}
    }
};
}
