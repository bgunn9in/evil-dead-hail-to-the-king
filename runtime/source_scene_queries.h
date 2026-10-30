#pragma once
#include "script_scene.h"
#include <algorithm>

// Source417050/417140 and412DE0. These are native visibility volumes and
// point-segment collision queries, independent of the modern player capsule.
struct SourceSceneQueries {
    struct Volume {ScriptVolume geometry;uint32_t mask=0,flags=0,camera=0;};
    TriggerGrid grid;
    std::vector<Volume> volumes;
    SourceSceneQueries()=default;
    explicit SourceSceneQueries(const std::vector<uint8_t>& data) {
        const ScriptScene source(data);
        auto u32=[&](size_t offset) {
            if(offset>data.size()||data.size()-offset<4)throw std::runtime_error("Native query table exceeds scene");
            uint32_t value;std::memcpy(&value,data.data()+offset,4);return value;
        };
        auto f32=[&](size_t offset) {const auto bits=u32(offset);float value;std::memcpy(&value,&bits,4);
            if(!std::isfinite(value))throw std::runtime_error("Non-finite native query grid");return value;};
        const size_t floor=u32(4),floorSize=u32(0),table=u32(12);
        if(floor<88||floor>data.size()||floorSize>data.size()-floor||floorSize<64)
            throw std::runtime_error("Invalid native query floor");
        grid={f32(floor),f32(floor+8),f32(floor+28),f32(floor+32)};
        if(grid.extentX<=0||grid.extentZ<=0)throw std::runtime_error("Invalid native query grid extent");
        for(size_t i=0;i<source.volumes.size();++i) {
            const size_t offset=table+i*72;
            volumes.push_back({source.volumes[i],u32(offset+60),u32(offset+64),u32(offset+68)});
        }
    }
    static bool maskMatch(uint32_t a,uint32_t b) {
        const uint32_t match=a&b;return (match&0xffffu)&&(match&0xffff0000u);
    }
    uint32_t pointMask(GroundPoint position) const {return triggerMask(grid,{position.x,position.z});}
    // 41CF10/41D070 clamp to inclusive grid extents, retaining bin16 and x86
    // shift wrapping. The radius routine shifts its complete Z bitset afterward.
    uint32_t relativeMask(float x0,float z0,float x1,float z1,bool radiusRoutine) const {
        auto bin=[](float relative,float extent) {
            return unsigned(std::clamp(double(relative),0.0,double(extent))/extent*16.0);
        };
        unsigned a=bin(x0,grid.extentX),b=bin(x1,grid.extentX);
        unsigned c=bin(z0,grid.extentZ),d=bin(z1,grid.extentZ);
        if(a>b)std::swap(a,b);if(c>d)std::swap(c,d);
        uint32_t result=0,zbits=0;
        for(unsigned i=a;i<=b;++i)result|=1u<<(i&31);
        for(unsigned i=c;i<=d;++i) {
            if(radiusRoutine)zbits|=1u<<(i&31);else result|=1u<<((i+16)&31);
        }
        return result|(zbits<<16);
    }
    uint32_t extentMask(float x0,float z0,float x1,float z1,bool radiusRoutine=false) const {
        return relativeMask(float(double(x0)-grid.minX),float(double(z0)-grid.minZ),
                            float(double(x1)-grid.minX),float(double(z1)-grid.minZ),radiusRoutine);
    }
    uint32_t nativeExtentMask(float x0,float z0,float x1,float z1,bool radiusRoutine=false) const {
        const auto bin=[&](float world,float minimum,float extent){
            const float relative=std::clamp(world-minimum,0.f,extent);
            const float unit=radiusRoutine?relative/extent:relative*(1.f/extent);
            return unsigned(unit*16.f);
        };
        unsigned a=bin(x0,grid.minX,grid.extentX),b=bin(x1,grid.minX,grid.extentX);
        unsigned c=bin(z0,grid.minZ,grid.extentZ),d=bin(z1,grid.minZ,grid.extentZ);
        if(a>b)std::swap(a,b);if(c>d)std::swap(c,d);
        uint32_t result=0,zbits=0;
        for(unsigned i=a;i<=b;++i)result|=1u<<(i&31);
        for(unsigned i=c;i<=d;++i){if(radiusRoutine)zbits|=1u<<(i&31);else result|=1u<<((i+16)&31);}
        return result|(zbits<<16);
    }
    uint32_t nearMask(GroundPoint point) const {
        return relativeMask(float(double(point.x)-.75-grid.minX),float(double(point.z)-.75-grid.minZ),
                            float(double(point.x)+.75-grid.minX),float(double(point.z)+.75-grid.minZ),true);
    }
    bool visible(unsigned category,unsigned camera,GroundPoint position) const {
        const auto mask=pointMask(position);
        for(unsigned kind:{4u,3u})for(const auto& volume:volumes) {
            if(volume.geometry.kind!=kind||volume.camera!=camera||!(volume.flags&category)||!maskMatch(mask,volume.mask))continue;
            if(volume.geometry.contains(position))return kind==3;
        }
        return false;
    }
    static bool edgeCrossing(Vec2 a,Vec2 b,Vec2 c,Vec2 d) {
        const double rx=double(b.x)-a.x,rz=double(b.z)-a.z,sx=double(d.x)-c.x,sz=double(d.z)-c.z;
        const double determinant=rx*sz-rz*sx;
        if(determinant==0)return false; // 4223B0: collinear overlap is not a hit.
        const double qx=double(c.x)-a.x,qz=double(c.z)-a.z;
        const double t=(qx*sz-qz*sx)/determinant,u=(qx*rz-qz*rx)/determinant;
        return t>=0&&t<=1&&u>=0&&u<=1;
    }
    static bool segmentHits(const ScriptVolume& volume,Vec2 a,Vec2 b) {
        if(volume.shape==3) {
            const auto& center=volume.points[0];const auto& radiusVector=volume.points[1];
            double radius=std::sqrt(double(radiusVector.x)*radiusVector.x+double(radiusVector.y)*radiusVector.y+double(radiusVector.z)*radiusVector.z);
            if(radius==0)radius=1;
            const double dx=double(b.x)-a.x,dz=double(b.z)-a.z,ox=double(a.x)-center.x,oz=double(a.z)-center.z;
            const double aa=dx*dx+dz*dz;if(aa==0)return false;
            const double bb=2*(ox*dx+oz*dz),cc=ox*ox+oz*oz-radius*radius,disc=bb*bb-4*aa*cc;
            if(disc<0)return false;const double root=std::sqrt(disc);
            const double t0=(-bb-root)/(2*aa),t1=(-bb+root)/(2*aa);
            return (t0>0&&t0<1)||(t1>0&&t1<1); // 419D70: strict endpoints.
        }
        if(volume.shape>2)return false;
        std::vector<Vec2> polygon;
        if(volume.shape==0) {
            const auto& p=volume.points[0];const auto& q=volume.points[1];
            polygon={{p.x,p.z},{p.x,q.z},{q.x,q.z},{q.x,p.z}};
        } else for(const auto& point:volume.points)polygon.push_back({point.x,point.z});
        for(size_t i=0;i<polygon.size();++i)if(edgeCrossing(a,b,polygon[i],polygon[(i+1)%polygon.size()]))return true;
        return false;
    }
    bool lineOfSight(GroundPoint originWorld,GroundPoint targetLocal,unsigned collisionClass,unsigned camera,
                     const std::vector<bool>& enabled,bool visibilityActive=true) const {
        if(enabled.size()!=volumes.size())throw std::runtime_error("Native LOS activation table differs from source");
        // 412E2F..412E4D checks only low16 overlap, not both grid axes.
        if(visibilityActive&&(nearMask(originWorld)&nearMask(targetLocal)&0xffffu)&&visible(2,camera,originWorld))return true;
        if(originWorld.x==targetLocal.x&&originWorld.y==targetLocal.y&&originWorld.z==targetLocal.z)return true;
        const auto mask=extentMask(originWorld.x,originWorld.z,targetLocal.x,targetLocal.z);
        for(size_t i=0;i<volumes.size();++i) {
            const auto& volume=volumes[i];
            if(volume.geometry.kind!=2||!enabled[i]||!maskMatch(mask,volume.mask))continue;
            if((volume.flags&14)!=14&&((collisionClass==2&&!(volume.flags&4))||
               (collisionClass==4&&!(volume.flags&2))||(collisionClass==1&&!(volume.flags&8))))continue;
            if(segmentHits(volume.geometry,{originWorld.x,originWorld.z},{targetLocal.x,targetLocal.z}))return false;
        }
        return true;
    }
};
