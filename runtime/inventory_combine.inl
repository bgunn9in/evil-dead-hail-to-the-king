// Included inside World. Modern physical-slot selection enters the complete
// original Inventory.combineItemSlots chain; recipes and mutations stay JPC.
int combineInventorySlots(int first,int second) {
    if(scriptFault||!inventoryView.open||inventoryView.reading||first==second||first<0||second<0)return -1;
    const auto container=game.vm.field(game.ash,99).reference();
    if(game.vm.objectClass(container)!=game.package.classIndex("Inventory"))throw jpc::Error("Original item inventory is unavailable");
    const auto& slots=game.vm.arrayValues(game.vm.field(container,game.hostField(HostFieldRole::InventorySlots)).reference());
    const auto selected=[&](int slot) {
        if(size_t(slot)>=slots.size())return false;
        const auto item=slots[size_t(slot)].reference();if(game.vm.field(item,32).integer()!=0)return false;
        // The frontend can retain a physical slot after a source pickup or
        // cleanup. Reject its stale identity before entering the bytecode.
        return std::any_of(inventoryView.items.begin(),inventoryView.items.end(),[&](const auto& view){
            return view.containerField==99&&view.slot==slot&&view.itemID==game.vm.field(item,33).integer();});
    };
    if(!selected(first)||!selected(second))return -1;
    const auto scriptBefore=game.checkpoint();const auto nativeBefore=checkpointNative();beginNativeStage();
    try {
        const int result=game.vm.invoke(game.package.methodIndex("Inventory","combineItemSlots","(II)I"),container,
            {jpc::Value::integer(first),jpc::Value::integer(second)}).integer();
        if(result==-1){game.rollback(scriptBefore);rollbackNative(nativeBefore);return -1;}
        if(result<0||result>65535)throw jpc::Error("Original recipe returned an invalid item identity");
        syncInventory();commitNativeStage();return result;
    } catch(const std::exception& error) {
        game.rollback(scriptBefore);rollbackNative(nativeBefore);notice="could not combine items";noticeDoor=-1;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Combine retained its previous state: %s",error.what());return -1;
    }
}
