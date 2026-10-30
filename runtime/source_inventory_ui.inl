#pragma once
// Test input helpers: owned ingredients and real inventory UI only.
static void useSourceMushrooms(World& world){auto& vm=world.game.vm;auto quantity=[&](int id){int n=0;for(const auto& item:world.game.items(99))if(item.itemID==id)n+=item.quantity;return n;};
    auto select=[&](int id){world.syncInventory();const auto it=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[&](const auto& item){return item.containerField==99&&item.itemID==id;});
        if(it==world.inventoryView.items.end())throw std::runtime_error("Real converter/ingredient/kit missing from UI");world.inventoryView.selected=unsigned(it-world.inventoryView.items.begin());};
    while((quantity(70)>0||quantity(46)>0)&&vm.field(world.game.ash,13).integer()<=65){const int before=vm.field(world.game.ash,13).integer();world.openInventory();
        if(quantity(46)==0){const int mushrooms=quantity(70),converter=quantity(203);if(!vm.staticField(429).integer()||converter<1)throw std::runtime_error("Original medicine recipe429/converter203 unavailable");
            select(203);world.markInventoryCombination();select(70);world.applyInventoryItem();
            if(quantity(70)!=0||quantity(46)!=mushrooms||quantity(203)!=converter||vm.field(world.game.ash,13).integer()!=before)throw std::runtime_error("Original203+70 whole-stack source recipe differs");
            std::cout<<"GENUINE_SOURCE70_CONVERT_UI 203+70x"<<mushrooms<<"->46x"<<quantity(46)<<" reusable203 "<<converter<<'\n';}
        const int kits=quantity(46),value=vm.invoke(world.game.package.methodIndex("ObjDB","getObjectValueHealth","(I)I"),0,{jpc::Value::integer(46)}).integer();select(46);world.applyInventoryItem();world.closeInventory();
        if(value!=35||quantity(46)!=kits-1||vm.field(world.game.ash,13).integer()!=before+value)throw std::runtime_error("Actual converted46 original UI consume/heal differs");
        std::cout<<"GENUINE_SOURCE46_UI HP"<<before<<"->"<<vm.field(world.game.ash,13).integer()<<" kits"<<kits<<"->"<<quantity(46)<<'\n';}
}

static void useSourceFuel(World& world){auto& vm=world.game.vm;auto quantity=[&](int id){int n=0;for(const auto& item:world.game.items(99))if(item.itemID==id)n+=item.quantity;return n;};
    auto select=[&](int id){world.syncInventory();const auto it=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[&](const auto& item){return item.containerField==99&&item.itemID==id;});if(it==world.inventoryView.items.end())throw std::runtime_error("Actual fuel ingredient/can unavailable");world.inventoryView.selected=unsigned(it-world.inventoryView.items.begin());};
    const auto saw=vm.field(world.game.ash,102).reference();const int before=vm.field(saw,815).integer(),maximum=vm.field(saw,817).integer(),health=vm.field(world.game.ash,13).integer(),raw=quantity(69),can=quantity(34),converter=quantity(203);
    if(raw<1||converter<1||!vm.staticField(430).integer()||before>=maximum)throw std::runtime_error("Source fuel recipe/precondition not available");
    world.openInventory();select(203);world.markInventoryCombination();select(69);world.applyInventoryItem();
    if(quantity(69)||quantity(34)!=can+raw||quantity(203)!=converter||vm.field(saw,815).integer()!=before)throw std::runtime_error("Original203+69 whole-stack fuel recipe differs");
    const int value=vm.invoke(world.game.package.methodIndex("ObjDB","getObjectValueFuel","(I)I"),0,{jpc::Value::integer(34)}).integer();select(34);world.applyInventoryItem();world.closeInventory();
    if(quantity(34)!=can+raw-1||vm.field(saw,815).integer()!=std::min(maximum,before+value)||vm.field(world.game.ash,13).integer()!=health)throw std::runtime_error("Actual original fuel-can use differs");
    std::cout<<"GENUINE_SOURCE69_CONVERT_FUEL_UI 203+69x"<<raw<<"->34; fuel"<<before<<"->"<<vm.field(saw,815).integer()<<" cap"<<maximum<<" sourceValue"<<value<<" reusable203 "<<converter<<'\n';
}


static bool sourceOwnedItemUsable(World& world,const ScriptInventoryItem& item){auto& vm=world.game.vm;
    const auto container=vm.field(world.game.ash,99).reference();
    const auto& slots=vm.arrayValues(vm.field(container,world.game.hostField(HostFieldRole::InventorySlots)).reference());
    return (vm.field(slots.at(size_t(item.slot)).reference(),35).integer()&4)!=0;
}
// Test/controller input: consume only real, normally usable owned health items.
static bool useSourceHealingItem(World& world){auto& vm=world.game.vm;const int before=vm.field(world.game.ash,13).integer();
    if(before<=0||before>65||world.game.mode!=0)return false;
    auto quantity=[&](int id){int n=0;for(const auto& item:world.game.items(99))if(item.itemID==id)n+=item.quantity;return n;};
    for(const auto& candidate:world.game.items(99))if(sourceOwnedItemUsable(world,candidate)){
        const int id=candidate.itemID,count=quantity(id);
        const int value=vm.invoke(world.game.package.methodIndex("ObjDB","getObjectValueHealth","(I)I"),0,{jpc::Value::integer(id)}).integer();
        if(value<=0||before+value>100)continue;
        world.openInventory();world.syncInventory();if(!world.inventoryView.open)return false;
        const auto item=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[&](const auto& item){return item.containerField==99&&item.itemID==id;});
        if(item==world.inventoryView.items.end())throw std::runtime_error("Owned source health item missing from inventory UI");
        world.inventoryView.selected=unsigned(item-world.inventoryView.items.begin());world.applyInventoryItem();world.closeInventory();
        if(quantity(id)!=count-1||vm.field(world.game.ash,13).integer()!=before+value)throw std::runtime_error("Original owned health item did not consume/heal its source value");
        std::cout<<"GENUINE_OWNED_UI_HEAL item="<<id<<" count="<<count<<"->"<<quantity(id)<<" HP="<<before<<"->"<<before+value<<" sourceValue="<<value<<'\n';return true;
    }return false;
}

static bool useSourceOwnedFuelItem(World& world){auto& vm=world.game.vm;if(world.game.mode!=0)return false;
    const auto saw=vm.field(world.game.ash,102).reference();const int before=vm.field(saw,815).integer(),maximum=vm.field(saw,817).integer();if(before>=20||before>=maximum)return false;
    auto quantity=[&](int id){int n=0;for(const auto& item:world.game.items(99))if(item.itemID==id)n+=item.quantity;return n;};
    for(const auto& candidate:world.game.items(99))if(sourceOwnedItemUsable(world,candidate)){
        const int id=candidate.itemID,count=quantity(id);
        const int value=vm.invoke(world.game.package.methodIndex("ObjDB","getObjectValueFuel","(I)I"),0,{jpc::Value::integer(id)}).integer();if(value<=0)continue;
        world.openInventory();world.syncInventory();if(!world.inventoryView.open)return false;
        const auto item=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[&](const auto& item){return item.containerField==99&&item.itemID==id;});if(item==world.inventoryView.items.end())throw std::runtime_error("Owned source fuel item missing from inventory UI");
        world.inventoryView.selected=unsigned(item-world.inventoryView.items.begin());world.applyInventoryItem();world.closeInventory();
        if(quantity(id)!=count-1||vm.field(saw,815).integer()!=std::min(maximum,before+value))throw std::runtime_error("Original owned fuel item did not consume/refill its source value");
        std::cout<<"GENUINE_OWNED_UI_FUEL item="<<id<<" count="<<count<<"->"<<quantity(id)<<" fuel="<<before<<"->"<<vm.field(saw,815).integer()<<" sourceValue="<<value<<'\n';return true;
    }return false;
}
