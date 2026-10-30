void testMenuContexts(const fs::path& root,bool draw,int width,int height,const fs::path& capture) {
    checked(SDL_Init(SDL_INIT_GAMEPAD|(draw?SDL_INIT_VIDEO:0)),"Initialize original menu context proof");
    auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(std::string("Menu context: ")+message);};
    std::unique_ptr<Graphics> graphics;
    for(const std::string id:{"aa01","bb01"}) {
        const auto directory=(id=="aa01"?root:root/"levelB")/id;const Scene scene=loadScene(directory);
        require(scene.menuContext&&scene.episode&&scene.id==id&&scene.vertexCount==0&&scene.vertices.empty(),"menu invented legacy character geometry");
        require(scene.floor.size()==2&&scene.scripting.markers.empty()&&scene.nativePortals.empty()&&scene.triggers.empty(),"original menu geometry differs");
        require(scene.cameras.size()==(id=="aa01"?4u:3u),"original menu cameras differ");
        auto actor=resetActor(scene,0);const auto before=actor;
        stepActor(scene,actor,{1,1},true,1);
        require(std::equal(std::begin(actor.position),std::end(actor.position),std::begin(before.position))&&!actor.walking,"menu allowed modern walking");
        if(draw) {
            if(!graphics){graphics=std::make_unique<Graphics>();graphics->initialize(scene,directory,width,height);graphics->loadFont(root);}
            else graphics->resources=graphics->prepare(scene,directory);
            for(unsigned camera=0;camera<scene.cameras.size();++camera) {
                bool rendered=false;for(unsigned attempt=0;attempt<30&&!rendered;++attempt){SDL_PumpEvents();rendered=graphics->render(scene,camera,actor.position,0,0,false);}
                require(rendered,"original menu background was unavailable");
                if(!capture.empty())graphics->screenshot(capture.parent_path()/(capture.stem().string()+"-"+id+"-"+std::to_string(camera)+capture.extension().string()));
            }
        }
        std::cout<<"Original menu context passed: "<<id<<" genuine floor, no authored spawn/walking/legacy mesh; "<<scene.cameras.size()<<" cameras\n";
    }
    struct Scratch {fs::path path;explicit Scratch(const fs::path& parent):path(fs::absolute(parent)/("menu-header-test-"+std::to_string(SDL_GetTicksNS()))){if(!fs::create_directory(path))throw std::runtime_error("Cannot create owned menu proof directory");}
        ~Scratch(){std::error_code ignored;fs::remove(path/"menu.txt",ignored);fs::remove(path,ignored);}} scratch(root.parent_path());
    for(const char* invalid:{"BAD aa01 4","EDMENU1 xx01 4","EDMENU1 aa01 0","EDMENU1 aa01 257","EDMENU1 aa01 4 extra"}) {
        {std::ofstream file(scratch.path/"menu.txt");file<<invalid;}
        bool rejected=false;try{loadMenuScene(scratch.path);}catch(const std::runtime_error& error){rejected=std::string(error.what())=="Invalid original menu context";}
        require(rejected,"malformed menu metadata passed validation");
    }
    graphics.reset();SDL_Quit();
}
