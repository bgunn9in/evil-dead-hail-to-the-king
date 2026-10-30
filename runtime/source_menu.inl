// Included inside World. This presents source GUI state; input still executes
// Pad/Menu JPC. No actor, inventory, quest or menu decisions are duplicated.
mutable fs::path sourceMenuTextRoot;
mutable std::map<uint32_t,std::string> sourceMenuTextCache;

bool sourceMenuOwnsInput() const {
    if(!originalRuntime||sourceGui.activeSystem<0)return false;
    const auto& system=SourceGui::at(sourceGui.systems,sourceGui.activeSystem);
    if(!(system.flags&8u)||!system.pad)return false; //40BCD0 does not test the draw-visible bit.
    const int mode=game.vm.staticField(844).integer();
    // Interface routes3/6 through InterfacePause. Inventory's Save button
    // changes its internal mode while applied Interface mode stays2.
    return mode==1||mode==3||mode==6||(mode==2&&(authoredUiComplete||game.vm.objectClass(system.pad)==game.package.classIndex("MenSaveLoad")));
}
bool sourceKeyboardMenuActive() const {
    if(!sourceMenuOwnsInput())return false;
    const auto& system=SourceGui::at(sourceGui.systems,sourceGui.activeSystem);
    const auto& owner=game.package.classes[size_t(game.vm.objectClass(system.pad))].name;
    return owner=="MenKeyboardLeft"||owner=="MenKeyboardRight";
}
int sourceMenuConfirmEvent() const {
    const auto& system=SourceGui::at(sourceGui.systems,sourceGui.activeSystem);
    const int menu=game.package.classIndex("MenBase");
    for(int type=game.vm.objectClass(system.pad);type>=0;type=game.package.classes[size_t(type)].parent)
        if(type==menu)return 111;
    return 99; // GuiSys-derived inventory and Pause use itemUse; Pause111 resumes immediately.
}
bool sourceOptionsMenuActive() const {
    if(!sourceMenuOwnsInput())return false;
    const auto& system=SourceGui::at(sourceGui.systems,sourceGui.activeSystem);
    const auto& name=game.package.classes[size_t(game.vm.objectClass(system.pad))].name;
    return name=="MenOptionsLeft"||name=="MenOptionsRight";
}
void resetOriginalOptions() {
    jpc::Ref receiver=0;
    for(const auto& system:sourceGui.systems)if((system.flags&8)&&system.pad&&
        game.vm.objectClass(system.pad)==game.package.classIndex("MenOptionsLeft")){receiver=system.pad;break;}
    if(!receiver)throw jpc::Error("Original options menu is unavailable");
    game.vm.invoke(game.package.methodIndex("MenOptionsLeft","adjustSFXVolume","(I)V"),receiver,
        {jpc::Value::integer(10-sourceGui.masterVolume/10)});
    game.vm.invoke(game.package.methodIndex("MenOptionsLeft","adjustStereoState","(Z)V"),receiver,{jpc::Value::integer(1)});
    game.vm.invoke(game.package.methodIndex("MenOptionsLeft","adjustRumbleState","(Z)V"),receiver,
        {jpc::Value::integer(1)}); // Original method gates this default on checkRumbleAbility.
}
void sourceKeyboardMenuClose() {
    if(!sourceKeyboardMenuActive())return;
    const auto receiver=SourceGui::at(sourceGui.systems,sourceGui.activeSystem).pad;
    const auto& owner=game.package.classes[size_t(game.vm.objectClass(receiver))].name;
    game.vm.startCallback(game.package.methodIndex(owner,"disable","()V"),receiver,{},0);
}
bool sourceMenuInput(const std::vector<int>& events) {
    if(!sourceMenuOwnsInput())return false;
    if(events.size()>32)throw jpc::Error("Source menu input batch exceeds bound");
    for(int event:events)if(!((event>=102&&event<=106)||event==99||event==111||event==112))
        throw jpc::Error("Unsupported source menu input event");
    // Modern host Back is102. Console GuiSys/Options/Pause use100 when platform()==3.
    auto nativeEvents=events;
    if(game.profile.dreamcast)for(auto& event:nativeEvents)if(event==102)event=100;
    sourceGui.dispatchInput(game.vm,nativeEvents);return true;
}
const std::map<uint32_t,std::string>& sourceMenuStrings() const {
    const fs::path directory=root/(levelBPackage?"bb01":"aa01");
    if(sourceMenuTextRoot==directory)return sourceMenuTextCache;
    std::ifstream stream(directory/"menu-resources.bin",std::ios::binary|std::ios::ate);
    if(!stream||stream.tellg()<12||uint64_t(stream.tellg())>128ull*1024*1024)throw jpc::Error("Original menu text catalog is unavailable");
    std::vector<uint8_t> raw(size_t(stream.tellg()));stream.seekg(0);
    if(!stream.read(reinterpret_cast<char*>(raw.data()),std::streamsize(raw.size())))throw jpc::Error("Truncated original menu catalog");
    size_t at=8;
    const auto word=[&] {if(at>raw.size()||raw.size()-at<4)throw jpc::Error("Truncated original menu record");
        uint32_t value=0;for(unsigned b=0;b<4;++b)value|=uint32_t(raw[at++])<<(b*8);return value;};
    if(std::memcmp(raw.data(),"EDMRES1\0",8))throw jpc::Error("Unsupported original menu catalog");
    const auto count=word();if(count>2000)throw jpc::Error("Excessive original menu records");
    std::set<std::pair<uint32_t,uint32_t>> seen;std::map<uint32_t,std::string> strings;
    for(unsigned i=0;i<count;++i) {
        const auto id=word(),flags=word(),bytes=word();
        if(!id||!seen.emplace(id,flags).second||bytes>64u*1024*1024||at>raw.size()||bytes>raw.size()-at)
            throw jpc::Error("Invalid original menu resource range");
        const unsigned kind=id>>24;
        const bool text=(kind&0x1fu)==0x11u||(id>=0xbead0001u&&id<=0xbead0005u);
        if(text) {
            if(flags||!bytes||bytes>4096||raw[at+bytes-1])throw jpc::Error("Invalid original menu text");
            std::string value;
            for(unsigned n=0;n<bytes-1;++n) {
                const auto c=raw[at+n];if(!c)break;
                if((c<32&&c!='\n')||c>126)throw jpc::Error("Unsupported original menu glyph code");value.push_back(char(c));
            }
            if(!strings.emplace(id,std::move(value)).second)throw jpc::Error("Duplicate original menu text");
        }
        at+=bytes;
    }
    if(at!=raw.size())throw jpc::Error("Trailing original menu catalog bytes");
    sourceMenuTextCache=std::move(strings);sourceMenuTextRoot=directory;return sourceMenuTextCache;
}
std::string sourceMenuText(bool russian=false,bool footerOnly=false) const {
    if(!sourceMenuOwnsInput())return {};
    russian|=sourceTexts().localized();
    const auto& strings=sourceMenuStrings();const int active=sourceGui.activeSystem;
    struct Row {int item;bool selected;std::string text;};std::vector<Row> rows;
    const auto append=[&](int system) {
        const auto& owner=SourceGui::at(sourceGui.systems,system);if((owner.flags&9u)!=9u)return;
        if(system!=active) {
            if(!owner.pad)return;int type=game.vm.objectClass(owner.pad);
            const int menu=game.package.classIndex("MenBase"),plain=game.package.classIndex("GuiSys");
            const bool companion=type==plain;while(type>=0&&type!=menu)type=game.package.classes[size_t(type)].parent;
            if(!companion&&type!=menu)return;
        }
        std::vector<std::pair<int,int>> order;
        for(size_t i=0;i<sourceGui.items.size();++i)if(sourceGui.items[i].owner==system&&(sourceGui.items[i].flags&9u)==9u)
            order.emplace_back(sourceGui.items[i].ordinal,int(i));
        std::sort(order.begin(),order.end());
        for(const auto& [ordinal,index]:order) {
            (void)ordinal;const auto& item=sourceGui.items[size_t(index)];
            const auto data=item.properties.find("guiItemSetDataID");
            if(data==item.properties.end()||data->second.empty())continue;
            const auto renderer=item.properties.find("guiItemSetRenderFunc");
            if(renderer==item.properties.end()||renderer->second.empty())continue;
            if(renderer->second[0]==6) { //41FBD0 formats source signedDataID with "%d"@46E6D8.
                rows.push_back({index,system==active&&index==owner.selected,std::to_string(data->second[0])});continue;
            }
            if(renderer->second[0]!=3||!data->second[0]||data->second[0]==-1)continue;
            if((uint32_t(data->second[0])&0xffff0000u)==0xffff0000u) {
                const auto text=sourceCartText(uint32_t(data->second[0]));
                if(!text)throw jpc::Error("Original Cart text resolver is unavailable");
                if(!text->empty())rows.push_back({index,system==active&&index==owner.selected,*text});continue;
            }
            const auto found=strings.find(uint32_t(data->second[0]));
            if(found==strings.end())throw jpc::Error("Original menu text resolver is not yet supported: "+std::to_string(uint32_t(data->second[0])));
            if(found->second.empty())continue;
            const auto scope=episode_portal_detail::sceneId(nativeScene().id);
            const auto& catalog=sourceTexts();
            rows.push_back({index,system==active&&index==owner.selected,catalog.localized()&&catalog.contains(scope,uint32_t(data->second[0]))?catalog.lookup(scope,uint32_t(data->second[0])):edui::text(found->second,russian)});
        }
    };
    append(active);
    // Visible companion/title systems carry authored labels and option values.
    for(size_t i=0;i<sourceGui.systems.size();++i)if(int(i)!=active)append(int(i));
    size_t selected=0;for(size_t i=0;i<rows.size();++i)if(rows[i].selected){selected=i;break;}
    const size_t first=selected>5?selected-5:0,last=std::min(rows.size(),first+12);
    std::string result;
    if(!footerOnly)for(size_t i=first;i<last;++i) {
        const std::string line=(rows[i].selected?"> ":"  ")+rows[i].text;
        if(result.size()+line.size()+1>460)break;result+=line+'\n';
    }
    result+=edui::text("arrows/stick  enter/a  esc/b",russian);return result;
}
