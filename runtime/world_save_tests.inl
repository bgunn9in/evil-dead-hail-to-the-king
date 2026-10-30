void testWorldSave(const fs::path& root,bool draw,int width,int height,const fs::path& capture) {
    checked(SDL_Init(SDL_INIT_GAMEPAD|(draw?SDL_INIT_VIDEO:0)),"Initialize world save proof");
    auto require=[](bool condition,const char* message){if(!condition)throw std::runtime_error(std::string("World save: ")+message);};
    struct Scratch {
        fs::path path;
        explicit Scratch(const fs::path& parent) {
            path=fs::weakly_canonical(fs::absolute(parent))/("world-save-test-"+std::to_string(SDL_GetTicksNS()));
            if(!fs::create_directory(path))throw std::runtime_error("Cannot create owned world-save proof directory");
        }
        ~Scratch(){std::error_code ignored;fs::remove(path/"quicksave.eds",ignored);fs::remove(path,ignored);}
    } scratch(root.parent_path());
    World world(root,nullptr,presentMovieHeadless);
    std::unique_ptr<Graphics> graphics;
    if(draw){graphics=std::make_unique<Graphics>();graphics->initialize(world.scene,world.directory(),width,height);graphics->loadFont(root);graphics->loadInventory(root);}
    const Vec2 position{2.45914788f,8.35600605f};const auto marker=world.scene.scripting.markers[33];
    world.fixture(position,std::atan2(marker.x-position.x,marker.z-position.z));world.use();world.closeInventory();
    const int clip=world.audioBank.resolve("ak01",0xee9221ed);
    require(clip>=0,"original ambience was not exported");
    const int voice=world.audioBank.play(clip,50,0);world.audioBank.advance(.125);
    const int pitched=world.audioBank.playAtFrequency(clip,75,world.audioBank.sampleRate(clip)*2);
    world.nativeProperties.voices[voice]={};world.nativeProperties.voices[pitched]={};
    const auto waiting=world.game.vm.start(world.game.package.methodIndex("ed/base/External","threadSleepEvent","(III)V"),
        0,{jpc::Value::integer(0),jpc::Value::integer(0),jpc::Value::integer(26)});
    require(world.saveGame(scratch.path),"actual picked-up world did not save");
    const auto saved=savefile::read(scratch.path,"quicksave.eds");
    world.game.vm.setField(world.game.ash,13,jpc::Value::integer(50));world.openInventory();world.applyInventoryItem();world.closeInventory();
    require(world.game.items().empty()&&world.game.vm.field(world.game.ash,13).integer()==85,"source item use did not change saved state");
    world.audioBank.stop(voice);world.audioBank.stop(pitched);world.game.vm.notify(0,0,26);world.game.vm.runReady();
    require(world.loadGame(scratch.path,graphics.get()),"saved world did not restore");
    require(world.scene.id=="ak01"&&world.game.vm.field(world.game.ash,13).integer()==100&&world.game.items().size()==1&&
        world.game.vm.staticField(418).integer()==1&&!world.audioBank.done(voice)&&!world.audioBank.done(pitched)&&
        world.game.vm.status(waiting)==jpc::Interpreter::ThreadStatus::Waiting,"load lost player, item, mission, voice token or waiting script");
    require(world.saveStateBytes()==saved,"roundtrip changed saved world bytes");
    const auto restoredSound=world.audioBank.snapshot();
    const auto restoredPitch=std::find_if(restoredSound.voices.begin(),restoredSound.voices.end(),[&](const auto& sound){return sound.token==pitched;});
    require(restoredPitch!=restoredSound.voices.end()&&restoredPitch->frequencyHz==world.audioBank.sampleRate(clip)*2,
        "load lost native initial sound frequency");
    world.openInventory();world.inventoryView.selected=world.inventoryView.items.empty()?0:unsigned(world.inventoryView.items.size()-1);
    const auto openInventory=world.saveStateBytes();const auto selected=world.inventoryView.selected;
    world.closeInventory();world.restoreStateBytes(openInventory);
    require(world.inventoryView.open&&world.inventoryView.selected==selected&&world.game.mode==2&&world.saveStateBytes()==openInventory,
        "load lost open inventory, selection or host interface state");
    world.restoreStateBytes(saved);
    world.advanceIdleTime(world.audioBank.duration(clip)*2);
    require(world.nativeProperties.voices.count(voice)&&!world.nativeProperties.voices.count(pitched),
        "completed voice metadata accumulated or a live loop lost its metadata");
    const auto original=world.saveStateBytes();
    for(auto corrupt:{std::vector<uint8_t>(saved.begin(),saved.end()-1),saved}) {
        if(corrupt.size()==saved.size())corrupt.back()^=1;
        bool rejected=false;try{world.restoreStateBytes(corrupt,graphics.get());}catch(const std::exception&){rejected=true;}
        require(rejected&&world.saveStateBytes()==original,"malformed save modified the active world");
    }
    world.nativeProperties.movies.push_back({"ak01",0x61303100,0,false});
    require(!world.saveGame(scratch.path)&&savefile::read(scratch.path,"quicksave.eds")==saved,"busy save replaced a valid file");
    world.nativeProperties.movies.pop_back();
    world.fixture({0.0643162f,11.2097956f},0.05967f);world.step({},false,true,0,graphics.get());
    require(world.pending>=0&&world.guard==45,"source door did not begin opening");
    for(unsigned i=0;i<12;++i)world.step({},false,false,1.0f/30,graphics.get());
    require(world.saveGame(scratch.path),"pending passage could not save");
    const auto opening=world.saveStateBytes();const auto tick=world.sourceTick;
    for(unsigned i=0;i<45&&world.scene.id=="ak01";++i)world.step({},false,false,1.0f/30,graphics.get());
    require(world.scene.id=="ak09","door did not reach the real destination");
    require(world.loadGame(scratch.path,graphics.get())&&world.scene.id=="ak01"&&world.sourceTick==tick&&world.saveStateBytes()==opening,
        "cross-scene load lost the pending source timer");
    for(unsigned i=0;i<45&&world.scene.id=="ak01";++i)world.step({},false,false,1.0f/30,graphics.get());
    require(world.scene.id=="ak09","restored pending door did not complete");
    if(graphics) {
        bool rendered=false;for(unsigned i=0;i<30&&!rendered;++i){SDL_PumpEvents();rendered=graphics->render(world.scene,world.actor.camera,
            world.actor.position,world.actor.yaw,world.actor.animationTime,world.actor.walking,world.hud());}
        require(rendered,"restored destination did not render");if(!capture.empty())graphics->screenshot(capture);
    }
    {
        World native(root,nullptr,presentMovieHeadless,true);
        const auto initial=native.saveStateBytes();
        require(floorHeight(native.scene.floor,{native.actor.position[0],native.actor.position[2]}).has_value(),"floor-cache fixture is outside the source floor");
        native.actor.floorTriangle=std::numeric_limits<unsigned>::max();
        const auto unknownFloor=native.saveStateBytes();native.restoreStateBytes(unknownFloor);
        require(native.actor.floorTriangle==std::numeric_limits<unsigned>::max()&&native.saveStateBytes()==unknownFloor,
            "load validation rewrote a saved unknown floor cache");
        require(stageSavedWorld(native,unknownFloor)->saveStateBytes()==unknownFloor,"fresh load rewrote the saved unknown floor cache");
        native.restoreStateBytes(initial);
        const auto current=native.saveStateBytes(),camera=native.cameraTimeline->snapshot();
        auto legacy=current,oldCamera=camera;
        require(oldCamera.size()>25&&oldCamera[12]=='2',"unexpected current camera save header");
        oldCamera[12]='1';const uint64_t oldAk01Fingerprint=0xd63ed9b61e4d0b93ull; // Original Dreamcast AK01 declared camera records.
        std::memcpy(oldCamera.data()+17,&oldAk01Fingerprint,8);
        const auto location=std::search(legacy.begin(),legacy.end(),camera.begin(),camera.end());
        require(location!=legacy.end(),"camera blob was absent from full World save");
        std::copy(oldCamera.begin(),oldCamera.end(),location);
        native.restoreStateBytes(legacy);
        require(native.saveStateBytes()==current,"legacy camera migration changed full World state");
        auto staged=stageSavedWorld(native,legacy);
        require(staged->saveStateBytes()==current,"fresh package load rejected a legacy camera World save");
        require(native.saveGame(scratch.path),"native quicksave did not pass its full validator");
        const auto file=savefile::read(scratch.path,"quicksave.eds"),actors=native.actors->snapshot(),vm=native.game.vm.snapshot();
        auto& actions=native.actors->at({0,0}).actions;const auto playback=actions.layers[0].playback;
        require(playback>=0,"native save validation fixture has no active playback");
        actions.playbacks[size_t(playback)].child=playback;
        require(!native.saveGame(scratch.path)&&savefile::read(scratch.path,"quicksave.eds")==file&&native.game.vm.snapshot()==vm,
            "invalid internal actor state replaced the previous quicksave");
        native.actors->restore(actors);
    }
    graphics.reset();SDL_Quit();
    std::cout<<"World save passed: actual item/mission/health, audio token, waiting JPC, corrupted-load rollback, unchanged previous file and cross-scene pending-door continuation\n";
}
