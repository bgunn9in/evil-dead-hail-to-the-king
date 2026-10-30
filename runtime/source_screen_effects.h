#pragma once
#include "state_bytes.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace edscreen {
struct Overlay {
    bool fade=false;
    uint8_t blackAlpha=0;
    bool bars=false;
    float barHeight=0; // Original 640x480 screen coordinates, maximum32.
};
inline std::array<float,16> backgroundTransform(float shakeOffset) {
    if(!std::isfinite(shakeOffset))throw std::runtime_error("Non-finite source screen shake");
    const double grow=double(shakeOffset)*10.;
    const float sx=float(128.+grow)/128.f,sy=float(160.+grow)/160.f;
    // Quad[-1,1], retain its top-left(-1,+1) while scaling both source textures.
    return {sx,0,0,0,0,sy,0,0,0,0,1,0,sx-1.f,1.f-sy,0,1};
}
inline std::array<float,16> foregroundTransform(std::array<float,16> vp,float shakeOffset) {
    if(!std::isfinite(shakeOffset))throw std::runtime_error("Non-finite source screen shake");
    if(!shakeOffset)return vp;
    vp[12]+=shakeOffset;vp[13]+=shakeOffset;return vp;
}

// Native417A60..417B6A /42F7B0..42FCF3. These are render-call counters,
// independent of milliseconds and of the VM/animation frame counter.
class State {
    bool outRequested_=false,inRequested_=false,outDone_=false,inDone_=false;
    uint16_t outProgress_=0,inProgress_=0;
    int16_t letterbox_=0,shake_=0;
    float barHeight_=0,shakeOffset_=0;
    static int16_t lowSigned16(int32_t value) {
        const auto word=uint32_t(value)&0xffffu;
        return int16_t(word<0x8000u?int32_t(word):int32_t(word)-0x10000);
    }
public:
    bool busy()const{return outRequested_?!outDone_:inRequested_?!inDone_:false;}
    bool fadeOutDone()const{return outDone_;}
    bool fadeOutRequested()const{return outRequested_;}
    bool fadeInDone()const{return inDone_;}
    unsigned fadeOutProgress()const{return outProgress_;}
    unsigned fadeInProgress()const{return inProgress_;}
    int shakeAmplitude()const{return shake_;}
    float lastShakeOffset()const{return shakeOffset_;}
    float barHeight()const{return barHeight_;}
    void fadeOut() {outRequested_=true;inRequested_=inDone_=outDone_=false;outProgress_=0;}
    void fadeIn() {inRequested_=true;outRequested_=inDone_=outDone_=false;inProgress_=0;}
    void letterbox(int32_t value) {letterbox_=lowSigned16(value);if(value)barHeight_=0;}
    void shake(int32_t value) {shake_=lowSigned16(value);if(!value)shakeOffset_=0;}
    Overlay sampleOverlay() {
        Overlay result;
        if(outRequested_&&!outDone_) {
            outProgress_=uint16_t(outProgress_+8);
            if(outProgress_>255){outProgress_=255;outDone_=true;}
            result.fade=true;result.blackAlpha=uint8_t(outProgress_);
        }
        if(inRequested_&&!inDone_) {
            inProgress_=uint16_t(inProgress_+16);
            if(inProgress_>255){inProgress_=255;inDone_=true;}
            result.fade=true;result.blackAlpha=uint8_t(255-inProgress_);
        }
        if(letterbox_)barHeight_=std::min(32.f,barHeight_+2.f);
        else if(barHeight_<=0)return result;
        else barHeight_=std::max(0.f,barHeight_-4.f);
        result.bars=true;result.barHeight=barHeight_;return result;
    }
    // scrFxComplete calls only the overlay worker: no camera, full redraw,
    // Present, event pump, or ordinary game tick occurs in this tight loop.
    unsigned complete() {
        unsigned calls=0;
        while(busy()) {
            if(calls==32)throw std::runtime_error("Invalid source screen fade counter");
            sampleOverlay();++calls;
        }
        return calls;
    }
    // Invoked once by native perspective construction423180→42FC40.
    // Return value adds to raw projection[12] and[13], not to the view matrix.
    float sampleProjection() {
        if(!shake_)return 0;
        const int product=int(shake_)*7;
        const int quotient=product>=0?product/8:-((-product+7)/8); // x86 SAR.
        shake_=int16_t(-quotient);
        if(shake_>=-1&&shake_<=1){shake_=0;shakeOffset_=0;return 0;}
        shakeOffset_=float(shake_)*0.025f;return shakeOffset_;
    }
    void applyProjection(std::array<float,16>& projection) {
        const auto offset=sampleProjection();projection[12]+=offset;projection[13]+=offset;
    }
    std::array<float,2> backgroundTileSize()const {
        const double grow=double(shakeOffset_)*10.;return {float(128.+grow),float(160.+grow)};
    }
    std::vector<uint8_t> snapshot()const {
        statebytes::Writer out;out.text("EDSCREEN1");out.flag(outRequested_);out.flag(inRequested_);
        out.flag(outDone_);out.flag(inDone_);out.put(outProgress_);out.put(inProgress_);
        out.put(letterbox_);out.put(shake_);out.put(barHeight_);out.put(shakeOffset_);return std::move(out.bytes);
    }
    void restore(const std::vector<uint8_t>& bytes) {
        statebytes::Reader in{bytes};if(in.text()!="EDSCREEN1")throw std::runtime_error("Unsupported source screen state");
        State candidate;candidate.outRequested_=in.flag();candidate.inRequested_=in.flag();
        candidate.outDone_=in.flag();candidate.inDone_=in.flag();candidate.outProgress_=in.get<uint16_t>();
        candidate.inProgress_=in.get<uint16_t>();candidate.letterbox_=in.get<int16_t>();candidate.shake_=in.get<int16_t>();
        candidate.barHeight_=in.get<float>();candidate.shakeOffset_=in.get<float>();in.end();
        if((candidate.outRequested_&&candidate.inRequested_)||candidate.outProgress_>255||candidate.inProgress_>255||
           (candidate.outDone_&&(!candidate.outRequested_||candidate.outProgress_!=255))||
           (candidate.inDone_&&(!candidate.inRequested_||candidate.inProgress_!=255))||
           (candidate.outRequested_&&!candidate.outDone_&&candidate.outProgress_%8)||
           (candidate.inRequested_&&!candidate.inDone_&&candidate.inProgress_%16)||
           candidate.barHeight_<0||candidate.barHeight_>32||std::abs(candidate.shakeOffset_)>819.2f)
            throw std::runtime_error("Invalid source screen state");
        *this=candidate;
    }
};
}
