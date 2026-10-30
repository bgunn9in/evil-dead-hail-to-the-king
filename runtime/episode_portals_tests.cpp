#include "episode_portals.h"
#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
void integer(std::vector<uint8_t>& data,uint32_t value) {for(unsigned b=0;b<4;++b)data.push_back(uint8_t(value>>(8*b)));}
void number(std::vector<uint8_t>& data,float value) {uint32_t bits;std::memcpy(&bits,&value,4);integer(data,bits);}
void patch(std::vector<uint8_t>& data,size_t offset,uint32_t value) {for(unsigned b=0;b<4;++b)data[offset+b]=uint8_t(value>>(8*b));}
std::vector<uint8_t> fixture(bool resolved=true,uint32_t index=3) {
    std::vector<uint8_t> data={'E','D','P','O','R','1',0,0,'a','a','0','1'};integer(data,1);
    for(float value:{0.f,0.f,10.f,10.f})number(data,value);
    for(uint32_t value:{index,0u,2u,0x10001u,0x62623200u,4u,2u})integer(data,value);
    data.insert(data.end(),{'b','b','5','0'});
    for(float value:{0.f,0.f,2.f,2.f,0.f,1.f,0.f})number(data,value);
    integer(data,resolved?1:0);
    if(resolved)for(float value:{3.f,7.f,5.f})number(data,value);
    return data;
}
void rejects(const std::vector<uint8_t>& data) {
    bool failed=false;try{(void)readEpisodePortals(data);}catch(const std::runtime_error&){failed=true;}assert(failed);
}
}

int main(int argc,char** argv) {
    const auto data=fixture();const auto parsed=readEpisodePortals(data);
    assert(parsed.scene=="aa01"&&parsed.gridExtent[0]==10&&parsed.portals.size()==1);
    const auto& portal=parsed.portals[0];
    assert(portal.index==3&&portal.shape==0&&portal.destination=="bb50"&&portal.destinationId==0x62623200);
    assert(portal.camera==4&&portal.marker==2&&portal.resolved&&portal.markerXYZ[1]==7&&portal.rotationXYZ[1]==1);
    assert(!readEpisodePortals(fixture(false)).portals[0].resolved);
    for(size_t length=0;length<data.size();++length)rejects(std::vector<uint8_t>(data.begin(),data.begin()+length));
    auto bad=data;bad.push_back(0);rejects(bad);
    bad=fixture(false);for(unsigned i=0;i<3;++i)number(bad,0);rejects(bad); // No pose may follow an unresolved record.
    for(const auto [offset,value]:std::vector<std::pair<size_t,uint32_t>>{{12,1001},{24,0},{32,1000},{36,1},{40,4},
            {48,0x62623300},{52,256},{56,256},{92,2},{64,0x7fc00000},{72,0}}) {
        bad=data;patch(bad,offset,value);rejects(bad);
    }
    bad=data;patch(bad,12,2);bad.insert(bad.end(),data.begin()+32,data.end());rejects(bad); // Duplicate source index.
    // A valid clockwise quad; a crossed or reversed polygon must fail.
    auto quad=fixture(false);quad.resize(64);patch(quad,36,2);patch(quad,40,4);
    for(float value:{0.f,0.f,0.f,2.f,2.f,2.f,2.f,0.f,0.f,1.f,0.f})number(quad,value);integer(quad,0);
    assert(readEpisodePortals(quad).portals[0].points.size()==4);
    bad=quad;patch(bad,72,0x40000000);patch(bad,76,0);rejects(bad);
    if(argc>1) {
        const std::filesystem::path root=argv[1];size_t count=0,resolved=0;
        std::ifstream manifest(root/"world.txt");std::string magic,initial,name;unsigned amount=0;
        manifest>>magic>>initial>>amount;assert(manifest&&magic=="EDWORLD1"&&amount>=8&&amount<=256);
        std::vector<std::string> names;for(unsigned i=0;i<amount;++i){manifest>>name;assert(manifest);names.push_back(name);}
        for(const auto& scene:names) {
            std::ifstream stream(root/scene/"portals.bin",std::ios::binary);assert(stream);
            const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)),{});
            const auto source=readEpisodePortals(bytes);assert(source.scene==scene);
            count+=source.portals.size();for(const auto& p:source.portals) {
                const bool available=std::find(names.begin(),names.end(),p.destination)!=names.end();assert(p.resolved==available);
                resolved+=p.resolved?1:0;
            }
        }
        assert(count>=30&&resolved>=23);
        if(names.size()==11)assert(count==39&&resolved==33);
    }
}
