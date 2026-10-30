#pragma once
#include "ground.h"
#include "triggers.h"
#include <array>
#include <cstring>
#include <cstdint>

// Source41D290 and41D1D0. The cache is the shared native46E61C, distinct
// from each actor's41D5A0 cached triangle. Misses retain this shared index.
struct SourceFloorQuery {
    using CoarseGrid=std::array<float,32*32>;
    TriggerGrid grid;
    float maxX=0,maxZ=0;
    std::vector<FloorTriangle> triangles;
    std::vector<uint32_t> masks;
    SourceFloorQuery()=default;
    explicit SourceFloorQuery(const std::vector<uint8_t>& scene) {
        auto range=[&](size_t offset,size_t bytes) {
            if(offset>scene.size()||bytes>scene.size()-offset)throw std::runtime_error("Native floor reference exceeds scene");
        };
        auto u32=[&](size_t offset) {range(offset,4);uint32_t value;std::memcpy(&value,scene.data()+offset,4);return value;};
        auto f32=[&](size_t offset) {const auto bits=u32(offset);float value;std::memcpy(&value,&bits,4);
            if(!std::isfinite(value))throw std::runtime_error("Non-finite native floor coordinate");return value;};
        range(0,88);const size_t size=u32(0),floor=u32(4);range(floor,size);
        if(floor<88||size<64)throw std::runtime_error("Invalid native floor header");
        grid={f32(floor),f32(floor+8),f32(floor+28),f32(floor+32)};
        maxX=f32(floor+12);maxZ=f32(floor+20);
        if(grid.extentX<=0||grid.extentZ<=0||maxX<grid.minX||maxZ<grid.minZ)
            throw std::runtime_error("Invalid native floor bounds");
        const size_t vertices=u32(floor+36),normals=u32(floor+40),count=u32(floor+44);
        if(vertices>100000||normals>100000||count>100000)throw std::runtime_error("Native floor exceeds bounded table sizes");
        const size_t vertexData=floor+64,normalData=vertexData+vertices*12,
            faceData=normalData+normals*12,maskData=faceData+count*8;
        if(maskData-floor>size||count*4>size-(maskData-floor))throw std::runtime_error("Native floor arrays exceed section");
        auto point=[&](unsigned index) {
            if(index>=vertices)throw std::runtime_error("Native floor vertex index exceeds table");
            const auto offset=vertexData+index*12;return GroundPoint{f32(offset),f32(offset+4),f32(offset+8)};
        };
        for(size_t i=0;i<count;++i) {
            uint16_t indices[4];std::memcpy(indices,scene.data()+faceData+i*8,8);
            triangles.push_back({point(indices[0]),point(indices[1]),point(indices[2])});masks.push_back(u32(maskData+i*4));
        }
    }
    static bool contains(const FloorTriangle& triangle,float x,float z) {
        const std::array<GroundPoint,3> p{{triangle.a,triangle.b,triangle.c}};
        // 421BC0's early bounds reject and three vertical extruded edge planes.
        if((x>p[0].x&&x>p[1].x&&x>p[2].x)||(x<p[0].x&&x<p[1].x&&x<p[2].x)||
           (z>p[0].z&&z>p[1].z&&z>p[2].z)||(z<p[0].z&&z<p[1].z&&z<p[2].z))return false;
        for(unsigned i=0;i<3;++i) {
            const auto a=p[i],b=p[(i+1)%3];
            const float ex=float(double(b.x)-a.x),ey=float(double(b.y)-a.y),ez=float(double(b.z)-a.z),
                raisedY=float(double(b.y)+1),rx=ex,ry=float(double(raisedY)-a.y),rz=ez;
            const float nx=float(double(rz)*ey-double(ry)*ez),
                ny=float(double(ez)*rx-double(rz)*ex),nz=float(double(ry)*ex-double(ey)*rx),
                dx=float(double(x)-a.x),dy=float(double(p[0].y)-a.y),dz=float(double(z)-a.z);
            if(!((double(dz)*nz+double(dy)*ny)+double(dx)*nx<0))return false;
        }
        return true;
    }
    static double interpolate(const FloorTriangle& t,float x,float z) {
        const double ax=double(t.a.x)-x,az=double(t.a.z)-z,bx=double(t.b.x)-x,bz=double(t.b.z)-z,
            cx=double(t.c.x)-x,cz=double(t.c.z)-z;
        // 41D419/427 store two signed areas asfloat; the third remains wide.
        const float wc=float(az*bx-ax*bz),wb=float(ax*cz-az*cx);
        const double wa=cx*bz-bx*cz,inverse=1/((wa+wb)+wc);
        return ((wa*inverse)*t.a.y+(inverse*wb)*t.b.y)+(inverse*wc)*t.c.y;
    }
    double height(float x,float ignoredY,float z,int32_t& sharedCache) const {
        (void)ignoredY;
        if(!std::isfinite(x)||!std::isfinite(z))throw std::runtime_error("Non-finite native floor query");
        if(triangles.size()!=masks.size())throw std::runtime_error("Native floor mask table differs");
        if(sharedCache< -1)throw std::runtime_error("Invalid native shared floor cache");
        if(triangles.empty()||x<grid.minX||x>maxX||z<grid.minZ||z>maxZ)return 0;
        const auto mask=triggerMask(grid,{x,z});
        // shortcut: native46E61C is never reset at scene entry and lacks a
        // range check. Skip its out-of-range fastpath instead of reading OOB;
        // retain the value on misses, preserving valid-cache behavior.
        if(sharedCache>=0&&size_t(sharedCache)<triangles.size()&&contains(triangles[size_t(sharedCache)],x,z))
            return interpolate(triangles[size_t(sharedCache)],x,z); // Cached fastpath bypasses mask.
        for(size_t i=0;i<triangles.size();++i) {
            const auto overlap=mask&masks[i];if(!(overlap&0xffffu)||!(overlap&0xffff0000u))continue;
            if(contains(triangles[i],x,z)){sharedCache=int32_t(i);return interpolate(triangles[i],x,z);}
        }
        return 0;
    }
    double actorHeight(float x,float ignoredY,float z,int32_t& actorCache,int32_t& sharedCache) const {
        (void)ignoredY;
        if(!std::isfinite(x)||!std::isfinite(z))throw std::runtime_error("Non-finite native actor floor query");
        if(triangles.size()!=masks.size())throw std::runtime_error("Native floor mask table differs");
        if(triangles.empty())return 0; // 41D5B4 empty BV leaves the actor cache unchanged.
        if(x<grid.minX||x>maxX||z<grid.minZ||z>maxZ){actorCache=-1;return 0;}
        const auto mask=triggerMask(grid,{x,z});
        const auto accepts=[&](size_t i) {const auto overlap=mask&masks[i];
            return (overlap&0xffffu)&&(overlap&0xffff0000u)&&contains(triangles[i],x,z);};
        // 41D68C validates the per-actor cache; unlike41D290 it checks masks.
        if(actorCache>=0&&size_t(actorCache)<triangles.size()&&accepts(size_t(actorCache)))
            return interpolate(triangles[size_t(actorCache)],x,z);
        for(size_t i=0;i<triangles.size();++i)if(accepts(i)) {
            actorCache=int32_t(i);sharedCache=int32_t(i);return interpolate(triangles[i],x,z);
        }
        actorCache=-1;return 0; // 41D815 miss resets only this actor's cache.
    }
    CoarseGrid coarseGrid(int32_t& sharedCache) const {
        CoarseGrid result{};
        for(unsigned x=0;x<32;++x)for(unsigned z=0;z<32;++z) {
            const float px=float(double(x)*grid.extentX*.03125+grid.minX),
                pz=float(double(z)*grid.extentZ*.03125+grid.minZ);
            result[x*32+z]=float(height(px,32767,pz,sharedCache));
        }
        return result;
    }
    float coarseHeight(float x,float z,const CoarseGrid& cells) const {
        const float scaleX=float(32.0/grid.extentX),scaleZ=float(32.0/grid.extentZ);
        auto bin=[](double value) {
            if(!std::isfinite(value)||value<std::numeric_limits<int32_t>::min()||value>std::numeric_limits<int32_t>::max())
                throw std::runtime_error("Native coarse floor index exceeds integer range");
            const auto index=int32_t(value);return uint32_t(index)<32?unsigned(index):31u;
        };
        const unsigned ix=bin((double(x)-grid.minX)*scaleX),iz=bin((double(z)-grid.minZ)*scaleZ);
        const float value=cells[ix*32+iz];
        if(!std::isfinite(value))throw std::runtime_error("Non-finite native coarse floor height");return value;
    }
    int16_t coarseCell(float x,float z,const CoarseGrid& cells) const {
        const double value=coarseHeight(x,z,cells);
        if(!std::isfinite(value)||value<std::numeric_limits<int32_t>::min()||value>std::numeric_limits<int32_t>::max())
            throw std::runtime_error("Native coarse floor height exceeds integer range");
        const uint16_t bits=uint16_t(uint32_t(int32_t(value)));int16_t result;std::memcpy(&result,&bits,2);return result;
    }
};
