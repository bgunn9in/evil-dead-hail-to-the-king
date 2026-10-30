#include <algorithm>
#include <iostream>
#include <chrono>
#undef NDEBUG
#include <cassert>
#include "source_vfx.h"

int main(int argc,char** argv){try{
    if(argc!=2)return 2;const auto root=std::filesystem::absolute(std::filesystem::u8path(argv[1]));
    edvfx::Host host;
    host.randomBound=[](int){return 0;};host.cachedBound=host.randomBound;
    host.visible=[](edvfx::Vector){return true;};host.coarseCell=[](float,float){return int16_t(-10000);};
    host.coarseHeight=[](float,float){return -10000.f;};host.floorSample=[](float,float,float){return -10000.;};
    host.actorWorld=[](int,int){return edvfx::Vector{{2,0,2}};};host.repellingActors=[](){return edparticle::PhysicalActors{};};
    edvfx::SourceVfx particles(root,host);
    for(unsigned kind:{0,2,3,6,7,8,9,15,16,17,18,19,20,21,22,23,24,25,32})assert(particles.supports(kind));
    assert(!particles.supports(1));
    for(float viewZ:{-.5f,.25f,.999f,1.f,5.f})for(float projection:{.5f,1.f,2.f,8.f}){
        edvfx::SourceVfx smoke(root,host);assert(smoke.spawn(9,{{.2f,.3f,viewZ}}));
        const auto saved=smoke.snapshot();const auto& particle=smoke.particles().at(9).front();
        const auto batches=smoke.draw({{1,0,0}},{{0,1,0}},{{0,0,0}},projection);assert(batches.size()==1);
        const auto& a=batches[0].vertices[0];const auto& b=batches[0].vertices[1];
        const float nativeZ=viewZ<1?1:viewZ*1.0000200271606445f;
        const float pixelHalf=(b.x-a.x)*.5f*240*projection/nativeZ;
        assert(std::abs(pixelHalf-particle.size*.5f/nativeZ)<.0001f);
        assert(a.z==nativeZ&&b.z==nativeZ&&std::abs((a.x+b.x)*.5f-.2f)<.00001f);
        assert(smoke.snapshot()==saved); // A draw never advances RNG, life, or source state.
    }
    {edvfx::SourceVfx flat(root,host);assert(flat.spawn(18,{{0,0,5}}));
        const auto old=flat.draw({{1,0,0}},{{0,1,0}},{{0,0,0}}),scaled=flat.draw({{1,0,0}},{{0,1,0}},{{0,0,0}},8);
        assert(old.size()==scaled.size());for(size_t n=0;n<old.size();++n){assert(old[n].tint==scaled[n].tint);
            assert(old[n].vertices.size()==scaled[n].vertices.size());assert(std::memcmp(old[n].vertices.data(),scaled[n].vertices.data(),old[n].vertices.size()*sizeof(edmesh::Vertex32))==0);}
    }
    {edvfx::SourceVfx blood(root,host);assert(blood.spawn(18,{{0,0,5}}));
        const auto atlas=edobjects::Texture::load(root/"vfx/atlas.rgba");assert(atlas.width==256&&atlas.height==256);
        auto& particle=const_cast<edvfx::Particle&>(blood.particles().at(18).front());
        for(unsigned tile=0;tile<16;++tile){particle.tile=tile;const auto saved=blood.snapshot();
            const auto draws=blood.draw({{1,0,0}},{{0,1,0}},{});assert(draws.size()==1&&draws[0].vertices.size()==6);
            const unsigned left=128+(tile%4)*16,top=(tile/4)*16;
            for(const auto& vertex:draws[0].vertices){
                // Both taps of bilinear filtering must belong to this blood cell, never the adjacent white flash.
                const float x=vertex.u*atlas.width-.5f,y=vertex.v*atlas.height-.5f;
                assert(std::floor(x)>=left&&std::ceil(x)<left+16&&std::floor(y)>=top&&std::ceil(y)<top+16);
            }
            assert(blood.snapshot()==saved);
        }
    }
    // Native090920 projects center with unbiased1/Z, then writes inverseZ+.04 and size*biased1/Z*.5 pixel halfextents.
    for(float z:{.5f,1.f,1.02f,2.f,10.f,50.f})for(float projection:{1.2f,2.4f,4.8f})for(bool rotated:{false,true}){
        const edvfx::Vector right=rotated?edvfx::Vector{{0,0,-1}}:edvfx::Vector{{1,0,0}},up{{0,1,0}},eye{{3,2,1}};
        const edvfx::Vector forward=rotated?edvfx::Vector{{1,0,0}}:edvfx::Vector{{0,0,1}};
        edvfx::Vector point{};for(unsigned axis=0;axis<3;++axis)point[axis]=eye[axis]+right[axis]*.2f+up[axis]*.3f+forward[axis]*z;
        edvfx::SourceVfx glint(root,host);assert(glint.spawn(23,point));for(unsigned tick=1;tick<=6;++tick)glint.update(tick);
        const auto saved=glint.snapshot();const auto batches=glint.draw(right,up,eye,projection);assert(batches.size()==1);
        const auto& a=batches[0].vertices[0];const auto& b=batches[0].vertices[2];
        edvfx::Vector center{{(a.x+b.x)*.5f-eye[0],(a.y+b.y)*.5f-eye[1],(a.z+b.z)*.5f-eye[2]}},edge{{b.x-a.x,b.y-a.y,b.z-a.z}};
        float x=0,y=0,depth=0,half=0;for(unsigned axis=0;axis<3;++axis){x+=center[axis]*right[axis];y+=center[axis]*up[axis];depth+=center[axis]*forward[axis];half+=edge[axis]*right[axis]*.5f;}
        const float nativeZ=z<1?1:z*1.0000200271606445f,inverse=1/nativeZ,biased=inverse+.03999999910593033f;
        assert(std::abs(1/depth-biased)<.00001f);
        assert(std::abs(320+240*projection*x/depth-(biased>1?1:320+240*projection*.2f*inverse))<.0004f); // Original near branch writes centerX=1.
        assert(std::abs(240-240*projection*y/depth-(240-240*projection*.3f*inverse))<.0004f);
        assert(std::abs(half*240*projection/depth-glint.particles().at(23).front().size*biased*.5f)<.0004f);
        assert(glint.snapshot()==saved);
    }
    for(float invalid:{-1.f,std::numeric_limits<float>::infinity()}){
        bool rejected=false;try{particles.draw({{1,0,0}},{{0,1,0}},{{0,0,0}},invalid);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    }
    for(unsigned kind:{0,2,3,6,7,8,9,15,16,17,18,19,20,21,22,23,24,25,32}){
        edvfx::SourceVfx native(root,host);assert(native.line(kind,{{0,2,0}},{{.1f,2.1f,.2f}},kind==25?3:kind==24?4:0));
        const auto draws=native.draw({{1,0,0}},{{0,1,0}},{{0,2,5}});assert(!draws.empty());
        const bool additive=kind==0||kind==3||kind==7||kind==9||kind==23||kind==24||kind==25;
        for(const auto& draw:draws){assert(draw.blend==(additive?edvfx::Blend::Additive:edvfx::Blend::Alpha));assert(draw.nativeRenderer!=0);}
        if(kind==6||kind==16||kind==32)assert(draws.size()==native.particles().at(kind).size());
        if(kind==3||kind==7){const auto& p=native.particles().at(kind).front();const auto& vertices=draws.front().vertices;
            assert(vertices.size()==3&&vertices[0].x==p.position[0]&&vertices[0].y==p.position[1]);
            assert(vertices[1].x==p.position[0]-p.velocity[0]&&vertices[1].z==p.position[2]-p.velocity[2]);
            assert(vertices[2].x==p.position[0]+.009999999776482582f&&vertices[2].y==p.position[1]+.009999999776482582f&&vertices[2].z==p.position[2]);}
    }
    assert(particles.line(16,{{0,2,0}},{{0,2,0}}));
    assert(particles.particles().at(16).size()==8);
    assert(particles.particles().at(16).front().color==(std::array<uint8_t,4>{{0,0,0,128}}));
    assert(particles.particles().at(16).front().size==60);
    particles.update(1);
    assert(particles.particles().at(9).size()==8);
    assert(particles.particles().at(22).empty()); // Native16 has only child9/chance5, no PC trail22.
    const auto cold=particles.snapshot();edvfx::SourceVfx restored(root,host);restored.restore(cold);assert(restored.snapshot()==cold);
    for(unsigned tick=2;tick<=20;++tick){particles.update(tick);restored.update(tick);assert(restored.snapshot()==particles.snapshot());}
    particles.clear();
    for(unsigned kind:{0,2,3,6,7,8,9,15,16,17,18,19,20,21,22,23,24,25,32})
        assert(particles.line(kind,{{0,2,0}},{{.1f,2.1f,.2f}},kind==25?3:kind==24?4:0));
    for(unsigned tick=21;tick<=28;++tick){particles.update(tick);particles.sampleRender();const auto batches=particles.draw({{1,0,0}},{{0,1,0}},{{0,2,5}});assert(!batches.empty());
        for(const auto& batch:batches)for(const auto& v:batch.vertices)assert(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z));}
    const auto temporary=std::filesystem::temp_directory_path()/("evil-dead-dc-vfx-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(!std::filesystem::create_directory(temporary))throw std::runtime_error("Cannot create isolated VFX fixture directory");
    std::filesystem::create_directory(temporary/"vfx");
    const std::array<const char*,8> names{{"effects.bin","hit-effects.bin","emitter-effects.bin","ranged-effects.bin","tree-effects.bin","ash-effects.bin","variant-effects.bin","npc-effects.bin"}};
    for(const auto name:names)std::filesystem::copy_file(root/"vfx"/name,temporary/"vfx"/name);
    const auto npc=edobjects::detail::read(root/"vfx/npc-effects.bin",1024*1024);assert(npc.size()==492);
    auto write=[&](const std::vector<uint8_t>& bytes){std::ofstream file(temporary/"vfx/npc-effects.bin",std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));};
    auto rejects=[&](std::vector<uint8_t> bytes){write(bytes);try{edvfx::SourceVfx invalid(temporary,host);return false;}catch(const std::exception&){return true;}};
    auto bad=npc;bad[bad.size()-2]=6;assert(rejects(bad)); // One child only permits the measured native chance5.
    bad=npc;bad[bad.size()-3]=22;assert(rejects(bad));
    bad=npc;bad[bad.size()-8]=0;assert(rejects(bad));
    auto legacy=npc;legacy[legacy.size()-8]=2;legacy[legacy.size()-2]=15;legacy.insert(legacy.end(),{4,22,2,0});write(legacy);
    edvfx::SourceVfx priorContract(temporary,host);assert(priorContract.supports(16)); // Existing exact two-child ABI remains accepted.
    assert(priorContract.spawn(6,{{0,2,0}}));const auto oldDraws=priorContract.draw({{1,0,0}},{{0,1,0}},{{0,2,5}});
    assert(oldDraws.size()==2);for(const auto& draw:oldDraws){assert(draw.blend==edvfx::Blend::Additive);assert(draw.nativeRenderer==0);}
    const auto legacyScaled=priorContract.draw({{1,0,0}},{{0,1,0}},{{0,2,5}},8);assert(legacyScaled.size()==oldDraws.size());
    for(size_t n=0;n<oldDraws.size();++n){assert(oldDraws[n].tint==legacyScaled[n].tint&&oldDraws[n].vertices.size()==legacyScaled[n].vertices.size());
        assert(std::memcmp(oldDraws[n].vertices.data(),legacyScaled[n].vertices.data(),oldDraws[n].vertices.size()*sizeof(edmesh::Vertex32))==0);}
    priorContract.clear();assert(priorContract.spawn(23,{{.2f,.3f,5}}));for(unsigned tick=1;tick<=6;++tick)priorContract.update(tick);
    const auto oldGlint=priorContract.draw({{1,0,0}},{{0,1,0}},{{0,0,0}}),projectionGlint=priorContract.draw({{1,0,0}},{{0,1,0}},{{0,0,0}},8);
    assert(oldGlint.size()==1&&projectionGlint.size()==1&&oldGlint[0].nativeRenderer==0&&oldGlint[0].tint==projectionGlint[0].tint);
    assert(std::memcmp(oldGlint[0].vertices.data(),projectionGlint[0].vertices.data(),oldGlint[0].vertices.size()*sizeof(edmesh::Vertex32))==0);
    const auto render=edobjects::detail::read(root/"vfx/render-states.bin",4096);assert(render.size()==348);
    auto rejectRender=[&](std::vector<uint8_t> bytes){std::ofstream f(temporary/"vfx/render-states.bin",std::ios::binary|std::ios::trunc);f.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));f.close();
        try{edvfx::SourceVfx invalid(temporary,host);return false;}catch(const std::exception&){return true;}};
    bad=render;bad[8]^=1;assert(rejectRender(bad));
    bad=render;bad[48]=9;assert(rejectRender(bad));
    bad=render;bad[56]=1;assert(rejectRender(bad));
    bad=render;bad[60]=0;assert(rejectRender(bad)); // Duplicate kind0.
    bad=render;bad.push_back(0);assert(rejectRender(bad));
    std::filesystem::remove(temporary/"vfx/render-states.bin");
    for(const auto name:names)std::filesystem::remove(temporary/"vfx"/name);
    std::filesystem::remove(temporary/"vfx");std::filesystem::remove(temporary);
    std::cout<<"PASS: all19 native parameter definitions load; native16 one-child sequence and malformed alternatives; prior two-child contract; save/restore20 ticks; all kinds update/draw8 ticks. Host ABI bridge only, zero World steps; native raster/runtime equivalence unproven.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
