#pragma once
#include "object_mesh.h"
#include "source_actor_math.h"

// Original DC085580/0827a0 default render state only. Optional light-projection mode1 is not enabled.
namespace ednativevisual {
enum class Blend:uint8_t {Alpha,Additive,InverseAdditive,DestinationInverse};
struct Material {Blend blend=Blend::Alpha;float alpha=1;bool white=false,opaque=false;};
inline bool specialObject(int16_t type){return type==48||type==134||type==135||type==143||type==144||type==146;}
inline Material material(uint8_t mode,bool special=false){
    if(special)return {Blend::Alpha,.5f,true};
    switch(mode){case 0:return {Blend::Alpha,.5f,false};case 1:return {Blend::Additive,1,false};
        case 2:return {Blend::InverseAdditive,1,false};case 3:return {Blend::Additive,.5f,false};default:return {};}
}
inline Material actorMaterial(uint8_t mode,uint32_t color){
    switch(mode){case 0:return {Blend::Alpha,.5f};case 1:return {Blend::Additive,1};
        case 2:return {Blend::DestinationInverse,0};case 3:return {Blend::Additive,.5f};
        case 4:{ // Native083b92..bd6 uses RAW postfade bytes, not normalized RGB.
            const float red=float((color>>16)&255),green=float((color>>8)&255),blue=float(color&255);
            const float alpha=(green*.5870000123977661f+red*.29899999499320984f)+blue*.11400000005960464f;
            return {Blend::Alpha,std::clamp(alpha,0.f,1.f)};}
        default:return {Blend::Alpha,1,false,mode==255}; // Only255 preserves the opaque basecontext; other modes use standard alpha.
    }
}
inline std::vector<edmesh::Vertex32> clipAboveFloor(const std::vector<edmesh::Vertex32>& source,float floor){
    if(source.size()%3||!std::isfinite(floor))throw std::runtime_error("Finite actor floor and triangles required");
    std::vector<edmesh::Vertex32> result;result.reserve(source.size());
    for(size_t first=0;first<source.size();first+=3){
        std::array<edmesh::Vertex32,4> polygon{};unsigned count=0;
        for(unsigned i=0;i<3;++i){
            const auto& a=source[first+i];const auto& b=source[first+(i+1)%3];
            if(!std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(a.z))throw std::runtime_error("Non-finite actor floor vertex");
            const bool inside=a.y>=floor,next=b.y>=floor;
            if(inside)polygon[count++]=a;
            if(inside!=next&&a.y!=floor&&b.y!=floor){
                const float t=(floor-a.y)/(b.y-a.y);const auto lerp=[&](float x,float y){return x+(y-x)*t;};
                polygon[count++]={lerp(a.x,b.x),floor,lerp(a.z,b.z),lerp(a.nx,b.nx),lerp(a.ny,b.ny),lerp(a.nz,b.nz),lerp(a.u,b.u),lerp(a.v,b.v)};
            }
        }
        for(unsigned i=1;i+1<count;++i){result.push_back(polygon[0]);result.push_back(polygon[i]);result.push_back(polygon[i+1]);}
    }
    return result;
}
inline std::vector<edmesh::Vertex32> objectShadow(const edobjects::ObjectMesh& mesh,const edobjects::Transform& transform){
    if(!(transform.flags&1))return {};for(float value:transform.position)if(!std::isfinite(value))throw std::runtime_error("Non-finite shadow position");
    if(!std::isfinite(transform.floorHeight))throw std::runtime_error("Non-finite shadow floor");
    const auto rotation=edobjects::detail::rotation(transform.degrees);auto position=transform.position;if(transform.flags&2)position[1]+=transform.floorHeight;
    std::vector<edmesh::Vertex32> result;result.reserve(mesh.vertices.size());
    for(const auto& source:mesh.vertices){const auto local=edobjects::detail::rotate(rotation,{{source.geometry.x,-transform.position[1],source.geometry.z}});
        edmesh::Vertex32 out{};out.x=local[0]*1.2000000476837158f+position[0];out.y=local[1]+position[1];
        out.z=local[2]*1.2000000476837158f+position[2];result.push_back(out);}
    return result; // Y is flattened BEFORE rotation; pitched/rolled original props retain the native tilted plane.
}
inline std::vector<edmesh::Vertex32> actorShadow(const std::vector<edmesh::Vertex32>& skinned,float localY,
        const std::array<float,3>& position,float yaw){
    if(!std::isfinite(localY))throw std::runtime_error("Non-finite actor shadow localY");std::vector<edmesh::Vertex32> result;result.reserve(skinned.size());
    for(const auto& source:skinned){const auto point=edactormath::jointWorld({{0,yaw,0}},{{source.x,-localY,source.z}},position);
        edmesh::Vertex32 out{};out.x=point[0];out.y=point[1];out.z=point[2];result.push_back(out);}
    return result; // POSTskin selected body/held parts, then existing MirrorX/yaw/world. No1.2 scale for actors.
}
inline void appendProjectedShadow(std::vector<edmesh::Vertex32>& target,const std::vector<edmesh::Vertex32>& source,
        const std::array<float,3>& eye,const std::array<float,3>& right,const std::array<float,3>& up){
    if(source.size()%3)throw std::runtime_error("Native shadow triangles required");
    for(unsigned axis=0;axis<3;++axis)if(!std::isfinite(eye[axis])||!std::isfinite(right[axis])||!std::isfinite(up[axis]))throw std::runtime_error("Non-finite native shadow camera");
    std::array<float,3> forward{{right[1]*up[2]-right[2]*up[1],right[2]*up[0]-right[0]*up[2],right[0]*up[1]-right[1]*up[0]}};
    const float length=std::sqrt(forward[0]*forward[0]+forward[1]*forward[1]+forward[2]*forward[2]);
    if(length<.999f||length>1.001f)throw std::runtime_error("Native shadow camera axes required");for(auto& value:forward)value/=length;
    for(size_t triangle=0;triangle<source.size();triangle+=3){std::array<edmesh::Vertex32,3> vertices{};bool keep=true;
        for(unsigned corner=0;corner<3;++corner){auto vertex=source[triangle+corner];const float point[]={vertex.x,vertex.y,vertex.z};float z=0;
            for(unsigned axis=0;axis<3;++axis){if(!std::isfinite(point[axis])||!std::isfinite(eye[axis]))throw std::runtime_error("Non-finite native shadow vertex");z+=(point[axis]-eye[axis])*forward[axis];}
            const float nativeZ=z<1?1:z*1.0000200271606445f;
            if(!corner&&1/nativeZ<.009999999776482582f){keep=false;break;} // Native082fba..fc6 tests FIRST projected vertex only.
            vertex.x+=forward[0]*(nativeZ-z);vertex.y+=forward[1]*(nativeZ-z);vertex.z+=forward[2]*(nativeZ-z);vertices[corner]=vertex;
        }
        if(keep)target.insert(target.end(),vertices.begin(),vertices.end());
    }
}
}
