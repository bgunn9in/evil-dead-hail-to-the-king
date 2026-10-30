#include "source_vfx.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

template<class F>static bool rejects(F f){try{f();return false;}catch(const std::exception&){return true;}}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Expected generated source VFX assets");unsigned randomCalls=0;
    edvfx::Host host;host.randomBound=host.cachedBound=[&](int){++randomCalls;return 0;};host.visible=[](auto){return true;};
    edvfx::SourceVfx fx(argv[1],host);assert(fx.spawn(9,{}));const auto serial=fx.particles().at(9)[0].serial;
    const auto old=fx.snapshot();fx.clearAndSuppress(60);const auto saved=fx.snapshot();const auto rng=randomCalls;
    statebytes::Reader reader{saved};assert(reader.text()=="EDVFXS6");
    // Unknown/invalid emissions are suppressed before native type dispatch;
    // simulation ticks and pure draws do not consume the60 attempts.
    for(unsigned n=0;n<10;++n)assert(!fx.line(99,{},{}));fx.update(1);assert(fx.draw({{1,0,0}},{{0,1,0}},{}).empty());
    const auto partial=fx.snapshot();uint32_t remaining;std::memcpy(&remaining,partial.data()+partial.size()-4,4);assert(remaining==50&&randomCalls==rng);
    fx.restore(partial);assert(fx.snapshot()==partial);
    for(unsigned n=0;n<50;++n)assert(!fx.spawn(9,{}));assert(fx.spawn(9,{})&&fx.particles().at(9)[0].serial==serial+1);
    assert(rejects([&]{fx.line(99,{},{});}));fx.restore(saved);assert(fx.snapshot()==saved);
    for(uint32_t badCount:{0u,61u}){auto bad=saved;std::memcpy(bad.data()+bad.size()-4,&badCount,4);assert(rejects([&]{fx.restore(bad);})&&fx.snapshot()==saved);}
    auto truncated=saved;truncated.pop_back();assert(rejects([&]{fx.restore(truncated);})&&fx.snapshot()==saved);
    assert(rejects([&]{fx.clearAndSuppress(61);})&&fx.snapshot()==saved);fx.restore(old);assert(fx.snapshot()==old);
    // Native ordinary clear preserves an already installed suppression count.
    fx.clearAndSuppress();fx.clear();const auto cleared=fx.snapshot();std::memcpy(&remaining,cleared.data()+cleared.size()-4,4);assert(remaining==60);
    if(fx.supports(24)){
        assert(!fx.line(24,{}, {},4)&&!fx.line(25,{}, {},2)&&!fx.line(15,{}, {},1));
        for(unsigned n=0;n<57;++n)assert(!fx.line(99,{},{}));
        assert(fx.line(24,{}, {},-1)&&fx.particles().at(24)[0].renderMode==4);
        assert(fx.line(25,{}, {},-1)&&fx.particles().at(25)[0].renderMode==2);
        //411D3B restores temporary15 color/dispersion even if suppressed.
        assert(fx.line(15,{}, {},-1)&&fx.particles().at(15)[0].renderMode==1);
        assert(fx.particles().at(15)[0].color[0]==128);
    }
    fx.clearAndSuppress();assert(!fx.line(25,{}, {},99)&&fx.globals().emitterModes.at(25)==13);
    const auto clamped=fx.snapshot();fx.restore(clamped);assert(fx.snapshot()==clamped); // Valid cached native mode; no unknown geometry allocated.
    std::cout<<"source411BB0/411BA0: clear retains source serial/timers;60 EMISSION attempts, no frame countdown/RNG, S6 opaque replay/rollback and legacy restore passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
