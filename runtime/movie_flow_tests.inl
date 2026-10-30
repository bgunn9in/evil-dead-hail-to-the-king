void testMovies(const fs::path& root,bool draw,int width,int height,const fs::path& capture) {
    checked(SDL_Init(SDL_INIT_GAMEPAD|(draw?SDL_INIT_VIDEO:0)),"Initialize source movie proof");
    auto require=[](bool condition,const char* message){if(!condition)throw std::runtime_error(std::string("Movie flow: ")+message);};
    World world(root,nullptr,presentMovieHeadless);
    require(world.scene.episode&&world.nativeProperties.movies.size()==2,"source AK01 did not request both original intro movies");
    require(world.nativeProperties.movies[0].resource==0x61303100&&world.nativeProperties.movies[1].resource==0x61303162&&
        std::all_of(world.nativeProperties.movies.begin(),world.nativeProperties.movies.end(),[](const auto& value){return value.played;}),
        "source movie order or completion was lost");
    std::unique_ptr<Graphics> graphics;
    if(draw) {
        graphics=std::make_unique<Graphics>();graphics->initialize(world.scene,world.directory(),width,height);
        const auto* mode=SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(graphics->window));
        if(mode&&width==mode->w&&height==mode->h) {
            for(unsigned i=0;i<3;++i){SDL_PumpEvents();graphics->render(world.scene,world.actor.camera,world.actor.position,world.actor.yaw,0,false);}
            graphics->setFullscreen(true);
        }
    }
    Scene next=loadScene(root/"ah02");Actor placed=resetActor(next,0);std::vector<GateState> states(next.doors.size());
    world.enterSourceScene(next,placed,states);
    world.scene=std::move(next);world.actor=std::move(placed);world.gates=std::move(states);
    require(world.nativeProperties.movies.size()==3&&world.nativeProperties.movies.back().resource==0x61303600&&
        world.nativeProperties.movies.back().played&&world.game.vm.staticField(436).integer()==1,"AH02 source movie did not finish before its mission flag");
    next=loadScene(root/"ah02");placed=resetActor(next,0);states=std::vector<GateState>(next.doors.size());
    world.enterSourceScene(next,placed,states);require(world.nativeProperties.movies.size()==3,"AH02 repeat arrival replayed the one-time movie");
    if(graphics) {
        MoviePlayer movie(root,0x61303600);movie.start(false);movie.advance(33.0/15+1e-9);
        bool rendered=false;
        const auto* mode=SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(graphics->window));
        const bool exact=mode&&width==mode->w&&height==mode->h;
        for(unsigned frame=0;frame<30&&!rendered;++frame) {
            SDL_Event event;while(SDL_PollEvent(&event))if(event.type==SDL_EVENT_QUIT)throw QuitRequested{};
            rendered=graphics->renderMovie(movie)&&(!exact||(graphics->targetWidth==unsigned(width)&&graphics->targetHeight==unsigned(height)));
            if(!rendered)SDL_Delay(1);
        }
        require(rendered,"native movie viewport did not settle");if(!capture.empty())graphics->screenshot(capture);
        // A new instance must upload even when its source ID and frame repeat.
        MoviePlayer reloaded(root,0x61303600);reloaded.start(false);require(graphics->renderMovie(reloaded),"reloaded source movie frame was unavailable");
    }
    graphics.reset();SDL_Quit();
    std::cout<<"Movie flow passed: original AK01 two-movie order, synchronous AH02 flag, no repeat, bounded source frames and 4:3 presentation\n";
}
