#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "source_audio_gain.h"

class AudioBank {
public:
    static constexpr unsigned VoiceLimit=64;
private:
    struct Clip { std::string bucket; std::uint32_t id=0; SDL_AudioSpec spec{}; std::vector<Uint8> pcm; double duration=0; };
    struct Voice {
        int token=-1,clip=-1,volume=100,frequencyHz=0,spatialVolume=-1; bool loop=false,pending=false,hardwarePending=false;
        double elapsed=0,hardwareDuration=0; Uint64 started=0;
        float presentationRatio=1;
        SDL_AudioStream* stream=nullptr;
        std::vector<Uint8> loopChunk;
    };
    std::vector<Clip> clips;
    std::array<Voice,VoiceLimit> voices{};
    SDL_AudioDeviceID device=0;
    SDL_AudioSpec deviceSpec{};
    bool audioInitialized=false,deviceAttempted=false,muted_=false,deferred_=false,loopsMuted_=false;
    uint8_t master=100;
    int nextVoice=0x10000;

    static bool validBucket(const std::string& value) {
        return value=="global"||value=="global_b"||(value.size()==4&&value[0]>='a'&&value[0]<='z'&&
            value[1]>='a'&&value[1]<='z'&&value[2]>='0'&&value[2]<='9'&&value[3]>='0'&&value[3]<='9');
    }
    static std::uint32_t identity(const std::string& value) {
        if(value.size()!=8) throw std::runtime_error("Invalid audio resource ID");
        std::uint32_t id=0;
        for(char c:value) {
            const unsigned digit=c>='0'&&c<='9'?unsigned(c-'0'):c>='a'&&c<='f'?unsigned(c-'a'+10):16;
            if(digit==16) throw std::runtime_error("Invalid audio resource ID");
            id=(id<<4)|digit;
        }
        return id;
    }
    static std::uint32_t u32(const Uint8* p) { return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24); }
    static void validateWave(const std::vector<Uint8>& bytes) {
        if(std::memcmp(bytes.data(),"RIFF",4)||std::memcmp(bytes.data()+8,"WAVE",4)||std::uint64_t(u32(bytes.data()+4))+8!=bytes.size())
            throw std::runtime_error("Invalid original RIFF length");
        bool fmt=false,data=false; unsigned frame=0; std::size_t samples=0,offset=12;
        while(offset<bytes.size()) {
            if(bytes.size()-offset<8) throw std::runtime_error("Truncated original RIFF chunk");
            const Uint8* chunk=bytes.data()+offset; const std::size_t length=u32(chunk+4); offset+=8;
            if(length>bytes.size()-offset) throw std::runtime_error("Original RIFF chunk exceeds file");
            if(!std::memcmp(chunk,"fmt ",4)) {
                if(fmt||length<16) throw std::runtime_error("Invalid original PCM format chunk");
                const Uint8* f=bytes.data()+offset;
                const unsigned encoding=f[0]|(unsigned(f[1])<<8),channels=f[2]|(unsigned(f[3])<<8),
                    alignment=f[12]|(unsigned(f[13])<<8),bits=f[14]|(unsigned(f[15])<<8),rate=u32(f+4);
                if(encoding!=1||(channels!=1&&channels!=2)||(bits!=8&&bits!=16)||rate<1000||rate>192000||
                    alignment!=channels*bits/8||u32(f+8)!=rate*alignment) throw std::runtime_error("Unsupported original PCM encoding");
                frame=alignment; fmt=true;
            } else if(!std::memcmp(chunk,"data",4)) {
                if(data||!length) throw std::runtime_error("Invalid original PCM data chunk");
                samples=length; data=true;
            }
            offset+=length+(length&1);
        }
        if(offset!=bytes.size()||!fmt||!data||samples%frame) throw std::runtime_error("Incomplete original PCM WAV");
    }
    static std::vector<Uint8> read(const std::filesystem::path& path,std::size_t expected) {
        std::ifstream input(path,std::ios::binary|std::ios::ate);
        if(!input||input.tellg()<0||static_cast<std::uint64_t>(input.tellg())!=expected)
            throw std::runtime_error("Missing audio file or mismatched source byte count: "+path.string());
        std::vector<Uint8> bytes(expected); input.seekg(0);
        if(!input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()))) throw std::runtime_error("Cannot read original audio");
        return bytes;
    }
    static void release(Voice& voice) {
        if(voice.stream) SDL_DestroyAudioStream(voice.stream);
        voice=Voice{};
    }
    Voice* find(int token) { for(auto& voice:voices) if(voice.token==token&&token>=0x10000) return &voice; return nullptr; }
    const Voice* find(int token) const { for(const auto& voice:voices) if(voice.token==token&&token>=0x10000) return &voice; return nullptr; }
    static bool validFrequency(int hz){return hz==0||(hz>=1000&&hz<=255000);} // Verified raw-SFX byteKHz ABI.
    double rate(const Voice& voice)const{return voice.frequencyHz?double(voice.frequencyHz)/clips[voice.clip].spec.freq:1;}
    float gain(const Voice& voice) const {return muted_||(loopsMuted_&&voice.loop)?0:sourceGain(voice.spatialVolume<0?voice.volume:voice.spatialVolume,master);}
    bool complete(const Voice& voice) const {
        if(voice.pending||voice.loop) return false;
        if(!voice.stream) return voice.elapsed>=clips[voice.clip].duration;
        const int queued=SDL_GetAudioStreamQueued(voice.stream),available=SDL_GetAudioStreamAvailable(voice.stream);
        if(queued<0||available<0) return voice.elapsed>=clips[voice.clip].duration;
        return double(SDL_GetTicksNS()-voice.started)/1e9>=voice.hardwareDuration&&queued==0&&available==0;
    }
    void loopChunk(Voice& voice) const {
        const auto& clip=clips[voice.clip];
        const double wallDuration=clip.duration/rate(voice);
        if(voice.loop&&wallDuration<0.25) {
            const auto copies=std::size_t(std::ceil(0.25/wallDuration));
            voice.loopChunk.reserve(clip.pcm.size()*copies);
            for(std::size_t i=0;i<copies;++i) voice.loopChunk.insert(voice.loopChunk.end(),clip.pcm.begin(),clip.pcm.end());
        }
    }
    bool prepareStream(Voice& voice) {
        const auto& clip=clips[voice.clip];
        auto sourceSpec=clip.spec;if(voice.frequencyHz)sourceSpec.freq=voice.frequencyHz;
        voice.stream=SDL_CreateAudioStream(&sourceSpec,&deviceSpec);
        bool ok=voice.stream&&SDL_SetAudioStreamGain(voice.stream,gain(voice));
        if(ok&&voice.presentationRatio!=1)ok=SDL_SetAudioStreamFrequencyRatio(voice.stream,voice.presentationRatio);
        const auto& bytes=voice.loopChunk.empty()?clip.pcm:voice.loopChunk;
        const auto frame=std::size_t(SDL_AUDIO_FRAMESIZE(clip.spec));
        const auto offset=std::min(clip.pcm.size()-frame,std::size_t(voice.elapsed*clip.spec.freq)*frame);
        voice.started=SDL_GetTicksNS(); voice.hardwareDuration=(clip.duration-voice.elapsed)/rate(voice);
        if(ok) ok=SDL_PutAudioStreamData(voice.stream,bytes.data()+offset,int(bytes.size()-offset));
        if(ok&&voice.loop) ok=SDL_PutAudioStreamData(voice.stream,bytes.data(),int(bytes.size()));
        if(ok&&!voice.loop) ok=SDL_FlushAudioStream(voice.stream);
        return ok;
    }
    void attach(Voice& voice) {
        if(!device||voice.pending||voice.hardwarePending) return;
        if(!prepareStream(voice)||!SDL_BindAudioStream(device,voice.stream)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,"Source sound playback unavailable; retaining duration: %s",SDL_GetError());
            if(voice.stream) SDL_DestroyAudioStream(voice.stream);
            voice.stream=nullptr;
        }
    }
    int start(int handle,int volume,bool loop,int frequencyHz=0) {
        if(handle<0||std::size_t(handle)>=clips.size()) return -1;
        if(!validFrequency(frequencyHz))throw std::runtime_error("Unverified original initial voice frequency");
        if(!deferred_)for(auto& voice:voices)if(voice.token>=0&&complete(voice))release(voice);
        auto slot=std::find_if(voices.begin(),voices.end(),[](const Voice& voice){return voice.token<0;});
        if(slot==voices.end()||nextVoice==std::numeric_limits<int>::max()) return -1;
        Voice prepared; prepared.clip=handle; prepared.loop=loop;prepared.frequencyHz=frequencyHz;
        loopChunk(prepared);
        *slot=std::move(prepared); Voice& voice=*slot;
        voice.token=nextVoice++; voice.clip=handle; voice.volume=std::clamp(volume,0,100);
        voice.loop=loop;voice.pending=deferred_;voice.started=SDL_GetTicksNS();attach(voice);
        return voice.token;
    }
public:
    struct VoiceState {
        int token=-1,clip=-1,volume=100;
        std::uint32_t id=0;
        std::string bucket;
        bool loop=false;
        double elapsed=0; // PCM/source seconds, independent of initial playback frequency.
        int frequencyHz=0; // 0 uses the original WAV rate; native430AA0 positiveinitialHz overrides it.
        bool pending=false; // Native430AA0 allocation awaits4300F0 source sound phase.
        int slot=-1; // Physical slot in the native 64-voice pool; preserve allocation and sound-phase order.
    };
    struct State { int nextVoice=0x10000; std::vector<VoiceState> voices; };
    explicit AudioBank(const std::filesystem::path& root,bool deferred=false):deferred_(deferred) {
        const auto manifest=root/"audio/manifest.txt";
        if(std::filesystem::file_size(manifest)>1024*1024) throw std::runtime_error("Audio manifest too large");
        std::ifstream input(manifest); std::string magic; std::size_t count=0,total=0;
        input>>magic>>count;
        if(!input||magic!="EDAUDIO1"||!count||count>4096) throw std::runtime_error("Invalid audio manifest");
        clips.reserve(count);
        for(std::size_t i=0;i<count;++i) {
            std::string bucket,hex; std::size_t length=0; input>>bucket>>hex>>length;
            if(!input||!validBucket(bucket)||length<44||length>16*1024*1024||length>256*1024*1024-total)
                throw std::runtime_error("Invalid source audio manifest entry");
            Clip clip; clip.bucket=bucket; clip.id=identity(hex);
            if(std::any_of(clips.begin(),clips.end(),[&](const Clip& old){return old.bucket==bucket&&old.id==clip.id;}))
                throw std::runtime_error("Duplicate source audio entry");
            total+=length;
            auto bytes=read(root/"audio"/bucket/(hex+".wav"),length);
            validateWave(bytes);
            SDL_IOStream* stream=SDL_IOFromConstMem(bytes.data(),bytes.size());
            Uint8* pcm=nullptr; Uint32 pcmBytes=0;
            if(!stream||!SDL_LoadWAV_IO(stream,true,&clip.spec,&pcm,&pcmBytes)) throw std::runtime_error("Cannot decode source PCM WAV");
            std::unique_ptr<Uint8,decltype(&SDL_free)> cleanup(pcm,SDL_free);
            const unsigned frame=SDL_AUDIO_FRAMESIZE(clip.spec);
            if((clip.spec.format!=SDL_AUDIO_S16LE&&clip.spec.format!=SDL_AUDIO_U8)||clip.spec.channels<1||clip.spec.channels>2||
               clip.spec.freq<1000||clip.spec.freq>192000||!frame||!pcmBytes||pcmBytes>16*1024*1024||pcmBytes%frame)
                throw std::runtime_error("Unsupported original PCM format");
            clip.pcm.assign(pcm,pcm+pcmBytes); clip.duration=double(pcmBytes)/(double(clip.spec.freq)*frame);
            clips.push_back(std::move(clip));
        }
        std::string trailing; if(input>>trailing) throw std::runtime_error("Trailing audio manifest data");
    }
    ~AudioBank() {
        stopAll(); if(device) SDL_CloseAudioDevice(device);
        if(audioInitialized) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    AudioBank(const AudioBank&)=delete;
    AudioBank& operator=(const AudioBank&)=delete;
    int resolve(const std::string& scene,std::uint32_t id,bool levelB=false) const {
        for(std::size_t i=0;i<clips.size();++i) if(clips[i].bucket==scene&&clips[i].id==id) return int(i);
        const char* global=levelB?"global_b":"global";
        for(std::size_t i=0;i<clips.size();++i) if(clips[i].bucket==global&&clips[i].id==id) return int(i);
        return -1;
    }
    static float sourceGain(int volume,int masterVolume=100) {
        return ednativeaudio::soundGain(volume,masterVolume);
    }
    bool openDevice() {
        if(deviceAttempted) return device!=0;
        deviceAttempted=true; audioInitialized=SDL_InitSubSystem(SDL_INIT_AUDIO);
        if(audioInitialized) device=SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,nullptr);
        if(device&&(!SDL_GetAudioDeviceFormat(device,&deviceSpec,nullptr)||!SDL_ResumeAudioDevice(device))) {
            SDL_CloseAudioDevice(device); device=0;
        }
        if(!device) SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,"Audio unavailable; retaining original clip durations: %s",SDL_GetError());
        else for(auto& voice:voices) if(voice.token>=0&&!voice.pending&&!voice.hardwarePending&&!voice.stream&&!complete(voice)) attach(voice);
        return device!=0;
    }
    int play(int handle,int volume=100) { return start(handle,volume,false); }
    int playAtFrequency(int handle,int volume,int frequencyHz){return start(handle,volume,false,frequencyHz);}
    int play(int handle,int volume,int loopArg) {
        if(handle<0||std::size_t(handle)>=clips.size()) return -1;
        // Native flag0x9801 always requests DirectSound looping; only source loopArg0 is decoded.
        if(loopArg!=0) throw std::runtime_error("Unsupported original sound loop argument");
        return start(handle,volume,true);
    }
    void sourceFrame(bool attachHardware=true) {
        // Native4300f0 visits physical64 slots ascending; late-message voices wait for the next phase.
        for(auto& voice:voices)if(voice.token>=0){
            if(voice.pending){
                voice.pending=false;voice.started=SDL_GetTicksNS();voice.hardwarePending=!attachHardware;
                if(attachHardware)attach(voice);
            }else if(deferred_&&complete(voice))release(voice); //4303d0 updates completion AFTER Main/Interface.
        }
    }
    void commitSourceFrame() {
        // The World commits hardware only after its source frame succeeds; late pending voices stay queued.
        for(auto& voice:voices)if(voice.token>=0&&voice.hardwarePending){voice.hardwarePending=false;if(!complete(voice))attach(voice);}
    }
    void presentChainsaw(int idleToken,bool revHeld,int attackPitch,double dt) {
        if(!std::isfinite(dt)||dt<0)throw std::runtime_error("Invalid chainsaw presentation time step");
        // Modern held RPM affects only the existing engine loop, never source clocks or the full rev one-shot.
        for(auto& voice:voices)if(voice.token>=0&&voice.loop&&clips[voice.clip].id==0x0e920903) {
            const float target=voice.token==idleToken?std::max(1.f+float(std::clamp(attackPitch,0,10000))/44100.f,revHeld?1.45f:1.f):1.f;
            const double tau=target>voice.presentationRatio?.14:.25;
            float next=float(target+(voice.presentationRatio-target)*std::exp(-dt/tau));
            if(std::abs(next-target)<.0001f)next=target;
            if(next==voice.presentationRatio)continue;
            if(voice.stream&&!SDL_SetAudioStreamFrequencyRatio(voice.stream,next))
                SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,"Cannot update chainsaw RPM: %s",SDL_GetError());
            else voice.presentationRatio=next;
        }
    }
    void setSpatialVolume(int token,int percent) {
        if(percent< -1||percent>100)throw std::runtime_error("Invalid derived source sound percent");
        if(auto* voice=find(token)){
            voice->spatialVolume=percent;
            if(voice->stream&&!SDL_SetAudioStreamGain(voice->stream,gain(*voice)))
                SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,"Cannot set original spatial volume: %s",SDL_GetError());
        }
    }
    bool done(int token) const { const auto* voice=find(token); return !voice||(!deferred_&&complete(*voice)); }
    void stop(int token) { if(auto* voice=find(token)) release(*voice); }
    void stopAll() { for(auto& voice:voices) release(voice); }
    void volume(int token,int value) {
        if(auto* voice=find(token)) {
            voice->volume=std::clamp(value,0,100);
            if(voice->stream&&!SDL_SetAudioStreamGain(voice->stream,gain(*voice)))
                SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,"Cannot set original voice volume: %s",SDL_GetError());
        }
    }
    void setMasterVolume(int value) {
        const auto next=uint8_t(value);if(master==next)return;master=next;
        for(auto& voice:voices)if(voice.stream&&!SDL_SetAudioStreamGain(voice.stream,gain(voice)))
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,"Cannot set original master volume: %s",SDL_GetError());
    }
    void mute(bool value) {
        if(muted_==value) return;
        muted_=value;
        for(auto& voice:voices) if(voice.stream&&!SDL_SetAudioStreamGain(voice.stream,gain(voice))) {
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,"Cannot change movie world-audio mute: %s",SDL_GetError());
            SDL_DestroyAudioStream(voice.stream); voice.stream=nullptr;
        }
    }
    bool muted() const { return muted_; }
    void muteLoops(bool value) {
        if(loopsMuted_==value)return;
        loopsMuted_=value;
        for(auto& voice:voices)if(voice.loop&&voice.stream&&!SDL_SetAudioStreamGain(voice.stream,gain(voice))) {
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,"Cannot change movie loop mute: %s",SDL_GetError());
            SDL_DestroyAudioStream(voice.stream);voice.stream=nullptr;
        }
    }
    bool loopsMuted() const {return loopsMuted_;}
    void advance(double dt) {
        if(!std::isfinite(dt)||dt<0) throw std::runtime_error("Invalid audio time step");
        for(auto& voice:voices) if(voice.token>=0&&!voice.pending) {
            const double duration=clips[voice.clip].duration;
            const double ratio=rate(voice);
            voice.elapsed=voice.loop?std::fmod(voice.elapsed+std::fmod(dt,duration/ratio)*ratio,duration):std::min(duration,voice.elapsed+dt*ratio);
            if(complete(voice)) {if(!deferred_)release(voice);continue;}
            if(voice.loop&&voice.stream) {
                const auto& bytes=voice.loopChunk.empty()?clips[voice.clip].pcm:voice.loopChunk;
                const int queued=SDL_GetAudioStreamQueued(voice.stream);
                bool ok=queued>=0;
                if(ok&&queued<int(bytes.size())) ok=SDL_PutAudioStreamData(voice.stream,bytes.data(),int(bytes.size()))&&
                    SDL_PutAudioStreamData(voice.stream,bytes.data(),int(bytes.size()));
                if(!ok) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,"Source sound loop unavailable: %s",SDL_GetError());
                    SDL_DestroyAudioStream(voice.stream); voice.stream=nullptr;
                }
            }
        }
    }
    State snapshot() const {
        State state; state.nextVoice=nextVoice;
        for(std::size_t slot=0;slot<voices.size();++slot) {const auto& voice=voices[slot];if(voice.token<0||(!deferred_&&complete(voice)))continue;
            const auto& clip=clips[voice.clip];
            state.voices.push_back({voice.token,voice.clip,voice.volume,clip.id,clip.bucket,voice.loop,voice.elapsed,voice.frequencyHz,voice.pending,int(slot)});
        }
        return state;
    }
    void restore(const State& state) {
        if(state.nextVoice<0x10000||state.voices.size()>voices.size()) throw std::runtime_error("Invalid saved audio voice table");
        std::vector<int> tokens;std::array<bool,VoiceLimit> occupied{};
        for(const auto& voice:state.voices) {
            if(voice.slot<0||unsigned(voice.slot)>=VoiceLimit||occupied[size_t(voice.slot)]||
               voice.token<0x10000||voice.token>=state.nextVoice||voice.clip<0||std::size_t(voice.clip)>=clips.size()||
               voice.volume<0||voice.volume>100||!validFrequency(voice.frequencyHz)||!std::isfinite(voice.elapsed)||voice.elapsed<0||(voice.pending&&voice.elapsed!=0)||
               std::find(tokens.begin(),tokens.end(),voice.token)!=tokens.end()) throw std::runtime_error("Invalid saved audio voice");
            const auto& clip=clips[voice.clip];
            if(voice.id!=clip.id||voice.bucket!=clip.bucket||voice.elapsed>clip.duration||(voice.loop&&voice.elapsed==clip.duration))
                throw std::runtime_error("Saved voice disagrees with original audio manifest or phase");
            tokens.push_back(voice.token);occupied[size_t(voice.slot)]=true;
        }
        std::array<Voice,VoiceLimit> prepared{};
        try {
            std::array<SDL_AudioStream*,VoiceLimit> streams{}; int streamCount=0;
            for(std::size_t i=0;i<state.voices.size();++i) {
                const auto& source=state.voices[i]; auto& voice=prepared[size_t(source.slot)];
                voice.token=source.token; voice.clip=source.clip; voice.volume=source.volume; voice.loop=source.loop; voice.elapsed=source.elapsed;voice.frequencyHz=source.frequencyHz;voice.pending=source.pending;
                loopChunk(voice);
                if(device&&!voice.pending&&(!deferred_||!complete(voice))) {
                    if(!prepareStream(voice)) throw std::runtime_error("Cannot prepare saved audio stream");
                    streams[streamCount++]=voice.stream;
                }
            }
            if(streamCount&&!SDL_BindAudioStreams(device,streams.data(),streamCount)) throw std::runtime_error("Cannot bind saved audio streams");
        } catch(...) {
            for(auto& voice:prepared) release(voice);
            throw;
        }
        stopAll();
        for(std::size_t i=0;i<prepared.size();++i) { voices[i]=std::move(prepared[i]); prepared[i].stream=nullptr; }
        nextVoice=state.nextVoice;
    }
    std::size_t size() const { return clips.size(); }
    std::size_t activeVoices() const { return std::count_if(voices.begin(),voices.end(),[&](const Voice& voice){return voice.token>=0&&(deferred_||!complete(voice));}); }
    double duration(int handle) const { return handle>=0&&std::size_t(handle)<clips.size()?clips[handle].duration:0; }
    int sampleRate(int handle)const{return handle>=0&&std::size_t(handle)<clips.size()?clips[handle].spec.freq:0;}
};
