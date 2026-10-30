#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace edmesh {
struct Vertex32 {float x=0,y=0,z=0,nx=0,ny=0,nz=0,u=0,v=0;};
static_assert(sizeof(Vertex32)==32,"Vertex32 must match the renderer vertex layout");
struct Vertex {Vertex32 value;uint16_t bone=0,part=0;};
struct Bone {std::array<double,16> bind{};std::array<double,3> translation{};int32_t parent=-1;};
struct Clip {uint32_t id=0,flags=0,frameCount=0;std::vector<float> curves;};
struct LocalPose {float rootY=0;std::vector<std::array<float,3>> rotationsDegrees;};

namespace detail {
using Matrix=std::array<double,16>;
inline Matrix multiply(const Matrix& a,const Matrix& b) {
    Matrix out{};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)for(unsigned k=0;k<4;++k)out[r*4+c]+=a[r*4+k]*b[k*4+c];
    return out;
}
inline Matrix rotation(const std::array<float,3>& degrees) {
    constexpr double radians=3.14159265358979323846/180;
    for(float value:degrees)if(!std::isfinite(value))throw std::runtime_error("Non-finite local actor rotation");
    const double x=degrees[0]*radians,y=degrees[1]*radians,z=degrees[2]*radians;
    const double cx=std::cos(x),sx=std::sin(x),cy=std::cos(y),sy=std::sin(y),cz=std::cos(z),sz=std::sin(z);
    const Matrix rx{{1,0,0,0,0,cx,sx,0,0,-sx,cx,0,0,0,0,1}};
    const Matrix ry{{cy,0,-sy,0,0,1,0,0,sy,0,cy,0,0,0,0,1}};
    const Matrix rz{{cz,sz,0,0,-sz,cz,0,0,0,0,1,0,0,0,0,1}};
    return multiply(multiply(rx,ry),rz);
}
class Reader {
    const std::vector<uint8_t>& data;size_t cursor=0;
public:
    explicit Reader(const std::vector<uint8_t>& bytes):data(bytes) {
        if(bytes.size()>128ull*1024*1024)throw std::runtime_error("Excessive compact actor size");
    }
    size_t remaining() const {return data.size()-cursor;}
    void range(size_t bytes) const {if(bytes>remaining())throw std::runtime_error("Truncated compact actor record");}
    uint16_t u16() {range(2);const uint16_t a=data[cursor++];return uint16_t(a|(uint16_t(data[cursor++])<<8));}
    uint32_t u32() {const uint32_t a=u16();return a|(uint32_t(u16())<<16);}
    int32_t i32() {const uint32_t value=u32();int32_t result;std::memcpy(&result,&value,4);return result;}
    float number() {
        const uint32_t bits=u32();float value;std::memcpy(&value,&bits,4);
        if(!std::isfinite(value))throw std::runtime_error("Non-finite compact actor value");return value;
    }
    std::string text(size_t bytes) {range(bytes);std::string out(data.begin()+cursor,data.begin()+cursor+bytes);cursor+=bytes;return out;}
};
} // namespace detail

class ActorMesh {
    size_t offset(const Clip& source,unsigned frame) const {
        if(frame>=source.frameCount)throw std::runtime_error("Actor curve frame outside source clip");
        return size_t(frame)*(3+bones.size()*3);
    }
public:
    std::vector<Vertex> vertices;
    std::vector<Bone> bones;
    std::map<uint32_t,Clip> clips;
    unsigned partCount=0;
    static ActorMesh load(const std::filesystem::path& path);
    const Clip& clip(uint32_t id) const {
        const auto found=clips.find(id);if(found==clips.end())throw std::runtime_error("Missing original actor clip");return found->second;
    }
    std::array<float,3> rootPosition(uint32_t id,unsigned frame) const {
        const auto& source=clip(id);const size_t start=offset(source,frame);
        return {{source.curves[start],source.curves[start+1],source.curves[start+2]}};
    }
    LocalPose restPose() const {
        if(bones.empty())throw std::runtime_error("Actor skeleton is empty");
        return {float(bones[0].translation[1]),std::vector<std::array<float,3>>(bones.size())};
    }
    void applyPose(LocalPose& pose,uint32_t id,unsigned frame,const std::vector<uint8_t>& mask={}) const {
        if(pose.rotationsDegrees.size()!=bones.size()||(!mask.empty()&&mask.size()!=bones.size()))
            throw std::runtime_error("Actor local pose or mask has the wrong bone count");
        const auto& source=clip(id);const size_t start=offset(source,frame);
        if(mask.empty()||mask[0])pose.rootY=source.curves[start+1];
        for(size_t bone=0;bone<bones.size();++bone)if(mask.empty()||mask[bone])
            for(unsigned axis=0;axis<3;++axis)pose.rotationsDegrees[bone][axis]=source.curves[start+3+bone*3+axis];
    }
    std::vector<detail::Matrix> globalMatrices(const LocalPose& pose) const {
        if(pose.rotationsDegrees.size()!=bones.size()||!std::isfinite(pose.rootY))throw std::runtime_error("Invalid actor local pose");
        std::vector<detail::Matrix> global;global.reserve(bones.size());
        for(size_t index=0;index<bones.size();++index) {
            const auto& bone=bones[index];auto matrix=detail::multiply(detail::rotation(pose.rotationsDegrees[index]),bone.bind);
            for(unsigned axis=0;axis<3;++axis)matrix[12+axis]=(index==0&&axis==1)?pose.rootY:bone.translation[axis];
            if(bone.parent>=0)matrix=detail::multiply(matrix,global.at(size_t(bone.parent)));
            global.push_back(matrix);
        }
        return global;
    }
    std::array<float,3> jointPosition(const LocalPose& pose,unsigned joint) const {
        if(joint>=bones.size())throw std::runtime_error("Actor joint is outside source skeleton");
        const auto matrix=globalMatrices(pose).at(joint);std::array<float,3> point{};
        for(unsigned axis=0;axis<3;++axis){point[axis]=float(matrix[12+axis]);if(!std::isfinite(point[axis]))throw std::runtime_error("Non-finite source actor joint");}
        return point; // Native41f740 raw MODEL global translation, before actor MirrorX/world matrix.
    }
    std::vector<Vertex32> skinPose(const LocalPose& pose,const std::vector<bool>& visibleParts={}) const {
        if(!visibleParts.empty()&&visibleParts.size()!=partCount)throw std::runtime_error("Actor visibility table does not match source parts");
        const auto global=globalMatrices(pose);
        std::vector<Vertex32> out;out.reserve(vertices.size());
        for(const auto& source:vertices) {
            if(!visibleParts.empty()&&!visibleParts[source.part])continue;
            const auto& m=global.at(source.bone);const auto& v=source.value;
            const std::array<double,3> position{{v.x,v.y,v.z}},normal{{v.nx,v.ny,v.nz}};
            std::array<double,3> p{},n{};
            for(unsigned axis=0;axis<3;++axis) {
                p[axis]=m[12+axis];for(unsigned k=0;k<3;++k){p[axis]+=position[k]*m[k*4+axis];n[axis]+=normal[k]*m[k*4+axis];}
            }
            const double length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
            if(length)for(auto& value:n)value/=length;
            Vertex32 value{float(p[0]),float(p[1]),float(p[2]),float(n[0]),float(n[1]),float(n[2]),v.u,v.v};
            for(float component:{value.x,value.y,value.z,value.nx,value.ny,value.nz})
                if(!std::isfinite(component))throw std::runtime_error("Actor pose exceeds finite vertex coordinates");
            out.push_back(value);
        }
        return out;
    }
    std::vector<Vertex32> samplePose(uint32_t id,unsigned curveFrame) const {
        auto pose=restPose();applyPose(pose,id,curveFrame);return skinPose(pose);
    }
};

inline ActorMesh readActorMesh(const std::vector<uint8_t>& data) {
    detail::Reader input(data);
    const auto magic=input.text(8);const bool tagged=magic=="EDACTOR2";
    if(magic!="EDACTOR1"&&!tagged)throw std::runtime_error("Invalid compact actor header");
    const auto vertices=input.u32(),bones=input.u32(),clips=input.u32();
    if(!vertices||vertices>1000000||vertices%3||!bones||bones>256||!clips||clips>4096)
        throw std::runtime_error("Invalid compact actor counts");
    input.range(size_t(vertices)*36+size_t(bones)*80+size_t(clips)*12);
    ActorMesh result;result.vertices.reserve(vertices);result.bones.reserve(bones);
    for(uint32_t i=0;i<vertices;++i) {
        Vertex v;v.value={input.number(),input.number(),input.number(),input.number(),input.number(),input.number(),input.number(),input.number()};
        v.bone=input.u16();v.part=input.u16();
        if(v.bone>=bones||v.part>=256||(!tagged&&v.part)||(i%3&&v.part!=result.vertices[i-i%3].part))
            throw std::runtime_error("Invalid compact actor bone, part or triangle visibility");
        result.partCount=std::max(result.partCount,unsigned(v.part)+1);
        result.vertices.push_back(v);
    }
    for(uint32_t i=0;i<bones;++i) {
        Bone bone;for(auto& value:bone.bind)value=input.number();for(auto& value:bone.translation)value=input.number();bone.parent=input.i32();
        if(bone.parent< -1||bone.parent>=int32_t(i))throw std::runtime_error("Actor skeleton parent must precede its child");
        for(const auto [index,expected]:std::array<std::pair<unsigned,double>,4>{{{3,0},{7,0},{11,0},{15,1}}})
            if(std::abs(bone.bind[index]-expected)>1e-5)throw std::runtime_error("Compact actor skeleton is not affine");
        result.bones.push_back(bone);
    }
    for(uint32_t i=0;i<clips;++i) {
        Clip clip;clip.id=input.u32();clip.flags=input.u32();clip.frameCount=input.u32();
        if(!clip.id||!clip.frameCount||clip.frameCount>65535||result.clips.count(clip.id))throw std::runtime_error("Invalid compact actor clip identity or frame count");
        const size_t samples=size_t(clip.frameCount)*(3+size_t(bones)*3);input.range(samples*4);clip.curves.reserve(samples);
        for(size_t sample=0;sample<samples;++sample)clip.curves.push_back(input.number());
        result.clips.emplace(clip.id,std::move(clip));
    }
    if(input.remaining())throw std::runtime_error("Trailing compact actor data");return result;
}

inline ActorMesh ActorMesh::load(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);if(!input)throw std::runtime_error("Cannot open compact actor: "+path.string());
    const auto length=input.tellg();if(length<20||uint64_t(length)>128ull*1024*1024)throw std::runtime_error("Invalid compact actor file length");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));input.seekg(0);
    if(!input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size())))throw std::runtime_error("Cannot read complete compact actor");
    return readActorMesh(bytes);
}
} // namespace edmesh
