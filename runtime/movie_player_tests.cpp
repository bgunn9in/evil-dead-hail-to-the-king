#include <SDL3/SDL.h>
#include <vector>
static bool recordMovieAudio=false;
static SDL_AudioSpec queuedSpec{};
static std::vector<Uint8> queuedPcm;
static SDL_AudioStream* recordMovieStream(SDL_AudioDeviceID device,const SDL_AudioSpec* spec,SDL_AudioStreamCallback callback,void* userdata){
    if(recordMovieAudio)queuedSpec=*spec;return SDL_OpenAudioDeviceStream(device,spec,callback,userdata);
}
static bool recordMovieData(SDL_AudioStream* stream,const void* data,int length){
    if(recordMovieAudio)queuedPcm.assign(static_cast<const Uint8*>(data),static_cast<const Uint8*>(data)+length);
    return SDL_PutAudioStreamData(stream,data,length);
}
#define SDL_OpenAudioDeviceStream recordMovieStream
#define SDL_PutAudioStreamData recordMovieData
#include "movie_player.h"
#undef SDL_OpenAudioDeviceStream
#undef SDL_PutAudioStreamData
#undef NDEBUG
#include <cassert>
#include <random>
#include <iostream>

namespace fs=std::filesystem;
template<class F> bool rejects(F action) { try { action(); return false; } catch(const std::exception&) { return true; } }
void sourceFrame(const fs::path& root,const MoviePlayer& movie) {
    if(movie.compressed()){
        MoviePlayer sequential(root,movie.identity());sequential.start(false);
        const double interval=sequential.duration()/sequential.frameCount();
        for(unsigned frame=0;frame<movie.frameIndex();++frame)sequential.advance(interval+1e-9);
        assert(sequential.frameIndex()==movie.frameIndex()&&sequential.frameRGBA()==movie.frameRGBA());return;
    }
    std::ifstream file(root/"movies/61303600.edm",std::ios::binary);
    const auto& rgba=movie.frameRGBA(); std::vector<Uint8> rgb(rgba.size()/4*3);
    file.seekg(64+std::uint64_t(movie.frameIndex())*rgb.size());
    assert(file.read(reinterpret_cast<char*>(rgb.data()),rgb.size()));
    for(std::size_t i=0,j=0;i<rgb.size();i+=3,j+=4)
        assert(rgba[j]==rgb[i]&&rgba[j+1]==rgb[i+1]&&rgba[j+2]==rgb[i+2]&&rgba[j+3]==255);
}
int main(int argc,char** argv) {
    if(argc!=2&&!(argc==3&&std::string(argv[2])=="--dreamcast")) throw std::runtime_error("Expected original movie asset root and optional --dreamcast");
    const fs::path root=fs::u8path(argv[1]);
    MoviePlayer movie(root,0x61303600);
    { MoviePlayer reloaded(root,0x61303600); assert(reloaded.generation()!=movie.generation()); }
    assert(movie.state()==MoviePlayer::State::Ready&&!movie.done());
    const unsigned scale=movie.compressed()?2:1;
    assert(movie.width()==320*scale&&movie.height()==240*scale&&movie.displayWidth()==movie.width()&&movie.displayHeight()==movie.height());
    assert(movie.frameCount()==132&&movie.duration()==4.4); sourceFrame(root,movie);
    movie.start(false); assert(movie.state()==MoviePlayer::State::Playing);
    assert(rejects([&]{movie.setStereo(false);}));
    movie.advance(movie.duration()/2+1e-9); assert(movie.frameIndex()==movie.frameCount()/2); sourceFrame(root,movie);
    movie.advance(movie.duration()-movie.elapsed()-1e-6); assert(movie.frameIndex()==movie.frameCount()-1&&!movie.done()); sourceFrame(root,movie);
    movie.advance(0.001); assert(movie.done()&&movie.state()==MoviePlayer::State::Finished);
    assert(rejects([&]{movie.start(false);}));
    assert(rejects([&]{movie.advance(-1);}));
    assert(rejects([&]{MoviePlayer missing(root,0xffffffff);}));
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER,"dummy",SDL_HINT_OVERRIDE);
    // Native SFD mono centers both original channels: their sum reaches each speaker, with output saturation.
    const auto pcmBytes=[](auto samples){std::vector<Uint8> bytes;for(int sample:samples){bytes.push_back(Uint8(sample));bytes.push_back(Uint8(unsigned(sample)>>8));}return bytes;};
    const auto sourceSamples=pcmBytes(std::array<int,8>{{1000,-500,30000,10000,-30000,-10000,-32768,32767}});
    const auto expectedMono=pcmBytes(std::array<int,4>{{500,32767,-32768,-1}});
    assert(ednativeaudio::centeredMoviePcm(sourceSamples)==expectedMono);
    auto incomplete=sourceSamples;incomplete.pop_back();assert(rejects([&]{ednativeaudio::centeredMoviePcm(incomplete);}));
    {const SDL_AudioSpec mono{SDL_AUDIO_S16LE,1,22050},stereo{SDL_AUDIO_S16LE,2,22050};
      SDL_AudioStream* sink=SDL_CreateAudioStream(&mono,&stereo);assert(sink);
      assert(SDL_PutAudioStreamData(sink,expectedMono.data(),int(expectedMono.size()))&&SDL_FlushAudioStream(sink));
      std::array<int16_t,8> output{};assert(SDL_GetAudioStreamData(sink,output.data(),sizeof(output))==sizeof(output));
      constexpr std::array<int16_t,4> expected{{500,32767,-32768,-1}};
      for(unsigned n=0;n<expected.size();++n)assert(output[n*2]==expected[n]&&output[n*2+1]==expected[n]);
      SDL_DestroyAudioStream(sink);}
    std::vector<Uint8> originalPcm;
    {std::ifstream original(root/"movies/61303600.edm",std::ios::binary|std::ios::ate);const auto end=original.tellg();
      std::array<Uint8,64> header{};original.seekg(0);assert(original.read(reinterpret_cast<char*>(header.data()),header.size()));
      std::uint64_t length=0;for(unsigned n=0;n<8;++n)length|=std::uint64_t(header[56+n])<<(8*n);
      assert(header[32]==2);originalPcm.resize(std::size_t(length));original.seekg(end-std::streamoff(length));
      assert(original.read(reinterpret_cast<char*>(originalPcm.data()),originalPcm.size()));}
    recordMovieAudio=true;
    { MoviePlayer audible(root,0x61303600); audible.start(true); assert(audible.hasAudioDevice());
      assert(queuedSpec.channels==2&&queuedPcm==originalPcm);
      audible.pause(true); audible.advance(1); assert(audible.paused()&&audible.elapsed()==0&&audible.frameIndex()==0);
      audible.pause(false); assert(!audible.paused());
      audible.advance(0.1); audible.skip(); assert(audible.state()==MoviePlayer::State::Skipped&&!audible.hasAudioDevice()); }
    {MoviePlayer centered(root,0x61303600);centered.setStereo(false);centered.start(true);assert(centered.hasAudioDevice());
      assert(queuedSpec.channels==1&&queuedPcm==ednativeaudio::centeredMoviePcm(originalPcm));centered.skip();}
    recordMovieAudio=false;
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER,"ed-intentionally-missing-driver",SDL_HINT_OVERRIDE);
    { MoviePlayer fallback(root,0x61303600); fallback.start(true); assert(!fallback.hasAudioDevice());
      fallback.advance(fallback.duration()); assert(fallback.state()==MoviePlayer::State::Finished); }
    SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);
    const auto scratch=fs::temp_directory_path()/("evil-dead-movie-test-"+std::to_string(std::random_device{}()));
    assert(fs::create_directory(scratch)); fs::create_directory(scratch/"movies");
    const auto broken=scratch/"movies/61303600.edm";
    { std::ifstream original(root/"movies/61303600.edm",std::ios::binary); std::array<char,64> bytes{}; original.read(bytes.data(),bytes.size());
      bytes[8]=char(0xff); bytes[9]=char(0xff); std::ofstream output(broken,std::ios::binary); output.write(bytes.data(),bytes.size()); }
    assert(rejects([&]{MoviePlayer invalid(scratch,0x61303600);}));
    if(movie.compressed()){
        std::ifstream original(root/"movies/61303600.edm",std::ios::binary|std::ios::ate);
        std::vector<char> valid(size_t(original.tellg()));original.seekg(0);assert(original.read(valid.data(),valid.size()));
        // Each malformed IVF remains inside an otherwise correctly bounded EDM/PCM file.
        for(size_t at:{size_t(64+12),size_t(64+24),size_t(64+32+4),size_t(64+32)}){
            auto bytes=valid;bytes.at(at)^=1;
            {std::ofstream output(broken,std::ios::binary|std::ios::trunc);output.write(bytes.data(),bytes.size());}
            assert(rejects([&]{MoviePlayer invalid(scratch,0x61303600);}));
        }
        auto bytes=valid;std::fill(bytes.begin()+64+32+12,bytes.begin()+64+32+16,char(0xff));
        {std::ofstream output(broken,std::ios::binary|std::ios::trunc);output.write(bytes.data(),bytes.size());}
        assert(rejects([&]{MoviePlayer invalid(scratch,0x61303600);}));
    }
    fs::remove(broken); fs::remove(scratch/"movies"); fs::remove(scratch);
    {
        unsigned count=0;
        for(const auto& entry:fs::directory_iterator(root/"movies"))if(entry.path().extension()==".edm") {
            MoviePlayer clip(root,std::uint32_t(std::stoul(entry.path().stem().string(),nullptr,16)));
            const unsigned clipScale=clip.compressed()?2:1;
            assert(clip.width()==320*clipScale&&clip.height()==240*clipScale&&clip.displayWidth()==clip.width()&&clip.displayHeight()==clip.height());
            clip.start(false);clip.advance(clip.duration()/2);assert(!clip.done());
            clip.advance(clip.duration());assert(clip.done());++count;
        }
        assert(count==56);
    }
    std::cout<<"Original Dreamcast movie tests passed:56clips, "<<(movie.compressed()?"2xAV1":"nativeRGB")
             <<", sequential/jump frames, bounded parsing, pause/skip/EOF and audio fallback\n";
}
