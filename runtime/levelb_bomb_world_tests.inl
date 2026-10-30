#pragma once
#include "levelb_merchant_world_tests.inl"
// Same earned World after Merchant; source objects/recipes/doors own progress.
void continueLevelBBomb(World& world) {
    auto& vm=world.game.vm;unsigned fixtures=0;
    auto require=[](bool value,const std::string& message){if(!value)throw std::runtime_error("Earned B continuation: "+message);};
    auto quantity=[&](int id){int count=0;for(int container:{98,99,100})for(const auto& item:world.game.items(container))if(item.itemID==id)count+=item.quantity;return count;};
    auto tick=[&](){world.step({},false,false,.033f);require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);
        require(world.notice!="action is not connected yet"&&world.notice!="could not perform action","source input failed");
        require(vm.field(world.game.ash,13).integer()>0,"Ash naturally died");};
    auto ready=[&](){for(unsigned n=0;n<480;++n){if(world.game.mode!=0)world.closeInventory();world.playerControls({});const auto& ash=world.actors->at({0,0});
            if(!(ash.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&
                !vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&ash.actions.possible(116))return;tick();}
        throw std::runtime_error("Earned B continuation: input did not become ready");};
    auto place=[&](Vec2 point,GroundPoint target){require(floorHeight(world.scene.floor,point).has_value()&&clearGround(world.scene.ground,point,0),"fixture outside original floor");
        auto& ash=world.actors->at({0,0});world.positionProgressBody(ash,{point.x,0,point.z});
        ash.pose.yaw=edactormath::modelYawFromHeading(float(std::atan2(double(target.z)-point.z,double(target.x)-point.x)));
        world.publishProgressBody(ash);world.actor.clearanceRadius=initialActorRadius(world.scene,point);world.actor.movement.held=false;++fixtures;};
    auto sample=[&](const auto& inside,const std::vector<Vec2>& points){require(!points.empty(),"source region has no points");
        float x0=points[0].x,x1=x0,z0=points[0].z,z1=z0;for(auto p:points){x0=std::min(x0,p.x);x1=std::max(x1,p.x);z0=std::min(z0,p.z);z1=std::max(z1,p.z);}
        const Vec2 center{(x0+x1)*.5f,(z0+z1)*.5f};Vec2 chosen{};float best=-std::numeric_limits<float>::infinity();
        for(unsigned x=1;x<32;++x)for(unsigned z=1;z<32;++z){Vec2 p{x0+(x1-x0)*float(x)/32,z0+(z1-z0)*float(z)/32};
            if(!inside(p)||!floorHeight(world.scene.floor,p)||!clearGround(world.scene.ground,p,0))continue;
            float c=world.scene.radius;while(c>0&&!clearGround(world.scene.ground,p,c))c=std::max(0.f,c-.01f);
            const float score=c*10-length(p-center);if(score>best){best=score;chosen=p;}}
        require(std::isfinite(best),"source region has no original floor approach");return chosen;};
    auto approach=[&](unsigned index,unsigned marker){ready();const auto& region=world.scene.scripting.volumes.at(index);std::vector<Vec2> points;
        for(auto p:region.points)points.push_back({p.x,p.z});const auto at=sample([&](Vec2 p){return region.contains({p.x,0,p.z});},points);
        place(at,world.scene.scripting.markers.at(marker));};
    auto passage=[&](const char* expected,unsigned index,const char* destination){
        if(world.scene.id!=expected){require(world.scene.id==destination,"early source passage reached unexpected area");
            std::cout<<"Earned B passage "<<expected<<" -> "<<world.scene.id<<" before door wait\n";return;}
        ready();if(world.scene.id==destination){std::cout<<"Earned B passage "<<expected<<" -> "<<world.scene.id<<" during door wait\n";return;}
        require(world.scene.id==expected,"source passage changed area unexpectedly while becoming ready");const auto source=world.scene.id;const auto portal=world.scene.nativePortals.at(index);
        require(world.scene.enabledPortals.at(index),"source passage not enabled: "+source+"/"+std::to_string(index));
        const auto p=sample([&](Vec2 at){return insideTrigger(portal,at);},portal.points);place(p,{p.x,0,p.z+1});
        for(unsigned n=0;n<180&&world.scene.id==source;++n)tick();require(world.scene.id==destination,"source passage did not reach "+std::string(destination)+"; actual "+world.scene.id+"; "+world.notice);
        std::cout<<"Earned B passage "<<source<<" -> "<<world.scene.id<<" at tick"<<world.sourceTick<<'\n';};
    require(world.scene.id=="bm11"&&vm.staticField(533).integer()&&quantity(141)==1,"missing earned Merchant rope checkpoint");
    const auto merchant=world.actors->bodies().find({24,0});
    require(merchant!=world.actors->bodies().end()&&(merchant->second.flags&1)&&
        vm.objectClass(merchant->second.script)==world.game.package.classIndex("Merchant"),"original Merchant actor was not present in its earned scene");
    // Continue the same earned World; no scene reload/quest/item fixture.
    if(!vm.staticField(536).integer()){approach(69,44);world.use();tick();}
    require(vm.staticField(536).integer()==1,"original BM11 lever did not earn536");
    approach(66,45);world.use();passage("bm11",5,"bt01");
    auto pickup=[&](int id){ready();const auto found=std::find_if(world.nativeProperties.objects.begin(),world.nativeProperties.objects.end(),
            [&](const auto& e){return (e.second.flags&1)&&e.second.setup[0]==id;});require(found!=world.nativeProperties.objects.end(),"source object not visible: "+std::to_string(id));
        const auto target=found->second.position;const auto point=sample([&](Vec2 p){const float d=length(p-Vec2{target.x,target.z});return d>.35f&&d<1.2f;},
            {{target.x-1.25f,target.z-1.25f},{target.x+1.25f,target.z+1.25f}});
        for(unsigned n=0;n<240&&!quantity(id);++n){if(world.game.mode!=0)ready();if(n%60==0){place(point,target);world.use();}tick();}
        require(quantity(id)>0,"real object contact did not grant"+std::to_string(id));ready();};
    require(!quantity(95)&&!vm.staticField(541).integer(),"BT01 component95 was already present");pickup(95);
    require(vm.staticField(541).integer()==1,"original BT01 reportPickup95 did not set541");
    approach(16,14);world.use();passage("bt01",5,"bm11");
    approach(20,12);world.use();for(unsigned n=0;n<60&&!quantity(94);++n)tick();
    require(quantity(94)==1&&vm.staticField(537).integer()==1,"original merchant did not now grant94 after533+541");ready();
    world.openInventory();world.syncInventory();auto slot=[&](int id){for(const auto& i:world.inventoryView.items)if(i.containerField==99&&i.itemID==id)return i.slot;return -1;};
    require(world.combineInventorySlots(slot(94),slot(95))==180&&quantity(94)==0&&quantity(95)==0&&quantity(180)==1,"source94+95 recipe did not grant180");
    require(world.combineInventorySlots(slot(180),slot(141))==18&&quantity(180)==0&&quantity(141)==0&&quantity(18)==1,"source180+141 bomb recipe failed");
    world.closeInventory();passage("bm11",0,"bm12");require(!vm.staticField(540).integer()&&!world.scene.enabledPortals.at(2),"BM12 bomb gate was already open");
    approach(7,2);world.openInventory();world.syncInventory();const auto bomb=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),
        [](const auto& i){return i.containerField==99&&i.itemID==18;});require(bomb!=world.inventoryView.items.end(),"earned bomb missing from actual inventory UI");
    world.inventoryView.selected=unsigned(bomb-world.inventoryView.items.begin());world.applyInventoryItem();
    require(vm.staticField(540).integer()==1&&quantity(18)==0&&world.scene.enabledPortals.at(2),"original item18 use/report did not earn540/consume bomb/open portal2");
    passage("bm12",2,"bt02");ready();const auto checkpoint=world.saveStateBytes();for(unsigned n=0;n<3;++n)tick();world.restoreStateBytes(checkpoint,nullptr);
    require(world.saveStateBytes()==checkpoint,"earned bomb passage did not roundtrip exactly");
    std::cout<<"Earned B bomb route: source lever536/BT01 object95+541 -> merchant94+537 -> source94+95=180 +rope141=18 -> actual BM12 bomb540 -> BT02; HP"
        <<vm.field(world.game.ash,13).integer()<<"; "<<fixtures<<" Ash-only approach fixtures\n";

}
void testLevelBBombWorld(const fs::path& levelA) {testLevelBStoryWorld(levelA,[](World& world){continueLevelBMerchant(world);continueLevelBBomb(world);});}
