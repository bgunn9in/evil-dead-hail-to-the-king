#pragma once
#include "source_gui_presentation.h"
#include "source_gui_text.h"
#include <variant>

namespace edguipresentation {
struct Command {int item=-1;std::variant<edguitext::Page,Sprite> value;};
struct Frame {bool complete=false;unsigned items=0;std::vector<Command> commands;};
class Authored {
    std::filesystem::path root;
    mutable std::map<uint32_t,edobjects::ObjectMesh> meshes;
    static const std::vector<int32_t>& property(const SourceGui::Record& item,const char* name,size_t size) {
        const auto found=item.properties.find(name);if(found==item.properties.end()||found->second.size()!=size)throw jpc::Error("Original authored GUI property is unavailable");return found->second;
    }
    static uint32_t rgba(const SourceGui::Record& item) {
        const auto found=item.properties.find("guiItemSetColor");if(found==item.properties.end())return 0x80808080u;
        if(found->second.size()!=4)throw jpc::Error("Original authored GUI color is invalid");uint32_t result=0;for(int value:found->second)result=(result<<8)|uint32_t(value);return result;
    }
    static uint32_t rgb(const SourceGui::Record& item) {return 0xff000000u|(rgba(item)>>8);}
    static int word(int value){return int(int16_t(uint16_t(value)));}
    static unsigned alignment(uint32_t flags){return flags&0x4000?0:flags&0x8000?1:flags&0x10000?2:0;}
    // shortcut: these source render paths keep fixed bounds; retain native cached mouse bounds before enabling source mouse picking.
    static edguitext::Rect bounds(const edguitext::Bounds& b){return {word(int(320.0+double(b.left)*20.0)),word(int(240.0-double(b.top)*20.0)),word(int(320.0+double(b.right)*20.0)),word(int(240.0-double(b.bottom)*20.0))};}
    static Sprite rectangle(int item,float l,float t,float r,float b,uint32_t color) {
        Sprite out;out.item=item;out.texture=0;out.color=color;
        l=l/320.f-1.f;r=r/320.f-1.f;t=1.f-t/240.f;b=1.f-b/240.f;
        const edmesh::Vertex32 a{l,t,.5f,0,0,1,0,0},c{r,t,.5f,0,0,1,1,0},d{r,b,.5f,0,0,1,1,1},e{l,b,.5f,0,0,1,0,1};
        out.vertices={a,c,d,a,d,e};return out;
    }
    const edobjects::ObjectMesh& mesh(uint32_t id,const SourceGui& gui)const {
        auto found=meshes.find(id);if(found!=meshes.end())return found->second;
        char name[32];std::snprintf(name,sizeof(name),"%08x.mesh",id);auto value=edobjects::ObjectMesh::load(root/"gui/all-models"/name);
        if(value.identity!=id||!gui.uvLayout)throw jpc::Error("Original authored GUI model identity differs");
        const auto source=gui.uvLayout->models.find(id);if(source==gui.uvLayout->models.end()||source->second.size()!=value.partFlags.size())throw jpc::Error("Original authored GUI model/UV part count differs");
        std::vector<size_t> offsets(value.partFlags.size());for(const auto& vertex:value.vertices){const auto& part=source->second[vertex.part];auto& offset=offsets[vertex.part];
            if(offset>=part.indices.size())throw jpc::Error("Original authored GUI UV indices differ");const auto& uv=part.uv[part.indices[offset++]];
            if(vertex.geometry.u!=uv[0]||vertex.geometry.v!=uv[1])throw jpc::Error("Original authored GUI source UV differs");}
        for(size_t n=0;n<offsets.size();++n)if(offsets[n]!=source->second[n].indices.size())throw jpc::Error("Original authored GUI vertex count differs");
        return meshes.emplace(id,std::move(value)).first->second;
    }
public:
    explicit Authored(std::filesystem::path directory):root(std::move(directory)){}
    Frame sample(SourceGui& gui,const edguitext::Catalog& texts,uint32_t scene,int mode,
                 const std::function<std::optional<std::string>(uint32_t)>& special={},double seconds=0)const {
        Frame out;if(!gui.uvLayout)return out;
        std::map<uint32_t,std::optional<std::string>> resolved;
        const auto resolvedText=[&](uint32_t id)->const std::string* {
            auto found=resolved.find(id);if(found==resolved.end())found=resolved.emplace(id,special?special(id):std::optional<std::string>{}).first;
            return found->second?&*found->second:nullptr;
        };
        std::vector<std::pair<int,int>> order;
        for(size_t system=0;system<gui.systems.size();++system){if((gui.systems[system].flags&9u)!=9u)continue;
            std::vector<std::pair<int,int>> items;for(size_t i=0;i<gui.items.size();++i)if(gui.items[i].owner==int(system))items.emplace_back(gui.items[i].ordinal,int(i));
            std::sort(items.begin(),items.end());for(const auto& [ordinal,id]:items){(void)ordinal;const auto& item=gui.items[size_t(id)];const auto renderer=item.properties.find("guiItemSetRenderFunc");
                if(renderer==item.properties.end()||renderer->second==std::vector<int32_t>{0})break; // Null callback terminates the native linked chain.
                if(renderer->second.size()!=1||renderer->second[0]<2||renderer->second[0]>6)throw jpc::Error("Unsupported authored GUI render callback");
                if(!(item.flags&1u)||renderer->second[0]==2)continue;
                const auto& maya=property(item,"guiItemSetMayaInfo",3);const auto model=uint32_t(maya[0]);const int part=maya[2];const int function=renderer->second[0];
                if(function==4&&model==0)continue; // Native4200DD skips an absent model before bounds, flags or texture.
                if(function==3){const auto resourceText=uint32_t(property(item,"guiItemSetDataID",1)[0]);if(!resourceText||resourceText==0xffffffffu)continue;
                    if(!resolvedText(resourceText)&&!texts.contains(scene,resourceText)){gui.finishRender();return out;}texts.bounds(model,unsigned(uint8_t(part)));}
                else {const auto& data=mesh(model,gui);if(part< -1||part>=int(data.partFlags.size()))throw jpc::Error("Original authored GUI selected part is out of range");
                    if(function!=4&&part<0)throw jpc::Error("Original authored GUI bounds need one selected part");
                    if(function==4){if(uint32_t(maya[1])>>24!=11)throw jpc::Error("Original authored GUI texture is invalid");}
                    else texts.bounds(model,unsigned(part));}
                order.emplace_back(int(system),id);
            }
        }
        if(order.empty()){out.complete=true;gui.finishRender();return out;}
        // Build against a copy: a malformed later item must not consume an earlier text advance.
        SourceGui next=gui;
        for(const auto& [systemIndex,id]:order){const auto& system=next.systems[size_t(systemIndex)];auto& item=next.items[size_t(id)];const int function=property(item,"guiItemSetRenderFunc",1)[0];const auto& maya=property(item,"guiItemSetMayaInfo",3);
            const auto model=uint32_t(maya[0]);const int part=maya[2];++out.items;
            if(function==3){const auto resource=uint32_t(property(item,"guiItemSetDataID",1)[0]);auto page=edguitext::sample(next,texts,scene,id,mode,false,resolvedText(resource),seconds).page;if(!page.glyphs.empty()||page.background)out.commands.push_back({id,std::move(page)});continue;}
            if(function==6){auto area=edguitext::window(texts.bounds(model,unsigned(part)),item,system);const int value=property(item,"guiItemSetDataID",1)[0];
                auto page=edguitext::marquee(std::to_string(value),area,alignment(item.flags),rgb(item),seconds);if(!page.glyphs.empty())out.commands.push_back({id,std::move(page)});continue;}
            auto area=part<0?edguitext::Rect{}:bounds(texts.bounds(model,unsigned(part)));const int x=word(int(item.x)+system.x),y=word(int(item.y)+system.y);
            if(function==5){const auto& values=property(item,"guiItemSetMayaVal",3);const int crop=values[1];const int percentage=crop==2?(values[0]==99?100:values[0]):int(int8_t(uint8_t(values[0])));
                float l=float(area.left),t=float(area.top),r=float(area.right),b=float(area.bottom);const double amount=double(percentage)*double(.01f);
                if(crop==0)l=float(double(l)+double(area.right-area.left)*amount);else if(crop==1)t=float(double(t)+double(area.bottom-area.top)*amount);
                else if(crop==2)r=float(double(r)-double(area.right-area.left)*amount);else if(crop==3)b=float(double(b)-double(area.bottom-area.top)*amount);
                out.commands.push_back({id,rectangle(id,float(int(double(l)+x)),float(int(double(t)+y)),float(int(double(r)+x)),float(int(double(b)+y)),rgb(item))});continue;}
            if((item.flags&0x400u)&&(part<0||!(area.left||area.top||area.right||area.bottom)))item.flags^=0x400u;
            if(item.flags&0x400u){
                const uint32_t color=rgb(item);uint32_t tinted=0xff000000u;for(unsigned shift:{0u,8u,16u})tinted|=(((color>>shift)&255u)*unsigned(next.selectionPulse)/255u)<<shift;
                out.commands.push_back({id,rectangle(id,float(area.left+int(item.x)+system.x+2),float(area.top+int(item.y)+system.y+2),float(area.right+int(item.x)+system.x-2),float(area.bottom+int(item.y)+system.y-2),tinted)});}
            Sprite sprite;sprite.item=id;sprite.texture=uint32_t(maya[1]);const auto& source=mesh(model,next);std::vector<size_t> offsets(source.partFlags.size());
            const int modelX=systemIndex==23&&system.selected==72&&part==0?2:x; // Native42037C fixed pool-ID placement case.
            for(const auto& sourceVertex:source.vertices){const auto index=sourceVertex.part;const auto& templatePart=next.uvLayout->at({model,int(index)});const auto ordinal=templatePart.indices.at(offsets[index]++);
                if(part>=0&&part!=int(index))continue;auto vertex=sourceVertex.geometry;const auto origin=next.uvSource({model,int(index)});const auto& uv=next.uvLayout->at(origin).uv.at(ordinal);
                vertex.u=uv[0];vertex.v=uv[1];const float px=float(double(vertex.x)*20.0+double(320+modelX)),py=float(double(240+y)-double(vertex.y)*20.0);
                vertex.x=px/320.f-1.f;vertex.y=1.f-py/240.f;vertex.z=.5f;vertex.nx=vertex.ny=0;vertex.nz=1;sprite.vertices.push_back(vertex);}
            if(!sprite.vertices.empty())out.commands.push_back({id,std::move(sprite)});
        }
        out.complete=true;next.finishRender();gui=std::move(next);return out;
    }
};
} // namespace edguipresentation
