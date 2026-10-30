#pragma once
#include <SDL3/SDL.h>
#include <dav1d/dav1d.h>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace edmovieav1 {
// The offline exporter fixes AV01/8-bit I420/BT601 limited and one timestamp per visible frame.
class Decoder {
    struct Context {Dav1dContext* value=nullptr;~Context(){if(value)dav1d_close(&value);}} context;
    struct Data {Dav1dData value{};~Data(){dav1d_data_unref(&value);}} pending;
    struct Picture {Dav1dPicture value{};~Picture(){dav1d_picture_unref(&value);}};
    struct Packet {uint64_t offset;uint32_t bytes;};
    std::ifstream& file;
    std::vector<Packet> packets;
    std::vector<uint8_t> planar;
    unsigned width,height,frames,packet=0,decoded=0;
    static uint32_t u32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
    static uint64_t u64(const uint8_t* p){return u32(p)|(uint64_t(u32(p+4))<<32);}
    void read(uint64_t at,void* destination,size_t size){
        file.clear();file.seekg(std::streamoff(at));
        if(!file.read(static_cast<char*>(destination),std::streamsize(size)))throw std::runtime_error("Truncated AV1 movie packet");
    }
    void reset(){
        dav1d_data_unref(&pending.value);dav1d_flush(context.value);packet=decoded=0;
    }
    bool next(Picture& picture){
        for(;;){
            const int result=dav1d_get_picture(context.value,&picture.value);
            if(!result)return true;
            if(result!=DAV1D_ERR(EAGAIN))throw std::runtime_error("Cannot decode AV1 movie picture");
            if(!pending.value.sz){
                if(packet==packets.size())return false; // get_picture above also drains the final delayed picture.
                const auto source=packets[packet];
                auto* bytes=dav1d_data_create(&pending.value,source.bytes);
                if(!bytes)throw std::runtime_error("Cannot allocate bounded AV1 packet");
                read(source.offset,bytes,source.bytes);pending.value.m.timestamp=packet++;
            }
            const int sent=dav1d_send_data(context.value,&pending.value);
            if(sent&&sent!=DAV1D_ERR(EAGAIN))throw std::runtime_error("Invalid AV1 movie bitstream");
            if(sent==DAV1D_ERR(EAGAIN)){
                const int received=dav1d_get_picture(context.value,&picture.value);
                if(!received)return true;
                throw std::runtime_error("AV1 movie decoder made no progress");
            }
        }
    }
    void validate(const Dav1dPicture& p)const{
        if(p.p.w!=int(width)||p.p.h!=int(height)||p.p.bpc!=8||p.p.layout!=DAV1D_PIXEL_LAYOUT_I420||
           !p.seq_hdr||(p.seq_hdr->pri!=DAV1D_COLOR_PRI_BT601&&p.seq_hdr->pri!=DAV1D_COLOR_PRI_UNKNOWN)||
           (p.seq_hdr->trc!=DAV1D_TRC_BT601&&p.seq_hdr->trc!=DAV1D_TRC_UNKNOWN)||
           p.seq_hdr->mtrx!=DAV1D_MC_BT601||p.seq_hdr->color_range||p.seq_hdr->film_grain_present||
           p.m.timestamp!=int64_t(decoded)||p.stride[0]<ptrdiff_t(width)||p.stride[1]<ptrdiff_t(width/2))
            throw std::runtime_error("AV1 movie dimensions, color or presentation order changed");
    }
    void copy(const Dav1dPicture& p){
        size_t offset=0;
        for(unsigned plane=0;plane<3;++plane){
            const unsigned columns=plane?width/2:width,rows=plane?height/2:height;
            const auto* source=static_cast<const uint8_t*>(p.data[plane]);
            if(!source)throw std::runtime_error("Missing AV1 picture plane");
            for(unsigned y=0;y<rows;++y){
                std::memcpy(planar.data()+offset,source+ptrdiff_t(y)*p.stride[plane?1:0],columns);offset+=columns;
            }
        }
    }
public:
    Decoder(std::ifstream& input,uint64_t start,uint64_t bytes,unsigned w,unsigned h,unsigned count,unsigned numerator,unsigned denominator):
        file(input),width(w),height(h),frames(count){
        if(!w||!h||w>2048||h>2048||(w&1)||(h&1)||bytes<32||bytes>uint64_t(4)*1024*1024*1024||
           start>UINT64_MAX-bytes||!count||count>100000)
            throw std::runtime_error("Invalid bounded AV1 movie dimensions/length");
        uint8_t header[32];read(start,header,sizeof(header));
        if(std::memcmp(header,"DKIF\0\0\x20\0AV01",12)||header[12]!=(w&255)||header[13]!=(w>>8)||
           header[14]!=(h&255)||header[15]!=(h>>8)||u32(header+16)!=numerator||u32(header+20)!=denominator||
           u32(header+24)!=count||u32(header+28))
            throw std::runtime_error("AV1 IVF header differs from movie metadata");
        uint64_t at=start+32,end=start+bytes;packets.reserve(count);
        for(unsigned n=0;n<count;++n){
            if(end-at<12)throw std::runtime_error("Truncated AV1 packet index");
            uint8_t entry[12];read(at,entry,sizeof(entry));at+=12;const auto size=u32(entry);
            if(!size||size>16*1024*1024||u64(entry+4)!=n||size>end-at)throw std::runtime_error("Invalid AV1 packet index");
            packets.push_back({at,size});at+=size;
        }
        if(at!=end)throw std::runtime_error("Trailing AV1 packets");
        Dav1dSettings settings;dav1d_default_settings(&settings);settings.n_threads=4;settings.max_frame_delay=1;
        settings.frame_size_limit=w*h;settings.apply_grain=0;settings.all_layers=0;settings.strict_std_compliance=1;
        if(dav1d_open(&context.value,&settings))throw std::runtime_error("Cannot open bundled AV1 decoder");
        planar.resize(size_t(w)*h*3/2);
    }
    Decoder(const Decoder&)=delete;
    Decoder& operator=(const Decoder&)=delete;
    void frame(unsigned target,std::vector<uint8_t>& rgba){
        if(target>=frames)throw std::runtime_error("AV1 movie frame outside stream");
        if(target+1<decoded)reset();
        while(decoded<=target){
            Picture picture;
            if(!next(picture))throw std::runtime_error("AV1 movie ended before declared frame count");
            validate(picture.value);if(decoded==target)copy(picture.value);++decoded;
        }
        if(target+1==frames){
            Picture extra;if(next(extra))throw std::runtime_error("AV1 movie produced extra visible frames");
        }
        rgba.resize(size_t(width)*height*4);
        if(!SDL_ConvertPixelsAndColorspace(int(width),int(height),SDL_PIXELFORMAT_IYUV,SDL_COLORSPACE_BT601_LIMITED,0,
                planar.data(),int(width),SDL_PIXELFORMAT_RGBA32,SDL_COLORSPACE_SRGB,0,rgba.data(),int(width*4)))
            throw std::runtime_error("Cannot convert original AV1 movie color");
    }
    const std::vector<uint8_t>& i420()const{return planar;}
};
}
