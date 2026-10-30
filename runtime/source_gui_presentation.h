#pragma once
#include "source_gui.h"
#include "object_mesh.h"

namespace edguipresentation {
struct Sprite {int item=-1;uint32_t texture=0;std::vector<edmesh::Vertex32> vertices;uint32_t color=0xffffffff;};
class GameOver {
    edobjects::ObjectMesh mesh;
public:
    explicit GameOver(const std::filesystem::path& root):mesh(edobjects::ObjectMesh::load(root/"gui/models/game-over.mesh")) {
        if(mesh.identity!=0x0b142088u||mesh.partFlags.size()!=8)throw jpc::Error("Original GameOver geometry differs");
    }
    std::vector<Sprite> sample(const SourceGui& gui,const jpc::Interpreter& vm)const {
        std::vector<Sprite> output;
        for(size_t systemIndex=0;systemIndex<gui.systems.size();++systemIndex) {
            const auto& system=gui.systems[systemIndex];
            if(!(system.flags&1)||!system.pad||vm.objectClass(system.pad)!=vm.package.classIndex("GameOver"))continue;
            for(size_t index=0;index<gui.items.size();++index) {
                const auto& item=gui.items[index];if(item.owner!=int(systemIndex)||!(item.flags&1))continue;
                const auto maya=item.properties.find("guiItemSetMayaInfo"),renderer=item.properties.find("guiItemSetRenderFunc");
                if(maya==item.properties.end()||renderer==item.properties.end())continue;
                if(maya->second!=std::vector<int32_t>{0x0b142088,0x0b942034,0}||renderer->second!=std::vector<int32_t>{4})
                    throw jpc::Error("Unsupported original GameOver Maya request");
                Sprite sprite;sprite.item=int(index);sprite.texture=0x0b942034;
                const int x=int16_t(uint16_t(int(item.x)+system.x)),y=int16_t(uint16_t(int(item.y)+system.y));
                for(const auto& source:mesh.vertices)if(source.part==0) {
                    auto v=source.geometry;const float px=v.x*20.f+float(320+x),py=float(240+y)-v.y*20.f;
                    v.x=px/320.f-1.f;v.y=1.f-py/240.f;v.z=.5f;v.nx=v.ny=0;v.nz=1;sprite.vertices.push_back(v);
                }
                if(sprite.vertices.size()!=6)throw jpc::Error("Original GameOver part is not its authored quad");output.push_back(std::move(sprite));
            }
        }
        return output;
    }
};
}
