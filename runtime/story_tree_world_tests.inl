#pragma once
#include "story_world_tests.inl"
// The preceding chapter earns herbicide132. This continuation changes only
// Ash's decoded-floor approach pose; all quest, inventory and enemy logic is JPC.
void continueTreeStoryWorld(World& world) {
    auto require=[](bool value,const std::string& message){if(!value)throw std::runtime_error("Earned Tree World: "+message);};
    auto& vm=world.game.vm;unsigned fixtures=0;
    auto quantity=[&](int identity){int count=0;for(int field:{98,99,100})
            for(const auto& item:world.game.items(field))if(item.itemID==identity)count+=item.quantity;return count;};
    require(world.scene.id=="ah06"&&vm.staticField(604).integer()==1&&quantity(132)==1&&quantity(73)==1,
        "preceding source Matriarch chapter was not earned");
    auto tick=[&](){world.step({},false,false,.033f);require(!world.scriptFault,"original frame failed: "+world.game.diagnostic);
        require(vm.field(world.game.ash,13).integer()>0,"Ash died during the source-geometry fixture in "+world.scene.id+" at source tick "+std::to_string(world.sourceTick));};
    auto ready=[&](){world.playerControls({});for(unsigned n=0;n<360;++n){if(world.game.mode!=0)world.closeInventory();const auto& body=world.actors->at({0,0});
            if(!(body.flags&0x48)&&!world.guard&&!vm.field(world.game.ash,11).integer()&&!vm.field(world.game.ash,123).integer()&&
                !vm.staticField(71).integer()&&vm.staticField(844).integer()==0&&body.actions.possible(116))return;tick();}
        throw std::runtime_error("Earned Tree World: source input did not become ready");};
    auto point=[&](const std::vector<Vec2>& points,const auto& contains){require(!points.empty(),"empty source approach region");
        Vec2 low=points[0],high=low;for(Vec2 p:points){low.x=std::min(low.x,p.x);low.z=std::min(low.z,p.z);high.x=std::max(high.x,p.x);high.z=std::max(high.z,p.z);}
        const Vec2 center=(low+high)*.5f;Vec2 result{};float best=-std::numeric_limits<float>::infinity();
        for(unsigned x=1;x<32;++x)for(unsigned z=1;z<32;++z){const Vec2 p{low.x+(high.x-low.x)*float(x)/32,low.z+(high.z-low.z)*float(z)/32};
            if(!contains(p)||!floorHeight(world.scene.floor,p)||!clearGround(world.scene.ground,p,0))continue;
            float radius=world.scene.radius;while(radius>0&&!clearGround(world.scene.ground,p,radius))radius=std::max(0.f,radius-.01f);
            const float score=radius*10-length(p-center);if(score>best){best=score;result=p;}}
        require(std::isfinite(best),"no decoded source-floor sample in "+world.scene.id);return result;};
    auto place=[&](Vec2 at,unsigned marker){auto& ash=world.actors->at({0,0});world.positionProgressBody(ash,{at.x,0,at.z});
        const auto target=world.scene.scripting.markers.at(marker);
        ash.pose.yaw=edactormath::modelYawFromHeading(float(std::atan2(double(target.z)-at.z,double(target.x)-at.x)));
        world.publishProgressBody(ash);world.actor.movement.held=false;world.actor.clearanceRadius=initialActorRadius(world.scene,at);++fixtures;};
    auto approach=[&](unsigned volume,unsigned marker){ready();const auto& region=world.scene.scripting.volumes.at(volume);
        std::vector<Vec2> polygon;for(const auto& p:region.points)polygon.push_back({p.x,p.z});
        place(point(polygon,[&](Vec2 p){return region.contains({p.x,0,p.z});}),marker);};
    auto queued=[&](const char* destination){require(world.pendingSceneSwitch&&edpackage::sceneName(world.pendingSceneSwitch->destinationId)==destination,
            "original script did not queue "+std::string(destination));const auto request=*world.pendingSceneSwitch;world.pendingSceneSwitch.reset();
        require(world.switchScene(request,nullptr)&&world.scene.id==destination,"original queued scene change failed: "+world.notice);};
    auto passage=[&](const char* source,unsigned index,const char* destination){
        if(world.scene.id!=source){require(world.scene.id==destination,"source passage already entered an unexpected area");return;}
        const auto region=world.scene.nativePortals.at(index);ready();if(world.scene.id==destination)return;
        require(world.scene.id==source,"readiness changed the expected source area");
        place(point(region.points,[&](Vec2 p){return insideTrigger(region,p);}),0);
        for(unsigned n=0;n<180&&world.scene.id==source;++n)tick();require(world.scene.id==destination,"source passage failed: "+std::string(source)+" -> "+destination+"; "+world.notice);};
    auto save=[&](){const auto before=world.saveStateBytes();for(unsigned n=0;n<4;++n)tick();world.restoreStateBytes(before,nullptr);
        require(world.saveStateBytes()==before,"full earned VM/native/routes/VFX state did not restore exactly");};
    approach(12,0);world.use();queued("ah05");
    passage("ah05",2,"ah02");passage("ah02",4,"ah50");passage("ah50",5,"ak09");passage("ak09",11,"al01");passage("al01",7,"al02");
    require(!vm.staticField(460).integer(),"source herbicide gate was already solved");
    approach(95,12);world.openInventory();world.syncInventory();
    const auto herbicide=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[](const auto& item){return item.containerField==99&&item.itemID==132;});
    require(herbicide!=world.inventoryView.items.end(),"earned herbicide is absent from inventory");
    world.inventoryView.selected=unsigned(herbicide-world.inventoryView.items.begin());world.applyInventoryItem();
    require(vm.staticField(460).integer()==1&&quantity(132)==0,"original AL02 item132 callback did not consume herbicide/set460");queued("at02");
    ready();save();const edactors::Key tree{5,0};
    for(unsigned n=0;n<180&&!world.actors->bodies().count(tree);++n)tick();
    require(world.actors->bodies().count(tree)!=0&&world.actors->bodies().count({34,0})!=0,"original AT02 activation did not create TreeBoss5/TreeArm34");
    const auto boss=world.actors->at(tree).script;require(vm.field(boss,13).integer()==100&&!vm.staticField(605).integer(),"TreeBoss source initialization differs");
    unsigned strikes=0,approaches=0;int previous=100;
    // Consume only the two healing pickups already present in the earned save.
    // Original Inventory.useItemSlot -> Ash.inventoryUse decides the HP change.
    unsigned medicines=0;
    while(vm.field(world.game.ash,13).integer()<80&&quantity(56)>0) {
        ready();const int beforeHP=vm.field(world.game.ash,13).integer(),beforeQuantity=quantity(56);
        world.openInventory();world.syncInventory();
        const auto medicine=std::find_if(world.inventoryView.items.begin(),world.inventoryView.items.end(),[](const auto& item){return item.containerField==99&&item.itemID==56;});
        require(medicine!=world.inventoryView.items.end(),"earned healing pickup is absent from inventory");
        world.inventoryView.selected=unsigned(medicine-world.inventoryView.items.begin());world.applyInventoryItem();
        require(vm.field(world.game.ash,13).integer()>beforeHP&&quantity(56)==beforeQuantity-1,"original healing item did not heal/consume");
        ++medicines;ready();
    }
    for(unsigned frame=0;frame<3000&&!vm.staticField(605).integer();++frame) {
        const auto& target=world.actors->at(tree);auto& ash=world.actors->at({0,0});bool found=false;
        const float distance=std::max(1.1f,float(target.radius)*.01f+world.scene.radius+.12f);
        for(unsigned angle=0;angle<32;++angle){const float yaw=target.pose.yaw+float(angle)*edactormath::Tau/32;
            const Vec2 at{target.pose.x+distance*std::sin(yaw),target.pose.z+distance*std::cos(yaw)};
            if(!floorHeight(world.scene.floor,at)||!clearGround(world.scene.ground,at,world.scene.radius+.05f))continue;
            world.positionProgressBody(ash,{at.x,0,at.z});ash.pose.yaw=edactormath::modelYawFromHeading(float(std::atan2(double(target.pose.z)-at.z,double(target.pose.x)-at.x)));
            world.publishProgressBody(ash);world.actor.clearanceRadius=initialActorRadius(world.scene,at);world.actor.movement.held=false;found=true;++approaches;break;}
        require(found,"no valid decoded-floor axe approach to the original tree");
        ControlFrame input;input.down[size_t(ControlAction::Aim)]=true;input.edges[size_t(ControlAction::SecondaryAttack)]=world.sourceTick%6==0;world.playerControls(input);tick();
        const int health=vm.field(boss,13).integer();if(health<previous){require(health==std::max(0,previous-7),"source tree hit did not use its fixed7 damage");++strikes;}previous=health;
    }
    require(strikes==15&&vm.field(boss,13).integer()==0&&vm.staticField(605).integer()==1,"real source Axe/Tree41/end26/death24 did not earn605");
    for(unsigned n=0;n<180&&!vm.staticField(424).integer();++n)tick();require(vm.staticField(424).integer()==1&&quantity(74)==1,"original tree death did not award page74/424");
    ready();save();for(unsigned n=0;n<180&&!world.pendingSceneSwitch;++n)tick();queued("at01");ready();save();
    std::cout<<"Earned Tree World: actual Matriarch132 -> AL02 herbicide460/AT02 -> TreeBoss5+Arm34 ->15 real Axe hits/fixed7/death605/page74/424 -> original AT01 return; "
        <<fixtures<<" Ash-only region fixtures, "<<medicines<<" earned healing pickups and "<<approaches<<" axe approach samples\n";
}
void testTreeStoryWorld(const fs::path& root,const std::function<void(World&)>& continueStory={}) {
    testStoryWorld(root,[&](World& world){continueTreeStoryWorld(world);if(continueStory)continueStory(world);});
}
