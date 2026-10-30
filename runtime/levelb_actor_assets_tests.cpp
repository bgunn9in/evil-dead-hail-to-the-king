#include "actor_mesh.h"
#include "actor_actions.h"
#undef NDEBUG
#include <cassert>
#include <iostream>
#include <set>

static std::vector<uint8_t> bytes(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("Missing B actor artifact: "+path.string());
    return {std::istreambuf_iterator<char>(input),{}};
}
struct Actor {const char* name;unsigned bones,clips,vertices,states;};
static constexpr Actor Actors[]={
    {"ash",17,60,1590,163},{"deadite",17,23,1104,30},{"skullbat",5,5,306,9},
    {"badash",17,14,1788,21},{"guildmember",18,16,1206,28},{"merchant",16,4,1314,4},
    {"wizardboss",51,14,3312,15},{"mudgolem",44,10,1920,17},{"armoryskeleton",15,23,1224,34},
    {"armoryboss",32,12,3636,13},{"netherpet",17,29,864,18},{"cthulhu",59,8,4512,7},
    {"captain",15,24,1242,30},{"pumpkeeper",16,13,1200,19},
    {"netherpet_06360000",17,29,840,18},{"netherpet_06362000",17,29,690,18}};
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("Usage: levelb-actor-assets-tests <B-assets-directory>");
        const std::filesystem::path root=argv[1];size_t totalFrames=0,totalVertices=0;
        for(const auto& expected:Actors) {
            const auto path=root/"actors"/expected.name;
            const auto mesh=edmesh::ActorMesh::load(path/"actor.bin");
            assert(mesh.bones.size()==expected.bones&&mesh.clips.size()==expected.clips&&mesh.vertices.size()==expected.vertices);
            const auto tagged=edmesh::ActorMesh::load(path/"actor-parts.bin");
            assert(tagged.bones.size()==expected.bones&&tagged.clips.size()==expected.clips);
            const auto states=edactions::load(path/"actions.bin");assert(states.size()==expected.states);
            for(const auto& state:states)if(state.animation) {
                const auto& clip=mesh.clip(state.animation);
                assert(clip.frameCount==state.frames&&clip.flags==state.clipFlags&&state.bones==mesh.bones.size());
            }
            const auto raw=bytes(path/"pose-goldens.bin");edmesh::detail::Reader input(raw);
            assert(input.text(8)==std::string("EDPOSE1\0",8));const auto count=input.u32();
            std::map<uint32_t,std::set<unsigned>> covered;double maxPosition=0,maxNormal=0;size_t samples=0;
            for(uint32_t index=0;index<count;++index) {
                const auto clip=input.u32(),frame=input.u32(),vertices=input.u32();covered[clip].insert(frame);
                const auto sourceRoot=mesh.rootPosition(clip,frame);
                for(unsigned axis=0;axis<3;++axis)assert(sourceRoot[axis]==input.number());
                const auto posed=mesh.samplePose(clip,frame);
                for(uint32_t sample=0;sample<vertices;++sample) {
                    const auto at=input.u32();assert(at<posed.size());const auto& v=posed[at];
                    const std::array<float,8> actual{{v.x,v.y,v.z,v.nx,v.ny,v.nz,v.u,v.v}};
                    for(unsigned component=0;component<8;++component) {
                        const auto error=std::abs(double(actual[component])-input.number());
                        if(component<3){maxPosition=std::max(maxPosition,error);assert(error<5e-5);}
                        else if(component<6){maxNormal=std::max(maxNormal,error);assert(error<3e-6);}
                        else assert(error==0);
                    }
                    ++samples;
                }
            }
            assert(input.remaining()==0&&covered.size()==mesh.clips.size());
            for(const auto& [id,clip]:mesh.clips)for(unsigned frame:{0u,clip.frameCount/2,clip.frameCount-1})assert(covered.at(id).count(frame));
            totalFrames+=count;totalVertices+=samples;
            std::cout<<expected.name<<": "<<count<<" golden frames, "<<samples<<" vertices, errors "<<maxPosition<<'/'<<maxNormal<<'\n';
        }
        // Resource variants are different meshes in the same native31 FSM pool.
        assert(bytes(root/"actors/netherpet/actions.bin")==bytes(root/"actors/netherpet_06360000/actions.bin"));
        assert(bytes(root/"actors/netherpet/actions.bin")==bytes(root/"actors/netherpet_06362000/actions.bin"));
        assert(bytes(root/"actors/netherpet/actor.bin")!=bytes(root/"actors/netherpet_06360000/actor.bin"));
        assert(bytes(root/"actors/netherpet/actor.bin")!=bytes(root/"actors/netherpet_06362000/actor.bin"));
        std::cout<<"B actor assets:16 resource tuples, "<<totalFrames<<" golden frames, "<<totalVertices<<" vertex samples passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
