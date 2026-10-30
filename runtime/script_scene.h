#pragma once
#include "doors.h"
#include <cstring>

struct ScriptVolume {
    unsigned kind=0,shape=6;
    std::vector<GroundPoint> points;
    bool contains(GroundPoint position) const {
        if(!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z))
            throw std::runtime_error("Non-finite script actor position");
        if(shape==0) {
            const auto& a=points[0];const auto& b=points[1];
            return position.x>=std::min(a.x,b.x)&&position.x<=std::max(a.x,b.x)&&
                position.z>=std::min(a.z,b.z)&&position.z<=std::max(a.z,b.z);
        }
        if(shape==5) {
            const auto& a=points[0];const auto& b=points[1];
            return position.x>=a.x&&position.x<=b.x&&position.y>=a.y&&position.y<=b.y&&position.z>=a.z&&position.z<=b.z;
        }
        if(shape==1||shape==2) {
            for(size_t i=0;i<points.size();++i) {
                const auto& a=points[i];const auto& b=points[(i+1)%points.size()];
                if((double(b.x)-a.x)*(double(position.z)-a.z)-
                   (double(b.z)-a.z)*(double(position.x)-a.x)>=0)return false;
            }
            return true;
        }
        if(shape==3||shape==4) {
            const auto& c=points[0];const auto& r=points[1];
            const float x=float(double(position.x)-c.x),y=float(double(position.y)-c.y),z=float(double(position.z)-c.z);
            const float squared=float((double(x)*x+double(y)*y)+double(z)*z);
            return (double(r.x)*r.x+double(r.y)*r.y)+double(r.z)*r.z>=squared;
        }
        return false;
    }
};

struct ScriptScene {
    std::vector<ScriptVolume> volumes;
    std::vector<GroundPoint> markers;
    ScriptScene()=default;
    explicit ScriptScene(const std::vector<uint8_t>& data) {
        auto range=[&](size_t offset,size_t bytes) {
            if(offset>data.size()||bytes>data.size()-offset)throw std::runtime_error("Native scene reference exceeds resource");
        };
        auto integer=[&](size_t offset) {
            range(offset,4);return uint32_t(data[offset])|(uint32_t(data[offset+1])<<8)|
                (uint32_t(data[offset+2])<<16)|(uint32_t(data[offset+3])<<24);
        };
        auto point=[&](size_t offset) {
            float values[3];range(offset,12);std::memcpy(values,data.data()+offset,12);
            for(float value:values)if(!std::isfinite(value))throw std::runtime_error("Non-finite native scene coordinate");
            return GroundPoint{values[0],values[1],values[2]};
        };
        range(0,88);
        const size_t volumeCount=integer(8),volumeOffset=integer(12),markerCount=integer(72),markerOffset=integer(76);
        if(volumeCount>100000||markerCount>100000||(volumeCount&&volumeOffset<88)||(markerCount&&markerOffset<88))
            throw std::runtime_error("Invalid native scene table");
        range(volumeOffset,volumeCount*72);range(markerOffset,markerCount*12);
        for(size_t i=0;i<volumeCount;++i) {
            const size_t offset=volumeOffset+i*72;ScriptVolume volume;
            volume.kind=data[offset];volume.shape=data[offset+1];
            const unsigned count=volume.shape==1?3:volume.shape==2?4:volume.shape<=5?2:0;
            for(unsigned p=0;p<count;++p)volume.points.push_back(point(offset+4+p*12));
            volumes.push_back(std::move(volume));
        }
        for(size_t i=0;i<markerCount;++i)markers.push_back(point(markerOffset+i*12));
    }
    bool inside(unsigned index,GroundPoint position) const {
        if(index>=volumes.size())throw std::runtime_error("Script interaction volume is missing");
        return volumes[index].contains(position);
    }
    bool facing(unsigned index,float angle,Vec2 position,float yaw) const {
        if(index>=markers.size())throw std::runtime_error("Script facing marker is missing");
        Door target;target.marker=markers[index];target.facingArg=angle;
        return facingDoor(target,position,yaw);
    }
};
