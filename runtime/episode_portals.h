#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct EpisodePortal {
    uint32_t index=0,shape=0,mask=0,destinationId=0,camera=0,marker=0;
    std::string destination;
    std::vector<std::array<float,2>> points;
    std::array<float,3> rotationXYZ{},markerXYZ{};
    bool resolved=false;
};
struct EpisodePortals {
    std::string scene;
    std::array<float,2> gridOrigin{},gridExtent{};
    std::vector<EpisodePortal> portals;
};

namespace episode_portal_detail {
inline uint32_t sceneId(const std::string& name) {
    if(name.size()!=4||name[0]<'a'||name[0]>'z'||name[1]<'a'||name[1]>'z'||
       name[2]<'0'||name[2]>'9'||name[3]<'0'||name[3]>'9')throw std::runtime_error("Invalid episode scene name");
    return (uint32_t(name[0])<<24)|(uint32_t(name[1])<<16)|(uint32_t((name[2]-'0')*10+name[3]-'0')<<8);
}
class Reader {
    const std::vector<uint8_t>& bytes;
    size_t cursor=0;
    void range(size_t count) const {
        if(cursor>bytes.size()||count>bytes.size()-cursor)throw std::runtime_error("Truncated episode portal table");
    }
public:
    explicit Reader(const std::vector<uint8_t>& data):bytes(data) {
        if(data.size()>1024*1024)throw std::runtime_error("Excessive episode portal table size");
    }
    std::string text(size_t count) {range(count);std::string result(bytes.begin()+cursor,bytes.begin()+cursor+count);cursor+=count;return result;}
    uint32_t integer() {range(4);uint32_t result=0;for(unsigned b=0;b<4;++b)result|=uint32_t(bytes[cursor++])<<(8*b);return result;}
    float number() {
        const uint32_t bits=integer();float result;std::memcpy(&result,&bits,4);
        if(!std::isfinite(result))throw std::runtime_error("Non-finite episode portal coordinate");return result;
    }
    bool finished() const {return cursor==bytes.size();}
};
inline void geometry(const EpisodePortal& portal) {
    if(portal.shape==0) {
        if(portal.points[0][0]==portal.points[1][0]||portal.points[0][1]==portal.points[1][1])
            throw std::runtime_error("Degenerate episode portal rectangle");
        return;
    }
    for(size_t i=0;i<4;++i) {
        const auto& a=portal.points[i];const auto& b=portal.points[(i+1)%4];const auto& c=portal.points[(i+2)%4];
        const double cross=(double(b[0])-a[0])*(double(c[1])-b[1])-(double(b[1])-a[1])*(double(c[0])-b[0]);
        if(!(cross<0))throw std::runtime_error("Episode portal quad must be strictly convex and clockwise");
    }
}
} // namespace episode_portal_detail

inline EpisodePortals readEpisodePortals(const std::vector<uint8_t>& bytes) {
    using namespace episode_portal_detail;
    Reader input(bytes);
    if(input.text(8)!=std::string("EDPOR1\0\0",8))throw std::runtime_error("Invalid episode portal header");
    EpisodePortals result;result.scene=input.text(4);sceneId(result.scene);
    const uint32_t count=input.integer();if(count>1000)throw std::runtime_error("Excessive episode portal count");
    for(auto& value:result.gridOrigin)value=input.number();
    for(auto& value:result.gridExtent){value=input.number();if(value<=0)throw std::runtime_error("Invalid episode portal grid extent");}
    std::set<uint32_t> indices;
    for(uint32_t i=0;i<count;++i) {
        EpisodePortal portal;portal.index=input.integer();portal.shape=input.integer();const uint32_t points=input.integer();
        portal.mask=input.integer();portal.destinationId=input.integer();portal.camera=input.integer();portal.marker=input.integer();
        portal.destination=input.text(4);
        if(portal.index>=1000||!indices.insert(portal.index).second||portal.camera>255||portal.marker>255||
           portal.destinationId!=sceneId(portal.destination)||(portal.shape!=0&&portal.shape!=2)||
           points!=(portal.shape==0?2u:4u))throw std::runtime_error("Invalid episode portal identity or shape");
        for(uint32_t p=0;p<points;++p)portal.points.push_back({input.number(),input.number()});
        geometry(portal);
        for(auto& value:portal.rotationXYZ)value=input.number();
        const uint32_t resolved=input.integer();if(resolved>1)throw std::runtime_error("Invalid episode portal resolution flag");
        portal.resolved=resolved!=0;
        if(portal.resolved)for(auto& value:portal.markerXYZ)value=input.number();
        result.portals.push_back(std::move(portal));
    }
    if(!input.finished())throw std::runtime_error("Trailing episode portal data or unresolved arrival pose");
    return result;
}
