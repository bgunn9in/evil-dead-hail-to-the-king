#pragma once
// Source-entry test bridge. All choices go through the original focused GUI.
void testTitleEpisodeWalkWorld(const fs::path& root,const std::function<void(World&)>& continueStory={}) {
    std::cout<<std::unitbuf;checked(SDL_Init(SDL_INIT_GAMEPAD),"Initialize actual AA01/NewGame walking bridge");
    auto require=[](bool value,const std::string& message){if(!value)throw std::runtime_error("Title walking bridge: "+message);};
    std::vector<uint32_t> movies;
    World world(root,nullptr,[&](World& active,uint32_t id,int flags){movies.push_back(id);presentMovieHeadless(active,id,flags);},true,nullptr,true);
    const auto& package=world.game.package;auto& vm=world.game.vm;
    for(unsigned n=0;n<1000&&!world.sourceMenuOwnsInput();++n){world.step({},false,false,.033f);require(!world.scriptFault,world.game.diagnostic);}
    require(world.scene.id=="aa01"&&world.sourceMenuOwnsInput(),"fresh source title did not reach its actual menu");
    require(movies==std::vector<uint32_t>{0x61303000u,0x61303161u},"source startup movie sequence differs");
    const auto& system=SourceGui::at(world.sourceGui.systems,world.sourceGui.activeSystem);
    require(system.pad&&vm.objectClass(system.pad)==package.classIndex("MenMain")&&system.selected>=0&&
        SourceGui::at(world.sourceGui.items,system.selected).ordinal==0,"fresh PC menu did not focus New Game");
    const auto menuTick=world.sourceTick;const auto menuClock=world.rawClockSeconds;
    require(world.sourceMenuInput({111}),"source menu refused actual Confirm111");
    require(world.pendingSceneSwitch&&world.pendingSceneSwitch->sourceSceneId==0x61610100&&world.pendingSceneSwitch->destinationId==0x616b0100&&
        world.pendingSceneSwitch->camera==2&&world.pendingSceneSwitch->marker==4&&
        world.pendingSceneSwitch->rotationXYZ==std::array<float,3>{{0,float(double(160)*.01745329238474369f),0}},"original New Game request differs");
    require(world.game.mode==0&&vm.staticField(844).integer()==1,"source New Game pending/applied mode distinction differs");
    world.step({},false,false,.033f);require(!world.scriptFault&&vm.staticField(844).integer()==0,world.game.diagnostic);
    const auto request=*world.pendingSceneSwitch;world.pendingSceneSwitch.reset();
    require(world.switchScene(request,nullptr)&&world.scene.id=="ak01"&&!world.levelBPackage,"ordinary same-package AA01 to AK01 failed");
    const auto marker=world.scene.scripting.markers.at(4);const auto& ash=world.actors->at({0,0});
    require(world.actor.camera==2&&ash.pose.x==marker.x&&ash.pose.z==marker.z&&world.transitions==1,"source New Game arrival differs");
    std::cout<<"ACTUAL_AA01_NEWGAME_AK01 tick"<<world.sourceTick<<" titleTick"<<menuTick<<" titleClock"<<menuClock<<" clock"<<world.rawClockSeconds
             <<" transitions"<<world.transitions<<" HP"<<vm.field(world.game.ash,13).integer()<<" state"<<ash.actions.layers[0].state<<"\n";
    const World* identity=&world;
    continueEpisodeWalkWorld(world,false,640,480,{},[&](World& same){
        require(&same==identity&&same.scene.id=="ah02"&&same.transitions==9,"first eight did not retain actual title World/history");
        std::cout<<"ACTUAL_AA01_FIRST8_AH02_PASS tick"<<same.sourceTick<<" transitions"<<same.transitions<<" HP"<<vm.field(same.game.ash,13).integer()<<"\n";
        if(continueStory)continueStory(same);
    });
    SDL_Quit();
}
