#include "audio_spatial.h"
#undef NDEBUG
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>
using Point=std::array<float,3>;
template<class F> static bool rejects(F action){try{action();return false;}catch(const std::exception&){return true;}}
int main(int argc,char** argv){try{
 using edaudiospatial::Percent;const Point zero{{0,0,0}};
 assert(Percent({{0,1000,0}},{{3,-1000,4}},100,4,0)==75);
 assert(Percent(zero,{{3,0,4}},100,12,10)==50);
 assert(Percent(zero,{{0,0,10}},100,4,1)==50);
 assert(Percent(zero,{{1000,0,1000}},100,0,0)==100);
 assert(Percent(zero,{{0,0,10}},100,12,10)==0&&Percent(zero,{{0,0,20}},100,4,0)==0);
 assert(Percent(zero,{{0,0,1}},50,12,4)==37);
 assert(Percent(zero,zero,100,0x4004,0)==0);
 assert(Percent(zero,{{0,0,1}},100,12,0)==0);
 assert(rejects([&]{Percent(zero,zero,100,12,0);}));
 assert(rejects([&]{Percent({{std::numeric_limits<float>::quiet_NaN(),0,0}},zero,100,4,0);}));
 if(argc!=2)throw std::runtime_error("Expected original exported A asset root");
 const auto file=std::filesystem::u8path(argv[1])/"ak01/scene.native";
 std::ifstream in(file,std::ios::binary|std::ios::ate);const auto length=in.tellg();
 if(length<88||length>1024*1024)throw std::runtime_error("Invalid AK01 native scene");
 std::vector<uint8_t> data(size_t(length),0);in.seekg(0);if(!in.read(reinterpret_cast<char*>(data.data()),length))throw std::runtime_error("Truncated AK01 source");
 auto u32=[&](size_t offset){if(offset>data.size()||data.size()-offset<4)throw std::runtime_error("Source header bounds");
     return uint32_t(data[offset])|(uint32_t(data[offset+1])<<8)|(uint32_t(data[offset+2])<<16)|(uint32_t(data[offset+3])<<24);};
 auto point=[&](size_t offset){if(offset>data.size()||data.size()-offset<12)throw std::runtime_error("Source point bounds");Point value{};std::memcpy(value.data(),data.data()+offset,12);return value;};
 assert(u32(0x30)>23&&u32(0x48)>58);const auto listener=point(u32(0x34)+23*44+8);
 const auto markers=u32(0x4c);const int expected[8]={0,34,0,0,0,0,0,0};
 for(unsigned n=0;n<8;++n){const int requested=n<4||n==5?50:30;assert(Percent(listener,point(markers+(51+n)*12),requested,12,4)==expected[n]);}
 assert(Percent(listener,point(markers+15*12),68,12,8)==6);
 std::cout<<"Original430190 XZ/radius/percent/mute boundaries and actual AK01 nine-loop goldens PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
