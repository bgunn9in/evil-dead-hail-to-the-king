#pragma once
void testPackageGpu(const fs::path& root,const fs::path& levelBRoot,int width,int height,const fs::path& capture) {
    std::cout<<std::unitbuf;checked(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD),"Initialize GPU package rollback proof");
    World source(root,nullptr,presentMovieHeadless,true),destination(levelBRoot,nullptr,presentMovieHeadless,true);
    auto require=[&](bool value,const std::string& text){if(!value)throw std::runtime_error("GPU package rollback: "+text);};
    for(unsigned frame=0;frame<3;++frame){source.step({},false,false,1.f/30);destination.step({},false,false,1.f/30);}
    require(!source.scriptFault&&!destination.scriptFault,"source fixtures failed before GPU staging");
    source.openInventory();destination.openInventory();
    Graphics graphics;graphics.initialize(source.scene,source.directory(),width,height);
    graphics.loadFont(root);graphics.loadInventory(root);graphics.loadActorTextures(root,*source.actors);
    auto render=[&](World& world){bool shown=false;for(unsigned attempt=0;attempt<30&&!shown;++attempt){SDL_PumpEvents();
            shown=graphics.render(world.scene,world.actor.camera,world.actor.position,world.actor.yaw,world.actor.animationTime,world.actor.walking,
                world.hud(),&world.inventoryView,world.actors.get(),world.drawObjects(),world.drawEffects(),world.drawActorTints(),world.lastOverlay,world.lastProjectionShake);}
        require(shown,"source GPU frame was unavailable");};
    for(unsigned frame=0;frame<3;++frame)render(source);
    const auto* display=SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(graphics.window));
    if(display&&width==display->w&&height==display->h) {
        graphics.setFullscreen(true);
        bool ready=false;for(unsigned attempt=0;attempt<30&&!ready;++attempt){render(source);
            ready=graphics.targetWidth==unsigned(width)&&graphics.targetHeight==unsigned(height);if(!ready)SDL_Delay(1);}
        require(ready,"package proof target did not reach the requested desktop size");
    }
    struct Scratch {
        fs::path path;std::vector<fs::path> files;
        explicit Scratch(const fs::path& base):path(fs::absolute(base)/("package-gpu-proof-"+std::to_string(SDL_GetTicksNS()))) {
            if(!fs::create_directory(path))throw std::runtime_error("Cannot create owned GPU proof directory");
            fs::create_directories(path/"actors/textures");fs::create_directory(path/"vfx");
        }
        void copy(const fs::path& source,const fs::path& relative){const auto target=path/relative;
            fs::copy_file(source,target);files.push_back(target);}
        ~Scratch(){std::error_code ignored;for(auto it=files.rbegin();it!=files.rend();++it)fs::remove(*it,ignored);
            for(const auto& folder:{path/"actors/textures",path/"actors",path/"vfx",path})fs::remove(folder,ignored);}
    } scratch(root.parent_path());
    for(const auto& file:fs::directory_iterator(levelBRoot/"actors/textures"))
        if(file.is_regular_file()&&(file.path().extension()==".rgba"||file.path().filename()=="manifest.txt"))scratch.copy(file.path(),fs::path("actors/textures")/file.path().filename());
    const auto beforePath=capture.empty()?scratch.path/"before.bmp":capture.parent_path()/(capture.stem().string()+"-before.bmp");
    const auto afterPath=capture.empty()?scratch.path/"after.bmp":capture.parent_path()/(capture.stem().string()+"-after.bmp");
    graphics.screenshot(beforePath);if(capture.empty())scratch.files.push_back(beforePath);
    const auto originalImage=readFile(beforePath);
    auto* scene=graphics.resources.get();auto* font=graphics.font;auto* hud=graphics.hudBuffer;auto* inventory=graphics.inventoryBuffer;
    auto* atlas=graphics.particleTexture;const auto actorTextures=graphics.actorTextures,objectTextures=graphics.objectTextures;
    const auto inventoryTextures=graphics.inventoryTextures;const auto assets=graphics.sourceAssets;
    const auto hudText=graphics.hudText,inventoryKey=graphics.inventoryKey,fontCharset=graphics.fontCharset;
    std::vector<std::pair<edactors::Key,ActorResources*>> actorBuffers;
    for(const auto& [key,buffer]:graphics.actorBuffers)actorBuffers.emplace_back(key,buffer.get());
    const auto sourceBytes=source.saveStateBytes();
    auto unchanged=[&](){require(graphics.resources.get()==scene&&graphics.font==font&&graphics.hudBuffer==hud&&graphics.inventoryBuffer==inventory&&
            graphics.particleTexture==atlas&&graphics.actorTextures==actorTextures&&graphics.objectTextures==objectTextures&&
            graphics.inventoryTextures==inventoryTextures&&graphics.sourceAssets==assets&&graphics.hudText==hudText&&
            graphics.inventoryKey==inventoryKey&&graphics.fontCharset==fontCharset&&graphics.actorBuffers.size()==actorBuffers.size(),"old GPU profile handles/path/cache changed");
        for(const auto& [key,buffer]:actorBuffers)require(graphics.actorBuffers.at(key).get()==buffer,"old actor GPU buffer changed");
        require(source.saveStateBytes()==sourceBytes,"GPU transaction changed the old World");};
    bool atomicRejected=false;
    try{graphics.loadActorTextures(scratch.path,*destination.actors);}catch(const std::exception&){atomicRejected=true;}
    require(atomicRejected,"missing VFX atlas did not reject standalone profile upload");unchanged();
    bool scopeRejected=false;
    try {
        Graphics::ProfileScope profile(graphics);
        graphics.loadFont(levelBRoot);graphics.loadInventory(levelBRoot);graphics.loadActorTextures(levelBRoot,*destination.actors);
        auto prepared=graphics.prepare(destination.scene,destination.directory());graphics.commit(std::move(prepared));render(destination);
        graphics.loadActorTextures(scratch.path,*destination.actors); // All actor uploads succeed before the deliberately missing atlas.
        profile.commit();
    } catch(const std::exception&){scopeRejected=true;}
    require(scopeRejected,"late target profile upload unexpectedly succeeded");unchanged();render(source);graphics.screenshot(afterPath);
    if(capture.empty())scratch.files.push_back(afterPath);
    require(readFile(afterPath)==originalImage,"old scene pixels changed after GPU profile rollback");
    {
        Graphics::ProfileScope profile(graphics);
        graphics.loadFont(levelBRoot);graphics.loadInventory(levelBRoot);graphics.loadActorTextures(levelBRoot,*destination.actors);
        auto prepared=graphics.prepare(destination.scene,destination.directory());graphics.commit(std::move(prepared));render(destination);profile.commit();
    }
    require(graphics.sourceAssets==levelBRoot&&graphics.resources->assets==destination.directory()&&!graphics.profileScopeActive,"successful GPU profile did not commit");
    if(!capture.empty())graphics.screenshot(capture.parent_path()/(capture.stem().string()+"-committed.bmp"));
    std::cout<<"GPU package profiles: missing-atlas upload atomic, real B render rolled back to identical A pixels/handles/path/cache, valid B commit passed\n";
}
