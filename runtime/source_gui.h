#pragma once
#include "jpc.h"
#include "state_bytes.h"
#include "source_gui_uv.h"
#include <array>
#include <map>

// Native PC GUI pools:60 systems at4395F40(stride3C),250 items at4396D60
// (stride68). The modern renderer may present this state through its own UI.
struct SourceGui {
    struct Record {
        uint32_t flags=0,pad=0;int16_t x=0,y=0,textOffset=0;int owner=-1,ordinal=0,selected=-1;
        std::array<int,4> links{{-1,-1,-1,-1}}; // Native item+48/system+18: L/R/U/D.
        std::map<std::string,std::vector<int32_t>> properties;
    };
    std::shared_ptr<const edguiuv::Layout> uvLayout;
    std::map<edguiuv::Key,edguiuv::Key> uvCopies;
    uint64_t uvFingerprint=0;
    int16_t selectionPulse=255;bool pulseDescending=false;
    void finishRender() {selectionPulse=int16_t(selectionPulse+(pulseDescending?-4:4));
        if(selectionPulse>=255){selectionPulse=255;pulseDescending=true;}
        else if(selectionPulse<=128){selectionPulse=128;pulseDescending=false;}}
    void loadUVs(const std::filesystem::path& path) {
        auto next=edguiuv::Layout::load(path);if(uvFingerprint&&uvFingerprint!=next->fingerprint)throw jpc::Error("Saved original GUI UV resources differ");
        for(const auto& [destination,source]:uvCopies)if(next->at(destination).uv.size()>next->at(source).uv.size())throw jpc::Error("Saved original GUI UV source is too small");
        uvLayout=std::move(next);uvFingerprint=uvLayout->fingerprint;
    }
    edguiuv::Key uvSource(edguiuv::Key key)const {const auto found=uvCopies.find(key);return found==uvCopies.end()?key:found->second;}
    void copyUVs(edguiuv::Key destination,edguiuv::Key source) {
        if(!uvLayout)return; // shortcut: legacy profiles lack indexed UV metadata; authored UI requires the sidecar.
        if(!uvLayout->models.count(source.first))return; // Native41E055: absent source resource is a no-op.
        const auto& target=uvLayout->at(destination);const auto& from=uvLayout->at(source);const auto origin=uvSource(source);
        if(target.uv.size()>from.uv.size()||target.uv.size()>uvLayout->at(origin).uv.size())throw jpc::Error("Original GUI UV copy exceeds source vertices");
        // Resolve the current source now: later changes to it cannot alter this copied value.
        if(destination==origin)uvCopies.erase(destination);else uvCopies[destination]=origin;
    }
    static std::vector<int32_t>& maya(Record& record) {auto& value=record.properties["guiItemSetMayaInfo"];if(value.empty())value.resize(3);if(value.size()!=3)throw jpc::Error("Invalid original GUI Maya state");return value;}
    static std::vector<int32_t>& mayaValues(Record& record) {auto& value=record.properties["guiItemSetMayaVal"];if(value.empty())value.resize(3);if(value.size()!=3)throw jpc::Error("Invalid original GUI Maya bytes");return value;}
    std::array<Record,60> systems;
    std::array<Record,250> items;
    int stereo=1,masterVolume=100,activeSystem=-1; // Original417BC9/417BDB defaults.
    static constexpr const char* Setters[]={
        "guiItemSetRenderFunc","guiItemSetDataID","guiItemSetMayaInfo","guiItemSetMayaVal",
        "guiItemSetColor","guiItemLink","guiSysLink","guiItemSetPos","guiSysSetPos",
        "guiItemAdjustPos","guiSysAdjustPos","guiItemSetPartID","guiItemSetMayaInfoModelID",
        "guiItemSetMayaInfoTextureID","guiItemSetOffset","guiItemAdjustOffset",
        "guiItemSetPartUVUsingPart","guiItemAdvanceText",
        "guiSysSetOffset","guiSysAdjustOffset","guiSysOffsetMoveTo","guiSysSetRenderFunc",
        "guiSysSetFlag","guiSysAddFlag","guiSysRemoveFlag","guiItemSetFlag","guiItemAddFlag","guiItemRemoveFlag",
        "guiSysSetPadObj","guiItemSetPadObj","guiItemSelect"};
    static bool setter(const std::string& name) {
        for(const auto* supported:Setters)if(name==supported)return true;return false;
    }
    template<size_t N>static Record& at(std::array<Record,N>& table,int index) {
        if(index<0||size_t(index)>=N)throw jpc::Error("GUI handle is outside original pool");return table[size_t(index)];
    }
    template<size_t N>static const Record& at(const std::array<Record,N>& table,int index) {
        if(index<0||size_t(index)>=N)throw jpc::Error("GUI handle is outside original pool");return table[size_t(index)];
    }
    static int16_t word(int64_t value){return int16_t(uint16_t(uint64_t(value)));}
    static int createFlags(uint32_t previous,int argument){return jpc::signedBits(previous|uint32_t(argument)|8u);}
    static void removeFlag(Record& record,uint32_t mask) {if(record.flags&mask)record.flags^=mask;}
    void itemEvent(jpc::Interpreter& vm,int item,int event) {
        auto& record=at(items,item);if(!record.pad)return;
        vm.startCallback(vm.package.methodIndex("Pad","actionItem","(Led/base/EDBase;III)V"),0,
            {jpc::Value::reference(record.pad),jpc::Value::integer(event),
             jpc::Value::integer(item),jpc::Value::integer(record.ordinal)},0);
    }
    void systemEvent(jpc::Interpreter& vm,int system,int event) {
        auto& record=at(systems,system);if(!record.pad)return;
        vm.startCallback(vm.package.methodIndex("Pad","actionSys","(Led/base/EDBase;III)V"),0,
            {jpc::Value::reference(record.pad),jpc::Value::integer(event),
             jpc::Value::integer(system),jpc::Value::integer(0)},0); // Native system+0C remains BSS0; item-count is byte+1.
    }
    void select(jpc::Interpreter& vm,int system,int item) {
        auto& sys=at(systems,system);auto& next=at(items,item);
        if(next.owner!=system)throw jpc::Error("GUI selection belongs to another system");
        if(sys.selected==item)return;
        if(sys.selected>=0) {removeFlag(at(items,sys.selected),4);itemEvent(vm,sys.selected,98);}
        sys.selected=item;next.flags|=4;itemEvent(vm,item,97); // Native40B780 immediate callbacks.
    }
    void setSystem(jpc::Interpreter& vm,int system) {
        auto& next=at(systems,system);if(activeSystem==system)return;
        const int previous=activeSystem;
        if(previous>=0)systemEvent(vm,previous,98);
        activeSystem=system;systemEvent(vm,system,97);
        //40B56C uses the captured new system even when its callback reenters.
        if(next.selected>=0) {auto& item=at(items,next.selected);item.flags|=4;itemEvent(vm,next.selected,97);}
    }
    void navigate(jpc::Interpreter& vm,int system,unsigned direction) {
        auto& sys=at(systems,system);if(direction>=4)throw jpc::Error("Invalid GUI link direction");
        if(sys.selected<0)return;
        auto& selected=at(items,sys.selected);int candidate=selected.links[direction];
        unsigned traversed=0;
        while(candidate>=0&&!(at(items,candidate).flags&16u)) {
            if(++traversed>items.size())throw jpc::Error("Source GUI nonselectable link cycle");
            candidate=at(items,candidate).links[direction];
        }
        if(candidate>=0) {select(vm,system,candidate);return;}
        //40ACEC/40BDEB/40BE5B/40BEFB permit linked-system focus only when
        // the current item's bit0x40 is set; disabled entries are skipped.
        if((selected.flags&64u)&&sys.links[direction]>=0)setSystem(vm,sys.links[direction]);
    }
    void dispatchInput(jpc::Interpreter& vm,const std::vector<int>& events) {
        //40BCD5 captures the active system once for the entire input batch.
        const int system=activeSystem;if(system<0)return;at(systems,system);
        for(int event:events) {
            const int item=systems[size_t(system)].selected;
            if(item>=0)itemEvent(vm,item,event);
            systemEvent(vm,system,event);
            if(event>=103&&event<=106)navigate(vm,system,unsigned(event-103));
        }
    }
    void input(jpc::Interpreter& vm,int event){dispatchInput(vm,{event});}
    jpc::Value native(jpc::Interpreter& vm,const std::string& name,const std::vector<jpc::Value>& values) {
        std::vector<int32_t> a;for(auto value:values)a.push_back(jpc::signedBits(value.bits));
        const auto integer=[](int n){return jpc::Value::integer(n);};
        if(name=="guiSysSet") {setSystem(vm,a.at(0));return {};}
        if(name=="guiSysCreate") {
            for(size_t i=0;i<systems.size();++i)if(!(systems[i].flags&8)) {
                auto& sys=systems[i];sys.flags=uint32_t(createFlags(sys.flags,a.at(0)));sys.selected=-1;sys.links.fill(-1);
                return integer(int(i));
            }
            throw jpc::Error("Original GUI system pool exhausted");
        }
        if(name=="guiItemCreate") {
            auto& sys=at(systems,a.at(1));
            for(size_t i=0;i<items.size();++i)if(!(items[i].flags&8)) {
                auto& item=items[i];item.flags=uint32_t(createFlags(item.flags,a.at(0)));
                item.owner=a[1];item.ordinal=sys.ordinal++;
                std::map<std::string,std::vector<int32_t>> retained;
                for(const auto* name:{"guiItemSetMayaInfo","guiItemSetMayaVal","guiItemSetDataID"}){const auto old=item.properties.find(name);if(old!=item.properties.end())retained.emplace(*old);}
                item.properties=std::move(retained);(void)maya(item);(void)mayaValues(item);if(!item.properties.count("guiItemSetDataID"))item.properties["guiItemSetDataID"]={0};item.properties["guiItemSetColor"]={128,128,128,128};item.properties["guiItemSetRenderFunc"]={0};
                item.pad=0;item.textOffset=0;item.links.fill(-1);
                if(sys.selected<0)sys.selected=int(i);return integer(int(i));
            }
            throw jpc::Error("Original GUI item pool exhausted");
        }
        if(name=="guiSysGetFlag")return integer(jpc::signedBits(at(systems,a.at(0)).flags));
        if(name=="guiSysAtOffset") {
            const auto& sys=at(systems,a.at(0));return integer(int(sys.x)==int64_t(a.at(1))*2&&int(sys.y)==int64_t(a.at(2))*2);
        }
        if(name=="guiItemGetDataID") {
            const auto& props=at(items,a.at(0)).properties;const auto found=props.find("guiItemSetDataID");
            return integer(found==props.end()?0:found->second.at(0));
        }
        if(name=="guiItemAdvanceText") {
            //40B760 ignores argument2 and only sets0x200. The text renderer
            // consumes this request after finding the next source page.
            at(items,a.at(0)).flags|=0x200u;return {};
        }
        if(!setter(name))throw jpc::Error("Unsupported original GUI service: "+name);
        if(name=="guiItemLink"||name=="guiSysLink") {
            const auto link=[&](auto& table) {
                auto& first=at(table,a.at(0));auto& second=at(table,a.at(2));
                if(a.at(1)<0||a[1]>=4||a.at(3)<0||a[3]>=4)throw jpc::Error("Invalid GUI link direction");
                first.links[size_t(a[1])]=a[2];second.links[size_t(a[3])]=a[0];
            };
            if(name=="guiItemLink")link(items);else link(systems);return {};
        }
        const bool system=name.rfind("guiSys",0)==0;
        auto& record=system?at(systems,a.at(0)):at(items,a.at(0));
        if(name=="guiItemSelect"){select(vm,a.at(0),a.at(1));return {};}
        if(name=="guiItemSetDataID")record.textOffset=0; //40BBF0 resets item+2 even for the same ID.
        if(name=="guiSysSetPadObj"||name=="guiItemSetPadObj")record.pad=values.at(1).reference();
        else if(name=="guiSysSetFlag"||name=="guiItemSetFlag")record.flags=uint32_t(a.at(1));
        else if(name=="guiSysAddFlag"||name=="guiItemAddFlag")record.flags|=uint32_t(a.at(1));
        else if(name=="guiSysRemoveFlag"||name=="guiItemRemoveFlag")removeFlag(record,uint32_t(a.at(1)));
        else if(name=="guiSysSetOffset"||name=="guiItemSetOffset") {record.x=word(int64_t(a.at(1))*2);record.y=word(int64_t(a.at(2))*2);}
        else if(name=="guiSysAdjustOffset"||name=="guiItemAdjustOffset") {record.x=word(record.x+int64_t(a.at(1))*2);record.y=word(record.y+int64_t(a.at(2))*2);}
        else if(name=="guiSysOffsetMoveTo") {
            const auto move=[](int16_t current,int target,int speed) {
                const int64_t goal=int64_t(target)*2,step=int64_t(speed)*2;
                if(int(current)<goal)return word(std::min(goal,int64_t(word(current+step))));
                if(int(current)>goal)return word(std::max(goal,int64_t(word(current-step))));return current;
            };
            // Native40B650 advances once per call; args3/4 are per-call speeds.
            record.x=move(record.x,a.at(1),a.at(3));record.y=move(record.y,a.at(2),a.at(4));
        }
        if(name=="guiItemSetColor"&&a.size()!=5)throw jpc::Error("Invalid original GUI color argument count");
        if(name=="guiItemSetRenderFunc"||name=="guiSysSetRenderFunc") {
            if(a.size()!=2)throw jpc::Error("Invalid original GUI renderer argument count");
            if(a[1]==1||uint32_t(a[1])>unsigned(system?5:6))return {}; // Native setter leaves the previous callback.
        } else if(name=="guiItemSetMayaInfo") {
            if(a.size()!=4)throw jpc::Error("Invalid original GUI Maya argument count");
            record.properties[name]={a[1],a[2],int8_t(uint8_t(a[3]))};return {};
        } else if(name=="guiItemSetPartID") {
            if(a.size()!=2)throw jpc::Error("Invalid original GUI part argument count");maya(record)[2]=int8_t(uint8_t(a[1]));
        } else if(name=="guiItemSetMayaInfoModelID"||name=="guiItemSetMayaInfoTextureID") {
            if(a.size()!=2)throw jpc::Error("Invalid original GUI texture argument count");maya(record)[1]=a[1]; // Both names register40BB30.
        } else if(name=="guiItemSetMayaVal") {
            if(a.size()!=3||a[1]<0||a[1]>=3)throw jpc::Error("Original GUI Maya byte is outside the three control bytes");
            mayaValues(record)[size_t(a[1])]=uint8_t(a[2]);return {};
        } else if(name=="guiItemSetPartUVUsingPart") {
            if(a.size()!=3)throw jpc::Error("Invalid original GUI UV argument count");
            const auto& target=maya(record);copyUVs({uint32_t(target[0]),target[2]},{uint32_t(a[1]),a[2]});
        }
        record.properties[name]=std::vector<int32_t>(a.begin()+1,a.end());return {};
    }
    std::vector<uint8_t> snapshot() const {
        statebytes::Writer out;out.text("EDGUI4");out.put<int32_t>(stereo);out.put<int32_t>(masterVolume);out.put<int32_t>(activeSystem);
        const auto table=[&](const auto& records) {
            out.put<uint32_t>(uint32_t(records.size()));for(const auto& record:records) {
                out.put(record.flags);out.put(record.pad);out.put(record.x);out.put(record.y);out.put(record.textOffset);
                out.put<int32_t>(record.owner);out.put<int32_t>(record.ordinal);out.put<int32_t>(record.selected);
                for(int link:record.links)out.put<int32_t>(link);
                out.put<uint32_t>(uint32_t(record.properties.size()));for(const auto& [name,args]:record.properties) {
                    out.text(name);out.put<uint32_t>(uint32_t(args.size()));for(int32_t value:args)out.put(value);
                }
            }
        };
        table(systems);table(items);out.put<uint64_t>(uvFingerprint);out.put<int16_t>(selectionPulse);out.flag(pulseDescending);out.put<uint32_t>(uint32_t(uvCopies.size()));
        for(const auto& [destination,source]:uvCopies){out.put(destination.first);out.put<int32_t>(destination.second);out.put(source.first);out.put<int32_t>(source.second);}
        return std::move(out.bytes);
    }
    void validateReferences(const jpc::Interpreter& vm) const {
        const int base=vm.package.classIndex("ed/base/EDBase");
        const auto valid=[&](const Record& record) {
            if(!record.pad)return;int owner=vm.objectClass(record.pad);
            while(owner>=0&&owner!=base)owner=vm.package.classes[size_t(owner)].parent;
            if(owner!=base)throw jpc::Error("Source GUI callback does not reference EDBase");
        };
        if(activeSystem>=0&&!(at(systems,activeSystem).flags&8))throw jpc::Error("Source GUI active system is not allocated");
        for(size_t index=0;index<systems.size();++index) {
            const auto& record=systems[index];valid(record);
            if(record.selected>=0) {
                const auto& item=at(items,record.selected);
                if(!(record.flags&8)||!(item.flags&8)||item.owner!=int(index))
                    throw jpc::Error("Source GUI selection references another system");
            }
            for(int link:record.links)if(link>=0&&!(at(systems,link).flags&8))throw jpc::Error("Source GUI links an unallocated system");
        }
        for(const auto& record:items) {
            valid(record);
            if((record.flags&8)&&(record.owner<0||!(at(systems,record.owner).flags&8)))throw jpc::Error("Source GUI item owner is not allocated");
            for(int link:record.links)if(link>=0&&!(at(items,link).flags&8))throw jpc::Error("Source GUI links an unallocated item");
        }
    }
    void restore(const std::vector<uint8_t>& bytes) {
        statebytes::Reader in{bytes};const auto version=in.text();
        if(version!="EDGUI2"&&version!="EDGUI3"&&version!="EDGUI4")throw jpc::Error("Unsupported source GUI state");
        SourceGui next;next.stereo=in.get<int32_t>();next.masterVolume=in.get<int32_t>();next.activeSystem=in.get<int32_t>();
        if(next.stereo<0||next.stereo>1||next.masterVolume<0||next.masterVolume>100||next.activeSystem< -1||next.activeSystem>=60)
            throw jpc::Error("Invalid source GUI/audio option");
        const auto table=[&](auto& records,bool system) {
            if(in.count(uint32_t(records.size()))!=records.size())throw jpc::Error("Invalid source GUI pool size");
            for(auto& record:records) {
                record.flags=in.get<uint32_t>();record.pad=in.get<uint32_t>();record.x=in.get<int16_t>();record.y=in.get<int16_t>();
                record.textOffset=version!="EDGUI2"?in.get<int16_t>():0;
                if(record.textOffset<0||(system&&record.textOffset))throw jpc::Error("Invalid source GUI text cursor");
                record.owner=in.get<int32_t>();record.ordinal=in.get<int32_t>();record.selected=in.get<int32_t>();
                if(record.owner< -1||record.owner>=60||record.ordinal<0||record.ordinal>250||record.selected< -1||record.selected>=250||
                   (system&&record.owner!=-1))throw jpc::Error("Invalid source GUI reference");
                for(auto& link:record.links) {link=in.get<int32_t>();if(link< -1||link>=int(records.size()))throw jpc::Error("Invalid source GUI link");}
                const auto count=in.count(64);for(unsigned i=0;i<count;++i) {
                    const auto name=in.text();if(!setter(name)||record.properties.count(name))throw jpc::Error("Invalid source GUI property");
                    const auto argc=in.count(8);auto& args=record.properties[name];for(unsigned n=0;n<argc;++n)args.push_back(in.get<int32_t>());
                }
            }
        };
        table(next.systems,true);table(next.items,false);next.uvLayout=uvLayout;
        if(version=="EDGUI4") {
            next.uvFingerprint=in.get<uint64_t>();next.selectionPulse=in.get<int16_t>();next.pulseDescending=in.flag();
            if(next.selectionPulse<128||next.selectionPulse>255)throw jpc::Error("Saved original GUI pulse is out of range");const auto count=in.count(65536);
            if(count&&!next.uvFingerprint)throw jpc::Error("Saved original GUI UV copies have no resource identity");
            for(unsigned n=0;n<count;++n){edguiuv::Key destination{in.get<uint32_t>(),in.get<int32_t>()},source{in.get<uint32_t>(),in.get<int32_t>()};
                if((destination.first>>24)!=11||(source.first>>24)!=11||destination.second<0||destination.second>=256||source.second<0||source.second>=256||destination==source||!next.uvCopies.emplace(destination,source).second)
                    throw jpc::Error("Invalid saved original GUI UV copy");}
            if(next.uvLayout){if(next.uvFingerprint&&next.uvFingerprint!=next.uvLayout->fingerprint)throw jpc::Error("Saved original GUI UV resources differ");
                if(!next.uvFingerprint)next.uvFingerprint=next.uvLayout->fingerprint;
                for(const auto& [destination,source]:next.uvCopies)if(next.uvLayout->at(destination).uv.size()>next.uvLayout->at(source).uv.size())throw jpc::Error("Saved original GUI UV source is too small");}
        } else {
            // shortcut: GUI2/3 lacks setter/UV chronology; exact authored replay begins with GUI4.
            // Reconstruct their retained last-known state; exact authored replay begins with GUI4.
            next.uvFingerprint=next.uvLayout?next.uvLayout->fingerprint:0;
            for(auto& record:next.items){
                if((record.flags&8u)&&!record.properties.count("guiItemSetColor"))record.properties["guiItemSetColor"]={128,128,128,128};
                const auto renderer=record.properties.find("guiItemSetRenderFunc");if(renderer!=record.properties.end()&&renderer->second==std::vector<int32_t>{1})renderer->second={0};
                const auto values=record.properties.find("guiItemSetMayaVal");if(values!=record.properties.end()){
                    const auto old=values->second;if(old.size()!=2||old[0]<0||old[0]>=3)throw jpc::Error("Invalid legacy original GUI Maya bytes");
                    values->second.assign(3,0);values->second[size_t(old[0])]=uint8_t(old[1]);}
                for(const auto* name:{"guiItemSetMayaInfoModelID","guiItemSetMayaInfoTextureID"}){const auto old=record.properties.find(name);if(old!=record.properties.end()){
                    if(old->second.size()!=1)throw jpc::Error("Invalid legacy original GUI texture override");maya(record)[1]=old->second[0];}}
                const auto part=record.properties.find("guiItemSetPartID");if(part!=record.properties.end()){
                    if(part->second.size()!=1)throw jpc::Error("Invalid legacy original GUI part override");maya(record)[2]=int8_t(uint8_t(part->second[0]));}
            }
            for(const auto& record:next.items){const auto copy=record.properties.find("guiItemSetPartUVUsingPart");if(copy==record.properties.end())continue;
                const auto model=record.properties.find("guiItemSetMayaInfo");if(copy->second.size()!=2||model==record.properties.end()||model->second.size()!=3)throw jpc::Error("Invalid legacy original GUI UV request");
                next.copyUVs({uint32_t(model->second[0]),model->second[2]},{uint32_t(copy->second[0]),copy->second[1]});}
        }
        for(const auto& record:next.items){const auto model=record.properties.find("guiItemSetMayaInfo");if(model!=record.properties.end()&&(model->second.size()!=3||model->second[2]<-128||model->second[2]>127))throw jpc::Error("Invalid saved original GUI Maya state");
            const auto values=record.properties.find("guiItemSetMayaVal");if(values!=record.properties.end()&&(values->second.size()!=3||std::any_of(values->second.begin(),values->second.end(),[](int32_t value){return value<0||value>255;})))throw jpc::Error("Invalid saved original GUI Maya bytes");}
        for(const auto* table:{&next.items})for(const auto& record:*table) {
            const auto color=record.properties.find("guiItemSetColor");if(color!=record.properties.end()&&color->second.size()!=4)throw jpc::Error("Invalid saved original GUI color");
            const auto data=record.properties.find("guiItemSetDataID");if(data!=record.properties.end()&&data->second.size()!=1)throw jpc::Error("Invalid saved original GUI data ID");
            const auto renderer=record.properties.find("guiItemSetRenderFunc");if(renderer!=record.properties.end()&&(renderer->second.size()!=1||renderer->second[0]<0||renderer->second[0]>6||(version=="EDGUI4"&&renderer->second[0]==1)))throw jpc::Error("Invalid saved original GUI renderer");
        }
        in.end();*this=std::move(next);
    }
};
