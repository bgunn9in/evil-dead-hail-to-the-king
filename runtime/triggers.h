#pragma once
#include "input.h"
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

struct CameraRule {
    unsigned event=1;
    bool cameraEvent=false;
    uint32_t cameraPayload=0;
    std::vector<unsigned> cameras;
};
struct CameraTrigger {
    unsigned shape = 0;
    std::vector<Vec2> points;
    std::vector<unsigned> cameras;
    uint32_t mask = 0xffffffffu;
    std::vector<unsigned> exitCameras;
    std::vector<CameraRule> rules;
};
struct TriggerGrid { float minX = 0, minZ = 0, extentX = 16, extentZ = 16; };
struct CameraTriggers { TriggerGrid grid;std::vector<CameraTrigger> triggers; };
inline CameraTriggers readCameraTriggers(const std::vector<uint8_t>& data,unsigned cameraCount) {
    const bool version2=data.size()>=28&&!std::memcmp(data.data(),"EDTRIG2\0",8);
    const bool version3=data.size()>=28&&!std::memcmp(data.data(),"EDTRIG3\0",8);
    if(data.size()<28||(!version2&&!version3&&std::memcmp(data.data(),"EDTRIG1\0",8)))
        throw std::runtime_error("Invalid trigger header");
    size_t offset=8;
    auto integer=[&]() {if(offset>data.size()||data.size()-offset<4)throw std::runtime_error("Truncated camera trigger");
        uint32_t value;std::memcpy(&value,data.data()+offset,4);offset+=4;return value;};
    auto number=[&]() {const auto bits=integer();float value;std::memcpy(&value,&bits,4);
        if(!std::isfinite(value))throw std::runtime_error("Non-finite camera trigger coordinate");return value;};
    const unsigned count=integer();CameraTriggers result;
    result.grid={number(),number(),number(),number()};
    if(count>10000||result.grid.extentX<=0||result.grid.extentZ<=0||!cameraCount||cameraCount>256)
        throw std::runtime_error("Invalid trigger count/grid/cameras");
    for(unsigned i=0;i<count;++i) {
        CameraTrigger trigger;trigger.shape=integer();const unsigned points=integer(),actions=integer();
        const unsigned exitActions=version2?integer():0;trigger.mask=integer();
        if(trigger.shape>2||points!=(trigger.shape==0?2u:trigger.shape==1?3u:4u)||actions>256||exitActions>256)
            throw std::runtime_error("Invalid camera trigger");
        for(unsigned p=0;p<points;++p){const float x=number(),z=number();trigger.points.push_back({x,z});}
        if(trigger.shape==0&&(trigger.points[0].x>trigger.points[1].x||trigger.points[0].z>trigger.points[1].z))
            throw std::runtime_error("Invalid rectangular trigger bounds");
        if(version3&&trigger.shape!=0)for(unsigned p=0;p<points;++p) {
            const Vec2 a=trigger.points[p],b=trigger.points[(p+1)%points],c=trigger.points[(p+2)%points];
            if((double(b.x)-a.x)*(double(c.z)-b.z)-(double(b.z)-a.z)*(double(c.x)-b.x)>=0)
                throw std::runtime_error("Invalid clockwise camera polygon");
        }
        auto cameras=[&](unsigned amount,std::vector<unsigned>& destination) {
            for(unsigned a=0;a<amount;++a){const unsigned camera=integer();
                if(camera>=cameraCount)throw std::runtime_error("Trigger camera is missing");destination.push_back(camera);}
        };
        if(version3)for(unsigned r=0;r<actions;++r) {
            CameraRule rule;rule.event=integer();const unsigned condition=integer();rule.cameraPayload=integer();
            const unsigned amount=integer();
            if((rule.event!=1&&rule.event!=2)||condition>1||(!condition&&rule.cameraPayload)||!amount||amount>256)
                throw std::runtime_error("Invalid camera rule");
            rule.cameraEvent=condition==1;cameras(amount,rule.cameras);trigger.rules.push_back(std::move(rule));
        }
        else {cameras(actions,trigger.cameras);cameras(exitActions,trigger.exitCameras);}
        result.triggers.push_back(std::move(trigger));
    }
    if(offset!=data.size())throw std::runtime_error("Unindexed trigger bytes");
    return result;
}
inline unsigned cameraRuleActions(const CameraTrigger& trigger,unsigned event,unsigned currentCamera) {
    //416358 resets condition results each frame;416EB0 sends one camera payload
    //per zone before any of its actions.408C25 rejects payload0 before equality.
    const unsigned payload=currentCamera;
    if(!trigger.rules.empty()) {
        for(const auto& rule:trigger.rules)if(rule.event==event&&
                (!rule.cameraEvent||(payload!=0&&payload==rule.cameraPayload)))
            for(unsigned camera:rule.cameras)currentCamera=camera;
    } else for(unsigned camera:event==1?trigger.cameras:trigger.exitCameras)currentCamera=camera;
    return currentCamera;
}
inline uint32_t triggerMask(const TriggerGrid& grid, Vec2 point) {
    const double ix = (double(point.x) - grid.minX) / grid.extentX * 16;
    const double iz = (double(point.z) - grid.minZ) / grid.extentZ * 16;
    const double minimum = double(std::numeric_limits<int>::min()) + 16;
    const double maximum = double(std::numeric_limits<int>::max()) - 16;
    if (!std::isfinite(ix) || !std::isfinite(iz) || ix < minimum || ix > maximum || iz < minimum || iz > maximum)
        throw std::runtime_error("Camera trigger grid index is out of range");
    const int x = int(ix), z = int(iz);
    return (1u << (x & 31)) | (1u << ((z + 16) & 31));
}
inline bool insideTrigger(const CameraTrigger& trigger, Vec2 point) {
    if (trigger.shape == 0) {
        return point.x >= trigger.points[0].x && point.x <= trigger.points[1].x &&
               point.z >= trigger.points[0].z && point.z <= trigger.points[1].z;
    }
    for (size_t i = 0; i < trigger.points.size(); ++i) {
        const Vec2 a = trigger.points[i], b = trigger.points[(i + 1) % trigger.points.size()];
        const double cross = (double(b.x) - a.x) * (double(point.z) - a.z) -
                             (double(b.z) - a.z) * (double(point.x) - a.x);
        if (cross >= 0) return false;
    }
    return true;
}
inline unsigned enterTriggers(const std::vector<CameraTrigger>& triggers, Vec2 position,
                              std::vector<bool>& previousInside, unsigned currentCamera,
                              const TriggerGrid& grid = {}) {
    const uint32_t mask = triggerMask(grid, position);
    for (size_t i = 0; i < triggers.size(); ++i) {
        const uint32_t match = mask & triggers[i].mask;
        if (!(match & 0xffffu) || !(match & 0xffff0000u)) continue;
        const bool inside = insideTrigger(triggers[i], position);
        if (inside && !previousInside[i])
            currentCamera=cameraRuleActions(triggers[i],1,currentCamera);
        if (!inside && previousInside[i])
            currentCamera=cameraRuleActions(triggers[i],2,currentCamera);
        previousInside[i] = inside;
    }
    return currentCamera;
}
