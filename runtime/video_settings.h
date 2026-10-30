#pragma once
#include "save_file.h"
#include "state_bytes.h"
#include "ui_language.h"
#include <cmath>
#include <utility>

struct VideoResolution {
    unsigned width=0,height=0;
    bool operator==(const VideoResolution& other)const{return width==other.width&&height==other.height;}
};

struct VideoSettings {
    uint32_t width=0,height=0;
    bool fullscreen=false,scanlines=false,film=false;
    float scanStrength=.16f,lineCount=240,grainStrength=.04f,grainSize=1,vignette=.065f,flicker=.012f;
    UiLanguage language=UiLanguage::Russian;
    static constexpr size_t ByteLimit=256;
    static bool validResolution(uint32_t w,uint32_t h){
        return (!w&&!h)||(w>=320&&h>=240&&w<=16384&&h<=16384);
    }
    bool valid()const{
        const auto range=[](float value,float low,float high){return std::isfinite(value)&&value>=low&&value<=high;};
        return validUiLanguage(language)&&validResolution(width,height)&&range(scanStrength,0,.5f)&&range(lineCount,120,480)&&
            range(grainStrength,0,.15f)&&range(grainSize,.5f,4)&&range(vignette,0,.25f)&&range(flicker,0,.05f);
    }
    bool operator==(const VideoSettings& other)const{
        return width==other.width&&height==other.height&&fullscreen==other.fullscreen&&
            scanlines==other.scanlines&&film==other.film&&scanStrength==other.scanStrength&&lineCount==other.lineCount&&
            grainStrength==other.grainStrength&&grainSize==other.grainSize&&vignette==other.vignette&&flicker==other.flicker&&language==other.language;
    }
    savefile::Bytes serialize()const{
        if(!valid())throw std::runtime_error("Invalid video settings");
        statebytes::Writer out;out.text("EDVIDEO1");out.put<uint32_t>(3);
        out.put(width);out.put(height);out.flag(fullscreen);out.flag(scanlines);out.flag(film);
        for(float value:{scanStrength,lineCount,grainStrength,grainSize,vignette,flicker})out.put(value);
        out.put<uint8_t>(uint8_t(language));
        return std::move(out.bytes);
    }
    void restore(const savefile::Bytes& bytes){
        if(bytes.size()>ByteLimit)throw std::runtime_error("Video settings exceed byte limit");
        statebytes::Reader in{bytes};
        if(in.text()!="EDVIDEO1")throw std::runtime_error("Unsupported video settings");
        const auto version=in.get<uint32_t>();
        if(version!=1&&version!=2&&version!=3)throw std::runtime_error("Unsupported video settings");
        VideoSettings next;next.width=in.get<uint32_t>();next.height=in.get<uint32_t>();
        next.fullscreen=in.flag();next.scanlines=in.flag();next.film=in.flag();
        if(version>=2)for(float* value:{&next.scanStrength,&next.lineCount,&next.grainStrength,&next.grainSize,&next.vignette,&next.flicker})
            *value=in.get<float>();
        if(version>=3)next.language=UiLanguage(in.get<uint8_t>());
        in.end();
        if(!next.valid())throw std::runtime_error("Invalid saved video settings");
        *this=next;
    }
    void save(const std::filesystem::path& preferences)const{
        savefile::atomicWrite(preferences,"video.eds",serialize(),ByteLimit);
    }
    bool load(const std::filesystem::path& preferences){
        const auto directory=savefile::detail::directory(preferences);
        std::error_code error;const auto status=std::filesystem::symlink_status(directory/"video.eds",error);
        if(status.type()==std::filesystem::file_type::not_found)return false;
        if(error)throw std::runtime_error("Cannot inspect video settings");
        restore(savefile::read(directory,"video.eds",ByteLimit));return true;
    }
};
