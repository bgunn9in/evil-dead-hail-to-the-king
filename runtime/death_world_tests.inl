#pragma once
#include "cart_world_tests.inl"
// Enemy/AI/HP/death/menu/load are original. Only Ash's approach pose is a
// source-floor fixture; no damage messages, health or mission writes are used.
void testDeathWorld(const fs::path& levelA) {
    using namespace cart_world_test;std::cout<<std::unitbuf;
    checked(SDL_Init(SDL_INIT_GAMEPAD),"Initialize original Ash death/restart proof");
    struct Quit {~Quit(){SDL_Quit();}} quit;Scratch scratch(levelA);
    for(bool b:{false,true}) {
        const auto root=b?levelA/"levelB":levelA,prefs=scratch.directory(b?"b-death":"a-death");
        scratch.track(prefs/"quicksave.eds");World world(root,nullptr,presentMovieHeadless,true);world.configureSourceCart(prefs);
        const char* destination=b?"bc02":"au04";
        world.endEpisodeScene();Scene next=loadScene(root/destination);Actor placed=resetActor(next,0);
        std::vector<GateState> gates(next.doors.size());world.enterSourceScene(next,placed,gates);
        world.scene=std::move(next);world.actor=std::move(placed);world.gates=std::move(gates);
        const edactors::Key key{b?33:18,0};auto& vm=world.game.vm;const auto& p=world.game.package;
        for(unsigned n=0;n<180&&!world.actors->bodies().count(key);++n)tick(world);
        require(world.actors->bodies().count(key)!=0,"source scene wave did not create its real enemy");
        const auto enemy=world.actors->at(key).script;
        require(enemy&&vm.objectClass(enemy)==p.classIndex(b?"Cthulhu":"AnnieZombie")&&vm.field(enemy,13).integer()==100&&
            vm.field(world.game.ash,13).integer()==100,"natural enemy/player defaults differ");
        world.saveGame(prefs);const auto saved=savefile::read(prefs,"quicksave.eds");
        int previous=100;unsigned hits=0,poses=0;uint64_t zeroTick=0;bool gameOverVisible=false,gameOverDrawn=false;
        for(unsigned n=0;n<3600&&!zeroTick;++n) {
            const auto& npc=world.actors->at(key);auto& ash=world.actors->at({0,0});
            const Vec2 front{1.1f*std::sin(npc.pose.yaw),1.1f*std::cos(npc.pose.yaw)};
            const Vec2 point{npc.pose.x+front.x,npc.pose.z+front.z};
            require(floorHeight(world.scene.floor,point).has_value()&&clearGround(world.scene.ground,point,world.scene.radius+.3f),
                "native enemy front has no clear original-floor fixture");
            const double dx=double(point.x)-ash.pose.x,dz=double(point.z)-ash.pose.z;
            if(dx*dx+dz*dz>.01) {
                world.positionProgressBody(ash,{point.x,0,point.z});world.publishProgressBody(ash);
                world.actor.clearanceRadius=initialActorRadius(world.scene,point);world.actor.movement.held=false;++poses;
            }
            tick(world);const int health=vm.field(world.game.ash,13).integer();
            require(health<=previous,"source combat unexpectedly healed Ash");
            if(health<previous){++hits;previous=health;std::cout<<"Natural "<<destination<<" enemy -> Ash"<<health<<" at source tick"<<world.sourceTick<<"\n";}
            if(health<=0)zeroTick=world.sourceTick;
        }
        require(zeroTick&&hits>0&&vm.field(enemy,13).integer()==100,"unattacked natural enemy did not kill Ash");
        require(world.game.mode==0&&vm.staticField(844).integer()==0,"natural death unexpectedly requested menu mode4");
        for(unsigned n=0;n<360&&!world.pendingSceneSwitch;++n) {
            tick(world);
            for(size_t index=0;index<world.sourceGui.systems.size();++index){const auto& system=world.sourceGui.systems[index];
                if(system.pad&&vm.objectClass(system.pad)==p.classIndex("GameOver")) {
                    gameOverVisible|=(system.flags&9u)==9u;
                    for(const auto& command:world.sourceGuiCommands)gameOverDrawn|=command.item>=0&&SourceGui::at(world.sourceGui.items,command.item).owner==int(index);
                }
            }
            require(vm.staticField(844).integer()!=4,"source death entered an unsupported fabricated mode4");
        }
        const uint32_t title=b?0x62620100u:0x61610100u;
        require(gameOverVisible&&world.pendingSceneSwitch&&world.pendingSceneSwitch->destinationId==title&&world.sourceTick>=zeroTick+120,
            "source death did not show timed GameOver and queue original AA/BB title");
        require(!world.game.profile.dreamcast||gameOverDrawn,"native GameOver produced no authored draw commands");
        std::cout<<"Original death restart tuple: "<<edpackage::sceneName(world.pendingSceneSwitch->destinationId)
            <<" camera"<<world.pendingSceneSwitch->camera<<" marker"<<world.pendingSceneSwitch->marker<<" EulerY"<<world.pendingSceneSwitch->rotationXYZ[1]
            <<" after"<<world.sourceTick-zeroTick<<" source ticks\n";
        const auto request=*world.pendingSceneSwitch;world.pendingSceneSwitch.reset();const auto old=liveState(world);
        auto owner=stagePackageWorld(world,request);
        require(liveState(world)==old&&owner->scene.id==(b?"bb01":"aa01")&&owner->rawClockSeconds==0,"death title staging changed old World or restart clock");
        for(unsigned n=0;n<8&&!owner->sourceMenuOwnsInput();++n)tick(*owner);
        confirmNativeStart(*owner);
        require(owner->sourceMenuOwnsInput()&&active(*owner)=="MenMain","natural death did not reach usable real Main menu");
        accessLoad(*owner);catalogReplay(*owner,prefs);queueLoad(*owner);const auto menuBefore=liveState(*owner);auto incoming=processSourceCart(*owner);
        require(incoming&&incoming->saveStateBytes()==saved&&liveState(*owner)==menuBefore,"death-menu Load failed to stage its actual pre-combat file");
        owner.swap(incoming);require(owner->scene.id==destination&&owner->game.vm.field(owner->game.ash,13).integer()==100,
            "outer owner did not commit restored pre-combat World");
        std::cout<<"Original death "<<destination<<": "<<hits<<" natural enemy hits, "<<poses<<" Ash-only floor approaches, timed GameOver -> "
            <<(b?"BB01":"AA01")<<" -> real Cart Load restored original100HP file passed\n";
    }
}
