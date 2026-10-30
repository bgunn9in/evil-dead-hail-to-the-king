#pragma once
#include "input.h"
#include <array>
#include <limits>
#include <optional>
#include <vector>

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.z + b.z}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.z - b.z}; }
inline Vec2 operator*(Vec2 a, float scale) { return {a.x * scale, a.z * scale}; }
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.z * b.z; }
struct GroundPolygon {
    bool keepInside=false;std::vector<Vec2> points;bool boundaryOnly=false;
    float circleRadius=0;bool enabled=true;
    unsigned sourceIndex=std::numeric_limits<unsigned>::max();
    unsigned contactFlags=0;
};
struct GroundPoint { float x,y,z; };
struct FloorTriangle { GroundPoint a,b,c; };
inline std::optional<float> floorHeight(const std::vector<FloorTriangle>& triangles,Vec2 point,unsigned* cached=nullptr) {
    auto sample=[&](const FloorTriangle& triangle)->std::optional<float> {
        const auto a=triangle.a,b=triangle.b,c=triangle.c;
        const double denominator=(double(b.z)-c.z)*(double(a.x)-c.x)+(double(c.x)-b.x)*(double(a.z)-c.z);
        if (std::abs(denominator)<1e-12) return std::nullopt;
        const double u=((double(b.z)-c.z)*(double(point.x)-c.x)+(double(c.x)-b.x)*(double(point.z)-c.z))/denominator;
        const double v=((double(c.z)-a.z)*(double(point.x)-c.x)+(double(a.x)-c.x)*(double(point.z)-c.z))/denominator;
        const double w=1-u-v;
        // Accept numerical seams so adjacent floor triangles do not cause height pops.
        if (u < -1e-6 || v < -1e-6 || w < -1e-6) return std::nullopt;
        return float(u*a.y+v*b.y+w*c.y);
    };
    if (cached&&*cached<triangles.size()) if (auto height=sample(triangles[*cached])) return height;
    for (unsigned i=0;i<triangles.size();++i) if (auto height=sample(triangles[i])) {
        if (cached) *cached=i;
        return height;
    }
    if (cached) *cached=std::numeric_limits<unsigned>::max();
    return std::nullopt;
}
constexpr float ContactSkin = 0.0001f;

inline bool containsPoint(const GroundPolygon& polygon, Vec2 point) {
    if(polygon.circleRadius>0)return length(point-polygon.points[0])<polygon.circleRadius;
    bool inside = false;
    for (size_t i = 0, j = polygon.points.size() - 1; i < polygon.points.size(); j = i++) {
        const Vec2 a = polygon.points[i], b = polygon.points[j];
        if ((a.z > point.z) != (b.z > point.z) &&
            point.x < (b.x - a.x) * (point.z - a.z) / (b.z - a.z) + a.x) inside = !inside;
    }
    return inside;
}
inline Vec2 nearestOnSegment(Vec2 point, Vec2 a, Vec2 b) {
    const Vec2 edge = b - a;
    const float squared = dot(edge, edge);
    return squared > 0 ? a + edge * std::clamp(dot(point - a, edge) / squared, 0.0f, 1.0f) : a;
}
struct NearestBoundary { Vec2 point{}, outward{}; float distance = std::numeric_limits<float>::max(); };
inline NearestBoundary nearestBoundary(const GroundPolygon& polygon, Vec2 position) {
    if(polygon.circleRadius>0) {
        const Vec2 delta=position-polygon.points[0];const float size=length(delta);
        const Vec2 direction=size>1e-8f?delta*(1/size):Vec2{1,0};
        return {polygon.points[0]+direction*polygon.circleRadius,direction,std::abs(size-polygon.circleRadius)};
    }
    float area = 0;
    const size_t edges=polygon.boundaryOnly?polygon.points.size()-1:polygon.points.size();
    for (size_t i = 0; i < edges; ++i) {
        const Vec2 a = polygon.points[i], b = polygon.points[(i + 1) % polygon.points.size()];
        area += a.x * b.z - b.x * a.z;
    }
    NearestBoundary result;
    for (size_t i = 0; i < edges; ++i) {
        const Vec2 a = polygon.points[i], b = polygon.points[(i + 1) % polygon.points.size()];
        const Vec2 point = nearestOnSegment(position, a, b), edge = b - a;
        const float distance = length(position - point), edgeLength = length(edge);
        if (distance < result.distance && edgeLength > 0) {
            result.point = point; result.distance = distance;
            const float sign = area > 0 ? 1.0f : -1.0f;
            result.outward = {sign * edge.z / edgeLength, -sign * edge.x / edgeLength};
        }
    }
    return result;
}
inline bool clearGround(const std::vector<GroundPolygon>& polygons, Vec2 position, float radius) {
    for (const auto& polygon : polygons) {
        if(!polygon.enabled)continue;
        if (!polygon.boundaryOnly&&containsPoint(polygon, position) != polygon.keepInside) return false;
        if (nearestBoundary(polygon, position).distance < radius - ContactSkin) return false;
    }
    return true;
}
struct GroundHit {
    bool found=false;float time=1;Vec2 normal{};unsigned contactFlags=0;
    unsigned polygonIndex=std::numeric_limits<unsigned>::max(),sourceIndex=std::numeric_limits<unsigned>::max();
};
struct GroundMoveTrace {
    Vec2 start{},delta{},recovered{},end{};
    unsigned recoveryCount=0,sweepCount=0;
    GroundHit firstRecovery{};
    std::array<GroundHit,8> sweeps{};
    bool contactLimit=false;
};
inline Vec2 recoverGround(const std::vector<GroundPolygon>& polygons, Vec2 position, float radius,unsigned* contacts=nullptr,GroundMoveTrace* trace=nullptr) {
    for (unsigned pass = 0; pass < 16; ++pass) {
        bool corrected = false;
        for (unsigned index=0;index<polygons.size();++index) {
            const auto& polygon=polygons[index];
            if(!polygon.enabled)continue;
            const bool inside = !polygon.boundaryOnly&&containsPoint(polygon, position);
            const auto nearest = nearestBoundary(polygon, position);
            if (!polygon.boundaryOnly&&inside != polygon.keepInside) {
                if(contacts)*contacts|=polygon.contactFlags;
                const Vec2 normal = nearest.outward * (polygon.keepInside ? -1.0f : 1.0f);
                position = nearest.point + normal * (radius + ContactSkin); corrected = true;
                if(trace&&trace->recoveryCount++==0)trace->firstRecovery={true,0,normal,polygon.contactFlags,index,polygon.sourceIndex};
            } else if (nearest.distance < radius) {
                if(contacts)*contacts|=polygon.contactFlags;
                const Vec2 normal = nearest.distance > 0.000001f ? (position - nearest.point) * (1 / nearest.distance)
                    : nearest.outward * (polygon.keepInside ? -1.0f : 1.0f);
                position = nearest.point + normal * (radius + ContactSkin); corrected = true;
                if(trace&&trace->recoveryCount++==0)trace->firstRecovery={true,0,normal,polygon.contactFlags,index,polygon.sourceIndex};
            }
        }
        if (!corrected) break;
    }
    return position;
}
inline void hitCircle(Vec2 position, Vec2 delta, Vec2 center, float radius, GroundHit& hit) {
    const Vec2 relative = position - center;
    const double a = double(delta.x) * delta.x + double(delta.z) * delta.z;
    const double b = double(relative.x) * delta.x + double(relative.z) * delta.z;
    const double c = double(relative.x) * relative.x + double(relative.z) * relative.z - double(radius) * radius;
    const double discriminant = b * b - a * c;
    if (a <= 1e-16 || b >= 0 || discriminant < 0) return;
    float time = float((-b - std::sqrt(discriminant)) / a);
    if (time < 0 && c >= -2.0 * radius * ContactSkin) time = 0;
    if (time < 0 || time > hit.time) return;
    const Vec2 normal = position + delta * time - center;
    const float magnitude = length(normal);
    if (magnitude > 0) { hit = {true, time, normal * (1 / magnitude)}; }
}
inline GroundHit sweepGround(const std::vector<GroundPolygon>& polygons, Vec2 position, Vec2 delta, float radius) {
    GroundHit hit;
    for (unsigned index=0;index<polygons.size();++index) {
        const auto& polygon=polygons[index];
        if(!polygon.enabled)continue;
        GroundHit candidate;candidate.time=hit.time;
        if(polygon.circleRadius>0)hitCircle(position,delta,polygon.points[0],radius+polygon.circleRadius,candidate);
        else {
        double area=0;
        if(radius==0&&!polygon.boundaryOnly)for(size_t i=0,j=polygon.points.size()-1;i<polygon.points.size();j=i++)
            area+=double(polygon.points[j].x)*polygon.points[i].z-double(polygon.points[i].x)*polygon.points[j].z;
        const float validSide=(area>0?-1.f:1.f)*(polygon.keepInside?-1.f:1.f);
        for (size_t i = 0; i < (polygon.boundaryOnly?polygon.points.size()-1:polygon.points.size()); ++i) {
        const Vec2 a = polygon.points[i], b = polygon.points[(i + 1) % polygon.points.size()];
        const Vec2 edge = b - a;
        const float edgeLength = length(edge);
        if (edgeLength <= 0) continue;
        const Vec2 tangent = edge * (1 / edgeLength), normal{-tangent.z, tangent.x};
        const float distance = dot(position - a, normal), approach = dot(delta, normal);
        if(radius==0&&!polygon.boundaryOnly&&area!=0&&approach*validSide<0) {
            // Original DC 8c088a60 intersects unnormalized float32 segments,
            // inclusively at both endpoints. The normalized-distance path can
            // round a shared corner outside both adjacent edges and tunnel.
            const Vec2 movement=position+delta-position;
            const float other=movement.z*edge.x,determinant=movement.x*edge.z-other;
            if(determinant!=0) {
                const Vec2 relative=position-a;
                const float p=edge.x*relative.z,q=edge.z*relative.x;
                const float t=(p-q)/determinant;
                const float u=(movement.x*relative.z-movement.z*relative.x)/determinant;
                if(t>=0&&t<=candidate.time&&u>=0&&u<=1)candidate={true,t,normal*validSide};
            }
        }
        if (std::abs(approach) > 1e-8f) for (float side : {-1.0f, 1.0f}) {
            // A point meets only the closed boundary side facing walkable space.
            if(area!=0&&side!=validSide)continue;
            if (approach * side >= 0) continue;
            float time = (side * radius - distance) / approach;
            // Rounded distance tests can disagree by one ULP at an existing contact.
            if (time < 0 && side * distance >= radius - ContactSkin) time = 0;
            const float along = dot(position + delta * time - a, tangent);
            if (time >= 0 && time <= candidate.time && along >= 0 && along <= edgeLength)
                candidate = {true, time, normal * side};
        }
        hitCircle(position, delta, a, radius, candidate);
        hitCircle(position, delta, b, radius, candidate);
        }
        }
        if(candidate.found){candidate.contactFlags=polygon.contactFlags;candidate.polygonIndex=index;candidate.sourceIndex=polygon.sourceIndex;hit=candidate;}
    }
    return hit;
}
inline Vec2 moveGround(const std::vector<GroundPolygon>& polygons, Vec2 position, Vec2 delta, float radius,unsigned* contacts=nullptr,GroundMoveTrace* trace=nullptr) {
    if(contacts)*contacts=0;
    if(trace){*trace={};trace->start=position;trace->delta=delta;}
    position = recoverGround(polygons, position, radius,contacts,trace);
    if(trace)trace->recovered=position;
    // shortcut: at most eight contacts per move; extend only for a scene needing more.
    for (unsigned contact = 0; contact < 8 && length(delta) > 1e-7f; ++contact) {
        const GroundHit hit = sweepGround(polygons, position, delta, radius);
        if (!hit.found) {
            const Vec2 next=position+delta;
            // A sub-skin tangent remainder can round a wall contact one ULP inside.
            if(contact&&radius==0&&length(delta)<=ContactSkin&&clearGround(polygons,position,0)&&!clearGround(polygons,next,0))break;
            position=next;break;
        }
        if(contacts)*contacts|=hit.contactFlags;
        if(trace)trace->sweeps[trace->sweepCount++]=hit;
        position = position + delta * std::max(0.0f, hit.time - ContactSkin / length(delta));
        delta = delta * (1 - hit.time);
        const float into = dot(delta, hit.normal);
        if (into < 0) delta = delta - hit.normal * into;
        if(trace&&contact==7&&length(delta)>1e-7f)trace->contactLimit=true;
    }
    if(trace)trace->end=position;
    return position;
}
