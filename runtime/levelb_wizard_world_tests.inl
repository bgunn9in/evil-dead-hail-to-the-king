#pragma once
#include "levelb_temple_world_tests.inl"
// Earned original WizardBoss fight; only Ash approach geometry is a fixture.
void continueLevelBWizard(World& world) {
    auto& vm=world.game.vm;unsigned fixtures=0;
    auto require=[](bool value,const std::string& message){if(!value)throw std::runtime_error("Earned B continuation: "+message);};
    auto tick=[&](){world.step({},false,false,.033f);if(world.pendingSceneSwitch){const auto request=*world.pendingSceneSwitch;world.pendingSceneSwitch.reset();require(!edpackage::reload(request.sourceSceneId,request.destinationId),"unexpected package request in temple");require(world.switchScene(request,nullptr),"original queued scene switch failed");}require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);
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
    require(world.scene.id=="bt11"&&vm.staticField(545).integer()&&vm.staticField(547).integer()&&!vm.staticField(609).integer(),"temple checkpoint or original WizardBoss state differs");
    unsigned damage=0;bool sawBoss=false;
    for(unsigned n=0;n<6000&&!vm.staticField(609).integer();++n){
        const edactors::Body* enemy=nullptr;for(const auto& [key,b]:world.actors->bodies())if(key.type==26&&vm.field(b.script,13).integer()>0&&!vm.field(b.script,world.game.hostField(HostFieldRole::GenericNpc30)).integer()){enemy=&b;break;}
        if(!enemy){tick();continue;}sawBoss=true;const auto key=enemy->key;const int hp=vm.field(enemy->script,13).integer();
        Vec2 chosen{};float best=-std::numeric_limits<float>::infinity();
        for(float d:{2.10f,1.60f})for(unsigned direction=0;direction<32;++direction){
            const float angle=2*edactormath::Pi*float(direction)/32;const Vec2 p{enemy->pose.x+d*std::sin(angle),enemy->pose.z+d*std::cos(angle)};
            if(!floorHeight(world.scene.floor,p)||!clearGround(world.scene.ground,p,world.scene.radius+.02f))continue;
            const float rear=std::cos(angle-enemy->pose.yaw-edactormath::Pi);const float score=rear+d*.01f;
            if(score>best){best=score;chosen=p;}}
        if(!std::isfinite(best)){tick();continue;}place(chosen,{enemy->pose.x,enemy->localY,enemy->pose.z});
        ControlFrame input;input.down[size_t(ControlAction::Aim)]=true;input.edges[size_t(ControlAction::SecondaryAttack)]=world.sourceTick%6==0;world.playerControls(input);tick();
        const auto found=world.actors->bodies().find(key);if(found!=world.actors->bodies().end()&&vm.field(found->second.script,13).integer()<hp)++damage;
        if(n%500==0)std::cout<<"Wizard combat targetHP"<<hp<<" AshHP"<<vm.field(world.game.ash,13).integer()<<" damageSteps"<<damage<<"\n";
    }
    require(sawBoss&&damage>0&&vm.staticField(609).integer()==1,"natural WizardBoss death did not earn609");
    std::cout<<"Wizard source death609 earned, damageSteps"<<damage<<" mode"<<world.game.mode<<" applied"<<vm.staticField(844).integer()<<"\n";ready();
    for(unsigned n=0;n<300&&world.scene.id=="bt11";++n)tick();require(world.scene.id=="ba01","original Wizard death processFrame did not requestBA01");ready();
    const auto checkpoint=world.saveStateBytes();for(unsigned n=0;n<3;++n)tick();world.restoreStateBytes(checkpoint,nullptr);require(world.saveStateBytes()==checkpoint,"earned Wizard state failed exact save/load");
    std::cout<<"Earned B Wizard: original BT11 native26/freeAI/input17 -> death609 -> original movie+sceneSwitch(BA01,0,1,160); "<<damage<<" damage steps, "<<fixtures<<" Ash-only approach fixtures, HP"<<vm.field(world.game.ash,13).integer()<<'\n';
}

void testLevelBWizardWorld(const fs::path& levelA){testLevelBStoryWorld(levelA,[](World& w){continueLevelBMerchant(w);continueLevelBBomb(w);continueLevelBTemple(w);continueLevelBWizard(w);});}
