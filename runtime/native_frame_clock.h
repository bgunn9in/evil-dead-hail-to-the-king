#pragma once
#include <cstdint>

namespace edclock {
// Outer420DF1..420E38: unsigned elapsed>=33ms, last=now before frame work.
// A slow frame yields one accepted iteration, with no accumulated catch-up.
class FrameGate {
    uint32_t last_;
public:
    static constexpr uint32_t PeriodMilliseconds=33;
    explicit FrameGate(uint32_t initial):last_(initial){}
    bool offer(uint32_t now) {
        if(uint32_t(now-last_)<PeriodMilliseconds)return false;
        last_=now;return true;
    }
    uint32_t last()const{return last_;}
};
}
