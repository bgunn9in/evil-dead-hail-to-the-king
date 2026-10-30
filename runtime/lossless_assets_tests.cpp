#include "lossless_image.h"
#include "flac_decode.h"
#include <chrono>
#include <iostream>
#include <string>
#undef NDEBUG
#include <cassert>

namespace fs=std::filesystem;
std::vector<uint8_t> bytes(const fs::path& path){std::ifstream input(path,std::ios::binary|std::ios::ate);const auto length=input.tellg();
    if(length<0||length>32*1024*1024)throw std::runtime_error("Bounded parity fixture required");std::vector<uint8_t> data(static_cast<size_t>(length));
    input.seekg(0);if(!input.read(reinterpret_cast<char*>(data.data()),std::streamsize(data.size())))throw std::runtime_error("Parity fixture read failed");return data;}
template<class F>bool rejects(F fn){try{fn();return false;}catch(const std::exception&){return true;}}
int main(int argc,char**argv){try{
    if(argc!=3)return 2;const fs::path base=fs::absolute(fs::u8path(argv[1])),packed=fs::absolute(fs::u8path(argv[2]));
    unsigned colors=0,depths=0,masks=0,music=0;uint64_t pcmBytes=0;
    for(const auto& entry:fs::recursive_directory_iterator(base))if(entry.is_regular_file()){
        const auto relative=fs::relative(entry.path(),base);const auto target=packed/relative;const auto extension=entry.path().extension();
        if(extension==".bmp"){
            const auto original=edimage::load(entry.path()),decoded=edimage::load(edimage::colorPath(target),original->w,original->h);
            for(int y=0;y<original->h;++y)assert(!std::memcmp(static_cast<const uint8_t*>(original->pixels)+size_t(y)*original->pitch,
                static_cast<const uint8_t*>(decoded->pixels)+size_t(y)*decoded->pitch,size_t(original->w)*4));++colors;
        }else if(extension==".depth"){assert(bytes(entry.path())==edimage::packedPixels(target,320,240,4));++depths;}
        else if(extension==".coverage8"){assert(bytes(entry.path())==edimage::packedPixels(target,256,256,1));++masks;}
        else if(extension==".wav"&&entry.path().parent_path().filename()=="streams"){
            auto compressed=target;compressed.replace_extension(".flac");const auto original=bytes(entry.path());
            if(fs::exists(compressed)){const auto pcm=edflac::readMusic(compressed,original.size()-44);
                assert(pcm.size()+44==original.size()&&!std::memcmp(pcm.data(),original.data()+44,pcm.size()));pcmBytes+=pcm.size();++music;}
            else assert(original==bytes(target));
        }
    }
    assert(colors==4394&&depths==4300&&masks==570&&music>0);
    const auto scratch=fs::temp_directory_path()/fs::u8path("evil-dead-lossless-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(!fs::create_directory(scratch))throw std::runtime_error("Fresh decoder scratch required");
    const auto bad=scratch/"bad.png";std::array<uint8_t,26> header{{137,80,78,71,13,10,26,10,0,0,0,13,'I','H','D','R',127,255,255,255,0,0,0,1,8,6}};
    {std::ofstream f(bad,std::ios::binary);f.write(reinterpret_cast<const char*>(header.data()),header.size());}
    assert(rejects([&]{edimage::load(bad);}));fs::remove(bad);
    const auto mask=scratch/"bad.coverage8.png";edimage::Surface surface(SDL_CreateSurface(2,2,SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);assert(surface);
    assert(SDL_FillSurfaceRect(surface.get(),nullptr,SDL_MapSurfaceRGBA(surface.get(),255,0,0,255))&&SDL_SavePNG(surface.get(),mask.u8string().c_str()));
    assert(rejects([&]{edimage::packedPixels(scratch/"bad.coverage8",2,2,1);}));
    assert(rejects([&]{edimage::packedPixels(scratch/"bad.coverage8",1,1,4);}));fs::remove(mask);
    const auto unicode=scratch/fs::u8path(u8"данные с пробелами");assert(fs::create_directory(unicode));
    const auto encoded=unicode/"mask.coverage8.png";
    assert(SDL_FillSurfaceRect(surface.get(),nullptr,SDL_MapSurfaceRGBA(surface.get(),85,85,85,255))&&SDL_SavePNG(surface.get(),encoded.u8string().c_str()));
    assert(edimage::packedPixels(unicode/"mask.coverage8",2,2,1)==std::vector<uint8_t>(4,85));
    fs::remove(encoded);fs::remove(unicode);
    const auto truncated=scratch/"truncated.flac";{std::ofstream f(truncated,std::ios::binary);f<<"fLaC";}
    assert(rejects([&]{edflac::readMusic(truncated,4);}));fs::remove(truncated);fs::remove(scratch);
    std::cout<<"PASS lossless SDL PNG pixels: "<<colors<<" colors, "<<depths<<" depth maps, "<<masks<<" coverages; native FLAC: "<<music<<" streams/"<<pcmBytes<<" exact PCM bytes; malformed inputs rejected\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
