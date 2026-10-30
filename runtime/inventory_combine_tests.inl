#pragma once
// Called at the two earned story checkpoints: after the first moonshine pickup,
// then after original AH05 recipe130. This never seeds inventory or mission430.
void testInventoryCombineAtCheckpoint(World& world,bool recipeEarned) {
    auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(std::string("Original inventory Combine: ")+message);};
    auto quantity=[&](int identity){int count=0;for(const auto& item:world.game.items())if(item.itemID==identity)count+=item.quantity;return count;};
    require((world.game.vm.staticField(430).integer()!=0)==recipeEarned,"source recipe state differs from earned checkpoint");
    require(quantity(170)>0&&quantity(203)>0,"earned moonshine/initial source converter are missing");
    world.openInventory();world.syncInventory();
    auto slot=[&](int identity){for(const auto& item:world.inventoryView.items)if(item.containerField==99&&item.itemID==identity)return item.slot;return -1;};
    const int first=slot(170),second=slot(203);require(first>=0&&second>=0,"actual item slots are unavailable");
    const auto before=world.saveStateBytes();
    for(const auto pair:std::array<std::pair<int,int>,4>{{{first,first},{-1,second},{first,-1},{first,999}}}) {
        require(world.combineInventorySlots(pair.first,pair.second)==-1,"same/invalid slot was accepted");
        require(world.saveStateBytes()==before,"same/invalid slot mutated source state");
    }
    const auto view=world.inventoryView;
    for(auto& item:world.inventoryView.items)if(item.containerField==99&&item.slot==first)item.itemID^=1;
    require(world.combineInventorySlots(first,second)==-1,"stale frontend identity was accepted");world.inventoryView=view;
    require(world.saveStateBytes()==before,"stale frontend selection mutated source state");
    const auto inventory=world.game.vm.field(world.game.ash,99).reference();
    const auto& values=world.game.vm.arrayValues(world.game.vm.field(inventory,world.game.hostField(HostFieldRole::InventorySlots)).reference());
    for(size_t n=0;n<values.size();++n)if(world.game.vm.field(values[n].reference(),32).integer()!=0) {
        require(world.combineInventorySlots(first,int(n))==-1,"empty source slot was accepted");
        require(world.saveStateBytes()==before,"empty source slot mutated source state");break;
    }
    const int moonshine=quantity(170),converter=quantity(203),fuel=quantity(34);
    if(!recipeEarned) {
        const int result=world.combineInventorySlots(first,second);
        require(result==-1&&world.saveStateBytes()==before,"pre-recipe original675 gate changed inventory or GUI state");
        std::cout<<"Original Combine before recipe430: actual1303/1323/675 rejects170+203 with exact rollback\n";
    } else {
        auto select=[&](int physical){const auto item=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),
                [&](const auto& value){return value.containerField==99&&value.slot==physical;});
            require(item!=world.inventoryView.items.end(),"source UI physical slot disappeared");world.inventoryView.selected=unsigned(item-world.inventoryView.items.begin());};
        select(first);world.markInventoryCombination();
        require(world.inventoryView.combineSlot==first&&world.inventoryView.combineItemID==170,"actual UI did not mark the original first item");
        const auto marked=world.saveStateBytes();world.markInventoryCombination();require(world.inventoryView.combineSlot==-1,"marking the same item did not cancel");
        world.restoreStateBytes(marked,nullptr);require(world.saveStateBytes()==marked&&world.inventoryView.combineSlot==first&&
            world.inventoryView.combineItemID==170,"saved physical-slot/itemID anchor did not restore exactly");
        // A stale frontend anchor must not combine another item after a source
        // cleanup has reused the physical slot. No VM/inventory writes here.
        const auto vmBefore=world.game.vm.snapshot(),actorsBefore=world.actors->snapshot();world.inventoryView.combineItemID^=1;
        select(second);world.applyInventoryItem();require(world.inventoryView.combineSlot==-1&&world.game.vm.snapshot()==vmBefore&&
            world.actors->snapshot()==actorsBefore,"stale marked UI identity changed source inventory");
        world.restoreStateBytes(marked,nullptr);select(second);world.applyInventoryItem();
        require(world.notice=="items combined"&&world.inventoryView.combineSlot==-1&&quantity(170)==0&&quantity(34)==fuel+moonshine&&quantity(203)==converter,
            "actual UI confirm did not run the original recipe and retain reusable converter203");
        const auto after=world.saveStateBytes();world.restoreStateBytes(marked,nullptr);select(second);world.applyInventoryItem();
        require(world.saveStateBytes()==after,"actual UI Combine from saved anchor did not replay exactly");
        std::cout<<"Original Combine after earned recipe130/430: actual UI mark/save/load/confirm ->1303/1323/675 ->34, reusable203 and exact replay passed\n";
    }
    world.closeInventory();
}
