#include "video_settings.h"
#undef NDEBUG
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <random>

template<class F>bool rejects(F action){try{action();return false;}catch(const std::exception&){return true;}}
static savefile::Bytes raw(const std::filesystem::path& path){
    std::ifstream input(path,std::ios::binary);return savefile::Bytes(std::istreambuf_iterator<char>(input),{});
}
int main(){
    VideoSettings defaults;assert(!defaults.width&&!defaults.height&&!defaults.fullscreen&&!defaults.scanlines&&!defaults.film);
    VideoSettings live{1920,1080,true,true,false};const auto good=live.serialize();VideoSettings restored;restored.restore(good);assert(restored==live);
    assert(VideoSettings::validResolution(320,240)&&VideoSettings::validResolution(16384,16384));
    for(auto pair:{VideoResolution{0,720},VideoResolution{1280,0},VideoResolution{319,240},
                   VideoResolution{320,239},VideoResolution{16385,1080},VideoResolution{1920,16385}}){
        VideoSettings invalid{pair.width,pair.height};assert(!invalid.valid()&&rejects([&]{invalid.serialize();}));
    }
    auto retained=[&](const savefile::Bytes& bytes){assert(rejects([&]{live.restore(bytes);}));assert(live.serialize()==good);};
    for(size_t length=0;length<good.size();++length)retained(savefile::Bytes(good.begin(),good.begin()+length));
    auto bad=good;bad.push_back(0);retained(bad);
    bad=good;bad[4]='?';retained(bad);
    bad=good;bad[12]=4;retained(bad);
    bad=good;bad.back()=2;retained(bad);
    for(size_t at=24;at<27;++at){bad=good;bad[at]=2;retained(bad);}
    auto legacy=good;legacy.resize(27);legacy[12]=1;
    restored.restore(legacy);assert(restored==live);
    auto previous=good;previous.resize(51);previous[12]=2;
    restored.restore(previous);assert(restored==live&&restored.language==UiLanguage::Russian);
    for(unsigned field=0;field<6;++field){
        for(float value:{-1.0f,10000.0f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
            bad=good;std::memcpy(bad.data()+27+field*4,&value,4);retained(bad);
        }
    }
    VideoSettings tuned=live;tuned.scanStrength=.3f;tuned.lineCount=360;tuned.grainStrength=.08f;
    tuned.grainSize=2;tuned.vignette=.1f;tuned.flicker=.02f;
    restored.restore(tuned.serialize());assert(restored==tuned&&!(restored==live));
    auto english=tuned;english.language=UiLanguage::English;
    restored.restore(english.serialize());assert(restored==english&&!(restored==tuned));
    previous=english.serialize();previous.resize(51);previous[12]=2;
    restored.restore(previous);assert(restored.language==UiLanguage::Russian&&restored.width==tuned.width&&
        restored.grainSize==tuned.grainSize&&restored.flicker==tuned.flicker&&restored.fullscreen==tuned.fullscreen);
    bad=good;std::fill(bad.begin()+16,bad.begin()+20,uint8_t(0));retained(bad);
    retained(savefile::Bytes(VideoSettings::ByteLimit+1,0));
    const auto directory=std::filesystem::temp_directory_path()/("evil-dead-video-settings-"+std::to_string(std::random_device{}()));
    assert(std::filesystem::create_directory(directory));
    assert(!live.load(directory)&&live.serialize()==good);
    live.save(directory);const auto original=raw(directory/"video.eds");
    VideoSettings loaded;assert(loaded.load(directory)&&loaded==live);
    VideoSettings invalid{0,720};assert(rejects([&]{invalid.save(directory);}));assert(raw(directory/"video.eds")==original);
    assert(rejects([&]{live.save(directory/"missing");}));assert(raw(directory/"video.eds")==original);
    auto corrupt=original;corrupt.back()^=1;{std::ofstream output(directory/"video.eds",std::ios::binary|std::ios::trunc);output.write(reinterpret_cast<const char*>(corrupt.data()),corrupt.size());}
    assert(rejects([&]{loaded.load(directory);}));assert(loaded==live);
    assert(std::filesystem::remove(directory/"video.eds")&&std::filesystem::remove(directory));
    std::cout<<"Video settings defaults/paired dimensions/caps/atomic restore/file CRC/save failure retained PASS\n";
}
