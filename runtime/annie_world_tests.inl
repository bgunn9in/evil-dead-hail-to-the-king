#pragma once
// Only Ash's approach positions are fixtures on the decoded AU04 floor. Enemy
// creation, thinker, HP, phase, attacks, death and handoff stay original JPC.
void testAnnieWorld(const fs::path& root,bool draw,int width,int height,const fs::path& capture) {
    std::cout<<std::unitbuf;checked(SDL_Init(SDL_INIT_GAMEPAD|(draw?SDL_INIT_VIDEO:0)),"Initialize original Annie World proof");
    auto require=[](bool value,const std::string& text){if(!value)throw std::runtime_error("Original Annie World: "+text);};
    World world(root,nullptr,presentMovieHeadless,true);
    if(world.scene.id!="au04"){world.endEpisodeScene();Scene next=loadScene(root/"au04");Actor placed=resetActor(next,0);
        std::vector<GateState> gates(next.doors.size());world.enterSourceScene(next,placed,gates);
        world.scene=std::move(next);world.actor=std::move(placed);world.gates=std::move(gates);}
    auto& vm=world.game.vm;const auto& p=world.game.package;const edactors::Key key{18,0};const unsigned phaseField=world.game.profile.dreamcast?1392u:1460u;
    auto tick=[&](){world.step({},false,false,.033f);require(!world.scriptFault,"original frame failed: "+world.game.diagnostic);
        require(vm.field(world.game.ash,13).integer()>0,"Ash died in the positional fight fixture");};
    for(unsigned n=0;n<180&&!world.actors->bodies().count(key);++n)tick();
    require(world.actors->bodies().count(key)!=0,"source AU04 wave did not create native18");
    const auto boss=world.actors->at(key).script;require(boss&&vm.objectClass(boss)==p.classIndex("AnnieZombie")&&
        vm.field(boss,13).integer()==100&&!vm.field(boss,phaseField).integer(),"original Annie initialization differs");
    bool sawTransform=false,phase=false;unsigned approaches=0,hits=0;int previousHealth=100;
    auto approach=[&](){const auto& npc=world.actors->at(key);auto& ash=world.actors->at({0,0});
        const Vec2 behind{-1.1f*std::sin(npc.pose.yaw),-1.1f*std::cos(npc.pose.yaw)};
        const double x=double(npc.pose.x)+behind.x-ash.pose.x,z=double(npc.pose.z)+behind.z-ash.pose.z;
        if(x*x+z*z>.1*.1) {
            bool found=false;for(unsigned n=0;n<=32&&!found;++n)for(float radius:{1.1f,1.3f,1.5f,1.7f,1.9f,2.1f}) {
                const int offset=n==0?0:(n%2?int(n+1)/2:-int(n)/2);
                const float angle=npc.pose.yaw+edactormath::Pi+float(offset)*edactormath::Tau/32;
                const Vec2 point{npc.pose.x+radius*std::sin(angle),npc.pose.z+radius*std::cos(angle)};
                if(!floorHeight(world.scene.floor,point)||!clearGround(world.scene.ground,point,world.scene.radius+.3f))continue;
                world.positionProgressBody(ash,{point.x,0,point.z});world.publishProgressBody(ash);
                world.actor.clearanceRadius=initialActorRadius(world.scene,point);world.actor.movement.held=false;++approaches;found=true;break;}
            require(found,"no source-floor approach sample around Annie");
        }
        const float heading=float(std::atan2(double(npc.pose.z)-ash.pose.z,double(npc.pose.x)-ash.pose.x));
        ash.pose.yaw=edactormath::modelYawFromHeading(heading);world.publishProgressBody(ash);
    };
    auto fight=[&](bool attack){approach();const auto& state=world.actors->at(key).actions.layers[0];sawTransform|=state.state==1;
        ControlFrame input;input.down[size_t(ControlAction::Aim)]=true;
        input.edges[size_t(ControlAction::SecondaryAttack)]=attack&&world.sourceTick%6==0;world.playerControls(input);
        require(world.notice!="could not perform action","source player action failed: "+world.game.diagnostic);tick();
        sawTransform|=world.actors->at(key).actions.layers[0].state==1;
        const int health=vm.field(boss,13).integer();if(health<previousHealth)++hits;previousHealth=health;
    };
    std::unique_ptr<Graphics> graphics;bool exactDisplay=false;
    auto render=[&](World& active,const char* name){if(!draw)return;
        if(!graphics){graphics=std::make_unique<Graphics>();graphics->initialize(active.scene,active.directory(),width,height);
            graphics->loadFont(active.root);graphics->loadInventory(active.root);graphics->loadActorTextures(active.root,*active.actors);}
        else if(graphics->sourceAssets!=active.root){Graphics::ProfileScope profile(*graphics);graphics->loadFont(active.root);
            graphics->loadInventory(active.root);graphics->loadActorTextures(active.root,*active.actors);
            graphics->commit(graphics->prepare(active.scene,active.directory()));profile.commit();}
        else graphics->commit(graphics->prepare(active.scene,active.directory()));
        auto frame=[&](){SDL_PumpEvents();return graphics->render(active.scene,active.actor.camera,active.actor.position,active.actor.yaw,
            active.actor.animationTime,active.actor.walking,active.hud(),nullptr,active.actors.get(),active.drawObjects(),active.drawEffects(),
            active.drawActorTints(),active.lastOverlay,active.lastProjectionShake);};
        const auto* mode=SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(graphics->window));
        if(!exactDisplay&&mode&&width==mode->w&&height==mode->h){for(unsigned n=0;n<3;++n)frame();
            graphics->setFullscreen(true);exactDisplay=true;}
        bool shown=false;for(unsigned n=0;n<30&&!shown;++n)shown=frame()&&(!exactDisplay||
            (graphics->targetWidth==unsigned(width)&&graphics->targetHeight==unsigned(height)));
        require(shown,"GPU fight/package frame unavailable");
        if(!capture.empty())graphics->screenshot(capture.parent_path()/(capture.stem().string()+"-"+name+".bmp"));};
    render(world,"au04-phase1");
    for(unsigned n=0;n<2400&&!phase;++n) {
        const bool wait=vm.field(boss,13).integer()<50;fight(!wait);
        if(vm.field(boss,phaseField).integer()) {
            require(sawTransform&&vm.field(boss,13).integer()==100,"source136/end34 did not regenerate Annie's second phase");phase=true;
        }
    }
    require(phase&&hits>0,"real input did not reach Annie's original transformation");render(world,"au04-phase2");
    const auto pending=world.saveStateBytes();
    for(unsigned n=0;n<24;++n)fight(true);const auto replay=world.saveStateBytes();
    world.restoreStateBytes(pending,nullptr);previousHealth=vm.field(boss,13).integer();
    for(unsigned n=0;n<24;++n)fight(true);require(world.saveStateBytes()==replay,"second-phase World/AI/native/VFX save replay differs");
    for(unsigned n=0;n<3600&&!world.pendingSceneSwitch;++n)fight(vm.field(boss,13).integer()>0);
    require(vm.field(boss,13).integer()<=0&&vm.staticField(608).integer()==1,"source second phase/death did not set its mission608");
    require(world.pendingSceneSwitch&&world.pendingSceneSwitch->sourceSceneId==0x61750400&&world.pendingSceneSwitch->destinationId==0x626d0100&&
        world.pendingSceneSwitch->camera==10&&world.pendingSceneSwitch->marker==0,"actual AU04 ending did not queue original BM01 arrival");
    const auto request=*world.pendingSceneSwitch;world.pendingSceneSwitch.reset();const auto before=world.saveStateBytes();
    auto next=stagePackageWorld(world,request);
    require(next->levelBPackage&&next->scene.id=="bm01"&&next->actor.camera==10&&world.saveStateBytes()==before,
        "earned Annie ending did not stage the actual fresh LevelB package atomically");
    require(next->game.vm.field(next->game.ash,13).integer()==vm.field(world.game.ash,13).integer(),"earned handoff lost original Ash health");
    const auto marker=next->scene.scripting.markers.at(0);const auto& ash=next->actors->at({0,0});
    require(ash.pose.x==marker.x&&ash.pose.z==marker.z&&ash.localY==0&&ash.pose.yaw==0,"earned BM01 source marker/Euler differs");
    render(*next,"bm01-arrival");
    std::cout<<"Original Annie World: source AU04wave18, real input17/HP<50/native136/end34/phase2HP100, exact pending replay, death608 -> true BM01 package passed; "
        <<approaches<<" Ash-only floor approach fixtures, "<<hits<<" observed damage steps\n";
    graphics.reset();SDL_Quit();
}
