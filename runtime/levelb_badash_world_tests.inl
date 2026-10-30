#pragma once
// Original penultimate boss; only Ash floor approaches are fixtures.
void continueLevelBBadAsh(World& world) {
    auto& vm=world.game.vm;unsigned fixtures=0,damage=0;
    auto require=[](bool value,const std::string& text){if(!value)throw std::runtime_error("Original BadAsh: "+text);};
    auto tick=[&](){world.step({},false,false,.033f);
        if(world.pendingSceneSwitch){const auto request=*world.pendingSceneSwitch;world.pendingSceneSwitch.reset();
            require(!edpackage::reload(request.sourceSceneId,request.destinationId),"unexpected package switch");require(world.switchScene(request,nullptr),"source scene switch failed");}
        require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);require(vm.field(world.game.ash,13).integer()>0,"Ash naturally died");};
    auto place=[&](Vec2 p,GroundPoint target){require(floorHeight(world.scene.floor,p).has_value()&&clearGround(world.scene.ground,p,0),"fixture outside original floor");
        auto& ash=world.actors->at({0,0});world.positionProgressBody(ash,{p.x,0,p.z});
        ash.pose.yaw=edactormath::modelYawFromHeading(float(std::atan2(double(target.z)-p.z,double(target.x)-p.x)));
        world.publishProgressBody(ash);world.actor.clearanceRadius=initialActorRadius(world.scene,p);world.actor.movement.held=false;++fixtures;};
    require(world.scene.id=="bc01", "requires the original library exit toBC01");
    bool sawBoss=false;
    for(unsigned n=0;n<8000&&world.scene.id=="bc01";++n){
        const edactors::Body* enemy=nullptr;for(const auto& [key,body]:world.actors->bodies())if(key.type==19&&vm.field(body.script,13).integer()>0&&!vm.field(body.script,world.game.hostField(HostFieldRole::GenericNpc30)).integer()){enemy=&body;break;}
        if(!enemy){tick();continue;}sawBoss=true;const auto key=enemy->key;const int hp=vm.field(enemy->script,13).integer();
        Vec2 chosen{};float best=-std::numeric_limits<float>::infinity();
        for(float d:{2.35f,2.10f,1.60f})for(unsigned direction=0;direction<32;++direction){const float angle=2*edactormath::Pi*float(direction)/32;
            const Vec2 p{enemy->pose.x+d*std::sin(angle),enemy->pose.z+d*std::cos(angle)};
            if(!floorHeight(world.scene.floor,p)||!clearGround(world.scene.ground,p,world.scene.radius+.02f))continue;
            const float score=std::cos(angle-enemy->pose.yaw-edactormath::Pi)+d*.01f;if(score>best){best=score;chosen=p;}}
        if(!std::isfinite(best)){tick();continue;}place(chosen,{enemy->pose.x,enemy->localY,enemy->pose.z});
        ControlFrame input;input.down[size_t(ControlAction::Aim)]=true;input.edges[size_t(ControlAction::SecondaryAttack)]=world.sourceTick%6==0;world.playerControls(input);tick();
        const auto found=world.actors->bodies().find(key);if(found!=world.actors->bodies().end()&&vm.field(found->second.script,13).integer()<hp)++damage;
        if(n%500==0)std::cout<<"BadAsh targetHP"<<hp<<" AshHP"<<vm.field(world.game.ash,13).integer()<<" damageSteps"<<damage<<'\n';
    }
    require(sawBoss&&damage>0&&world.scene.id=="bc02","natural BadAsh death did not requestBC02");
    require(world.actor.camera==1,"source BadAsh transition did not selectcamera1");
    const auto saved=world.saveStateBytes();world.restoreStateBytes(saved,nullptr);require(world.saveStateBytes()==saved,"actual BadAsh arrival save changed state");
    std::cout<<"Original BadAsh19: real attacks/free AI ->original death movie/sceneSwitch BC02(camera1,marker1); "<<damage<<" damage steps, "<<fixtures<<" Ash-only approach fixtures, HP"<<vm.field(world.game.ash,13).integer()<<'\n';
}
