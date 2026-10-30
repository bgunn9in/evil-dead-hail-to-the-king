#include "source_native_visual.h"
#undef NDEBUG
#include <cassert>
#include <iostream>
#include <limits>
static bool near(float a,float b){return std::abs(a-b)<.00001f;}
int main(){using namespace ednativevisual;
for(int mode:{0,1,2,3,4,255}){const auto m=material(uint8_t(mode));assert(m.alpha==((mode==0||mode==3)?.5f:1));assert(!m.white);assert(m.blend==(mode==1||mode==3?Blend::Additive:mode==2?Blend::InverseAdditive:Blend::Alpha));
    for(int type:{48,134,135,143,144,146}){assert(specialObject(int16_t(type)));const auto s=material(uint8_t(mode),true);assert(s.alpha==.5f&&s.white&&s.blend==Blend::Alpha);}}
for(int type:{43,53,133,140,161})assert(!specialObject(int16_t(type)));assert(material(254).blend==Blend::Alpha&&material(254).alpha==1);
assert(actorMaterial(255,0).opaque&&actorMaterial(255,0).alpha==1);
for(uint8_t mode:{uint8_t(5),uint8_t(254)})assert(!actorMaterial(mode,0).opaque&&actorMaterial(mode,0).alpha==1&&actorMaterial(mode,0).blend==Blend::Alpha);
assert(actorMaterial(2,0xffffffff).blend==Blend::DestinationInverse&&actorMaterial(2,0xffffffff).alpha==0&&!actorMaterial(2,0xffffffff).opaque);
assert(actorMaterial(4,0).alpha==0&&near(actorMaterial(4,0xff010000).alpha,.299f)&&near(actorMaterial(4,0xff000100).alpha,.587f)&&near(actorMaterial(4,0xff000001).alpha,.114f));
assert(actorMaterial(4,0xff808080).alpha==1&&actorMaterial(0,0).alpha==.5f&&actorMaterial(3,0).blend==Blend::Additive);
edobjects::ObjectMesh mesh;mesh.identity=0x0b000001;mesh.partFlags={0};for(auto p:{std::array<float,3>{{1,6,3}},std::array<float,3>{{-1,-3,3}},std::array<float,3>{{1,2,-2}}}){edobjects::Vertex v;v.geometry.x=p[0];v.geometry.y=p[1];v.geometry.z=p[2];mesh.vertices.push_back(v);}
edobjects::Transform t;t.position={{10,2,30}};t.floorHeight=5;t.flags=3;auto s=objectShadow(mesh,t);assert(s.size()==3&&near(s[0].x,11.2f)&&s[0].y==5&&near(s[0].z,33.6f));
t.flags=1;s=objectShadow(mesh,t);assert(s[0].y==0);t.flags=3;t.degrees={{90,0,0}};s=objectShadow(mesh,t);assert(near(s[0].x,11.2f)&&near(s[0].y,4)&&near(s[0].z,27.6f)); // RawY replacement precedes pitch; worldY is deliberately NOT flattened afterward.
t.flags=0;assert(objectShadow(mesh,t).empty());
std::vector<edmesh::Vertex32> skin(3);for(auto& v:skin){v.x=1;v.y=99;v.z=3;}const auto a=actorShadow(skin,2,{{10,7,30}},0);assert(a.size()==3&&a[0].x==9&&a[0].y==5&&a[0].z==33);
const auto b=actorShadow(skin,2,{{10,7,30}},edactormath::HalfPi);assert(near(b[0].x,13)&&b[0].y==5&&near(b[0].z,31)); // ExistingMirrorX once; no actor1.2.
std::vector<edmesh::Vertex32> tri(3),out;for(unsigned n=0;n<3;++n){tri[n].x=float(n);tri[n].y=.3f;tri[n].z=.5f;}appendProjectedShadow(out,tri,{{0,0,0}},{{1,0,0}},{{0,1,0}});assert(out.size()==3&&out[0].z==1&&out[1].x==1&&out[0].y==.3f);
for(auto& v:tri)v.z=100;out.clear();appendProjectedShadow(out,tri,{{0,0,0}},{{1,0,0}},{{0,1,0}});assert(out.empty());
tri[0].z=99;out.clear();appendProjectedShadow(out,tri,{{0,0,0}},{{1,0,0}},{{0,1,0}});assert(out.size()==3&&out[1].z>100); // Firstvertex-only far test, not an invented allvertices reject.
std::vector<edmesh::Vertex32> sinking{{-1,-1,0,0,0,1,0,0},{1,1,0,0,0,1,1,1},{-1,1,0,0,0,1,0,1}};
const auto clipped=clipAboveFloor(sinking,0);assert(clipped.size()==6);
float area=0;bool interpolated=false;
for(size_t i=0;i<clipped.size();i+=3){const auto& a=clipped[i];const auto& b=clipped[i+1];const auto& c=clipped[i+2];
    const float triangle=((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))*.5f;assert(triangle>0);area+=triangle;}
for(const auto& v:clipped){assert(v.y>=0&&v.nz==1);if(v.x==0&&v.y==0){assert(v.u==.5f&&v.v==.5f);interpolated=true;}}
assert(near(area,1.5f)&&interpolated&&clipAboveFloor(sinking,2).empty());
const auto unchanged=clipAboveFloor(sinking,-2);assert(unchanged.size()==3);
for(size_t i=0;i<3;++i)assert(unchanged[i].x==sinking[i].x&&unchanged[i].y==sinking[i].y&&unchanged[i].u==sinking[i].u);
sinking[0].y=0;assert(clipAboveFloor(sinking,0).size()==3); // Plane endpoint is not emitted twice.
for(auto& v:sinking)v.y=0;assert(clipAboveFloor(sinking,0).size()==3);
auto rejects=[](auto action){try{action();return false;}catch(const std::runtime_error&){return true;}};
assert(rejects([&]{clipAboveFloor(sinking,std::numeric_limits<float>::quiet_NaN());}));
sinking.push_back({});assert(rejects([&]{clipAboveFloor(sinking,0);}));sinking.pop_back();
sinking[0].x=std::numeric_limits<float>::infinity();assert(rejects([&]{clipAboveFloor(sinking,0);}));
std::cout<<"NATIVE_VISUAL_CPU_PASS modes/shadows/floor_clip/winding/interpolated_UV/fully_submerged/invalid_geometry\n";
}
