#include <SDL3/SDL.h>
#undef NDEBUG
#include <cassert>
#include <vector>
static std::vector<float> appliedGains;
static std::vector<std::pair<SDL_AudioStream*,float>> appliedRatios;
static bool recordAudioGain(SDL_AudioStream* stream,float gain){
    const bool result=SDL_SetAudioStreamGain(stream,gain);
    if(result){assert(SDL_GetAudioStreamGain(stream)==gain);appliedGains.push_back(gain);}return result;
}
static bool recordAudioRatio(SDL_AudioStream* stream,float ratio){
    const bool result=SDL_SetAudioStreamFrequencyRatio(stream,ratio);
    if(result){assert(SDL_GetAudioStreamFrequencyRatio(stream)==ratio);appliedRatios.emplace_back(stream,ratio);}return result;
}
#define SDL_SetAudioStreamGain recordAudioGain
#define SDL_SetAudioStreamFrequencyRatio recordAudioRatio
#include "audio_bank.h"
#undef SDL_SetAudioStreamGain
#undef SDL_SetAudioStreamFrequencyRatio
#include <iterator>
#include <random>

namespace fs=std::filesystem;
template<class F> bool rejects(F action) { try { action(); return false; } catch(const std::exception&) { return true; } }

int main(int argc,char** argv) {
    if(argc!=2) throw std::runtime_error("Expected exported asset root");
    const fs::path root=fs::u8path(argv[1]);
    AudioBank bank(root);
    assert(bank.size()==695); // Sealed native A scene buckets and both original DC global banks.
    const int door=bank.resolve("ak01",0x0e8a4aca),ah=bank.resolve("ah01",0x2e8720cd);
    assert(door>=0&&ah>=0&&bank.resolve("ak09",0x0e8a4aca)>=0);
    {AudioBank otherPackage(root/"levelB");assert(otherPackage.resolve("bm01",0x0e8a4aca,true)==-1);}
    const int global=bank.resolve("ak01",0xee9221ed),globalB=bank.resolve("ak01",0xee9221ed,true);
    // AZ01.DC entry14/slot2 and BZ01.DC entry15/slot3 retain separate handles for identical PCM.
    assert(global>=0&&globalB>=0&&global!=globalB);
    assert(bank.sampleRate(global)==22050&&bank.sampleRate(globalB)==22050);
    assert(bank.duration(global)==7216.0/22050&&bank.duration(globalB)==bank.duration(global));
    auto waveBytes=[](const fs::path& file){std::ifstream input(file,std::ios::binary);return std::vector<char>(std::istreambuf_iterator<char>(input),{});};
    const auto globalWave=waveBytes(root/"audio/global/ee9221ed.wav");
    assert(globalWave.size()==14476&&globalWave==waveBytes(root/"audio/global_b/ee9221ed.wav"));
    assert(bank.resolve("ak01",0x0e9201ed,true)>=0&&bank.resolve("ak01",0x0e9201ed)==-1);
    assert(bank.resolve("ak01",0xffffffff)==-1);
    assert(bank.done(-1)&&bank.play(-1)==-1&&bank.play(-1,100,0)==-1);
    // Original8c099220 quantizes percentage to7bits; amSoundSetVolume then sends level>>3 to AICA.
    struct NativeGain {int percent;unsigned level,q15;};
    constexpr NativeGain nativeGains[]={{0,0,0},{1,21,362},{2,37,724},{5,58,2048},{10,74,4096},
        {20,90,8192},{25,95,8192},{35,103,11585},{37,105,16384},{50,111,16384},{75,121,32768},{100,127,32768}};
    for(const auto& row:nativeGains){assert(ednativeaudio::volumeLevel(row.percent)==row.level);
        assert(AudioBank::sourceGain(row.percent)==float(row.q15)/32768);}
    assert(AudioBank::sourceGain(-10)==0&&AudioBank::sourceGain(50,50)==.25f);
    assert(AudioBank::sourceGain(100,50)==.5f&&AudioBank::sourceGain(100,0)==0);
    assert(ednativeaudio::masterPercent(21,37)==7&&AudioBank::sourceGain(21,37)==2896.f/32768);
    assert(AudioBank::sourceGain(21,293)==AudioBank::sourceGain(21,37));
    assert(AudioBank::sourceGain(100,37)==.5f&&AudioBank::sourceGain(50,101)==.5f);
    assert(AudioBank::sourceGain(21,256)==0&&AudioBank::sourceGain(21,-1)==AudioBank::sourceGain(21,255));
    for(int master=0;master<256;++master)for(int volume=0;volume<=100;++volume){
        const float actual=AudioBank::sourceGain(volume,master);
        assert(actual>=0&&actual<=1&&AudioBank::sourceGain(volume,master+256)==actual);
        if(volume)assert(actual>=AudioBank::sourceGain(volume-1,master));
    }
    // The actual SDL sink applies this gain once, with PCM samples preserved until mixing.
    {const SDL_AudioSpec spec{SDL_AUDIO_S16LE,1,22050};SDL_AudioStream* sink=SDL_CreateAudioStream(&spec,&spec);assert(sink);
      const std::array<int16_t,4> samples{{12000,-12000,30000,-30000}};std::array<int16_t,4> mixed{};
      assert(SDL_SetAudioStreamGain(sink,AudioBank::sourceGain(100,50))&&SDL_PutAudioStreamData(sink,samples.data(),sizeof(samples))&&SDL_FlushAudioStream(sink));
      assert(SDL_GetAudioStreamData(sink,mixed.data(),sizeof(mixed))==sizeof(mixed));
      for(unsigned i=0;i<samples.size();++i)assert(std::abs(int(mixed[i])-int(samples[i])/2)<=1);
      SDL_DestroyAudioStream(sink);}
    int voice=bank.play(door); assert(voice>=0x10000&&!bank.done(voice));
    bank.advance(bank.duration(door)-0.001); assert(!bank.done(voice));
    bank.advance(0.002); assert(bank.done(voice)&&bank.activeVoices()==0);
    const int sourceHz=bank.sampleRate(door);assert(sourceHz>0);
    voice=bank.playAtFrequency(door,100,sourceHz*2);bank.advance(.1);
    assert(bank.snapshot().voices.at(0).frequencyHz==sourceHz*2&&std::abs(bank.snapshot().voices.at(0).elapsed-.2)<1e-12);
    bank.advance(bank.duration(door)/2);assert(bank.done(voice));
    voice=bank.playAtFrequency(door,100,sourceHz/2);bank.advance(bank.duration(door));assert(!bank.done(voice));
    bank.advance(bank.duration(door)+.01);assert(bank.done(voice));
    const auto noVoices=bank.snapshot();assert(rejects([&]{bank.playAtFrequency(door,100,999);}));
    assert(bank.snapshot().nextVoice==noVoices.nextVoice&&bank.activeVoices()==0);
    voice=bank.play(ah,50,0); bank.advance(1000000); assert(!bank.done(voice));
    assert(rejects([&]{bank.play(ah,50,1);}));
    assert(rejects([&]{bank.advance(-1);}));
    bank.volume(voice,0); bank.stop(voice); assert(bank.done(voice));
    std::array<int,AudioBank::VoiceLimit> tokens{};
    for(auto& token:tokens) { token=bank.play(door,10,0); assert(token>=0x10000); }
    assert(bank.activeVoices()==AudioBank::VoiceLimit&&bank.play(door)==-1);
    bank.stop(tokens[0]); const int replacement=bank.play(door,10,0);
    assert(replacement>=0x10000&&replacement!=tokens[0]&&bank.done(tokens[0]));
    bank.stop(tokens[0]); bank.volume(tokens[0],0); assert(!bank.done(replacement));
    bank.stopAll(); assert(bank.activeVoices()==0);
    const int savedOne=bank.playAtFrequency(door,42,sourceHz*2),savedLoop=bank.play(ah,50,0);
    bank.advance(0.1); const auto saved=bank.snapshot();
    bank.advance(0.2); bank.stopAll(); bank.restore(saved);
    assert(!bank.done(savedOne)&&!bank.done(savedLoop));
    auto unchanged=[&] {
        const auto now=bank.snapshot(); assert(now.nextVoice==saved.nextVoice&&now.voices.size()==saved.voices.size());
        for(std::size_t i=0;i<saved.voices.size();++i) {
            const auto& a=now.voices[i]; const auto& b=saved.voices[i];
            assert(a.token==b.token&&a.clip==b.clip&&a.id==b.id&&a.bucket==b.bucket&&a.volume==b.volume&&a.loop==b.loop&&a.elapsed==b.elapsed&&a.frequencyHz==b.frequencyHz&&a.pending==b.pending);
        }
    };
    unchanged();
    auto invalid=saved; invalid.voices[0].id^=1; assert(rejects([&]{bank.restore(invalid);})); unchanged();
    invalid=saved; invalid.voices[0].elapsed=std::numeric_limits<double>::quiet_NaN();
    assert(rejects([&]{bank.restore(invalid);})); unchanged();
    invalid=saved;invalid.voices[0].frequencyHz=255001;assert(rejects([&]{bank.restore(invalid);}));unchanged();
    bank.mute(true); assert(bank.muted()); unchanged();
    bank.restore(saved); assert(bank.muted()); unchanged();
    bank.mute(false); assert(!bank.muted()); unchanged();
    invalid=saved; invalid.voices[1].token=invalid.voices[0].token;
    assert(rejects([&]{bank.restore(invalid);})); unchanged();
    assert(bank.play(global)==saved.nextVoice); bank.stopAll();

    // Native430aa0 allocates immediately, but4300f0 starts queued sounds in its next source phase.
    { AudioBank deferred(root,true);const int queued=deferred.play(door);
      auto pending=deferred.snapshot();assert(pending.voices.size()==1&&pending.voices[0].pending&&pending.voices[0].elapsed==0);
      deferred.advance(100000);assert(!deferred.done(queued)&&deferred.snapshot().voices[0].elapsed==0);
      deferred.stopAll();deferred.restore(pending);assert(deferred.snapshot().voices[0].pending);
      auto malformed=pending;malformed.voices[0].elapsed=.01;
      assert(rejects([&]{deferred.restore(malformed);}));assert(deferred.snapshot().voices[0].pending&&deferred.snapshot().nextVoice==pending.nextVoice);
      deferred.sourceFrame();assert(!deferred.snapshot().voices[0].pending);deferred.advance(.01);
      const auto elapsed=deferred.snapshot().voices[0].elapsed;deferred.sourceFrame();assert(deferred.snapshot().voices[0].elapsed==elapsed);
      deferred.stop(queued);const int cancelled=deferred.play(door);deferred.stop(cancelled);deferred.sourceFrame();assert(deferred.done(cancelled)); }

    // The source's9000 busy flags are cleared only by the sound phase, not by Main's earlier queries.
    { AudioBank deferred(root,true);const int finished=deferred.play(door);deferred.sourceFrame(false);
      deferred.advance(deferred.duration(door)+1);const auto boundary=deferred.snapshot();
      assert(!deferred.done(finished)&&deferred.activeVoices()==1&&boundary.voices.size()==1&&!boundary.voices[0].pending&&boundary.voices[0].elapsed==deferred.duration(door));
      deferred.restore(boundary);assert(!deferred.done(finished)&&deferred.snapshot().voices[0].elapsed==boundary.voices[0].elapsed);
      for(unsigned i=1;i<AudioBank::VoiceLimit;++i)assert(deferred.play(door)>=0);
      assert(deferred.activeVoices()==AudioBank::VoiceLimit&&deferred.play(door)==-1); // No early allocator reuse.
      deferred.sourceFrame(false);assert(deferred.done(finished)&&deferred.activeVoices()==AudioBank::VoiceLimit-1);
      assert(deferred.play(door)>=0); }

    // A sparse native64 table must retain holes across rollback and its next sound phase.
    { AudioBank sparse(root,true);const int a=sparse.play(global,10,0),hole=sparse.play(global,20,0),c=sparse.play(global,30,0),d=sparse.play(global,40,0);
      sparse.stop(hole);const auto checkpoint=sparse.snapshot();
      assert(checkpoint.voices.size()==3&&checkpoint.voices[0].token==a&&checkpoint.voices[0].slot==0&&
          checkpoint.voices[1].token==c&&checkpoint.voices[1].slot==2&&checkpoint.voices[2].token==d&&checkpoint.voices[2].slot==3);
      auto same=[](const AudioBank::State& x,const AudioBank::State& y){if(x.nextVoice!=y.nextVoice||x.voices.size()!=y.voices.size())return false;
          for(size_t i=0;i<x.voices.size();++i){const auto&a=x.voices[i];const auto&b=y.voices[i];
              if(a.token!=b.token||a.slot!=b.slot||a.clip!=b.clip||a.id!=b.id||a.bucket!=b.bucket||a.volume!=b.volume||
                 a.loop!=b.loop||a.elapsed!=b.elapsed||a.frequencyHz!=b.frequencyHz||a.pending!=b.pending)return false;}return true;};
      auto future=[&](){const int allocated=sparse.play(global,50,0);assert(allocated==checkpoint.nextVoice);
          const auto queued=sparse.snapshot();assert(queued.voices[1].token==allocated&&queued.voices[1].slot==1&&queued.voices[1].pending);
          sparse.sourceFrame(false);const int late=sparse.play(global,60,0);const auto next=sparse.snapshot();
          assert(!next.voices[0].pending&&next.voices.back().token==late&&next.voices.back().pending);return next;};
      const auto expected=future();sparse.restore(checkpoint);assert(same(sparse.snapshot(),checkpoint));assert(same(future(),expected));
      const auto intact=sparse.snapshot();for(int badSlot:{-1,int(AudioBank::VoiceLimit),intact.voices[1].slot}){
          auto malformed=intact;malformed.voices[0].slot=badSlot;assert(rejects([&]{sparse.restore(malformed);}));assert(same(sparse.snapshot(),intact));} }

    // Device failure retains deterministic source-duration semantics.
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER,"ed-intentionally-missing-driver",SDL_HINT_OVERRIDE);
    { AudioBank fallback(root); assert(!fallback.openDevice()); voice=fallback.play(door);
      fallback.advance(fallback.duration(door)+0.01); assert(fallback.done(voice)); }
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER,"dummy",SDL_HINT_OVERRIDE);
    { AudioBank rpm(root,true);const int idle=rpm.play(rpm.resolve("ak01",0x0e920903),50,0);
      const int rev=rpm.play(rpm.resolve("ak01",0x0e920a35),58),other=rpm.play(global,50,0);
      assert(rejects([&]{rpm.presentChainsaw(idle,true,0,-.01);}));
      assert(rejects([&]{rpm.presentChainsaw(idle,true,0,std::numeric_limits<double>::quiet_NaN());}));
      rpm.presentChainsaw(idle,true,0,.033); // A pending/late-open loop retains its presentation ramp.
      appliedRatios.clear();assert(rpm.openDevice());rpm.sourceFrame(false);rpm.commitSourceFrame();
      assert(appliedRatios.size()==1&&appliedRatios.back().second>1&&appliedRatios.back().second<1.45f);
      auto* engineStream=appliedRatios.back().first;const auto before=rpm.snapshot();
      auto sameSource=[&]{const auto after=rpm.snapshot();assert(after.nextVoice==before.nextVoice&&after.voices.size()==before.voices.size());
          for(size_t i=0;i<before.voices.size();++i){const auto&a=before.voices[i];const auto&b=after.voices[i];
              assert(a.token==b.token&&a.clip==b.clip&&a.id==b.id&&a.bucket==b.bucket&&a.volume==b.volume&&a.frequencyHz==b.frequencyHz&&a.elapsed==b.elapsed&&a.loop==b.loop&&a.pending==b.pending&&a.slot==b.slot);}};
      sameSource();float previous=appliedRatios.back().second;
      for(unsigned n=0;n<40;++n){rpm.presentChainsaw(idle,true,0,.033);const float next=SDL_GetAudioStreamFrequencyRatio(engineStream);assert(next>=previous&&next<=1.45f);previous=next;}
      assert(previous>1.449f);sameSource();
      rpm.presentChainsaw(idle,false,0,.033);const float released=appliedRatios.back().second;
      assert(released>1&&released<previous); // Release cannot chop the loop or reset its PCM queue.
      const int queued=SDL_GetAudioStreamQueued(engineStream);
      assert(queued>0);for(unsigned n=0;n<80;++n)rpm.presentChainsaw(-1,false,0,.033);
      assert(SDL_GetAudioStreamFrequencyRatio(engineStream)==1);sameSource();
      rpm.presentChainsaw(idle,false,10000,.25);assert(appliedRatios.back().second>1.18f&&appliedRatios.back().second<1.227f);
      rpm.presentChainsaw(rev,true,10000,.033);assert(appliedRatios.back().second<1.227f);
      for(const auto& item:appliedRatios)assert(item.first==engineStream); // Neither rev one-shot nor unrelated loop is pitched.
      const auto checkpoint=rpm.snapshot();appliedRatios.clear();rpm.restore(checkpoint);
      assert(appliedRatios.empty());rpm.presentChainsaw(idle,false,0,.033);assert(appliedRatios.empty()); // Presentation is transient across load.
      assert(!rpm.done(rev)&&!rpm.done(other));rpm.stopAll(); }
    { AudioBank device(root); voice=device.play(global,25,0); device.advance(0.1);
      assert(device.openDevice()&&!device.done(voice)); device.stop(voice);
      voice=device.play(global,0); SDL_Delay(Uint32(std::ceil(device.duration(global)*1000))+100);
      device.advance(device.duration(global)+0.1); assert(device.done(voice));
      voice=device.play(global,25,0); device.advance(1000); assert(!device.done(voice));
      device.mute(true); device.advance(0.1); assert(device.muted()&&!device.done(voice));
      const auto active=device.snapshot(); device.restore(active); assert(!device.done(voice));
      device.mute(false); assert(!device.muted());
      const auto beforeMaster=device.snapshot();
      for(int master:{0,37,50,100,101,293,-1})device.setMasterVolume(master);
      device.setMasterVolume(0);device.volume(voice,50);device.setMasterVolume(50);
      const auto afterMaster=device.snapshot();
      assert(beforeMaster.nextVoice==afterMaster.nextVoice&&beforeMaster.voices.at(0).token==afterMaster.voices.at(0).token&&
          beforeMaster.voices.at(0).elapsed==afterMaster.voices.at(0).elapsed);
      assert(!device.done(voice)&&device.snapshot().voices.at(0).volume==50);
      device.stop(voice); assert(device.done(voice));
      voice=device.playAtFrequency(global,0,device.sampleRate(global)*2);
      const double fastWall=device.duration(global)/2;SDL_Delay(Uint32(std::ceil(fastWall*1000))+100);
      device.advance(fastWall+.1);assert(device.done(voice));
      voice=device.playAtFrequency(global,0,device.sampleRate(global)/2);device.advance(.02);
      const auto pitched=device.snapshot();device.restore(pitched);assert(!device.done(voice));
      assert(device.snapshot().voices.at(0).frequencyHz==device.sampleRate(global)/2);device.stop(voice); }
    { AudioBank deferred(root,true);const int first=deferred.play(global,100,0),second=deferred.play(global,50,0);
      deferred.setSpatialVolume(first,34);deferred.setSpatialVolume(second,0);appliedGains.clear();
      assert(deferred.openDevice()&&appliedGains.empty());deferred.advance(10);assert(appliedGains.empty());
      const auto queued=deferred.snapshot();deferred.restore(queued);assert(appliedGains.empty());
      assert(deferred.snapshot().voices[0].pending&&deferred.snapshot().voices[1].pending);
      // Derivedmixcache is notserialized; the World recomputes it before sourceFrame/unmute.
      deferred.setSpatialVolume(first,34);deferred.setSpatialVolume(second,0);deferred.sourceFrame();
      assert((appliedGains==std::vector<float>{AudioBank::sourceGain(34),0}));
      const auto started=deferred.snapshot();assert(!started.voices[0].pending&&!started.voices[1].pending);
      deferred.sourceFrame();assert(appliedGains.size()==2); // No restart on repeated sound phases.
      deferred.setMasterVolume(50);assert(appliedGains[appliedGains.size()-2]==AudioBank::sourceGain(34,50)&&appliedGains.back()==0);
      deferred.mute(true);deferred.setMasterVolume(37);deferred.volume(first,80);assert(appliedGains.back()==0);
      deferred.mute(false);assert(appliedGains[appliedGains.size()-2]==AudioBank::sourceGain(34,37)&&appliedGains.back()==0);
      deferred.setSpatialVolume(first,-1);assert(appliedGains.back()==AudioBank::sourceGain(80,37));
      deferred.setSpatialVolume(first,6);assert(appliedGains.back()==AudioBank::sourceGain(6,37));
      const auto active=deferred.snapshot();assert(active.voices[0].volume==80&&active.voices[0].elapsed==started.voices[0].elapsed);
      assert(rejects([&]{deferred.setSpatialVolume(first,-2);}));assert(rejects([&]{deferred.setSpatialVolume(first,101);}));
      deferred.mute(true);deferred.restore(active);assert(appliedGains.back()==0);
      deferred.setSpatialVolume(first,6);deferred.setSpatialVolume(second,0);deferred.mute(false);
      assert(appliedGains[appliedGains.size()-2]==AudioBank::sourceGain(6,37)&&appliedGains.back()==0);
      const auto restored=deferred.snapshot();assert(restored.nextVoice==active.nextVoice&&restored.voices[0].volume==80&&restored.voices[0].elapsed==active.voices[0].elapsed);
      deferred.stopAll(); }
    { AudioBank transactional(root,true);const auto empty=transactional.snapshot();
      const int first=transactional.play(global,100,0);transactional.setSpatialVolume(first,34);appliedGains.clear();
      transactional.sourceFrame(false);assert(!transactional.snapshot().voices[0].pending&&transactional.snapshot().voices[0].elapsed==0&&!transactional.done(first));
      assert(transactional.openDevice()&&appliedGains.empty()); // Opening a device cannot bypass the transaction.
      const int late=transactional.play(global,50,0);transactional.commitSourceFrame();
      assert((appliedGains==std::vector<float>{AudioBank::sourceGain(34)})&&transactional.snapshot().voices[1].pending);
      transactional.commitSourceFrame();assert(appliedGains.size()==1); // A commit never starts late-message voices.
      transactional.sourceFrame(false);assert(!transactional.snapshot().voices[1].pending&&appliedGains.size()==1);
      transactional.restore(empty);transactional.commitSourceFrame();
      assert(transactional.activeVoices()==0&&appliedGains.size()==1&&transactional.done(late)); // Rollback drops the unbound new fragment.
      const auto checkpoint=transactional.snapshot();const int failed=transactional.play(global,80);
      transactional.sourceFrame(false);assert(!transactional.done(failed)&&appliedGains.size()==1);
      transactional.restore(checkpoint);transactional.commitSourceFrame();assert(transactional.done(failed)&&appliedGains.size()==1);
      const int cancelled=transactional.play(global,80);transactional.sourceFrame(false);transactional.stop(cancelled);
      transactional.commitSourceFrame();assert(transactional.done(cancelled)&&appliedGains.size()==1); }
    { AudioBank completed(root,true);assert(completed.openDevice());const int token=completed.play(global,80);
      appliedGains.clear();completed.sourceFrame(false);completed.advance(completed.duration(global)+1);
      assert(!completed.done(token)&&completed.snapshot().voices[0].elapsed==completed.duration(global));
      completed.commitSourceFrame();assert(appliedGains.empty()&&!completed.done(token)); // No last-sample click after a long VM/movie.
      const auto boundary=completed.snapshot();completed.restore(boundary);
      assert(appliedGains.empty()&&!completed.done(token)&&completed.activeVoices()==1);
      completed.sourceFrame();assert(completed.done(token)&&completed.activeVoices()==0&&appliedGains.empty()); }
    { AudioBank movie(root,true);assert(movie.openDevice());
      const int speech=movie.play(global,100),ambience=movie.play(global,50,0);
      movie.sourceFrame();movie.advance(.1);const auto before=movie.snapshot();
      appliedGains.clear();movie.muteLoops(true);
      assert(movie.loopsMuted()&&!movie.muted()&&(appliedGains==std::vector<float>{0}));
      const auto during=movie.snapshot();assert(during.voices.size()==2&&during.voices[0].token==speech&&
          during.voices[0].elapsed==before.voices[0].elapsed&&!movie.done(speech));
      const int laterSpeech=movie.play(global,100),laterLoop=movie.play(global,50,0);movie.sourceFrame();
      assert(appliedGains[appliedGains.size()-2]==1&&appliedGains.back()==0);
      movie.mute(true);movie.muteLoops(false);assert(movie.muted()&&!movie.loopsMuted()&&appliedGains.back()==0);
      movie.mute(false);assert(appliedGains[appliedGains.size()-4]==1&&appliedGains[appliedGains.size()-3]==AudioBank::sourceGain(50)&&
          appliedGains[appliedGains.size()-2]==1&&appliedGains.back()==AudioBank::sourceGain(50));
      movie.setMasterVolume(0);movie.muteLoops(true);movie.muteLoops(false);assert(appliedGains.back()==0);
      movie.stop(speech);movie.stop(ambience);movie.stop(laterSpeech);movie.stop(laterLoop); }
    SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);

    const auto scratch=fs::temp_directory_path()/("evil-dead-audio-test-"+std::to_string(std::random_device{}()));
    if(!fs::create_directory(scratch)) throw std::runtime_error("Cannot create audio test directory");
    fs::create_directories(scratch/"audio/global");
    const auto manifest=scratch/"audio/manifest.txt",wave=scratch/"audio/global/ee9221ed.wav";
    fs::copy_file(root/"audio/global/ee9221ed.wav",wave);
    { std::ofstream out(manifest); out<<"EDAUDIO1\n1\nglobal ee9221ed "<<fs::file_size(wave)<<"\n"; }
    { AudioBank valid(scratch); assert(valid.size()==1); }
    { std::fstream file(wave,std::ios::in|std::ios::out|std::ios::binary);
      file.seekp(40); const char excessive[4]={char(0xff),char(0xff),char(0xff),char(0xff)}; file.write(excessive,4); }
    assert(rejects([&]{AudioBank invalid(scratch);}));
    { std::ofstream out(manifest); out<<"EDAUDIO1\n1\n../game ee9221ed 100\n"; }
    assert(rejects([&]{AudioBank invalid(scratch);}));
    fs::remove(wave); fs::remove(manifest); fs::remove(scratch/"audio/global"); fs::remove(scratch/"audio"); fs::remove(scratch);
}
