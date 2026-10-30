#include "stream_audio.h"
#undef NDEBUG
#include <cassert>
#include <iostream>
#include <limits>
template<class F>bool rejects(F fn){try{fn();return false;}catch(const std::exception&){return true;}}
int main(int argc,char**argv){try{
    if(argc!=2)throw std::runtime_error("Expected generated source assets");StreamAudio audio(argv[1]);assert(audio.size()==52&&!audio.status());
    assert(audio.play(0x660200ff,0));const auto first=audio.snapshot();assert(first.id==0x66020000&&!first.loop&&first.elapsed==0&&first.playing);
    assert(!audio.play(0xffffffff,2)&&audio.snapshot().id==first.id);audio.advance(.1);const auto advanced=audio.snapshot();
    assert(audio.play(0x66030000,1)&&audio.snapshot().loop);audio.advance(100000);assert(audio.status());
    audio.restore(advanced);assert(audio.snapshot().elapsed==advanced.elapsed&&audio.status());
    auto invalid=advanced;invalid.elapsed=std::numeric_limits<double>::quiet_NaN();assert(rejects([&]{audio.restore(invalid);}));assert(audio.snapshot().id==advanced.id);
    invalid=advanced;invalid.id=0x66020001;assert(rejects([&]{audio.restore(invalid);}));
    assert(rejects([&]{audio.play(0x66020000,3);}));audio.advance(1000);assert(audio.status()&&!audio.pcmPlaying());
    assert(!audio.play(0x66030000,2)); // PCstatus byte remains1 after EOF.
    audio.contextReset();assert(!audio.status());assert(audio.play(0x66030000,2)&&!audio.snapshot().loop);audio.commit();audio.advance(1000);audio.commit();assert(audio.status()&&!audio.pcmPlaying());
    {StreamAudio dc(argv[1],true);assert(dc.play(0x66020000,0));dc.advance(1000);
        assert(!dc.status()&&!dc.pcmPlaying()&&dc.snapshot().id==0);
        const auto stopped=dc.snapshot();dc.restore(stopped);assert(!dc.status());
        assert(dc.play(0x66030000,2)&&!dc.snapshot().loop);dc.advance(1000);assert(!dc.status());
        assert(dc.play(0x66020000,1));dc.advance(1000);assert(dc.status()&&dc.pcmPlaying());
        const auto looping=dc.snapshot();dc.contextReset();assert(!dc.status());dc.restore(looping);assert(dc.status()&&dc.snapshot().loop);}
    {StreamAudio dc(argv[1],true);assert(dc.play(0x66020000,1));dc.advance(.25);
        const auto title=dc.snapshot();assert(!dc.play(0x660200ff,1));
        assert(dc.snapshot().id==title.id&&dc.snapshot().elapsed==title.elapsed&&dc.snapshot().loop&&dc.status());
        assert(dc.play(0x66030000,1)&&dc.snapshot().elapsed==0);
        dc.advance(.25);assert(dc.play(0x66030000,0)&&dc.snapshot().elapsed==0&&!dc.snapshot().loop);
        assert(dc.play(0x66030000,1));dc.advance(.25);dc.contextReset();
        assert(dc.play(0x66030000,1)&&dc.snapshot().elapsed==0);}
    assert(StreamAudio::sourceGain(100)==1&&StreamAudio::sourceGain(0)==0);
    assert(StreamAudio::sourceGain(50)==.5f&&StreamAudio::sourceGain(35)==11585.f/32768);
    assert(StreamAudio::sourceGain(306)==StreamAudio::sourceGain(50));
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER,"dummy",SDL_HINT_OVERRIDE);
    {StreamAudio hardware(argv[1]);assert(hardware.openDevice());const auto empty=hardware.snapshot();hardware.play(0x66020000,0);
        assert(hardware.status()&&!hardware.hardwarePlaying());hardware.restore(empty);hardware.commit();assert(!hardware.hardwarePlaying());
        hardware.play(0x66020000,0);hardware.commit();assert(hardware.hardwarePlaying());const auto old=hardware.snapshot();
        hardware.play(0x66030000,1);hardware.restore(old);hardware.commit();assert(hardware.snapshot().id==old.id&&hardware.hardwarePlaying());
        hardware.mute(true);hardware.commit();assert(hardware.muted()&&hardware.status());hardware.setMasterVolume(50);hardware.mute(false);hardware.commit();
        hardware.advance(1000);hardware.commit();assert(hardware.status()&&!hardware.pcmPlaying()&&!hardware.hardwarePlaying());}
    {StreamAudio dc(argv[1],true);assert(dc.openDevice());assert(dc.play(0x66020000,1));dc.commit();assert(dc.hardwarePlaying());
        dc.contextReset();assert(!dc.status()&&dc.hardwarePlaying());dc.commit();assert(!dc.hardwarePlaying());}
    SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);std::cout<<"Original standalone streams: flags0/1/2, canonical IDs, logical timing, rollback and deferred SDL commit passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
