#pragma once
#include <array>
#include <cmath>
#include <istream>
#include <stdexcept>
#include <string>
#include <vector>

namespace eddcprofile {
// Native8c095910..5950 uses the resolved loop start only for this camera.
inline unsigned foregroundFrame(const std::string& scene,unsigned camera,unsigned start) {
    return scene=="bl06"&&camera==5?start:0;
}
struct Background {unsigned width=640,height=384;float factor=1;bool native=false;};
inline Background background(std::istream& input) {
    Background result;std::string magic,extra;
    if(!(input>>magic>>result.width>>result.height>>result.factor)||magic!="EDPROFILEBG1"||(input>>extra))
        throw std::runtime_error("Invalid explicit background profile dimensions/color");
    result.native=result.factor==.875f&&((result.width==320&&result.height==240)||
                                      (result.width==640&&result.height==480));
    if(!result.native&&!(result.width==640&&result.height==384&&result.factor==1))
        throw std::runtime_error("Invalid explicit background profile dimensions/color");
    return result;
}
struct Mask {float x,y,width,height,effectiveDistance,inverseZ,rgb,u,v;};
inline bool byteUnits(float value,float unit,unsigned maximum) {
    return value>=0&&value<=unit*maximum&&value/unit==std::floor(value/unit);
}
inline std::vector<Mask> masks(std::istream& input) {
    std::string magic,extra;unsigned count=0;
    if(!(input>>magic>>count)||magic!="EDDCMASKDRAW1"||count>32)
        throw std::runtime_error("Invalid original DC layer table");
    std::vector<Mask> result;result.reserve(count);
    for(unsigned i=0;i<count;++i) {
        Mask layer{};
        if(!(input>>layer.x>>layer.y>>layer.width>>layer.height>>layer.effectiveDistance>>layer.inverseZ>>layer.rgb>>layer.u>>layer.v))
            throw std::runtime_error("Incomplete original DC layer");
        for(float value:{layer.x,layer.y,layer.width,layer.height,layer.effectiveDistance,layer.inverseZ,layer.rgb,layer.u,layer.v})
            if(!std::isfinite(value))throw std::runtime_error("Nonfinite original DC layer");
        if(!byteUnits(layer.x,10,255)||!byteUnits(layer.y,1.875f,255)||
           !byteUnits(layer.width,10,256)||!byteUnits(layer.height,1.875f,256)||layer.width==0||layer.height==0||
           layer.effectiveDistance<=0||layer.inverseZ!=1.0f/layer.effectiveDistance||layer.rgb!=.875f||
           layer.u!=layer.width/640||layer.v!=layer.height/480)
            throw std::runtime_error("Invalid original DC layer geometry/depth");
        result.push_back(layer);
    }
    if(input>>extra)throw std::runtime_error("Trailing original DC layer data");
    return result;
}
struct DepthMapping {
    double a,b;
    float map(float inverseZ) const {
        const float result=float(a+b*inverseZ);
        if(!std::isfinite(result)||result<0||result>1)throw std::runtime_error("DC layer outside current GPU projection");
        return result;
    }
};
inline DepthMapping depthMapping(const std::array<float,16>& vp) {
    for(float value:vp)if(!std::isfinite(value))throw std::runtime_error("Nonfinite DC camera projection");
    double axis2=0,product=0;
    for(unsigned n:{0u,4u,8u}){axis2+=double(vp[n+3])*vp[n+3];product+=double(vp[n+2])*vp[n+3];}
    if(std::abs(axis2-1)>1e-4)throw std::runtime_error("DC coverage expects unit camera viewZ");
    const double a=product/axis2,b=double(vp[14])-a*vp[15];
    for(unsigned n:{0u,4u,8u})if(std::abs(double(vp[n+2])-a*vp[n+3])>1e-5)
        throw std::runtime_error("DC inverseZ cannot map to this projection");
    return {a,b};
}
}
