#include "source_effects.h"
#undef NDEBUG
#include <cassert>
#include <iostream>
#include <limits>
template<class F>bool rejects(F fn){try{fn();return false;}catch(const std::exception&){return true;}}
int main(){using edeffects::State;
    State fade;assert(fade.done()&&!fade.sampled&&fade.sample()==0xff808080u);
    fade.info(0,128,0,4,0);fade.info(128,128,128,4,1);fade.start(1,30);
    // Original SpawnCommander970 PC172..205: green -> white, whole30 visible samples.
    const std::array<uint8_t,30> red{{0,4,8,12,17,21,25,29,34,38,42,46,51,55,59,63,68,72,76,80,85,89,93,97,102,106,110,114,119,123}};
    for(unsigned tick=0;tick<30;++tick){const auto argb=fade.sample();assert(argb==(0xff008000u|(uint32_t(red[tick])<<16)|red[tick]));
        assert(fade.done()==(tick==29));}
    assert(fade.flags==0&&fade.duration==0&&fade.elapsed==0&&fade.fromMode==4&&fade.from==fade.to);
    assert(fade.rgb[0]==123&&fade.sample()==0xff808080u); // Full endpoint is next visible sample.
    State death;death.info(200,57,57,1,0);death.info(0,0,0,1,1);death.start(1,20);
    // GenericNpc.doDeathEffectNoPoop1554 with Deadite.initialize1706 duration20.
    const std::array<uint8_t,20> deathRed{{128,121,114,108,102,95,89,82,76,70,63,57,51,44,38,31,25,19,12,6}};
    const std::array<uint8_t,20> deathGreen{{57,54,51,48,45,42,39,37,34,31,28,25,22,19,17,14,11,8,5,2}};
    for(unsigned tick=0;tick<20;++tick)assert(death.sample()==(0xff000000u|(uint32_t(deathRed[tick])<<16)|(uint32_t(deathGreen[tick])<<8)|deathGreen[tick]));
    assert(death.done()&&death.rgb[0]==6&&death.sample()==0xff000000u);
    fade.info(0,0,0,3,0);fade.info(128,128,128,7,1);fade.start(2,2);
    assert(fade.sample({{1,1,1}})==0xff000000u&&fade.elapsed==1);
    assert(fade.sample({{1,1,1}})==0xff7f7f7fu&&fade.elapsed==0&&fade.flags==2&&!fade.done());
    assert(fade.fromMode==3&&fade.toMode==7&&fade.from[0]==255&&fade.to[0]==0); // Ping-pong swaps RGB only.
    assert(fade.sample({{1,1,1}})==0xffffffffu&&fade.sample({{1,1,1}})==0xff7f7f7fu);
    const auto saved=fade.snapshot();State restored;restored.restore(saved);assert(restored.snapshot()==saved);
    auto bad=saved;bad.back()=2;assert(rejects([&]{restored.restore(bad);}));assert(restored.snapshot()==saved);
    bad=saved;bad.pop_back();assert(rejects([&]{restored.restore(bad);}));assert(restored.snapshot()==saved);
    State unknown;unknown.start(1,30);assert(rejects([&]{unknown.sample();})); // Initial native target bytes are not initialized.
    State wrapped;wrapped.info(-1,126,127,260,0);assert((wrapped.from==std::array<uint8_t,3>{{254,252,255}})&&wrapped.fromMode==4);
    wrapped.info(0,0,0,3,1);wrapped.start(1,256);assert(wrapped.done()&&wrapped.sample()==0xff000000u&&wrapped.flags==1);
    wrapped.info(128,128,128,2,0);assert(wrapped.flags==0&&wrapped.done()&&wrapped.targetKnown);
    wrapped.start(3,2);wrapped.sample();wrapped.sample();assert(wrapped.flags==2&&wrapped.done()&&wrapped.sample()==0xff000000u);
    State unrelated;unrelated.start(4,2);unrelated.sample();assert(!unrelated.done());unrelated.sample();assert(unrelated.done()&&unrelated.flags==4);
    unrelated.setFlag(-1,false);assert(unrelated.flags==0);const auto before=unrelated.snapshot();
    assert(rejects([&]{unrelated.sample({{1,std::numeric_limits<float>::quiet_NaN(),1}});}));assert(unrelated.snapshot()==before);
    assert(State::truncateByte(-1)==255&&State::truncateByte(256)==0&&State::truncateByte(std::numeric_limits<double>::infinity())==0);
    std::cout<<"Source fade: Spawn30-sample RGB goldens, endpoint order, ping-pong, byte wrapping and snapshot rollback passed\n";
}
