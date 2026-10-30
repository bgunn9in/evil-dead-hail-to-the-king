#pragma once
#include "cart_world_tests.inl"
// Natural enemy damage and original Ash709 own this lifetime. In particular,
// its counter120 hide branch is unreachable; the fresh title VM hides GameOver.
void testGameOverFlow(const fs::path& levelA,bool draw,int width,int height,const fs::path& capture) {
    using namespace cart_world_test;std::cout<<std::unitbuf;
    checked(SDL_Init(SDL_INIT_GAMEPAD|(draw?SDL_INIT_VIDEO:0)),"Initialize original GameOver sprite proof");
    struct Quit{~Quit(){SDL_Quit();}} quit;Scratch scratch(levelA);std::unique_ptr<Graphics> graphics;bool exact=false;
    auto captureName=[&](const std::string& label){const auto path=capture.empty()?scratch.path/(label+".bmp"):
        capture.parent_path()/(capture.stem().string()+"-"+label+".bmp");if(capture.empty())scratch.track(path);return path;};
    auto frame=[&](World& world,const std::vector<edguipresentation::Sprite>& sprites){
        bool shown=false;for(unsigned n=0;n<30&&!shown;++n){SDL_PumpEvents();shown=graphics->render(world.scene,world.actor.camera,world.actor.position,
            world.actor.yaw,world.actor.animationTime,world.actor.walking,world.hud(),world.inventoryView.open?&world.inventoryView:nullptr,
            world.actors.get(),world.drawObjects(),world.drawEffects(),world.drawActorTints(),world.lastOverlay,world.lastProjectionShake,world.sourceTextPages,sprites);}
        require(shown,"GameOver GPU source frame unavailable");};
    auto prepare=[&](World& world){if(!draw)return;
        if(!graphics){graphics=std::make_unique<Graphics>();graphics->initialize(world.scene,world.directory(),width,height);}
        else graphics->commit(graphics->prepare(world.scene,world.directory()));
        Graphics::ProfileScope profile(*graphics);graphics->loadFont(world.root);graphics->loadInventory(world.root);graphics->loadActorTextures(world.root,*world.actors);
        graphics->commit(graphics->prepare(world.scene,world.directory()));profile.commit();
        if(!exact){for(unsigned n=0;n<3;++n)frame(world,world.drawGuiSprites());const auto* display=SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(graphics->window));
            if(display&&width==display->w&&height==display->h){graphics->setFullscreen(true);
                bool settled=false;for(unsigned n=0;n<30&&!settled;++n){frame(world,world.drawGuiSprites());settled=graphics->targetWidth==unsigned(width)&&graphics->targetHeight==unsigned(height);if(!settled)SDL_Delay(1);}
                require(settled,"GameOver target did not reach exact requested desktop pixels");exact=true;}}
    };
    auto pixels=[](const fs::path& path){auto* loaded=checked(SDL_LoadBMP(path.string().c_str()),"Read GameOver screenshot");
        auto* image=SDL_ConvertSurface(loaded,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(loaded);image=checked(image,"Convert GameOver screenshot");
        std::vector<uint8_t> bytes(size_t(image->w)*size_t(image->h)*4);for(int row=0;row<image->h;++row)
            std::memcpy(bytes.data()+size_t(row)*image->w*4,static_cast<uint8_t*>(image->pixels)+size_t(row)*image->pitch,size_t(image->w)*4);
        SDL_DestroySurface(image);return bytes;};
    for(bool b:{false,true}) {
        World world(b?levelA/"levelB":levelA,nullptr,presentMovieHeadless,true);const std::string location=b?"bc02":"au04",title=b?"bb01":"aa01";
        world.endEpisodeScene();Scene next=loadScene(world.root/location);Actor placed=resetActor(next,0);std::vector<GateState> gates(next.doors.size());
        world.enterSourceScene(next,placed,gates);world.scene=std::move(next);world.actor=std::move(placed);world.gates=std::move(gates);
        const edactors::Key key{b?33:18,0};auto& vm=world.game.vm;
        for(unsigned n=0;n<180&&!world.actors->bodies().count(key);++n)tick(world);
        require(world.actors->bodies().count(key)!=0&&world.drawGuiSprites().empty(),"source enemy wave or initially hidden GameOver differs");
        const auto enemy=world.actors->at(key).script;prepare(world);
        if(draw){frame(world,{});graphics->screenshot(captureName(location+"-before-death"));}
        bool visible=false;uint64_t zero=0,shown=0;unsigned hits=0;int health=100;
        for(unsigned n=0;n<4000&&!world.pendingSceneSwitch;++n) {
            if(!zero){const auto& npc=world.actors->at(key);auto& ash=world.actors->at({0,0});const Vec2 point{
                    npc.pose.x+1.1f*std::sin(npc.pose.yaw),npc.pose.z+1.1f*std::cos(npc.pose.yaw)};
                require(floorHeight(world.scene.floor,point).has_value()&&clearGround(world.scene.ground,point,world.scene.radius+.3f),"source enemy front fixture left original floor");
                const double dx=double(point.x)-ash.pose.x,dz=double(point.z)-ash.pose.z;if(dx*dx+dz*dz>.01){
                    world.positionProgressBody(ash,{point.x,0,point.z});world.publishProgressBody(ash);world.actor.clearanceRadius=initialActorRadius(world.scene,point);world.actor.movement.held=false;}}
            tick(world);const int hp=vm.field(world.game.ash,13).integer();if(!zero&&hp<health){++hits;health=hp;if(hp<=0)zero=world.sourceTick;}
            const auto sprites=world.drawGuiSprites();
            if(!visible&&!sprites.empty()) {
                require(zero&&sprites.size()==1&&sprites[0].vertices.size()==6,"GameOver appeared before source death or has another primitive");visible=true;shown=world.sourceTick;
                if(draw){const auto frozen=vm.snapshot(),gui=world.sourceGui.snapshot();const auto without=captureName(location+"-without-sprite"),with=captureName(location+"-game-over");
                    frame(world,{});graphics->screenshot(without);frame(world,sprites);graphics->screenshot(with);
                    require(vm.snapshot()==frozen&&world.sourceGui.snapshot()==gui,"pure GUI draw advanced source VM/GUI");
                    const auto old=pixels(without),current=pixels(with);require(old.size()==current.size(),"GameOver framebuffer resized between frozen passes");
                    const auto area=sceneViewport(graphics->targetWidth,graphics->targetHeight);float minX=1,minY=1,maxX=-1,maxY=-1;
                    for(const auto& v:sprites[0].vertices){minX=std::min(minX,v.x);maxX=std::max(maxX,v.x);minY=std::min(minY,v.y);maxY=std::max(maxY,v.y);}
                    const int left=int(std::floor(area.x+(minX+1)*.5f*area.width))-1,right=int(std::ceil(area.x+(maxX+1)*.5f*area.width))+1,
                        top=int(std::floor(area.y+(1-maxY)*.5f*area.height))-1,bottom=int(std::ceil(area.y+(1-minY)*.5f*area.height))+1;
                    size_t changed=0;for(unsigned y=0;y<graphics->targetHeight;++y)for(unsigned x=0;x<graphics->targetWidth;++x){const size_t offset=(size_t(y)*graphics->targetWidth+x)*4;
                        if(std::memcmp(old.data()+offset,current.data()+offset,3)){++changed;require(int(x)>=left&&int(x)<=right&&int(y)>=top&&int(y)<=bottom,"GameOver changed pixels outside original Maya quad");}}
                    require(changed>100,"original GameOver sprite did not affect visible pixels");
                    std::cout<<"GameOver GPU "<<location<<": "<<graphics->targetWidth<<"x"<<graphics->targetHeight<<", "<<changed<<" changed pixels inside source quad\n";
                }
            }
            if(visible)require(!sprites.empty(),"source GameOver was hidden before its original VM reload");
        }
        require(zero&&visible&&shown>zero&&world.pendingSceneSwitch&&world.pendingSceneSwitch->destinationId==episode_portal_detail::sceneId(title)&&
            vm.field(enemy,13).integer()==100,"natural source death/title request did not complete");
        const auto request=*world.pendingSceneSwitch;world.pendingSceneSwitch.reset();auto menu=stagePackageWorld(world,request);
        require(menu->scene.id==title&&menu->drawGuiSprites().empty()&&!world.drawGuiSprites().empty(),"fresh title did not hide GameOver independently of old VM");
        for(unsigned n=0;n<8&&!menu->sourceMenuOwnsInput();++n)tick(*menu);
        confirmNativeStart(*menu);
        require(menu->sourceMenuOwnsInput()&&active(*menu)=="MenMain","death title has no original usable menu");
        prepare(*menu);if(draw){frame(*menu,menu->drawGuiSprites());graphics->screenshot(captureName(title+"-hidden-menu"));}
        std::cout<<"GameOver source "<<location<<": "<<hits<<" natural hits, visible at"<<shown-zero<<", retained through source120tick loop, fresh"<<title<<" hidden/menu passed\n";
    }
    graphics.reset();
}
