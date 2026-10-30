#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <memory>
#include "movie_av1.h"
#include "source_audio_gain.h"

class MoviePlayer {
public:
    enum class State { Ready,Playing,Finished,Skipped };
private:
    inline static std::atomic<std::uint64_t> nextGeneration{1};
    const std::uint64_t generation_=nextGeneration.fetch_add(1,std::memory_order_relaxed);
    std::ifstream file;
    std::uint32_t identity_=0,width_=0,height_=0,frames_=0,fpsNum=0,fpsDen=0,rate=0,channels=0,displayWidth_=0,displayHeight_=0,index=0;
    std::uint64_t videoBytes=0,audioBytes=0;
    std::vector<Uint8> rgb,rgba,pcm,centeredPcm;
    std::unique_ptr<edmovieav1::Decoder> av1;
    SDL_AudioStream* stream=nullptr;
    bool audioInitialized=false,paused_=false,stereo_=true;
    State state_=State::Ready;
    double elapsed_=0;
    static std::uint32_t u32(const Uint8* p) { return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24); }
    static std::uint64_t u64(const Uint8* p) { return u32(p)|(std::uint64_t(u32(p+4))<<32); }
    void read(std::uint64_t offset,std::vector<Uint8>& target) {
        file.clear(); file.seekg(std::streamoff(offset));
        if(!file.read(reinterpret_cast<char*>(target.data()),std::streamsize(target.size()))) throw std::runtime_error("Truncated decoded original movie");
    }
    void readFrame(unsigned target) {
        if(av1){av1->frame(target,rgba);index=target;return;}
        read(64+std::uint64_t(target)*rgb.size(),rgb);
        for(std::size_t i=0,j=0;i<rgb.size();i+=3,j+=4) {
            rgba[j]=rgb[i]; rgba[j+1]=rgb[i+1]; rgba[j+2]=rgb[i+2]; rgba[j+3]=255;
        }
        index=target;
    }
    void stopAudio() {
        if(stream) { SDL_DestroyAudioStream(stream); stream=nullptr; }
        if(audioInitialized) { SDL_QuitSubSystem(SDL_INIT_AUDIO); audioInitialized=false; }
    }
public:
    MoviePlayer(const std::filesystem::path& root,std::uint32_t identity) {
        identity_=identity;
        std::ostringstream name; name<<std::hex<<std::setfill('0')<<std::setw(8)<<identity<<".edm";
        file.open(root/"movies"/name.str(),std::ios::binary|std::ios::ate);
        if(!file||file.tellg()<64) throw std::runtime_error("Missing original movie export");
        const auto length=std::uint64_t(file.tellg());
        std::array<Uint8,64> header{}; file.seekg(0);
        if(!file.read(reinterpret_cast<char*>(header.data()),header.size())||
           (std::memcmp(header.data(),"EDMOVIE1",8)&&std::memcmp(header.data(),"EDMOVIE2",8)))
            throw std::runtime_error("Invalid original movie export header");
        const bool compressed=!std::memcmp(header.data(),"EDMOVIE2",8);
        width_=u32(header.data()+8); height_=u32(header.data()+12); frames_=u32(header.data()+16);
        fpsNum=u32(header.data()+20); fpsDen=u32(header.data()+24); rate=u32(header.data()+28); channels=u32(header.data()+32);
        const auto audioFrames=u32(header.data()+36);
        displayWidth_=u32(header.data()+40); displayHeight_=u32(header.data()+44);
        videoBytes=u64(header.data()+48); audioBytes=u64(header.data()+56);
        if(!width_||width_>2048||!height_||height_>2048||!frames_||frames_>100000||!fpsNum||!fpsDen||
           double(fpsNum)/fpsDen<0.1||double(fpsNum)/fpsDen>120||rate<8000||rate>192000||(channels!=1&&channels!=2)||!audioFrames||
           displayWidth_!=width_||(displayHeight_!=height_&&displayHeight_!=2*height_)||
           (!compressed&&videoBytes!=std::uint64_t(width_)*height_*3*frames_)||
           (compressed&&videoBytes<32)||videoBytes>std::uint64_t(4)*1024*1024*1024||
           audioBytes!=std::uint64_t(audioFrames)*channels*2||audioBytes>128*1024*1024||
           length!=64+videoBytes+audioBytes||std::abs(double(audioFrames)/rate-duration())>0.25)
            throw std::runtime_error("Invalid original movie dimensions, timing or bounded stream lengths");
        if(!compressed)rgb.resize(std::size_t(width_)*height_*3);
        rgba.resize(std::size_t(width_)*height_*4); pcm.resize(std::size_t(audioBytes));
        read(64+videoBytes,pcm);
        if(compressed)av1=std::make_unique<edmovieav1::Decoder>(file,64,videoBytes,width_,height_,frames_,fpsNum,fpsDen);
        readFrame(0);
    }
    ~MoviePlayer() { stopAudio(); }
    MoviePlayer(const MoviePlayer&)=delete;
    MoviePlayer& operator=(const MoviePlayer&)=delete;
    void setStereo(bool value) {
        if(state_!=State::Ready)throw std::runtime_error("Original movie audio mode already started");
        stereo_=value;
    }
    void start(bool audio=true) {
        if(state_!=State::Ready) throw std::runtime_error("Original movie already started");
        if(audio) {
            const bool centered=channels==2&&!stereo_;
            if(centered)centeredPcm=ednativeaudio::centeredMoviePcm(pcm);
            const auto& playback=centered?centeredPcm:pcm;
            audioInitialized=SDL_InitSubSystem(SDL_INIT_AUDIO);
            SDL_AudioSpec spec{SDL_AUDIO_S16LE,centered?1:int(channels),int(rate)};
            if(audioInitialized) stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
            if(!stream||!SDL_PutAudioStreamData(stream,playback.data(),int(playback.size()))||!SDL_FlushAudioStream(stream)||!SDL_ResumeAudioStreamDevice(stream)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,"Movie audio unavailable; retaining original video timing: %s",SDL_GetError());
                stopAudio();
            }
        }
        state_=State::Playing;
    }
    void advance(double dt) {
        if(!std::isfinite(dt)||dt<0) throw std::runtime_error("Invalid movie time step");
        if(state_!=State::Playing||paused_) return;
        const double next=std::min(duration(),elapsed_+dt);
        const unsigned target=unsigned(std::min(double(frames_-1),std::floor(next*fpsNum/fpsDen)));
        if(target!=index) readFrame(target);
        elapsed_=next;
        if(elapsed_>=duration()) { state_=State::Finished; stopAudio(); }
    }
    void skip() { if(state_==State::Playing) { state_=State::Skipped; stopAudio(); } }
    void pause(bool value) {
        if(state_!=State::Playing||paused_==value) return;
        paused_=value;
        if(stream&&!(value?SDL_PauseAudioStreamDevice(stream):SDL_ResumeAudioStreamDevice(stream))) {
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,"Cannot change movie focus pause: %s",SDL_GetError()); stopAudio();
        }
    }
    bool paused() const { return paused_; }
    State state() const { return state_; }
    bool done() const { return state_==State::Finished||state_==State::Skipped; }
    bool hasAudioDevice() const { return stream!=nullptr; }
    unsigned width() const { return width_; }
    bool compressed() const { return bool(av1); }
    std::uint32_t identity() const { return identity_; }
    std::uint64_t generation() const { return generation_; }
    unsigned height() const { return height_; }
    unsigned displayWidth() const { return displayWidth_; }
    unsigned displayHeight() const { return displayHeight_; }
    unsigned frameCount() const { return frames_; }
    unsigned frameIndex() const { return index; }
    double elapsed() const { return elapsed_; }
    double duration() const { return double(frames_)*fpsDen/fpsNum; }
    const std::vector<Uint8>& frameRGBA() const { return rgba; }
};
