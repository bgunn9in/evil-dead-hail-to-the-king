// Explicit headless environment proof; no assertion of story reachability.
void testScreenFlow(const fs::path& root,bool draw=false,int width=1280,int height=960,const fs::path& capture={}) {
    checked(SDL_Init(SDL_INIT_GAMEPAD|(draw?SDL_INIT_VIDEO:0)),"Initialize source screen flow proof");
    auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(std::string("Screen flow: ")+message);};
    World world(root,nullptr,presentMovieHeadless,true);
    std::unique_ptr<Graphics> graphics;
    auto captureFrame=[&](const std::string& suffix) {
        if(capture.empty())return fs::path{};
        const auto path=capture.parent_path()/(capture.stem().string()+suffix+capture.extension().string());
        graphics->screenshot(path);return path;
    };
    auto pixels=[&](const fs::path& path) {
        auto* image=checked(SDL_LoadBMP(path.u8string().c_str()),"Read screen proof bitmap");
        auto* rgba=SDL_ConvertSurface(image,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(image);checked(rgba,"Convert screen proof bitmap");
        std::vector<uint8_t> result(size_t(rgba->w)*rgba->h*4);
        for(int row=0;row<rgba->h;++row)std::memcpy(result.data()+size_t(row)*rgba->w*4,static_cast<uint8_t*>(rgba->pixels)+size_t(row)*rgba->pitch,size_t(rgba->w)*4);
        SDL_DestroySurface(rgba);return result;
    };
    if(draw) {
        graphics=std::make_unique<Graphics>();graphics->initialize(world.scene,world.directory(),width,height);
        graphics->loadFont(root);graphics->loadInventory(root);graphics->loadActorTextures(root,*world.actors);
        // Drain the initial Win32 resize before requesting exact desktop size.
        for(unsigned i=0;i<3;++i){SDL_PumpEvents();graphics->render(world.scene,world.actor.camera,world.actor.position,world.actor.yaw,0,false);}
        const auto* mode=SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(graphics->window));
        const bool exact=mode&&width==mode->w&&height==mode->h;
        if(exact) {
            graphics->setFullscreen(true);
        }
        bool ready=false;
        for(unsigned i=0;i<30&&!ready;++i){SDL_PumpEvents();ready=graphics->render(world.scene,world.actor.camera,world.actor.position,world.actor.yaw,0,false)&&
            (!exact||(graphics->targetWidth==unsigned(width)&&graphics->targetHeight==unsigned(height)));if(!ready)SDL_Delay(1);}
        require(ready,"GPU render target did not settle");
    }
    const auto ticks=world.sourceTick,nativeTicks=world.actors->cloneState().sourceTick;
    const int sourceFrames=world.game.vm.staticField(393).integer();const double clock=world.rawClockSeconds;
    const auto cameraBefore=world.cameraTimeline->countdown;
    world.sourceScreen.fadeOut();const auto first=world.sourceRenderOnlyFrames;
    const auto elapsed=world.waitForScreenFade(graphics.get());
    require(draw?elapsed>=0:elapsed==0,"headless render invented elapsed time");
    require(world.sourceRenderOnlyFrames-first==32&&world.sourceScreen.fadeOutDone()&&world.lastOverlay.blackAlpha==255,
            "source32-pass out fade failed");
    require(world.sourceTick==ticks&&world.actors->cloneState().sourceTick==nativeTicks&&
            world.game.vm.staticField(393).integer()==sourceFrames&&
            (draw?world.rawClockSeconds>=clock:world.rawClockSeconds==clock),
            "render-only advanced ordinary VM/animation clocks");
    require(world.screenPresentationRevision!=0,"modal presentation did not invalidate outer wall dt");
    if(draw) {
        const auto black=captureFrame("-black");
        if(!black.empty()) {
            const auto image=pixels(black);
            for(size_t i=0;i<image.size();i+=4)require(!(image[i]|image[i+1]|image[i+2]),"final native fade is not entirely black");
        }
        world.sourceScreen.fadeIn();
        for(unsigned i=0;i<8;++i)world.sourceRenderOnlyFrame(graphics.get());captureFrame("-fade-in-half");
        for(unsigned i=0;i<8;++i)world.sourceRenderOnlyFrame(graphics.get());require(world.sourceScreen.fadeInDone(),"native16-pass in fade failed");
        // Compare only pure GPU redraws of the same source state. Camera and
        // render-time VFX producers deliberately do not advance between them.
        world.lastOverlay={};world.lastProjectionShake=0;
        auto redraw=[&] {require(graphics->render(world.scene,world.actor.camera,world.actor.position,world.actor.yaw,0,false,
            world.hud(),nullptr,world.actors.get(),world.drawObjects(),world.drawEffects(),world.drawActorTints(),world.lastOverlay,world.lastProjectionShake),"GPU screen redraw unavailable");};
        redraw();const auto clear=captureFrame("-clear");
        world.lastOverlay={true,128,false,0};redraw();const auto half=captureFrame("-alpha128");
        if(!clear.empty()) {
            const auto before=pixels(clear),after=pixels(half);require(before.size()==after.size(),"composite dimensions differ");
            uint64_t color=0;
            for(size_t i=0;i<before.size();++i)if(i%4!=3) {
                color+=before[i];const int expected=int(std::lround(double(before[i])*127./255.));
                require(std::abs(int(after[i])-expected)<=1,"GPU black alpha differs from SRC_ALPHA/INV_SRC_ALPHA");
            }
            require(color!=0,"composite baseline did not contain the original scene");
        }
        world.sourceScreen=edscreen::State{};world.sourceScreen.letterbox(1);
        for(unsigned i=0;i<16;++i)world.sourceRenderOnlyFrame(graphics.get());captureFrame("-bars");
        world.sourceScreen.shake(80);world.sourceRenderOnlyFrame(graphics.get());
        require(world.lastProjectionShake==-1.75f&&world.lastOverlay.barHeight==32,"source shake/bar fixture differs");
        captureFrame("-shake-bars");
    }
    (void)cameraBefore; // Camera countdown/frame is allowed to advance in these32 render passes.
    const auto saved=world.screenSnapshot();world.sourceScreen.fadeIn();world.sampleSourceScreen();
    world.restoreScreen(saved);require(world.screenSnapshot()==saved,"screen state did not roundtrip");
    auto corrupt=saved;corrupt.push_back(0);bool rejected=false;
    try{world.restoreScreen(corrupt);}catch(const std::exception&){rejected=true;}
    require(rejected&&world.screenSnapshot()==saved,"bad screen state changed live state");
    const std::string adjacent=world.levelBPackage?"bm02":"ak09";
    if(fs::exists(root/adjacent/"scene.txt")) {
        world.endEpisodeScene();Scene target=loadScene(root/adjacent);Actor placed=resetActor(target,0);
        std::vector<GateState> states(target.doors.size());world.enterSourceScene(target,placed,states);
        const auto oldId=world.scene.id;world.scriptScene=&target;world.scriptActor=&placed;world.scriptGates=&states;
        struct Reset {World& world;~Reset(){world.scriptScene=nullptr;world.scriptActor=nullptr;world.scriptGates=nullptr;}} reset{world};
        require(!target.portalData.portals.empty(),"staged scene has no source portal fixture");
        const auto& portal=target.portalData.portals.front();const auto& zone=target.nativePortals.at(portal.index);
        Vec2 center{};for(const auto& p:zone.points)center=center+p;center=center*(1.f/float(zone.points.size()));
        std::fill(target.enabledPortals.begin(),target.enabledPortals.end(),false);target.enabledPortals.at(portal.index)=true;
        world.positionProgressBody(world.actors->at({0,0}),{center.x,0,center.z});world.screenQueuedPortal.reset();
        const auto* priorResources=graphics?graphics->resources.get():nullptr;
        world.sourceScreen.fadeOut();const auto stagedTick=world.sourceTick;world.waitForScreenFade(graphics.get());
        require(world.screenQueuedPortal&&world.screenQueuedPortal->sourceSceneId==episode_portal_detail::sceneId(adjacent)&&
                world.screenQueuedPortal->portalIndex==portal.index&&world.screenQueuedPortal->destinationId==portal.destinationId,
                "staged portal metadata used the member scene");
        require(world.scene.id==oldId&&world.sourceTick==stagedTick,"render-only recursively committed a scene change");
        if(draw) {
            require(graphics->resources.get()==priorResources,"staged fade committed its temporary GPU scene resources");
            captureFrame("-staged-black");
            {
                World::ScreenGraphicsScope resources(graphics.get(),target,root/adjacent);
                world.sourceScreen.fadeIn();for(unsigned i=0;i<16;++i)world.sourceRenderOnlyFrame(graphics.get());
                captureFrame("-staged-clear");
            }
        }
        if(adjacent=="ak09")for(unsigned portalIndex:{9u,10u}) {
            const auto found=std::find_if(target.portalData.portals.begin(),target.portalData.portals.end(),
                [&](const auto& item){return item.index==portalIndex;});
            require(found!=target.portalData.portals.end()&&found->destinationId==episode_portal_detail::sceneId(adjacent),"AK09 same-scene portal fixture missing");
            const auto& region=target.nativePortals.at(portalIndex);Vec2 point{};
            for(const auto& vertex:region.points)point=point+vertex;point=point*(1.f/float(region.points.size()));
            std::fill(target.enabledPortals.begin(),target.enabledPortals.end(),false);target.enabledPortals.at(portalIndex)=true;
            world.positionProgressBody(world.actors->at({0,0}),{point.x,0,point.z});
            const auto sceneRef=world.game.scene;const auto arena=world.progressArenaGeneration;
            const auto tick=world.sourceTick,actorTick=world.actors->cloneState().sourceTick;
            const int frames=world.game.vm.staticField(393).integer();
            world.actors->post({{0,0},{4,0},37,{{1,0,0,0}}});
            require(!world.actors->cloneState().messages.empty(),"raw-message reset fixture was not queued");
            world.pendingSceneSwitch=PackageRequest{found->destinationId,0x616b0100u,0,0,{{0,0,0}}};
            world.screenPortalWorker();const auto& body=world.actors->at({0,0});const auto marker=target.scripting.markers.at(found->marker);
            require(body.pose.x==marker.x&&body.pose.z==marker.z&&body.localY==0&&body.pose.yaw==found->rotationXYZ[1],
                    "same-scene portal did not apply original marker/model yaw");
            require(world.game.scene==sceneRef&&world.game.vm.field(world.game.level,38).reference()==sceneRef&&
                    world.progressArenaGeneration==arena&&world.scene.id==oldId&&world.sourceTick==tick&&
                    world.actors->cloneState().sourceTick==actorTick&&world.game.vm.staticField(393).integer()==frames,
                    "same-scene portal recreated scene or advanced VM/animation");
            require(!world.screenQueuedPortal&&!world.pendingSceneSwitch&&world.actors->cloneState().messages.empty(),
                    "same-scene portal retained a pending scene/message queue");
            for(const auto& [kind,particles]:world.vfx->particles()){(void)kind;require(particles.empty(),"same-scene portal retained active VFX");}
            for(unsigned attempt=0;attempt<60;++attempt)require(!world.vfx->spawn(9,{{point.x,0,point.z}}),"portal suppression ended before60emission attempts");
            require(world.vfx->spawn(9,{{point.x,0,point.z}}),"portal suppression behaved as a frame timer");
        }
        // Invoke the actual imported native through the VM. Same-scene marker
        //-1 preserves all LOCAL coordinates but still copies raw modelEulerY.
        auto& ash=world.actors->at({0,0});world.positionProgressBody(ash,{ash.pose.x,7.25f,ash.pose.z});
        const std::array<float,3> local{{ash.pose.x,ash.localY,ash.pose.z}};
        const auto sceneRef=world.game.scene;const auto tick=world.sourceTick,actorTick=world.actors->cloneState().sourceTick;
        const auto arena=world.progressArenaGeneration;const int frames=world.game.vm.staticField(393).integer();
        world.actors->post({{0,0},{4,0},37,{{1,0,0,0}}});
        const uint32_t sourceId=episode_portal_detail::sceneId(adjacent);
        world.game.vm.invoke(world.game.package.methodIndex("ed/base/External","sceneSwitch","(IIII)V"),0,
            {jpc::Value::integer(jpc::signedBits(sourceId)),jpc::Value::integer(int32_t(world.screenCamera())),
             jpc::Value::integer(-1),jpc::Value::integer(-123)});
        require(ash.pose.x==local[0]&&ash.localY==local[1]&&ash.pose.z==local[2]&&
                ash.pose.yaw==float(double(-123)*0.01745329238474369f),"native same-scene marker-1 changed position or skipped/raw-normalized Euler");
        require(world.game.scene==sceneRef&&world.game.vm.field(world.game.level,38).reference()==sceneRef&&
                world.progressArenaGeneration==arena&&world.sourceTick==tick&&world.actors->cloneState().sourceTick==actorTick&&
                world.game.vm.staticField(393).integer()==frames&&!world.pendingSceneSwitch&&!world.screenQueuedPortal&&
                world.actors->cloneState().messages.empty(),"native same-scene marker-1 reloaded scene, ticked, or retained queued messages");
    }
    std::cout<<"Original source screen flow passed:32 render-only passes, frozen VM/animation ticks, snapshot and staged portal queue\n";
}
