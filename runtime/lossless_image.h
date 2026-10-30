#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace edimage {
using Surface=std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)>;
inline Surface load(const std::filesystem::path& path,unsigned width=0,unsigned height=0){
    if(std::filesystem::file_size(path)>64*1024*1024)throw std::runtime_error("Excessive image file");
    std::ifstream input(path,std::ios::binary);std::array<unsigned char,26> header{};input.read(reinterpret_cast<char*>(header.data()),header.size());
    const unsigned char png[]{137,80,78,71,13,10,26,10};
    if(input.gcount()>=24&&!std::memcmp(header.data(),png,8)){
        auto word=[&](unsigned at){return unsigned(header[at])<<24|unsigned(header[at+1])<<16|unsigned(header[at+2])<<8|header[at+3];};
        if(std::memcmp(header.data()+12,"IHDR",4)||!word(16)||!word(20)||word(16)>4096||word(20)>4096||
            (width&&word(16)!=width)||(height&&word(20)!=height)||header[24]!=8)
            throw std::runtime_error("Invalid bounded PNG dimensions/precision");
    }
    Surface image(SDL_LoadSurface(path.u8string().c_str()),SDL_DestroySurface);
    if(!image||image->w<=0||image->h<=0||image->w>4096||image->h>4096||
        (width&&unsigned(image->w)!=width)||(height&&unsigned(image->h)!=height))
        throw std::runtime_error("Cannot load bounded image: "+path.u8string());
    Surface rgba(SDL_ConvertSurface(image.get(),SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
    if(!rgba)throw std::runtime_error("Cannot convert image to RGBA32");return rgba;
}
inline std::filesystem::path colorPath(std::filesystem::path path){
    if(!std::filesystem::exists(path))path.replace_extension(".png");return path;
}
inline std::vector<uint8_t> packedPixels(const std::filesystem::path& path,unsigned width,unsigned height,unsigned channels){
    if(!width||!height||width>4096||height>4096||(channels!=1&&channels!=4))throw std::runtime_error("Invalid packed image shape");
    std::vector<uint8_t> result(size_t(width)*height*channels);
    if(std::filesystem::exists(path)){
        if(std::filesystem::file_size(path)!=result.size())throw std::runtime_error("Packed pixel byte count differs");
        std::ifstream input(path,std::ios::binary);if(!input.read(reinterpret_cast<char*>(result.data()),std::streamsize(result.size())))
            throw std::runtime_error("Truncated packed pixels");return result;
    }
    auto encoded=path;encoded+=".png";
    const auto image=load(encoded,width,height);
    for(unsigned y=0;y<height;++y){const auto* row=static_cast<const uint8_t*>(image->pixels)+size_t(y)*image->pitch;
        if(channels==4)std::memcpy(result.data()+size_t(y)*width*4,row,size_t(width)*4);
        else for(unsigned x=0;x<width;++x){if(row[x*4]!=row[x*4+1]||row[x*4]!=row[x*4+2]||row[x*4+3]!=255)
                throw std::runtime_error("Non-grayscale coverage pixels");result[size_t(y)*width+x]=row[x*4];}}
    return result;
}
}
