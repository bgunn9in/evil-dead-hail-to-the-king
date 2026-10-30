#include "source_floor_query.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

namespace {
template<class T>void write(std::vector<uint8_t>& bytes,size_t offset,T value) {std::memcpy(bytes.data()+offset,&value,sizeof value);}
std::vector<uint8_t> source() {
    // Header88 + floor64 + sixXYZ vertices + two8Bfaces + two4Bmasks.
    std::vector<uint8_t> bytes(88+64+72+16+8);const size_t floor=88,vertices=floor+64,faces=vertices+72,masks=faces+16;
    write<uint32_t>(bytes,0,uint32_t(bytes.size()-floor));write<uint32_t>(bytes,4,uint32_t(floor));
    write<float>(bytes,floor+12,2);write<float>(bytes,floor+20,2);
    write<float>(bytes,floor+28,2);write<float>(bytes,floor+32,2);
    write<uint32_t>(bytes,floor+36,6);write<uint32_t>(bytes,floor+44,2);
    const GroundPoint points[]={{0,0,0},{0,2,2},{2,4,0},{0,10,0},{0,12,2},{2,14,0}};
    std::memcpy(bytes.data()+vertices,points,sizeof points);
    const uint16_t indices[]={0,1,2,0,3,4,5,0};std::memcpy(bytes.data()+faces,indices,sizeof indices);
    write<uint32_t>(bytes,masks,0xffffffff);write<uint32_t>(bytes,masks+4,0xffffffff);return bytes;
}
}
int main() {
    const SourceFloorQuery query(source());int32_t cache=-1;
    assert(std::abs(query.height(.25f,-999,.25f,cache)-.75)<1e-8&&cache==0);
    assert(query.height(.25f,999,.25f,cache)==query.height(.25f,0,.25f,cache));
    cache=1;assert(std::abs(query.height(.25f,0,.25f,cache)-10.75)<1e-8&&cache==1);
    // Strict shared edge / outside misses return0 without erasing the cache.
    assert(query.height(.5f,0,1.5f,cache)==0&&cache==1);
    assert(query.height(-1,0,0,cache)==0&&cache==1);
    auto masked=query;masked.masks[0]=0;cache=-1;
    assert(std::abs(masked.height(.25f,0,.25f,cache)-10.75)<1e-8&&cache==1);
    cache=0;assert(std::abs(masked.height(.25f,0,.25f,cache)-.75)<1e-8&&cache==0);
    masked.masks[0]=0x0000ffff;masked.masks[1]=0xffff0000;cache=-1;
    assert(masked.height(.25f,0,.25f,cache)==0&&cache==-1);
    int32_t actorCache=1;cache=0;
    assert(std::abs(query.actorHeight(.25f,999,.25f,actorCache,cache)-10.75)<1e-8&&actorCache==1&&cache==0);
    actorCache=-1;cache=1;
    assert(std::abs(query.actorHeight(.25f,0,.25f,actorCache,cache)-.75)<1e-8&&actorCache==0&&cache==0);
    actorCache=1;cache=0;
    assert(query.actorHeight(.5f,0,1.5f,actorCache,cache)==0&&actorCache==-1&&cache==0);
    actorCache=0;cache=1;
    assert(masked.actorHeight(.25f,0,.25f,actorCache,cache)==0&&actorCache==-1&&cache==1);
    auto empty=query;empty.triangles.clear();empty.masks.clear();actorCache=20;
    assert(empty.actorHeight(0,0,0,actorCache,cache)==0&&actorCache==20&&cache==1);
    auto single=query;single.triangles.resize(1);single.masks.resize(1);cache=-1;
    const auto cells=single.coarseGrid(cache);
    assert(cells[0]==0&&cells[8*32+8]==1.5f); // Sourcecorner(.5,.5), no half-cell offset.
    assert(single.coarseCell(.5f,.5f,cells)==1);
    assert(single.coarseHeight(.5f,.5f,cells)==1.5f);
    auto labels=cells;for(unsigned x=0;x<32;++x)for(unsigned z=0;z<32;++z)labels[x*32+z]=float(x*100+z)+.75f;
    assert(single.coarseCell(-1,.5f,labels)==3108); // Negative integer bin wraps to31.
    assert(single.coarseCell(-.01f,.5f,labels)==8); // Truncation towardzero before unsigned clamp.
    assert(single.coarseCell(2,2,labels)==3131);
    labels[0]=65535.75f;assert(single.coarseCell(0,0,labels)==-1); // Native signed low16.
    assert(single.coarseHeight(0,0,labels)==65535.75f);
    cache=200;assert(query.height(-1,0,0,cache)==0&&cache==200);
    assert(std::abs(query.height(.25f,0,.25f,cache)-.75)<1e-8&&cache==0);
    bool rejected=false;try{cache=-2;query.height(.25f,0,.25f,cache);}catch(const std::exception&){rejected=true;}assert(rejected);
    auto bad=source();write<uint32_t>(bad,88+44,0x100000);rejected=false;
    try{SourceFloorQuery invalid(bad);}catch(const std::exception&){rejected=true;}assert(rejected);
    std::cout<<"Native cached floor query and coarse grid passed\n";
}
