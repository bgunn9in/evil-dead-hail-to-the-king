#include "actor_mesh.h"
#include "actor_actions.h"
#undef NDEBUG
#include <cassert>
#include <iostream>
#include <iterator>
#include <set>

namespace {
void integer(std::vector<uint8_t>& data,uint32_t value) {for(unsigned b=0;b<4;++b)data.push_back(uint8_t(value>>(8*b)));}
void half(std::vector<uint8_t>& data,uint16_t value) {data.push_back(uint8_t(value));data.push_back(uint8_t(value>>8));}
void number(std::vector<uint8_t>& data,float value) {uint32_t bits;std::memcpy(&bits,&value,4);integer(data,bits);}
void patch(std::vector<uint8_t>& data,size_t offset,uint32_t value) {for(unsigned b=0;b<4;++b)data.at(offset+b)=uint8_t(value>>(8*b));}
std::vector<uint8_t> fixture() {
    std::vector<uint8_t> data={'E','D','A','C','T','O','R','1'};integer(data,3);integer(data,2);integer(data,2);
    for(unsigned i=0;i<3;++i) {
        for(float v:std::array<float,8>{{i==1?1.f:0.f,i==2?1.f:0.f,0,1,0,0,i==1?1.f:0.f,i==2?1.f:0.f}})number(data,v);
        half(data,i?1:0);half(data,0);
    }
    for(unsigned i=0;i<2;++i) {
        const std::array<float,16> matrix=i?std::array<float,16>{{0,1,0,0,-1,0,0,0,0,0,1,0,0,0,0,1}}:
            std::array<float,16>{{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
        for(float value:matrix)number(data,value);
        number(data,i?0.f:10.f);number(data,i?2.f:0.f);number(data,0);integer(data,i?0:0xffffffffu);
    }
    for(unsigned i=0;i<2;++i) {
        integer(data,i?200:100);integer(data,i?0:2);integer(data,2);
        for(unsigned frame=0;frame<2;++frame) {
            for(float value:std::array<float,3>{{3.f+frame,i?(20.f+10*frame):(5.f+frame),7.f+frame}})number(data,value);
            for(float value:std::array<float,6>{{0,0,0,0,0,i?90.f:0.f}})number(data,value);
        }
    }
    return data;
}
template<class Callback>void rejects(Callback call) {bool failed=false;try{call();}catch(const std::runtime_error&){failed=true;}assert(failed);}
std::vector<uint8_t> bytes(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("Missing golden artifact: "+path.string());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(stream)),{});
}
void sourceGoldens(const std::filesystem::path& directory,const char* name,size_t clipCount,size_t vertexCount,bool allParts=false,size_t boneCount=17) {
    const auto mesh=edmesh::ActorMesh::load(directory/(allParts?"actor-parts.bin":"actor.bin"));assert(mesh.bones.size()==boneCount&&mesh.clips.size()==clipCount&&mesh.vertices.size()==vertexCount);
    const auto golden=bytes(directory/(allParts?"parts-pose-goldens.bin":"pose-goldens.bin"));edmesh::detail::Reader input(golden);
    assert(input.text(8)==std::string("EDPOSE1\0",8));const auto count=input.u32();
    std::map<uint32_t,std::set<unsigned>> covered;double maxPosition=0,maxNormal=0;size_t checked=0;
    for(uint32_t i=0;i<count;++i) {
        const auto id=input.u32(),frame=input.u32(),samples=input.u32();covered[id].insert(frame);
        const auto root=mesh.rootPosition(id,frame);for(unsigned axis=0;axis<3;++axis)assert(root[axis]==input.number());
        const auto posed=mesh.samplePose(id,frame);
        for(uint32_t n=0;n<samples;++n) {
            const auto index=input.u32();assert(index<posed.size());const auto& v=posed[index];
            const std::array<float,8> actual{{v.x,v.y,v.z,v.nx,v.ny,v.nz,v.u,v.v}};
            for(unsigned component=0;component<8;++component) {
                const double error=std::abs(double(actual[component])-input.number());
                if(component<3){maxPosition=std::max(maxPosition,error);assert(error<5e-5);}
                else if(component<6){maxNormal=std::max(maxNormal,error);assert(error<3e-6);}else assert(error==0);
            }
            ++checked;
        }
    }
    assert(input.remaining()==0&&covered.size()==mesh.clips.size());
    for(const auto& [id,clip]:mesh.clips)for(unsigned frame:{0u,clip.frameCount/2,clip.frameCount-1})assert(covered.at(id).count(frame));
    const auto states=edactions::load(directory/"actions.bin");
    for(const auto& state:states)if(state.animation) {
        const auto& clip=mesh.clip(state.animation);assert(clip.frameCount==state.frames&&clip.flags==state.clipFlags&&state.bones==mesh.bones.size());
    }
    if(std::string(name)=="ash"&&!allParts) {
        const auto mixed=bytes(directory/"pose-layer-golden.bin");edmesh::detail::Reader layers(mixed);
        assert(layers.text(8)==std::string("EDMIX1\0\0",8));const auto layerCount=layers.u32();assert(layerCount==2);
        auto local=mesh.restPose();
        for(uint32_t i=0;i<layerCount;++i) {
            const auto id=layers.u32(),frame=layers.u32(),maskCount=layers.u32();assert(maskCount==17);
            const auto raw=layers.text(maskCount);const std::vector<uint8_t> mask(raw.begin(),raw.end());mesh.applyPose(local,id,frame,mask);
        }
        const auto posed=mesh.skinPose(local);const auto samples=layers.u32();assert(samples>=15);
        for(uint32_t i=0;i<samples;++i) {
            const auto index=layers.u32();assert(index<posed.size());const auto& v=posed[index];
            for(float actual:{v.x,v.y,v.z,v.nx,v.ny,v.nz,v.u,v.v})assert(std::abs(double(actual)-layers.number())<5e-5);
        }
        assert(layers.remaining()==0);
    }
    std::cout<<name<<": "<<mesh.clips.size()<<" clips, "<<count<<" golden frames, "<<checked
             <<" vertex comparisons, max position error "<<maxPosition<<", normal "<<maxNormal<<'\n';
}
}

int main(int argc,char** argv) {
    const auto data=fixture();const auto mesh=edmesh::readActorMesh(data);
    assert(mesh.vertices.size()==3&&mesh.bones.size()==2&&mesh.clips.size()==2);
    const auto pose=mesh.samplePose(100,0);
    assert(pose[0].x==10&&pose[0].y==5&&pose[1].x==10&&pose[1].y==8&&pose[2].x==9&&pose[2].y==7);
    assert(std::abs(pose[1].nx)<1e-6&&std::abs(pose[1].ny-1)<1e-6);
    assert(mesh.samplePose(100,1)[0].y==6); // Curve frame is used directly, despite clipFlags&2.
    assert((mesh.rootPosition(100,1)==std::array<float,3>{{4,6,8}}));
    auto local=mesh.restPose();mesh.applyPose(local,100,0);mesh.applyPose(local,200,0,{0,1});
    assert(local.rootY==5&&local.rotationsDegrees[1][2]==90&&local.rotationsDegrees[0][2]==0);
    mesh.applyPose(local,200,1,{2,0});assert(local.rootY==30&&local.rotationsDegrees[1][2]==90);
    assert(mesh.skinPose(local).size()==3);
    assert(mesh.skinPose(local,{false}).empty());
    auto tagged=data;tagged[7]='2';for(unsigned i=0;i<3;++i)tagged[54+i*36]=1;
    const auto parts=edmesh::readActorMesh(tagged);assert(parts.partCount==2&&parts.vertices[0].part==1);
    assert(parts.skinPose(parts.restPose(),{false,true}).size()==3&&parts.skinPose(parts.restPose(),{true,false}).empty());
    rejects([&]{parts.skinPose(parts.restPose(),{true});});
    tagged[54+36]=0;rejects([&]{edmesh::readActorMesh(tagged);});
    rejects([&]{mesh.samplePose(100,2);});rejects([&]{mesh.samplePose(999,0);});
    rejects([&]{mesh.applyPose(local,100,0,{1});});
    for(size_t length=0;length<data.size();++length)rejects([&]{edmesh::readActorMesh(std::vector<uint8_t>(data.begin(),data.begin()+length));});
    for(const auto [offset,value]:std::vector<std::pair<size_t,uint32_t>>{{8,0},{8,2},{12,257},{16,4097},{52,2},{52,0x10000},
            {140,0x3f800000},{204,0},{284,1},{288,0},{296,0},{300,0x7fc00000},{372,100}}) {
        auto bad=data;patch(bad,offset,value);rejects([&]{edmesh::readActorMesh(bad);});
    }
    auto bad=data;bad.push_back(0);rejects([&]{edmesh::readActorMesh(bad);});
    {auto jointPose=mesh.restPose();
        auto joint=mesh.jointPosition(jointPose,1);assert(joint[0]==10&&joint[1]==2&&joint[2]==0);
        jointPose.rotationsDegrees[0][2]=90;joint=mesh.jointPosition(jointPose,1);assert(std::abs(joint[0]-8)<1e-6f&&std::abs(joint[1])<1e-6f);
        rejects([&]{mesh.jointPosition(jointPose,2);});}
    if(argc>1) {
        const std::filesystem::path root=argv[1];
        sourceGoldens(root/"actors/ash","ash",60,1590);sourceGoldens(root/"actors/deadite","deadite",23,1104);
        sourceGoldens(root/"actors/skullbat","skullbat",5,306,false,5);
        sourceGoldens(root/"actors/skeleton","skeleton",27,1098,false,16);
        sourceGoldens(root/"actors/hellbillyfat","hellbillyfat",19,1020,false,16);
        sourceGoldens(root/"actors/hellbillythin","hellbillythin",18,834,false,16);
        sourceGoldens(root/"actors/demonpig","demonpig",18,894,false,25);
        sourceGoldens(root/"actors/headlessskeleton","headlessskeleton",14,1158,false,16);
        sourceGoldens(root/"actors/troopknife","troopknife",22,762,false,16);
        sourceGoldens(root/"actors/troopclaw","troopclaw",22,822,false,16);
        sourceGoldens(root/"actors/trooprange","trooprange",20,834,false,16);
        sourceGoldens(root/"actors/treeboss","treeboss",10,4050,false,45);
        sourceGoldens(root/"actors/treearm","treearm",3,222,false,4);
        sourceGoldens(root/"actors/troopleaderboss","troopleaderboss",16,1422,false,18);
        sourceGoldens(root/"actors/matriarchboss","matriarchboss",15,2148,false,32);
        sourceGoldens(root/"actors/deadash","deadash",17,2112,false,25);
        sourceGoldens(root/"actors/anniezombie","anniezombie",26,2838,false,57);
        sourceGoldens(root/"actors/ash","ash",60,3996,true);
        const auto allAsh=edmesh::ActorMesh::load(root/"actors/ash/actor-parts.bin");
        assert(allAsh.partCount==14&&allAsh.vertices.size()==3996);
        std::vector<bool> visible(allAsh.partCount,false);for(unsigned part:{0,6,12})visible[part]=true;
        assert(allAsh.skinPose(allAsh.restPose(),visible).size()==1710);
    }
}
