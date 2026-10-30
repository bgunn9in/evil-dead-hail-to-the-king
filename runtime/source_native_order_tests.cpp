#include "source_native_order.h"
#undef NDEBUG
#include <cassert>
#include <iostream>
int main(){using namespace ednativeorder;
    const std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    edmesh::Vertex32 triangle[6]{};for(auto& v:triangle)v.z=.6f;
    std::vector<Draw> draws;append(draws,Kind::Object,1,0,triangle,6,identity);
    for(auto& v:triangle)v.z=.2f;append(draws,Kind::Particle,2,0,triangle,6,identity);
    sortAndCoalesce(draws);assert(draws.size()==2&&draws[0].batch==1&&draws[0].count==6&&draws[1].batch==2);
    draws={{Kind::Particle,7,0,3,.4f},{Kind::Actor,8,0,3,.4f},{Kind::Object,9,0,3,.4f}};sortAndCoalesce(draws);
    assert(draws[0].batch==7&&draws[1].batch==8&&draws[2].batch==9); // Original source-order ties survive.
    auto perspective=identity;perspective[15]=0;perspective[11]=1;perspective[10]=1;
    triangle[0].z=-.1f;triangle[1].z=.2f;triangle[2].z=.4f;
    assert(triangleDepth(perspective,triangle)==-1); // Crossing W=0 is retained, finite and conservatively ordered.
    for(unsigned i=0;i<3;++i)triangle[i].z=0;assert(triangleDepth(perspective,triangle)==-1);
    for(float depth:{0.f,.2f,.4f,.7f,1.f}){unsigned bands=0;float upper=1;
        for(float lower:{.7f,.4f,.2f,-1.f}){bands+=inBand(depth,lower,upper);upper=lower;}assert(bands==1);}
    triangle[0].x=INFINITY;bool rejected=false;try{triangleDepth(identity,triangle);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    rejected=false;try{append(draws,Kind::Actor,0,0,triangle,2,identity);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    std::cout<<"NATIVE_ORDER_CPU_PASS far-near/stableties/coalescing/nearplane/halfopenbands/invalidgeometry\n";
}
