// Asset-spawn fixtures check native dependencies and persistence, not story reachability.
void testSourceScenes(const fs::path& root,bool draw,int width,int height,const fs::path& capture) {
    std::cout<<std::unitbuf;
    checked(SDL_Init(SDL_INIT_GAMEPAD|(draw?SDL_INIT_VIDEO:0)),"Initialize original scene coverage proof");
    std::vector<std::string> areas;
    {World initial(root,nullptr,presentMovieHeadless,true);areas=initial.areas;}
    unsigned passed=0;std::vector<std::string> failures;std::unique_ptr<Graphics> graphics;
    bool exactDisplayProof=false;
    for(const auto& identity:areas) {
        try {
            World world(root,nullptr,presentMovieHeadless,true);
            auto enter=[&] {
                world.endEpisodeScene();
                Scene next=loadScene(root/identity);Actor placed=resetActor(next,0);
                std::vector<GateState> states(next.doors.size());
                world.enterSourceScene(next,placed,states);
                world.scene=std::move(next);world.actor=std::move(placed);world.gates=std::move(states);
            };
            if(identity!="ak01")enter();
            for(unsigned i=0;i<5;++i)world.step({},false,false,1.0f/30);
            if(world.scriptFault)throw std::runtime_error(world.game.diagnostic);
            if(world.scene.id!=identity)throw std::runtime_error("Asset-spawn fixture left the requested room");
            if(draw) {
                if(!graphics) {
                    graphics=std::make_unique<Graphics>();graphics->initialize(world.scene,world.directory(),width,height);
                    graphics->loadFont(root);graphics->loadInventory(root);graphics->loadActorTextures(root,*world.actors);
                    const auto* mode=SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(graphics->window));
                    if(mode&&width==mode->w&&height==mode->h) {
                        for(unsigned i=0;i<3;++i){SDL_PumpEvents();graphics->render(world.scene,world.actor.camera,world.actor.position,world.actor.yaw,0,false);}
                        graphics->setFullscreen(true);exactDisplayProof=true;
                    }
                } else graphics->resources=graphics->prepare(world.scene,world.directory());
                bool rendered=false;
                for(unsigned i=0;i<30&&!rendered;++i){SDL_PumpEvents();rendered=graphics->render(world.scene,world.actor.camera,
                    world.actor.position,world.actor.yaw,world.actor.animationTime,world.actor.walking,world.hud(),nullptr,
                    world.actors.get(),world.drawObjects(),world.drawEffects(),world.drawActorTints(),world.lastOverlay,world.lastProjectionShake)&&
                    (!exactDisplayProof||(graphics->targetWidth==unsigned(width)&&graphics->targetHeight==unsigned(height)));}
                if(!rendered)throw std::runtime_error("Source scene GPU frame was unavailable");
                if(!capture.empty())graphics->screenshot(capture.parent_path()/(capture.stem().string()+"-"+identity+capture.extension().string()));
            }
            world.closeInventory();const auto saved=world.saveStateBytes();
            world.step({},false,false,1.0f/30);world.restoreStateBytes(saved,graphics.get());
            if(world.saveStateBytes()!=saved)throw std::runtime_error("Opaque source save/load changed state bytes");
            enter();
            for(unsigned i=0;i<5;++i)world.step({},false,false,1.0f/30);
            if(world.scriptFault)throw std::runtime_error(world.game.diagnostic);
            ++passed;std::cout<<"Original scene passed: "<<identity<<" fresh/frame/save/load/revisit; "<<world.actors->bodies().size()<<" actors\n";
        } catch(const std::exception& error) {
            graphics.reset();failures.push_back(identity+": "+error.what());std::cerr<<"Original scene failed: "<<failures.back()<<"\n";
        }
    }
    graphics.reset();SDL_Quit();
    if(!failures.empty())throw std::runtime_error("Original scene coverage: "+std::to_string(passed)+"/"+std::to_string(areas.size())+" passed; first: "+failures.front());
    std::cout<<"Original scene coverage passed: "<<passed<<" source rooms; bounded frames and exact source save/load\n";
}
