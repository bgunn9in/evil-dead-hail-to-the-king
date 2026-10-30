#pragma once
#include "actor_mesh.h"

// Native translucent geometry shares the same far-to-near stream as particles.
// Constant-depth foreground planes split that stream with per-fragment bands.
// shortcut: centroid order inside a band; split intersecting triangles if an earned scene still misorders them.
namespace ednativeorder {
enum class Kind:uint8_t {Actor,Object,Shadow,Particle};
struct Draw {Kind kind;unsigned batch,first,count;float depth;};
inline float triangleDepth(const std::array<float,16>& matrix,const edmesh::Vertex32* vertices){
    double z=0,w=0;bool behind=false;
    for(unsigned i=0;i<3;++i){const auto& v=vertices[i];
        const double clipZ=double(matrix[2])*v.x+double(matrix[6])*v.y+double(matrix[10])*v.z+matrix[14];
        const double clipW=double(matrix[3])*v.x+double(matrix[7])*v.y+double(matrix[11])*v.z+matrix[15];
        if(!std::isfinite(clipZ)||!std::isfinite(clipW))throw std::runtime_error("Non-finite native draw depth");
        z+=clipZ;w+=clipW;behind|=clipW<=0;
    }
    // GPU clipping still owns near-plane intersections. Put them last rather
    // than divide by zero or reject a triangle whose visible portion is valid.
    if(behind||w<=.000001)return -1;
    return float(std::clamp(z/w,-1.0,1.0));
}
inline void append(std::vector<Draw>& draws,Kind kind,unsigned batch,unsigned first,
                   const edmesh::Vertex32* vertices,unsigned count,const std::array<float,16>& matrix){
    if(count%3)throw std::runtime_error("Native ordering needs triangle lists");
    for(unsigned i=0;i<count;i+=3)draws.push_back({kind,batch,first+i,3,triangleDepth(matrix,vertices+i)});
}
inline void sortAndCoalesce(std::vector<Draw>& draws){
    std::stable_sort(draws.begin(),draws.end(),[](const Draw& a,const Draw& b){return a.depth>b.depth;});
    size_t written=0;
    for(const auto draw:draws){
        if(written&&draws[written-1].kind==draw.kind&&draws[written-1].batch==draw.batch&&
           draws[written-1].first+draws[written-1].count==draw.first)draws[written-1].count+=draw.count;
        else draws[written++]=draw;
    }
    draws.resize(written);
}
inline bool inBand(float depth,float nearExclusive,float farInclusive){return depth>nearExclusive&&depth<=farInclusive;}
}
