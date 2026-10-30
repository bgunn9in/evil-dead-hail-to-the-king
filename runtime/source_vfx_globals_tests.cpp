#include "source_vfx.h"
#undef NDEBUG
#include <cassert>
#include <iostream>

template<class F>static bool rejects(F f){try{f();return false;}catch(const std::exception&){return true;}}
int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("Expected original A and B VFX asset roots");unsigned rng=0;
    edvfx::Host host;host.randomBound=host.cachedBound=[&](int){++rng;return 0;};host.visible=[](auto){return true;};
    edvfx::SourceVfx a(argv[1],host),b(argv[2],host);assert(a.line(25,{}, {},1));a.update(1);a.clearAndSuppress(60);
    assert(!a.line(24,{}, {},4));const auto source=a.globals();assert(source.suppressedEmissions==59&&source.emitterModes.at(25)==1&&source.emitterModes.at(24)==4);
    assert(a.snapshot()!=b.snapshot());b.restoreGlobals(source);const auto imported=b.globals();
    assert(imported.frameTimers==source.frameTimers&&imported.childTimers==source.childTimers&&imported.emitterModes==source.emitterModes&&
        imported.suppressedEmissions==59&&imported.nextSerial==source.nextSerial);
    const auto saved=b.snapshot();for(unsigned which=0;which<6;++which){auto bad=source;
        if(which==0)bad.nextSerial=0;if(which==1)bad.suppressedEmissions=61;
        if(which==2)bad.frameTimers[999]=0;if(which==3)bad.childTimers.begin()->second=99;
        if(which==4)bad.emitterModes[24]=99;if(which==5)bad.frameTimers.begin()->second=0xffffffff;
        assert(rejects([&]{b.restoreGlobals(bad);})&&b.snapshot()==saved);}
    const auto before=rng;for(unsigned n=0;n<59;++n)assert(!b.line(99,{},{}));assert(rng==before);
    assert(b.line(25,{}, {},-1)&&b.particles().at(25)[0].renderMode==1&&b.particles().at(25)[0].serial==source.nextSerial);
    const auto busy=b.snapshot();assert(rejects([&]{b.restoreGlobals(source);})&&b.snapshot()==busy);
    b.clear();b.restoreGlobals(source);const auto global=b.globals();
    // A fresh destination clock starts at1; it must not inherit source update1.
    b.update(1);assert(b.globals().suppressedEmissions==global.suppressedEmissions);
    edvfx::SourceVfx returning(argv[1],host);returning.restoreGlobals(b.globals());assert(returning.globals().suppressedEmissions==59);
    assert(a.globals().suppressedEmissions==59); // Transfer is read-only for the old World.
    std::cout<<"SourceVFX actual A->B->A tables/globals, inherited59attempts, freshclock, strict transactional rejection and sourceWorld immutability: passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
