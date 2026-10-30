// Included inside World; the source cursor also drives the modern document view.
mutable std::unique_ptr<edguitext::Catalog> textCatalog;
std::vector<edguitext::Page> sourceTextPages;
mutable std::unique_ptr<edguipresentation::GameOver> gameOverPresentation;
mutable std::unique_ptr<edguipresentation::Authored> authoredPresentation;
std::vector<edguipresentation::Command> sourceGuiCommands;
bool authoredUiComplete=false;
bool sampleAuthoredGui() {
    sourceGuiCommands.clear();authoredUiComplete=false;if(!sourceGui.uvLayout)return false;
    if(!authoredPresentation)authoredPresentation=std::make_unique<edguipresentation::Authored>(root);
    auto frame=authoredPresentation->sample(sourceGui,sourceTexts(),episode_portal_detail::sceneId(nativeScene().id),game.vm.staticField(844).integer(),[this](uint32_t id){return sourceCartText(id);},rawClockSeconds);
    if(!frame.complete)return false;sourceGuiCommands=std::move(frame.commands);authoredUiComplete=true;return true;
}
std::vector<edguipresentation::Sprite> drawGuiSprites() const {
    if(!originalRuntime||authoredUiComplete)return {};
    if(!gameOverPresentation)gameOverPresentation=std::make_unique<edguipresentation::GameOver>(root);
    return gameOverPresentation->sample(sourceGui,game.vm);
}
const edguitext::Catalog& sourceTexts() const {
    if(!textCatalog)textCatalog=std::make_unique<edguitext::Catalog>(root);
    return *textCatalog;
}
void setUiLanguage(UiLanguage language) {
    const auto revision=sourceTexts().textRevision();textCatalog->setLanguage(language);
    if(revision!=textCatalog->textRevision())edguitext::restartTextCursors(sourceGui,game.vm);
    sourceTextPages.clear();sourceGuiCommands.clear();inventoryView.documentPage.reset();authoredUiComplete=false;
}
UiLanguage uiLanguage()const{return sourceTexts().language();}
int documentReaderItem() const {
    const auto reader=game.vm.field(game.vm.staticField(877).reference(),game.hostField(HostFieldRole::InvBook7)).reference();
    if(game.vm.objectClass(reader)!=game.package.classIndex("InvBookText"))throw jpc::Error("Original document reader is unavailable");
    const auto& items=game.vm.arrayValues(game.vm.field(reader,835).reference());
    if(items.empty())throw jpc::Error("Original document text item is unavailable");
    const int item=items[0].integer();SourceGui::at(sourceGui.items,item);return item;
}
jpc::Value documentTextID(const InventoryItemView& selected) {
    if(!originalRuntime||selected.containerField!=100)throw jpc::Error("Document is outside the source inventory");
    const auto container=game.vm.field(game.ash,100).reference();
    const auto& slots=game.vm.arrayValues(game.vm.field(container,game.hostField(HostFieldRole::InventorySlots)).reference());
    if(selected.slot<0||size_t(selected.slot)>=slots.size())throw jpc::Error("Document slot is unavailable");
    const auto entry=slots[size_t(selected.slot)].reference();
    if(game.vm.field(entry,32).integer()!=0||game.vm.field(entry,33).integer()!=selected.itemID)throw jpc::Error("Document selection is stale");
    return game.vm.invoke(game.package.methodIndex("ObjDB","getObjectTextInventoryDescription","(I)I"),0,{jpc::Value::integer(selected.itemID)});
}
edguitext::Page documentPage() const {
    const auto& item=SourceGui::at(sourceGui.items,documentReaderItem());const auto data=item.properties.find("guiItemSetDataID");
    if(data==item.properties.end()||data->second.size()!=1)throw jpc::Error("Original document text resource is unavailable");
    return edguitext::layout(sourceTexts().lookup(episode_portal_detail::sceneId(nativeScene().id),uint32_t(data->second[0])),uint16_t(item.textOffset),{44,84,596,414},0,0xffffffff,false,true);
}
void readDocument(const InventoryItemView& selected) {
    if(!inventoryView.open)throw jpc::Error("Document inventory is closed");
    const auto text=documentTextID(selected);
    const auto reader=game.vm.field(game.vm.staticField(877).reference(),game.hostField(HostFieldRole::InvBook7)).reference();
    // Modern physical-slot adapter bypasses original1190's broken scrolling/UI-ordinal lookup.
    if(inventoryView.reading||game.vm.staticField(game.hostField(HostFieldRole::InvBookText1)).integer()!=text.integer())
        game.vm.invoke(game.package.methodIndex("InvBookText","setTextAssetID","(I)V"),reader,{text});
    if(!inventoryView.reading) {
        game.vm.invoke(game.package.methodIndex("GuiSys","setDataID","(II)V"),reader,{jpc::Value::integer(0),text});
        game.vm.invoke(game.package.methodIndex("Gui","itemRemoveFlag","(II)V"),0,{jpc::Value::integer(documentReaderItem()),jpc::Value::integer(0x200)});
    }
    inventoryView.reading=true;
    inventoryView.documentPage=documentPage();
}
void closeDocument() {inventoryView.reading=false;inventoryView.documentPage.reset();}
void sampleSourceTexts() {
    sourceTextPages.clear();inventoryView.documentPage.reset();
    if(!originalRuntime)return;
    if(sampleAuthoredGui())return;
    const auto scope=episode_portal_detail::sceneId(nativeScene().id);
    const int document=inventoryView.reading?documentReaderItem():-1;
    for(size_t index=0;index<sourceGui.items.size();++index) {
        const auto& item=sourceGui.items[index];if(int(index)==document||!(item.flags&1)||item.owner<0||!(SourceGui::at(sourceGui.systems,item.owner).flags&1))continue;
        const auto renderer=item.properties.find("guiItemSetRenderFunc"),maya=item.properties.find("guiItemSetMayaInfo");
        if(renderer==item.properties.end()||renderer->second.size()!=1||renderer->second[0]!=3||maya==item.properties.end()||maya->second.size()!=3)continue;
        const uint32_t model=uint32_t(maya->second[0]);
        if(model==0x0b142038u) {
            const auto& system=SourceGui::at(sourceGui.systems,item.owner);
            const auto data=item.properties.find("guiItemSetDataID");
            if(!system.pad||game.vm.objectClass(system.pad)!=game.package.classIndex("MenMain")||
               data==item.properties.end()||data->second.size()!=1||
               !sourceTexts().contains(scope,uint32_t(data->second[0])))continue;
            const auto& catalog=sourceTexts();
            try{(void)catalog.bounds(model,unsigned(uint8_t(maya->second[2])));}
            catch(const jpc::Error&){continue;} // Older profiles have only the textbox models.
        } else if(model!=0x0b14208cu&&model!=0x0b142188u)continue;
        auto box=edguitext::sample(sourceGui,sourceTexts(),scope,int(index),game.vm.staticField(844).integer(),false,nullptr,rawClockSeconds);
        if(!box.page.glyphs.empty())sourceTextPages.push_back(std::move(box.page));
    }
    if(document>=0) {
        auto page=documentPage();edguitext::commitAdvance(SourceGui::at(sourceGui.items,document),page);inventoryView.documentPage=std::move(page);
    }
}

// Present MenMain only after all visible rows have their authored geometry/text.
// Other menu classes retain the existing bounded fallback and input behavior.
bool authoredMainMenuVisible() const {
    if(!originalRuntime||!sourceMenuOwnsInput()||sourceTextPages.empty())return false;
    const auto& system=SourceGui::at(sourceGui.systems,sourceGui.activeSystem);
    if(!system.pad||game.vm.objectClass(system.pad)!=game.package.classIndex("MenMain"))return false;
    const auto scope=episode_portal_detail::sceneId(nativeScene().id);unsigned rows=0;
    for(const auto& item:sourceGui.items)if(item.owner==sourceGui.activeSystem&&(item.flags&1u)) {
        const auto renderer=item.properties.find("guiItemSetRenderFunc"),maya=item.properties.find("guiItemSetMayaInfo"),
                   data=item.properties.find("guiItemSetDataID");
        if(renderer==item.properties.end()||renderer->second!=std::vector<int32_t>{3}||
           maya==item.properties.end()||maya->second.size()!=3||uint32_t(maya->second[0])!=0x0b142038u||
           data==item.properties.end()||data->second.size()!=1||!sourceTexts().contains(scope,uint32_t(data->second[0])))return false;
        const auto& catalog=sourceTexts();
        try{(void)catalog.bounds(uint32_t(maya->second[0]),unsigned(uint8_t(maya->second[2])));}
        catch(const jpc::Error&){return false;}
        ++rows;
    }
    return rows!=0;
}
