#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace edactormath {
using Vector=std::array<float,3>;
using Euler=std::array<float,3>; // Model XYZ radians; Y is modern +Z yaw.
constexpr float HalfPi=1.5707963705062866f,Pi=3.1415927410125732f,Tau=6.2831854820251465f;
constexpr float Centimeter=.009999999776482582f,AngleEpsilon=.0000009999999974752427f;
inline void finite(const Vector& value) {
    for(float component:value)if(!std::isfinite(component))throw std::runtime_error("Non-finite source actor vector");
}
// 421760/4217E0 snap only just-aboveTau / just-below0, then wrap to[0,Tau).
inline float normalizeAngle(double angle) {
    if(!std::isfinite(angle))throw std::runtime_error("Non-finite source actor angle");
    if((angle>=Tau&&angle<=double(Tau)+AngleEpsilon)||(angle<0&&angle>=-double(AngleEpsilon)))return 0;
    angle=std::fmod(angle,double(Tau));if(angle<0)angle+=Tau;return float(angle);
}
inline float nativeHeading(float modelYaw) {
    // 401CA0: normalize(modelYaw+HalfPi) is stored tofloat before Pi-value.
    return normalizeAngle(float(double(Pi)-normalizeAngle(double(modelYaw)+HalfPi)));
}
inline float modelYawFromHeading(double heading) {
    // 401BF0/40142A store Pi-H-HalfPi before normalizing the model angle.
    return normalizeAngle(float(double(Pi)-heading-HalfPi));
}
namespace detail {
using Matrix=std::array<float,9>;
inline Matrix multiply(const Matrix& a,const Matrix& b) {
    Matrix out{};for(unsigned row=0;row<3;++row)for(unsigned column=0;column<3;++column) {
        double value=0;for(unsigned k=0;k<3;++k)value+=double(a[row*3+k])*b[k*3+column];
        out[row*3+column]=float(value); // 423220 rounds each matrix entry.
    }
    return out;
}
inline Vector transform(const Euler& angles,const Vector& value,bool inverseSigns) {
    finite(angles);finite(value);
    const float cx=float(std::cos(double(angles[0]))),sx=float(std::sin(double(angles[0]))),
                cy=float(std::cos(double(angles[1]))),sy=float(std::sin(double(angles[1]))),
                cz=float(std::cos(double(angles[2]))),sz=float(std::sin(double(angles[2])));
    const float direction=inverseSigns?-1.f:1.f;
    const Matrix rx{{1,0,0,0,cx,direction*sx,0,-direction*sx,cx}},
                 ry{{cy,0,-direction*sy,0,1,0,direction*sy,0,cy}},
                 rz{{cz,direction*sz,0,-direction*sz,cz,0,0,0,1}};
    const auto matrix=multiply(multiply(rx,ry),rz);Vector out{};
    for(unsigned column=0;column<3;++column)
        out[column]=float((double(value[2])*matrix[6+column]+double(value[1])*matrix[3+column])+double(value[0])*matrix[column]);
    return out;
}
}
// 4225D0 uses422CD0/422EB0/423090, opposite the skin/model matrix signs.
inline Vector nativeTransform(const Euler& headingEuler,const Vector& value) {return detail::transform(headingEuler,value,true);}
inline Vector modelTransform(const Euler& modelEuler,const Vector& value) {return detail::transform(modelEuler,value,false);}
inline Vector jointWorld(const Euler& modelEuler,const Vector& rawJoint,const Vector& actorWorld,
                         const std::array<int32_t,3>& offsetCm={{0,0,0}}) {
    finite(modelEuler);finite(rawJoint);finite(actorWorld);auto local=rawJoint;
    // 411AD0 adds centimetre offsets to41F740's unreflected global joint first.
    for(unsigned i=0;i<3;++i)local[i]=float(double(local[i])+double(offsetCm[i])*Centimeter);
    // 401D30's row-vector matrix is MirrorX*Rx*Ry*Rz, then WORLD translation.
    local[0]=-local[0];auto world=modelTransform(modelEuler,local);
    for(unsigned i=0;i<3;++i)world[i]=float(double(world[i])+actorWorld[i]);
    return world;
}
inline Vector moveForwardPending(int32_t amount,const Euler& modelEuler,unsigned motionFlags) {
    finite(modelEuler);const float distance=float(double(amount)*Centimeter);
    if(motionFlags&4)return {{0,0,distance}};
    return nativeTransform({{modelEuler[0],nativeHeading(modelEuler[1]),modelEuler[2]}},{{distance,0,0}});
}
inline Vector moveStrafePending(int32_t amount,const Euler& modelEuler,unsigned motionFlags) {
    finite(modelEuler);if(!amount)return {{0,0,0}};
    const float distance=float(double(amount)*Centimeter);
    if(motionFlags&4)return {{distance,0,0}};
    const float heading=normalizeAngle(double(nativeHeading(modelEuler[1]))+(amount>0?HalfPi:-HalfPi));
    auto result=nativeTransform({{0,heading,0}},{{distance,0,0}});
    if(amount<0)for(auto& component:result)component=-component;return result;
}
inline Vector moveVerticalPending(int32_t amount) {return {{0,float(double(amount)*Centimeter),0}};}
inline Vector pendingToWorld(const Vector& pending,const Euler& modelEuler,unsigned motionFlags) {
    finite(pending);finite(modelEuler);return (motionFlags&4)?modelTransform(modelEuler,pending):pending;
}
inline void appendMovement(Vector& pending,int32_t forward,int32_t strafe,int32_t vertical,const Euler& modelEuler,unsigned motionFlags) {
    finite(pending);finite(modelEuler);
    if(motionFlags&4) {
        // Direct branches add unrounded int*floatScale to the old float slot.
        pending[0]=float(double(pending[0])+double(strafe)*Centimeter);
        pending[1]=float(double(pending[1])+double(vertical)*Centimeter);
        pending[2]=float(double(pending[2])+double(forward)*Centimeter);
    } else {
        const auto f=moveForwardPending(forward,modelEuler,motionFlags),s=moveStrafePending(strafe,modelEuler,motionFlags);
        for(unsigned i=0;i<3;++i)pending[i]=float(double(pending[i])+f[i]);
        for(unsigned i=0;i<3;++i)pending[i]=float(double(pending[i])+s[i]);
        pending[1]=float(double(pending[1])+double(vertical)*Centimeter);
    }
}
inline Vector losDirectionOffset(int32_t arg2,int32_t arg3,const Euler& modelEuler,unsigned motionFlags) {
    finite(modelEuler);
    const float x=float(double(arg2)*Centimeter),z=float(double(arg3)*Centimeter);
    // Native404540 reverses both mode and component order relative to movement.
    if(!(motionFlags&4))return {{x,0,z}};
    return nativeTransform({{modelEuler[0],nativeHeading(modelEuler[1]),modelEuler[2]}},{{z,0,x}});
}
inline Vector losDirectionTarget(const Vector& localPosition,int32_t arg2,int32_t arg3,const Euler& modelEuler,unsigned motionFlags) {
    finite(localPosition);finite(modelEuler);auto target=localPosition;
    if(!(motionFlags&4)) {
        target[0]=float(double(target[0])+double(arg2)*Centimeter);
        target[2]=float(double(target[2])+double(arg3)*Centimeter);
    } else {
        const auto offset=losDirectionOffset(arg2,arg3,modelEuler,motionFlags);
        for(unsigned i=0;i<3;++i)target[i]=float(double(target[i])+offset[i]);
    }
    return target;
}
inline int32_t distanceXZCentimeters(const Vector& localPosition,const Vector& marker) {
    finite(localPosition);finite(marker);
    const float x=float(double(localPosition[0])-marker[0]),z=float(double(localPosition[2])-marker[2]);
    const double squared=double(x)*x+double(z)*z;
    const double distance=(squared==0?1:std::sqrt(squared))*100; // 421B60 zero-length fallback.
    if(distance>std::numeric_limits<int32_t>::max())throw std::runtime_error("Source actor distance exceeds integer range");
    return int32_t(distance); // 403D40: truncates, no Y or target-radius subtraction.
}
inline bool objectContact(const Vector& position,const Vector& delta,const Vector& object,float radius) {
    const float x=object[0]-position[0],z=object[2]-position[2];
    const float current=x*x+z*z,nextX=x-delta[0],nextZ=z-delta[2];
    return current<radius*radius&&nextX*nextX+nextZ*nextZ<=current;
}
inline Vector slideContact(const Vector& delta,Vector normal) {
    const float magnitude=std::sqrt(normal[0]*normal[0]+normal[2]*normal[2]);
    if(magnitude==0)return delta;
    normal={{normal[0]/magnitude,0,normal[2]/magnitude}};
    const float projection=1.100000023841858f*(delta[0]*normal[0]+delta[2]*normal[2]);
    Vector result{{delta[0]-normal[0]*projection,delta[1],delta[2]-normal[2]*projection}};
    // DC8c0707e0 snaps each component strictly inside the +/-1cm interval.
    for(auto& value:result)if(value>-Centimeter&&value<Centimeter)value=0;
    return result;
}
inline int nearestMarker(const Vector& localPosition,const std::vector<Vector>& markers,int32_t minimumCm,int32_t maximumCm) {
    finite(localPosition);const double a=double(minimumCm)*Centimeter,b=double(maximumCm)*Centimeter;
    const float minimumSquared=float(a*a),maximumSquared=float(b*b);int found=-1;float best=0;
    for(size_t i=0;i<markers.size();++i) {
        finite(markers[i]);const float x=float(double(localPosition[0])-markers[i][0]),
            y=float(double(localPosition[1])-markers[i][1]),z=float(double(localPosition[2])-markers[i][2]);
        const double squared=(double(x)*x+double(y)*y)+double(z)*z;
        if(squared<minimumSquared||squared>maximumSquared)continue;
        if(found<0||squared<best){found=int(i);best=float(squared);}
    }
    return found; // 405010: full localXYZ, inclusive squared bounds, source order.
}
} // namespace edactormath
