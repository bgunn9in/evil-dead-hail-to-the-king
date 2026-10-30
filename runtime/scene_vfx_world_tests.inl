#pragma once
// Source-volume position fixtures exercise actual World input/lifecycle/AI;
// they do not claim continuous walking through these three boss rooms.
void testSceneVfxWorld(const fs::path& levelA,const fs::path& levelB,bool draw,int width,int height,const fs::path& capture) {
    std::cout<<std::unitbuf;
    checked(SDL_Init(SDL_INIT_GAMEPAD|(draw?SDL_INIT_VIDEO:0)),"Initialize source scene VFX World proof");
    auto require=[](bool value,const std::string& text){if(!value)throw std::runtime_error("Source scene VFX World: "+text);};
    auto enter=[&](World& world,const std::string& id){if(world.scene.id!=id){
        world.endEpisodeScene();Scene next=loadScene(world.root/id);Actor placed=resetActor(next,0);
        std::vector<GateState> gates(next.doors.size());world.enterSourceScene(next,placed,gates);
        world.scene=std::move(next);world.actor=std::move(placed);world.gates=std::move(gates);}
        world.closeInventory();require(!world.scriptFault,"scene activation failed: "+world.game.diagnostic);};
    auto step=[&](World& world,bool attack=false){ControlFrame input;
        input.down[size_t(ControlAction::Aim)]=attack;input.edges[size_t(ControlAction::SecondaryAttack)]=attack;
        if(attack)world.playerControls(input);world.step({},false,false,.033f);
        require(!world.scriptFault,"original frame failed: "+world.game.diagnostic);
        require(world.notice!="could not perform action"&&world.notice!="action is not connected yet","native action failed: "+world.notice);};
    auto place=[&](World& world,edactors::Key key,GroundPoint point,float yaw){auto& body=world.actors->at(key);
        world.positionProgressBody(body,{point.x,0,point.z});body.pose.yaw=yaw;world.publishProgressBody(body);
        if(key.type==0){world.actor.clearanceRadius=initialActorRadius(world.scene,{point.x,point.z});world.actor.movement.held=false;}};
    auto sample=[&](World& world,unsigned volume){const auto& points=world.scene.scripting.volumes.at(volume).points;
        require(points.size()==2,"fixture needs the actual rectangular source trigger");GroundPoint chosen{};float nearest=std::numeric_limits<float>::max();
        const Vec2 center{(points[0].x+points[1].x)*.5f,(points[0].z+points[1].z)*.5f};
        for(unsigned x=1;x<16;++x)for(unsigned z=1;z<16;++z){GroundPoint p{points[0].x+(points[1].x-points[0].x)*float(x)/16,0,
                points[0].z+(points[1].z-points[0].z)*float(z)/16};const Vec2 q{p.x,p.z};const float distance=length(q-center);
            if(distance<nearest&&world.scene.scripting.inside(volume,p)&&floorHeight(world.scene.floor,q)&&clearGround(world.scene.ground,q,0)){
                chosen=p;nearest=distance;}}
        require(nearest<std::numeric_limits<float>::max(),"source trigger has no original walkable sample");return chosen;};
    auto count=[](World& world,unsigned kind,int mode){size_t amount=0;const auto at=world.vfx->particles().find(kind);
        if(at!=world.vfx->particles().end())for(const auto& particle:at->second)if(particle.renderMode==mode)++amount;return amount;};
    std::unique_ptr<Graphics> graphics;bool exactDisplay=false;
    auto render=[&](World& world,const char* name){if(!draw)return;
        if(!graphics){graphics=std::make_unique<Graphics>();graphics->initialize(world.scene,world.directory(),width,height);
            graphics->loadFont(world.root);graphics->loadInventory(world.root);graphics->loadActorTextures(world.root,*world.actors);}
        else if(graphics->sourceAssets!=world.root){Graphics::ProfileScope profile(*graphics);graphics->loadFont(world.root);
            graphics->loadInventory(world.root);graphics->loadActorTextures(world.root,*world.actors);
            graphics->commit(graphics->prepare(world.scene,world.directory()));profile.commit();}
        else graphics->commit(graphics->prepare(world.scene,world.directory()));
        auto frame=[&](){SDL_PumpEvents();return graphics->render(world.scene,world.actor.camera,world.actor.position,world.actor.yaw,
            world.actor.animationTime,world.actor.walking,world.hud(),nullptr,world.actors.get(),world.drawObjects(),world.drawEffects(),
            world.drawActorTints(),world.lastOverlay,world.lastProjectionShake);};
        const auto* mode=SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(graphics->window));
        if(!exactDisplay&&mode&&width==mode->w&&height==mode->h){for(unsigned i=0;i<3;++i)frame();
            graphics->setFullscreen(true);exactDisplay=true;}
        bool shown=false;for(unsigned attempt=0;attempt<30&&!shown;++attempt)shown=frame()&&(!exactDisplay||
            (graphics->targetWidth==unsigned(width)&&graphics->targetHeight==unsigned(height)));
        require(shown,"GPU scene/effects frame unavailable");
        if(!capture.empty())graphics->screenshot(capture.parent_path()/(capture.stem().string()+"-"+name+".bmp"));};
    {
        World world(levelA,nullptr,presentMovieHeadless,true);enter(world,"ac05");
        for(unsigned i=0;i<5;++i)step(world);
        const auto boss=world.game.vm.field(world.game.scene,1570);unsigned pillar=0;
        if(!boss.null()){const auto ref=boss.reference();const auto& npc=world.actors->at({world.game.vm.field(ref,6).integer(),world.game.vm.field(ref,5).integer()});
            for(;pillar<3;++pillar)if(!world.scene.scripting.inside(pillar+3,{npc.pose.x,npc.localY,npc.pose.z}))break;}
        require(pillar<3,"all three source pillars unexpectedly contain the boss");
        const auto point=sample(world,pillar),marker=world.scene.scripting.markers.at(pillar+1);
        place(world,{0,0},point,edactormath::modelYawFromHeading(float(std::atan2(double(marker.z)-point.z,double(marker.x)-point.x))));
        const auto before=count(world,15,1);bool emitted=false;
        for(unsigned frame=0;frame<240&&!emitted;++frame){step(world,frame%6==0);emitted=count(world,15,1)>before;}
        require(emitted,"real wrong-pillar attack report did not execute dropRockOnAsh1870");
        const auto after=world.saveStateBytes();render(world,"ac05-rocks");step(world);world.restoreStateBytes(after,nullptr);
        require(world.saveStateBytes()==after,"AC05 active-rock save changed state");
        std::cout<<"AC05 actual World attack-report -> original1870 -> actor-offset rock15/mode1 and active save passed\n";
    }
    {
        World world(levelB,nullptr,presentMovieHeadless,true);enter(world,"bg12");auto& vm=world.game.vm;
        for(unsigned n=0;n<180&&vm.field(world.game.scene,1605).null();++n)step(world);
        const auto boss=vm.field(world.game.scene,1605).reference();require(boss!=0,"BG12 source wave did not create ArmoryBoss");
        const edactors::Key key{vm.field(boss,6).integer(),vm.field(boss,5).integer()};
        auto ready=[&](){unsigned stable=0;for(unsigned n=0;n<180;++n){const auto& ash=world.actors->at({0,0});
                if(!vm.field(world.game.ash,11).integer()&&!world.guard&&ash.actions.possible(116)){if(++stable==3)return;}else stable=0;step(world);}
            throw std::runtime_error("Source scene VFX World: BG12 Use action did not finish");};
        auto valve=[&](unsigned ashVolume,unsigned bossVolume){ready();place(world,{0,0},sample(world,ashVolume),0);
            place(world,key,sample(world,bossVolume),0);world.use();require(world.notice!="action is not connected yet","BG12 Use native failed");};
        valve(11,23);require(vm.field(world.game.scene,1613).integer()==1,"first molten valve did not run its source movie");
        valve(11,23);require(count(world,25,11)>0&&count(world,24,6)>0,"second molten Use did not emit source ring11/corona6");
        render(world,"bg12-molten");for(unsigned n=0;n<100;++n)step(world);
        valve(10,22);require(vm.field(world.game.scene,1612).integer()==1,"first steam valve did not run its source movie");
        valve(10,22);require(count(world,25,12)>0,"second steam Use did not emit source ring12");
        render(world,"bg12-steam");const auto pending=world.saveStateBytes();for(unsigned n=0;n<12;++n)step(world);
        const auto replay=world.saveStateBytes();world.restoreStateBytes(pending,nullptr);for(unsigned n=0;n<12;++n)step(world);
        require(world.saveStateBytes()==replay,"BG12 pending valve/VFX/RNG replay differs");
        require(vm.field(boss,13).integer()==50,"real molten/steam puzzle did not apply its source50HP damage");
        std::cout<<"BG12 actual World Use -> first movies -> ring25/modes11/12 + corona24/mode6, real50HP and pending replay passed\n";
    }
    {
        World world(levelB,nullptr,presentMovieHeadless,true);enter(world,"bc02");auto& vm=world.game.vm;
        for(unsigned n=0;n<180&&vm.field(world.game.scene,1547).null();++n)step(world);
        const auto boss=vm.field(world.game.scene,1547).reference();require(boss!=0,"BC02 source wave did not create Cthulhu");
        const edactors::Key key{vm.field(boss,6).integer(),vm.field(boss,5).integer()};const auto& npc=world.actors->at(key);
        const GroundPoint point{npc.pose.x+3.4f,0,npc.pose.z};require(floorHeight(world.scene.floor,{point.x,point.z}).has_value()&&
            clearGround(world.scene.ground,{point.x,point.z},0),"Cthulhu attack fixture is not on source floor");
        place(world,{0,0},point,-edactormath::HalfPi);const auto pending=world.saveStateBytes();unsigned elapsed=0;
        for(;elapsed<180&&!count(world,24,1);++elapsed)step(world);
        require(count(world,24,1)>0,"natural Cthulhu1709 did not execute joint fireball1713");
        const auto emitted=world.saveStateBytes();render(world,"bc02-fireball");world.restoreStateBytes(pending,nullptr);
        for(unsigned n=0;n<elapsed;++n)step(world);require(world.saveStateBytes()==emitted,"BC02 joint effect/AI/shared RNG save replay differs");
        std::cout<<"BC02 actual World natural think1709 -> joint34 fireball1713 -> source24/mode1 and exact AI/VFX/RNG replay passed\n";
    }
    graphics.reset();SDL_Quit();
}
