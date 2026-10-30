#include "source_screen_effects.h"
#include "native_frame_clock.h"
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <iostream>

namespace {
template<class F>void rejects(F run){bool failed=false;try{run();}catch(const std::runtime_error&){failed=true;}assert(failed);}
void near(float a,float b){assert(std::abs(a-b)<0.00001f);}
}
int main() {
    edscreen::State fx;assert(!fx.busy()&&!fx.sampleOverlay().fade);
    fx.fadeOut();assert(fx.busy());
    for(unsigned step=1;step<=32;++step) {
        const auto overlay=fx.sampleOverlay();assert(overlay.fade);
        assert(overlay.blackAlpha==std::min(255u,step*8));assert(fx.busy()==(step<32));
    }
    assert(fx.fadeOutDone()&&!fx.sampleOverlay().fade); // No persistent black hold.
    fx.fadeIn();assert(fx.busy()&&!fx.fadeOutDone());
    for(unsigned step=1;step<=16;++step) {
        const auto overlay=fx.sampleOverlay();assert(overlay.fade);
        assert(overlay.blackAlpha==255-std::min(255u,step*16));assert(fx.busy()==(step<16));
    }
    assert(fx.fadeInDone()&&!fx.sampleOverlay().fade);
    fx.fadeOut();fx.sampleOverlay();fx.fadeIn();assert(fx.sampleOverlay().blackAlpha==239);
    assert(fx.complete()==15&&fx.complete()==0);
    fx.fadeOut();fx.letterbox(1);assert(fx.complete()==32);near(fx.barHeight(),32);
    fx.letterbox(0);for(unsigned i=0;i<8;++i)fx.sampleOverlay();near(fx.barHeight(),0);
    assert(!fx.sampleOverlay().bars);fx.letterbox(65536);assert(!fx.sampleOverlay().bars);
    fx.shake(100);const int expected[]={-87,77,-67,59,-51,45,-39,35,-30,27};
    for(int value:expected){near(fx.sampleProjection(),float(value)*0.025f);assert(fx.shakeAmplitude()==value);}
    while(fx.shakeAmplitude())fx.sampleProjection();near(fx.lastShakeOffset(),0);
    fx.shake(-2);near(fx.sampleProjection(),0.05f);assert(fx.shakeAmplitude()==2);
    near(fx.sampleProjection(),0);assert(fx.shakeAmplitude()==0);
    fx.shake(100);std::array<float,16> projection{};fx.applyProjection(projection);
    near(projection[12],-2.175f);near(projection[13],-2.175f);
    near(fx.backgroundTileSize()[0],106.25f);near(fx.backgroundTileSize()[1],138.25f);
    const auto bg=edscreen::backgroundTransform(-2.175f);
    near(-bg[0]+bg[12],-1);near(bg[5]+bg[13],1);
    near(bg[0]+bg[12],2*(106.25f/128.f)-1);near(-bg[5]+bg[13],1-2*(138.25f/160.f));
    const std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    assert(edscreen::backgroundTransform(0)==identity&&edscreen::foregroundTransform(identity,0)==identity);
    const auto shaken=edscreen::foregroundTransform(identity,-2.175f);near(shaken[12],-2.175f);near(shaken[13],-2.175f);
    const auto stale=fx.lastShakeOffset();fx.shake(65536);near(fx.sampleProjection(),0);near(fx.lastShakeOffset(),stale);
    fx.shake(0);near(fx.lastShakeOffset(),0);
    fx.fadeOut();for(unsigned i=0;i<7;++i)fx.sampleOverlay();fx.shake(-32768);fx.sampleProjection();
    const auto saved=fx.snapshot();edscreen::State resumed;resumed.restore(saved);assert(resumed.snapshot()==saved);
    for(unsigned i=0;i<25;++i){assert(fx.sampleOverlay().blackAlpha==resumed.sampleOverlay().blackAlpha);near(fx.sampleProjection(),resumed.sampleProjection());}
    for(size_t length=0;length<saved.size();++length)rejects([&]{resumed.restore({saved.begin(),saved.begin()+length});});
    auto corrupt=saved;corrupt.push_back(0);const auto before=resumed.snapshot();rejects([&]{resumed.restore(corrupt);});assert(resumed.snapshot()==before);
    corrupt=saved;corrupt[13]=2;rejects([&]{resumed.restore(corrupt);});
    edclock::FrameGate gate(0);assert(!gate.offer(32)&&gate.offer(33)&&!gate.offer(65));
    assert(gate.offer(200)&&gate.last()==200&&!gate.offer(200)&&!gate.offer(232)&&gate.offer(233));
    edclock::FrameGate wrap(0xfffffff0u);assert(!wrap.offer(0x10)&&wrap.offer(0x11));
    edclock::FrameGate second(0);unsigned accepted=0;for(uint32_t now=1;now<=1000;++now)accepted+=second.offer(now);assert(accepted==30);
    std::cout<<"Native screen fade/shake/snapshot and33ms frame gate passed\n";
}
