#include "object_mesh.h"
#undef NDEBUG
#include <cassert>
#include <iostream>
#include <map>
#include <set>

template<class F>static bool rejects(F fn){try{fn();return false;}catch(const std::exception&){return true;}}
int main(int argc,char** argv){try{
    for(int type=0;type<200;++type) {
        const std::set<int> special{48,53,133,134,135,143,144,146,161};
        assert(edobjects::alphaBlended(int16_t(type))==bool(special.count(type)));
    }
    if(argc!=2&&!(argc==3&&std::string(argv[2])=="--dreamcast"))throw std::runtime_error("Pass exported episode asset root and optional --dreamcast");
    const bool dreamcast=argc==3;const std::filesystem::path root=std::filesystem::path(argv[1])/"objects";
    std::map<uint32_t,edobjects::ObjectMesh> meshes;unsigned samples=0;
    const auto golden=edobjects::detail::read(root/"pose-goldens.bin",8*1024*1024);edobjects::detail::Reader input{golden};input.magic("EDOBPOSE");
    unsigned modelFiles=0;for(const auto& file:std::filesystem::directory_iterator(root/"models"))if(file.path().extension()==".bin")++modelFiles;
    const auto cases=input.get<uint32_t>();assert(modelFiles>=19&&cases==3*modelFiles);
    for(unsigned i=0;i<cases;++i){const auto identity=input.get<uint32_t>();
        if(!meshes.count(identity)){char name[20];std::snprintf(name,sizeof(name),"%08x.bin",identity);meshes.emplace(identity,edobjects::ObjectMesh::load(root/"models"/name));}
        edobjects::Transform transform;for(auto& n:transform.position)n=input.get<float>();for(auto& n:transform.degrees)n=input.get<int32_t>();
        transform.floorHeight=input.get<float>();transform.flags=uint16_t(input.get<uint32_t>());const auto count=input.get<uint32_t>();
        const auto actual=meshes.at(identity).drawVertices(transform);assert(actual.size()==count);
        for(const auto& a:actual){const auto b=input.get<edobjects::Vertex>();
            const auto& v=a.geometry;const auto& w=b.geometry;const float x[]={v.x,v.y,v.z,v.nx,v.ny,v.nz,v.u,v.v},y[]={w.x,w.y,w.z,w.nx,w.ny,w.nz,w.u,w.v};
            for(unsigned axis=0;axis<8;++axis)assert(std::abs(x[axis]-y[axis])<3e-6f);
            assert(a.color==b.color&&a.color==0xffffffff&&a.part==b.part&&a.reserved==0);++samples;}
    }
    assert(input.cursor==golden.size()&&meshes.size()==modelFiles);
    const auto& first=meshes.begin()->second;edobjects::Transform placement;placement.position={{1,2,3}};placement.floorHeight=4;
    auto relative=first.drawVertices(placement);placement.flags&=~2;const auto absolute=first.drawVertices(placement);
    assert(relative[0].geometry.y-absolute[0].geometry.y==4);
    assert(first.drawVertices(placement,std::vector<bool>(first.partFlags.size(),false)).empty());
    placement.flags=0;assert(first.drawVertices(placement).empty());placement.position[0]=std::numeric_limits<float>::quiet_NaN();
    assert(rejects([&]{first.drawVertices(placement);}));
    const auto bytes=edobjects::detail::read(root/"models"/"0b018328.bin",1000000);auto bad=bytes;bad.pop_back();assert(rejects([&]{edobjects::ObjectMesh::decode(bad);}));
    bad=bytes;bad[16]=1;assert(rejects([&]{edobjects::ObjectMesh::decode(bad);}));
    bad=bytes;const float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(bad.data()+24,&nan,4);assert(rejects([&]{edobjects::ObjectMesh::decode(bad);}));
    bad=bytes;bad[24+36]=1;assert(rejects([&]{edobjects::ObjectMesh::decode(bad);}));
    bad=bytes;bad[24+38]=1;assert(rejects([&]{edobjects::ObjectMesh::decode(bad);}));
    unsigned transparent=0,textures=0;
    for(const auto& file:std::filesystem::directory_iterator(root/"textures"))if(file.path().extension()==".rgba"){
        const auto textureBytes=edobjects::detail::read(file.path(),1024*1024);const auto texture=edobjects::Texture::decode(textureBytes);
        assert(texture.width>0&&texture.height>0&&texture.rgba.size()==size_t(texture.width)*texture.height*4);std::set<uint8_t> alpha;
        for(size_t pixel=3;pixel<texture.rgba.size();pixel+=4)alpha.insert(texture.rgba[pixel]);
        if(dreamcast)for(auto value:alpha)assert(value%17==0);
        if(alpha.size()>1){++transparent;if(!dreamcast)assert(alpha.count(0)&&alpha.count(255)&&alpha.size()>=24);}++textures;
        auto badTexture=textureBytes;badTexture.pop_back();assert(rejects([&]{edobjects::Texture::decode(badTexture);}));
        badTexture=textureBytes;const uint32_t huge=0xffffffff;std::memcpy(badTexture.data()+12,&huge,4);assert(rejects([&]{edobjects::Texture::decode(badTexture);}));
    }
    assert(textures>=3&&transparent>=2);
    std::cout<<"Original objects:"<<modelFiles<<" meshes/"<<textures<<" textures; "<<samples<<" source-transformed vertices, BV/visibility/malformed assets: passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
