#pragma once
#include "actor_mesh.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace edobjects {
inline bool alphaBlended(int16_t type) {
    switch(type){case 48:case 53:case 133:case 134:case 135:case 143:case 144:case 146:case 161:return true;default:return false;}
}
struct Vertex {
    edmesh::Vertex32 geometry;
    uint32_t color=0xffffffff; // Original D3DCOLOR ARGB; raw bytes are BGRA.
    uint16_t part=0,reserved=0;
};
static_assert(sizeof(Vertex)==40,"Object source vertex ABI");
struct Transform {
    std::array<float,3> position{};
    std::array<int32_t,3> degrees{}; // Native objSetOrientation integer XYZ degrees.
    float floorHeight=0;
    uint16_t flags=0x0b; // bit2 adds original BV floor to localY; bit0 is visible.
};
namespace detail {
inline std::vector<uint8_t> read(const std::filesystem::path& path,size_t maximum) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);if(!input)throw std::runtime_error("Cannot open original object asset");
    const auto length=input.tellg();if(length<0||uint64_t(length)>maximum)throw std::runtime_error("Invalid object asset length");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));input.seekg(0);
    if(!bytes.empty()&&!input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size())))throw std::runtime_error("Truncated object asset");return bytes;
}
struct Reader {
    const std::vector<uint8_t>& data;size_t cursor=0;
    template<class T>T get(){if(cursor>data.size()||sizeof(T)>data.size()-cursor)throw std::runtime_error("Truncated original object record");
        T value;std::memcpy(&value,data.data()+cursor,sizeof(T));cursor+=sizeof(T);return value;}
    void magic(const char* expected){for(unsigned i=0;i<8;++i)if(get<uint8_t>()!=uint8_t(expected[i]))throw std::runtime_error("Invalid original object asset magic");}
};
using Matrix=std::array<float,9>;
inline Matrix multiply(const Matrix& a,const Matrix& b){Matrix result{};
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col){double value=0;
        for(unsigned k=0;k<3;++k)value+=double(a[row*3+k])*b[k*3+col];result[row*3+col]=float(value);}return result;}
inline Matrix rotation(const std::array<int32_t,3>& degrees){
    constexpr float radians=0.01745329238474369f;
    const float x=float(double(degrees[0])*radians),y=float(double(degrees[1])*radians),z=float(double(degrees[2])*radians);
    const float cx=float(std::cos(double(x))),sx=float(std::sin(double(x))),cy=float(std::cos(double(y))),sy=float(std::sin(double(y))),
                cz=float(std::cos(double(z))),sz=float(std::sin(double(z)));
    const Matrix rx{{1,0,0,0,cx,sx,0,-sx,cx}},ry{{cy,0,-sy,0,1,0,sy,0,cy}},rz{{cz,sz,0,-sz,cz,0,0,0,1}};
    return multiply(rz,multiply(ry,rx)); // Native423d0f..423d74: row-vector Rz*Ry*Rx.
}
inline std::array<float,3> rotate(const Matrix& matrix,const std::array<float,3>& value){std::array<float,3> result{};
    for(unsigned col=0;col<3;++col)result[col]=float(double(value[0])*matrix[col]+double(value[1])*matrix[3+col]+double(value[2])*matrix[6+col]);return result;}
inline void finite(const Vertex& vertex){const auto& g=vertex.geometry;
    for(float v:{g.x,g.y,g.z,g.nx,g.ny,g.nz,g.u,g.v})if(!std::isfinite(v))throw std::runtime_error("Non-finite original object vertex");}
}
class ObjectMesh {
public:
    uint32_t identity=0;
    std::vector<uint32_t> partFlags;
    std::vector<Vertex> vertices; // Original bytes;423a70 initializes render normals/color below.
    static ObjectMesh decode(const std::vector<uint8_t>& bytes){
        detail::Reader input{bytes};input.magic("EDOBJ1\0\0");ObjectMesh result;result.identity=input.get<uint32_t>();
        const auto parts=input.get<uint32_t>(),count=input.get<uint32_t>();
        if(!result.identity||!parts||parts>256||!count||count>1000000||count%3||
           uint64_t(input.cursor)+uint64_t(parts)*4+uint64_t(count)*sizeof(Vertex)!=bytes.size())throw std::runtime_error("Invalid object mesh counts");
        for(unsigned i=0;i<parts;++i)result.partFlags.push_back(input.get<uint32_t>());
        for(unsigned i=0;i<count;++i){auto vertex=input.get<Vertex>();detail::finite(vertex);
            if(vertex.part>=parts||vertex.reserved)throw std::runtime_error("Invalid object source part");
            if(i%3&&vertex.part!=result.vertices.back().part)throw std::runtime_error("Object triangle crosses source parts");result.vertices.push_back(vertex);}
        return result;
    }
    static ObjectMesh load(const std::filesystem::path& path){return decode(detail::read(path,40*1000000+1044));}
    std::vector<Vertex> drawVertices(const Transform& transform,const std::vector<bool>& visibleParts={}) const {
        if(!visibleParts.empty()&&visibleParts.size()!=partFlags.size())throw std::runtime_error("Object part visibility count mismatch");
        for(float n:transform.position)if(!std::isfinite(n))throw std::runtime_error("Non-finite object translation");
        if(!std::isfinite(transform.floorHeight))throw std::runtime_error("Non-finite object BV height");
        if(!(transform.flags&1))return {};
        auto position=transform.position;if(transform.flags&2)position[1]+=transform.floorHeight;
        const auto matrix=detail::rotation(transform.degrees);std::vector<Vertex> result;result.reserve(vertices.size());
        for(auto vertex:vertices){if(!visibleParts.empty()&&!visibleParts[vertex.part])continue;
            auto& v=vertex.geometry;vertex.color=0xffffffff; // Native423b1c..423b35 initializes source mesh render data.
            const auto point=detail::rotate(matrix,{{v.x,v.y,v.z}}),normal=detail::rotate(matrix,{{0,1,0}});
            v.x=point[0]+position[0];v.y=point[1]+position[1];v.z=point[2]+position[2];v.nx=normal[0];v.ny=normal[1];v.nz=normal[2];
            detail::finite(vertex);result.push_back(vertex);}
        return result;
    }
};
struct Texture {
    uint32_t identity=0,width=0,height=0;
    std::vector<uint8_t> rgba;
    static Texture decode(const std::vector<uint8_t>& bytes){detail::Reader input{bytes};input.magic("EDTEX1\0\0");Texture result;
        result.identity=input.get<uint32_t>();result.width=input.get<uint32_t>();result.height=input.get<uint32_t>();
        if(!result.identity||!result.width||!result.height||result.width>16384||result.height>16384||uint64_t(result.width)*result.height>16777216||
           uint64_t(input.cursor)+uint64_t(result.width)*result.height*4!=bytes.size())throw std::runtime_error("Invalid object texture dimensions");
        result.rgba.assign(bytes.begin()+input.cursor,bytes.end());return result;
    }
    static Texture load(const std::filesystem::path& path){return decode(detail::read(path,64*1024*1024+20));}
};
} // namespace edobjects
