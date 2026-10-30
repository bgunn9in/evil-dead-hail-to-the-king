#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "source_audio_gain.h"
#include "flac_decode.h"

// Original standalone music owns one stream, independently of the64 SFX voices.
// Logical updates are immediate; SDL changes occur only on World transaction commit.
class StreamAudio {
    bool clearStatusAtEnd_=false;
public:
    struct State {uint32_t id=0;bool loop=false;double elapsed=0;bool playing=false;}; // Logical status follows the selected source EOF policy.
private:
    struct Clip {uint32_t id=0;std::vector<Uint8> pcm;double duration=0;};
    std::filesystem::path root;
    std::map<uint32_t,size_t> files;
    State state{},hardwareState{};
    std::shared_ptr<Clip> clip,hardwareClip;
    SDL_AudioDeviceID device=0;SDL_AudioSpec deviceSpec{};
    SDL_AudioStream* stream=nullptr;
    bool initialized=false,attempted=false,dirty=false,gainDirty=false,muted_=false;
    uint8_t master=100;
    static uint32_t u32(const Uint8* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
    static bool equal(const State& a,const State& b){return a.id==b.id&&a.loop==b.loop&&a.elapsed==b.elapsed&&a.playing==b.playing;}
    static std::string hex(uint32_t id){const char* digits="0123456789abcdef";std::string out(8,'0');for(int i=7;i>=0;--i){out[size_t(i)]=digits[id&15];id>>=4;}return out;}
    std::shared_ptr<Clip> load(uint32_t id)const {
        if(clip&&clip->id==id)return clip;if(hardwareClip&&hardwareClip->id==id)return hardwareClip;
        const auto found=files.find(id);if(found==files.end())throw std::runtime_error("Missing original standalone audio stream: "+hex(id));
        auto path=root/(hex(id)+".wav");
        if(!std::filesystem::exists(path)){path.replace_extension(".flac");auto result=std::make_shared<Clip>();result->id=id;
            result->pcm=edflac::readMusic(path,found->second-44);result->duration=double(result->pcm.size())/176400;return result;}
        std::ifstream input(path,std::ios::binary|std::ios::ate);
        if(!input||input.tellg()<0||uint64_t(input.tellg())!=found->second)throw std::runtime_error("Standalone audio byte count differs from source");
        std::vector<Uint8> bytes(found->second);input.seekg(0);if(!input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size())))throw std::runtime_error("Truncated source stream");
        const Uint8 expected[]{1,0,2,0,0x44,0xac,0,0,0x10,0xb1,2,0,4,0,16,0};
        if(std::memcmp(bytes.data(),"RIFF",4)||u32(bytes.data()+4)+uint64_t(8)!=bytes.size()||std::memcmp(bytes.data()+8,"WAVEfmt ",8)||
           u32(bytes.data()+16)!=16||std::memcmp(bytes.data()+20,expected,16)||std::memcmp(bytes.data()+36,"data",4)||
           u32(bytes.data()+40)+uint64_t(44)>bytes.size()||!u32(bytes.data()+40)||u32(bytes.data()+40)%4)
            throw std::runtime_error("Unverified source stream PCM encoding");
        const size_t dataEnd=44+size_t(u32(bytes.data()+40));size_t offset=dataEnd;
        while(offset<bytes.size()){if(bytes.size()-offset<8)throw std::runtime_error("Truncated stream metadata");const size_t size=u32(bytes.data()+offset+4);
            if(!std::memcmp(bytes.data()+offset,"fmt ",4)||!std::memcmp(bytes.data()+offset,"data",4)||size>bytes.size()-offset-8)throw std::runtime_error("Invalid stream metadata range");
            offset+=8+size+(size&1);}
        if(offset!=bytes.size())throw std::runtime_error("Misaligned stream metadata");
        auto result=std::make_shared<Clip>();result->id=id;result->pcm.assign(bytes.begin()+44,bytes.begin()+dataEnd);result->duration=double(result->pcm.size())/176400;return result;
    }
    void closeStream(){if(stream)SDL_DestroyAudioStream(stream);stream=nullptr;hardwareClip.reset();}
    void warn(const char* action){SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,"Original stream %s unavailable; retaining PCM duration: %s",action,SDL_GetError());}
public:
    explicit StreamAudio(const std::filesystem::path& assets,bool clearStatusAtEnd=false):clearStatusAtEnd_(clearStatusAtEnd),root(assets/"audio/streams") {
        const auto manifest=root/"manifest.txt";if(std::filesystem::file_size(manifest)>16384)throw std::runtime_error("Excessive source stream manifest");
        std::ifstream input(manifest);std::string magic;unsigned count=0;input>>magic>>count;
        if(!input||magic!="EDSTREAM1"||!count||count>256)throw std::runtime_error("Invalid source stream manifest");
        for(unsigned i=0;i<count;++i){std::string name;uint64_t size=0;input>>name>>size;uint32_t id=0;
            if(!input||name.size()!=8||size<48||size>16*1024*1024)throw std::runtime_error("Invalid source stream entry");
            for(char c:name){const unsigned digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:16;if(digit>15)throw std::runtime_error("Invalid stream identity");id=(id<<4)|digit;}
            if((id&65535)||id>>24<'a'||id>>24>'z'||!files.emplace(id,size_t(size)).second)throw std::runtime_error("Duplicate or noncanonical stream identity");}
        std::string extra;if(input>>extra)throw std::runtime_error("Trailing stream manifest data");
    }
    ~StreamAudio(){closeStream();if(device)SDL_CloseAudioDevice(device);if(initialized)SDL_QuitSubSystem(SDL_INIT_AUDIO);}
    StreamAudio(const StreamAudio&)=delete;StreamAudio& operator=(const StreamAudio&)=delete;
    bool play(uint32_t id,int mode){if(mode<0||mode>2)throw std::runtime_error("Unverified native audioPlay flag");
        if(mode==2&&state.playing)return false;id&=0xffff0000u;
        if(mode==1&&state.playing&&state.loop&&state.id==id)return false; // Title and MenMain request the same loop on focus.
        auto next=load(id);
        state={id,mode==1,0,true};clip=std::move(next);dirty=true;return true;}
    bool status()const{return state.playing;}
    bool pcmPlaying()const{return state.playing&&clip&&(state.loop||state.elapsed<clip->duration);}
    void advance(double dt){if(!std::isfinite(dt)||dt<0)throw std::runtime_error("Invalid original stream clock");if(!state.playing)return;
        const bool before=pcmPlaying();
        state.elapsed=state.loop?std::fmod(state.elapsed+std::fmod(dt,clip->duration),clip->duration):std::min(clip->duration,state.elapsed+dt);
        // Native worker41bc80 signals its event at EOF, but never clears the
        // registered audioStatus byte. Keep that PCbehavior separate from PCM.
        // Dreamcast8c07db60 clears audioStatus when the non-loop stream record ends.
        if(clearStatusAtEnd_&&!state.loop&&state.elapsed>=clip->duration){contextReset();return;}
        if(before&&!pcmPlaying())dirty=true;
        if(!dirty)hardwareState=state;
    }
    State snapshot()const{return state;}
    void contextReset(){state={};clip.reset();dirty=!equal(state,hardwareState);} // Native417f60/417fe0→41b770, distinct from audioStop's RET.
    void restore(const State& next){if(!std::isfinite(next.elapsed)||next.elapsed<0||(next.id&65535))throw std::runtime_error("Invalid saved original stream state");
        std::shared_ptr<Clip> prepared;if(next.id)prepared=load(next.id);
        if((!next.id&&(next.loop||next.playing||next.elapsed!=0))||(next.id&&(!next.playing||next.elapsed>prepared->duration||
           (next.loop&&next.elapsed>=prepared->duration))))throw std::runtime_error("Saved stream phase disagrees with source PCM");
        state=next;clip=std::move(prepared);dirty=!equal(state,hardwareState);
    }
    static float sourceGain(int volume){return ednativeaudio::streamGain(volume);}
    void setMasterVolume(int value){const auto next=uint8_t(value);if(master!=next){master=next;gainDirty=true;}}
    void mute(bool value){if(muted_!=value){muted_=value;gainDirty=true;}}
    bool muted()const{return muted_;}
    bool openDevice(){if(attempted)return device!=0;attempted=true;initialized=SDL_InitSubSystem(SDL_INIT_AUDIO);
        if(initialized)device=SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,nullptr);
        if(device&&(!SDL_GetAudioDeviceFormat(device,&deviceSpec,nullptr)||!SDL_ResumeAudioDevice(device))){SDL_CloseAudioDevice(device);device=0;}
        if(!device)warn("device");else {dirty=true;commit();}return device!=0;}
    void commit(){if(dirty){SDL_AudioStream* prepared=nullptr;
            if(device&&pcmPlaying()){const SDL_AudioSpec source{SDL_AUDIO_S16LE,2,44100};prepared=SDL_CreateAudioStream(&source,&deviceSpec);
                const size_t offset=std::min(clip->pcm.size()-4,size_t(state.elapsed*44100)*4);
                bool ok=prepared&&SDL_SetAudioStreamGain(prepared,muted_?0:sourceGain(master))&&SDL_PutAudioStreamData(prepared,clip->pcm.data()+offset,int(clip->pcm.size()-offset));
                if(ok)ok=state.loop?SDL_PutAudioStreamData(prepared,clip->pcm.data(),int(clip->pcm.size())):SDL_FlushAudioStream(prepared);
                if(ok)ok=SDL_BindAudioStream(device,prepared);
                if(!ok){warn("playback");if(prepared)SDL_DestroyAudioStream(prepared);prepared=nullptr;}}
            closeStream();stream=prepared;hardwareClip=pcmPlaying()?clip:nullptr;hardwareState=state;dirty=false;gainDirty=false;
        }else if(gainDirty){if(stream&&!SDL_SetAudioStreamGain(stream,muted_?0:sourceGain(master)))warn("gain");gainDirty=false;}
        if(stream&&state.playing&&state.loop){const int queued=SDL_GetAudioStreamQueued(stream);if(queued<0){warn("queue");closeStream();}
            else if(queued<int(clip->pcm.size())&&!SDL_PutAudioStreamData(stream,clip->pcm.data(),int(clip->pcm.size()))){warn("refill");closeStream();}}
    }
    size_t size()const{return files.size();}
    bool hardwarePlaying()const{return stream!=nullptr;} // For deferred-commit verification, not the source audioStatus ABI.
};
