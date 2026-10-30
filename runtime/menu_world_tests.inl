#pragma once
// PC selects MenMain directly. The optional MenStart branch below is an
// explicit source-GUI fixture, not a fabricated console/platform capability.
void testMenuWorld(const fs::path& levelA,bool draw,int width,int height,const fs::path& capture) {
    std::cout<<std::unitbuf;checked(SDL_Init(SDL_INIT_GAMEPAD|(draw?SDL_INIT_VIDEO:0)),"Initialize original menu World proof");
    auto require=[](bool value,const std::string& text){if(!value)throw std::runtime_error("Original menu World: "+text);};
    auto I=[](int n){return jpc::Value::integer(n);};
    for(bool b:{false,true}) {
        std::vector<uint32_t> movies;
        World boot(b?levelA/"levelB":levelA,nullptr,[&](World& active,uint32_t movie,int flags){movies.push_back(movie);presentMovieHeadless(active,movie,flags);},true,nullptr,true);
        require(boot.game.vm.invoke(boot.game.package.methodIndex("ed/base/External","switchToDisk","(II)I"),0,{I(-1),I(12345)}).integer()==1,
            "PC disk-switch native did not preserve its constant success result");
        const auto initialVM=boot.game.vm.snapshot(),initialActors=boot.actors->snapshot();
        ControlFrame attack;attack.down[size_t(ControlAction::Aim)]=true;attack.edges[size_t(ControlAction::PrimaryAttack)]=true;
        boot.playerControls(attack);boot.use();boot.openInventory();
        require(boot.game.vm.snapshot()==initialVM&&boot.actors->snapshot()==initialActors&&boot.hud().empty(),"title accepted gameplay controls before its menu");
        for(unsigned n=0;n<1000&&!boot.sourceMenuOwnsInput();++n)boot.step({},false,false,.033f);
        require(boot.scene.id==(b?"bb01":"aa01")&&boot.sourceMenuOwnsInput(),"initial source title " +boot.scene.id+" mode="+std::to_string(boot.game.mode)+" applied="+std::to_string(boot.game.vm.staticField(844).integer())+" movies="+std::to_string(movies.size())+" diagnostic="+boot.game.diagnostic);
        require(movies==std::vector<uint32_t>{0x61303000u,0x61303161u},"initial source title did not request the two authored startup movies");
        require(std::all_of(boot.nativeProperties.movies.begin(),boot.nativeProperties.movies.end(),[&](const auto& movie){return movie.scene==boot.scene.id;}),
            "initial source boot ran an unintended gameplay intro before the title");
    }
    std::unique_ptr<Graphics> graphics;bool exactDisplay=false;
    auto render=[&](World& world,const std::string& label){if(!draw)return;
        if(!graphics){graphics=std::make_unique<Graphics>();graphics->initialize(world.scene,world.directory(),width,height);
            graphics->loadFont(world.root);graphics->loadInventory(world.root);graphics->loadActorTextures(world.root,*world.actors);}
        else if(graphics->sourceAssets!=world.root){Graphics::ProfileScope profile(*graphics);graphics->loadFont(world.root);
            graphics->loadInventory(world.root);graphics->loadActorTextures(world.root,*world.actors);
            graphics->commit(graphics->prepare(world.scene,world.directory()));profile.commit();}
        else graphics->commit(graphics->prepare(world.scene,world.directory()));
        auto frame=[&](){SDL_PumpEvents();return graphics->render(world.scene,world.actor.camera,world.actor.position,world.actor.yaw,
            world.actor.animationTime,world.actor.walking,world.hud(),&world.inventoryView,world.actors.get(),world.drawObjects(),world.drawEffects(),
            world.drawActorTints(),world.lastOverlay,world.lastProjectionShake);};
        const auto* mode=SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(graphics->window));
        if(!exactDisplay&&mode&&width==mode->w&&height==mode->h){for(unsigned n=0;n<3;++n)frame();
            graphics->setFullscreen(true);exactDisplay=true;}
        bool shown=false;for(unsigned n=0;n<30&&!shown;++n)shown=frame()&&(!exactDisplay||
            (graphics->targetWidth==unsigned(width)&&graphics->targetHeight==unsigned(height)));
        require(shown,"GPU menu/profile frame unavailable");
        if(!capture.empty())graphics->screenshot(capture.parent_path()/(capture.stem().string()+"-"+label+".bmp"));};
    for(bool b:{false,true}) {
        const auto root=b?levelA/"levelB":levelA;const std::string id=b?"bb01":"aa01";
        World source(root,nullptr,presentMovieHeadless,true);source.advanceIdleTime(123.371);
        auto& sourceVM=source.game.vm;const auto& sourcePackage=source.game.package;
        if(b)sourceVM.invoke(sourcePackage.methodIndex("ed/base/External","reboot","()V"));
        else sourceVM.invoke(sourcePackage.methodIndex("ed/base/External","sceneSwitch","(IIII)V"),0,
            {I(int(episode_portal_detail::sceneId(id))),I(0),I(-1),I(0)});
        require(source.pendingSceneSwitch.has_value(),"native restart did not queue the menu");
        const auto restart=*source.pendingSceneSwitch;source.pendingSceneSwitch.reset();const auto old=source.saveStateBytes();
        auto menu=stagePackageWorld(source,restart);auto& world=*menu;auto& vm=world.game.vm;const auto& p=world.game.package;
        require(world.scene.id==id&&world.scene.menuContext&&world.rawClockSeconds==0,"real AA/BB package restart did not reset time before JPC startup");
        require(source.saveStateBytes()==old,"menu restart changed its old World");
        auto tick=[&](){world.step({},false,false,.033f);require(!world.scriptFault,"menu frame failed: "+world.game.diagnostic);};
        for(unsigned n=0;n<8&&!world.sourceMenuOwnsInput();++n)tick();
        require(world.sourceMenuOwnsInput()&&world.game.mode==1&&vm.staticField(844).integer()==1,"source Main/Interface did not apply menu mode1");
        auto active=[&](){const auto& system=SourceGui::at(world.sourceGui.systems,world.sourceGui.activeSystem);
            require(system.pad!=0,"active source menu has no Pad");return p.classes[size_t(vm.objectClass(system.pad))].name;};
        require(active()=="MenMain"&&!world.sourceMenuText().empty(),"PC menu did not select original MenMain labels");
        const auto beforeActors=world.actors->cloneState();const auto beforeTick=world.sourceTick;const auto frame=vm.staticField(393).integer();
        vm.invoke(p.methodIndex("ed/base/External","actorMoveForward","(III)I"),0,{I(0),I(0),I(10)});
        const auto pending=world.actors->at({0,0}).pendingMove;
        for(unsigned n=0;n<5;++n)tick();
        require(world.sourceTick==beforeTick+5&&vm.staticField(393).integer()==frame+5,"Main/source scheduler frame did not continue in menu");
        const auto paused=world.actors->cloneState();require(paused.bodies.size()==beforeActors.bodies.size(),"menu created unexpected native actors");
        for(size_t n=0;n<paused.bodies.size();++n)require(paused.bodies[n].updates==beforeActors.bodies[n].updates&&
            paused.bodies[n].pose.x==beforeActors.bodies[n].pose.x&&paused.bodies[n].pose.z==beforeActors.bodies[n].pose.z,
            "menu stepped actor animation/physics");
        require(world.actors->at({0,0}).pendingMove==pending,"menu consumed pending movement");
        // Cancel the test-only queued displacement with its exact native inverse.
        vm.invoke(p.methodIndex("ed/base/External","actorMoveForward","(III)I"),0,{I(0),I(0),I(-10)});
        auto input=[&](int event){require(world.sourceMenuInput({event}),"source menu refused its PC input");};
        input(106);input(106);input(111);require(active()=="MenOptionsLeft","source Main did not enter Options");
        const int stereo=world.sourceGui.stereo;input(111);require(world.sourceGui.stereo!=stereo,"original Options stereo callback did not run");
        const auto ui=world.saveStateBytes();const auto options=world.sourceMenuText();const int system=world.sourceGui.activeSystem;
        input(102);tick();world.restoreStateBytes(ui,nullptr);
        require(world.saveStateBytes()==ui&&world.game.mode==1&&vm.staticField(844).integer()==1&&world.sourceGui.activeSystem==system&&
            world.sourceMenuText()==options&&world.sourceMenuOwnsInput()&&!world.inventoryView.open,"World13 failed exact menu/UI restoration");
        auto loaded=stageSavedWorld(source,ui);require(loaded->saveStateBytes()==ui&&loaded->sourceMenuOwnsInput(),"fresh package load lost menu mode/UI");
        render(world,id+"-options");input(102);require(active()=="MenMain","source Options cancel did not return to Main");
        // Exercise the genuine Start callback without claiming PC enable chooses it.
        const auto start=vm.staticField(881).reference();require(start&&vm.objectClass(start)==p.classIndex("MenStart"),"source Main did not create MenStart");
        vm.invoke(p.methodIndex("GuiSys","enable","()V"),start);vm.invoke(p.methodIndex("GuiSys","setVisible","(Z)V"),start,{I(1)});
        require(active()=="MenStart","source-GUI Start fixture did not acquire focus");input(111);require(active()=="MenMain","real Start111 callback did not return to Main");
        for(unsigned n=0;n<8;++n){const auto& main=SourceGui::at(world.sourceGui.systems,world.sourceGui.activeSystem);
            if(main.selected>=0&&SourceGui::at(world.sourceGui.items,main.selected).ordinal==0)break;input(105);}
        const int selected=SourceGui::at(world.sourceGui.systems,world.sourceGui.activeSystem).selected;
        require(selected>=0&&SourceGui::at(world.sourceGui.items,selected).ordinal==0,"source links did not reach NewGame");
        // A real inventory-method fixture makes the exact BB->AK restore gate observable.
        require(vm.invoke(p.methodIndex("Ash","pickupItem","(II)Z"),world.game.ash,{I(29),I(1)}).integer()!=0,"source transfer item fixture failed");
        auto quantity=[](const World& target){int count=0;for(const auto& item:target.game.items())if(item.itemID==29)count+=item.quantity;return count;};
        const int held=quantity(world);render(world,id+"-main");input(111);
        require(world.pendingSceneSwitch.has_value()&&world.pendingSceneSwitch->sourceSceneId==episode_portal_detail::sceneId(id)&&
            world.pendingSceneSwitch->destinationId==0x616b0100&&world.pendingSceneSwitch->camera==2&&world.pendingSceneSwitch->marker==4&&
            world.pendingSceneSwitch->rotationXYZ[1]==float(double(160)*.01745329238474369f),"source NewGame request differs from original AK01 pose");
        require(world.game.mode==0&&vm.staticField(844).integer()==1,"NewGame did not preserve pending/applied mode distinction");
        const auto actions=world.actors->at({0,0}).actions.updates;tick();
        require(!world.sourceMenuOwnsInput()&&vm.staticField(844).integer()==0&&world.actors->at({0,0}).actions.updates==actions,
            "end-frame menu exit advanced actors before applying mode0");
        const auto request=*world.pendingSceneSwitch;world.pendingSceneSwitch.reset();
        if(b){const auto unchanged=world.saveStateBytes();auto game=stagePackageWorld(world,request);
            require(game->root==levelA&&game->scene.id=="ak01"&&!game->levelBPackage&&quantity(*game)==held&&world.saveStateBytes()==unchanged,
                "BB01->AK01 real player transfer or atomic old World failed");render(*game,"bb01-newgame-ak01");}
        else {require(world.switchScene(request,nullptr)&&world.scene.id=="ak01"&&!world.levelBPackage,"AA01 same-package NewGame did not load AK01");
            render(world,"aa01-newgame-ak01");}
        std::cout<<"Original "<<id<<" World: real restart clock0, Main/Interface/menu1 pause, source Options/Start111/NewGame, exact UI13 save/load and AK01 "
            <<(b?"cross-package restore":"same-package arrival")<<" passed\n";
    }
    graphics.reset();SDL_Quit();
}
