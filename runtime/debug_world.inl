// Included inside World. Settings belong to the live application session, not saved progress.
DebugSettings* debugSettings=nullptr;
std::vector<DebugItem> debugItemCatalog() {
    std::vector<DebugItem> result;
    const int flagsMethod=game.package.methodIndex("ObjDB","getObjectFlags","(I)I");
    const int nameMethod=game.package.methodIndex("ObjDB","getObjectTextInventoryName","(I)I");
    const int descriptionMethod=game.package.methodIndex("ObjDB","getObjectTextInventoryDescription","(I)I");
    // Read lookup keys; evaluating health/fuel getters would consume original adaptive RNG.
    const auto health=eddebug::sourceLookupIDs(game.package,"getObjectValueHealth"),fuel=eddebug::sourceLookupIDs(game.package,"getObjectValueFuel");
    for(const int id:eddebug::sourceItemIDs(game.package)) {
        const int flags=game.vm.invoke(flagsMethod,0,{jpc::Value::integer(id)}).integer();bool accepted=false;
        for(unsigned field:{98u,99u,100u}){const auto inventory=game.vm.field(game.ash,field).reference();
            accepted|=(flags&game.vm.field(inventory,game.hostField(HostFieldRole::InventoryAcceptedFlags)).integer())&&!(flags&game.vm.field(inventory,game.hostField(HostFieldRole::InventoryExcludedFlags)).integer());}
        if(!accepted)continue;
        const auto text=uint32_t(game.vm.invoke(nameMethod,0,{jpc::Value::integer(id)}).integer());
        // Source0/1/16/38/140 are unnamed templates, prototypes or scenery.
        if(!text||!game.vm.invoke(descriptionMethod,0,{jpc::Value::integer(id)}).integer())continue;
        std::string name="item "+std::to_string(id);
        if(text)try{name=sourceTexts().lookup(episode_portal_detail::sceneId(nativeScene().id),text);}catch(const jpc::Error&){}
        for(char& c:name)if(uint8_t(c)<32||uint8_t(c)==127||uint8_t(c)==152)c=' ';
        DebugItem entry{id,std::move(name),eddebug::sourceCategory(id,flags,health,fuel)};
        const auto scope=episode_portal_detail::sceneId(nativeScene().id);
        entry.english=sourceTexts().textForLanguage(scope,text,UiLanguage::English);
        entry.russian=sourceTexts().textForLanguage(scope,text,UiLanguage::Russian);
        for(auto* label:{&entry.english,&entry.russian})for(char& c:*label)if(uint8_t(c)<32||c=='$'||c=='@')c=' ';
        result.push_back(std::move(entry));
    }
    return result;
}
bool debugGiveItem(int id,int quantity) {
    if(!originalRuntime||!actors||scene.menuContext||scriptFault||quantity<1||quantity>999)return false;
    const auto catalog=debugItemCatalog();
    if(std::none_of(catalog.begin(),catalog.end(),[&](const auto& item){return item.id==id;}))return false;
    const auto before=game.checkpoint();const auto nativeBefore=checkpointNative();beginNativeStage();
    try {
        const bool received=game.vm.invoke(game.package.methodIndex("Ash","pickupItem","(II)Z"),game.ash,
            {jpc::Value::integer(id),jpc::Value::integer(quantity)}).integer()!=0;
        if(!received){game.rollback(before);rollbackNative(nativeBefore);return false;}
        syncInventory();commitNativeStage();return true;
    } catch(const std::exception& error){game.rollback(before);rollbackNative(nativeBefore);
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,"Debug grant retained previous state: %s",error.what());return false;}
}
void debugResources() {
    if(!debugSettings||(!debugSettings->infiniteFuel&&!debugSettings->infiniteAmmo)||!actors||!game.ash)return;
    std::set<int> owned;for(const auto& item:game.items(98))owned.insert(item.itemID);
    std::set<jpc::Ref> seen;
    for(unsigned field=102;field<=113;++field){const auto weapon=game.vm.field(game.ash,field).reference();
        if(!weapon||!seen.insert(weapon).second)continue;
        const int id=game.vm.field(weapon,812).integer();if(!owned.count(id))continue;
        const bool fuel=id>=100&&id<=102;
        if(!(fuel?debugSettings->infiniteFuel:debugSettings->infiniteAmmo))continue;
        const int capacity=game.vm.field(weapon,817).integer();
        if(capacity>0&&capacity<=1000000)game.vm.setField(weapon,815,jpc::Value::integer(capacity));
    }
}
void debugConfirm(DebugMenu& menu) {
    if(menu.selecting){if(menu.confirmSelection())menu.notice=debugGiveItem(menu.items[menu.item].id,menu.quantity)?"item received":"not received: full or locked";}
    else if(menu.selected==0){menu.beginSelection();}
    else if(menu.selected==2)menu.settings.invulnerable=!menu.settings.invulnerable;
    else if(menu.selected==3)menu.settings.infiniteFuel=!menu.settings.infiniteFuel;
    else if(menu.selected==4)menu.settings.infiniteAmmo=!menu.settings.infiniteAmmo;
    else if(menu.selected==5)menu.open=false;
    debugResources();
}
