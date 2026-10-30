// Included inside World after episode_progress.inl. This is native420690(1),
// not a simulation tick. The outer World consumes queued scene changes later.
edscreen::State sourceScreen;
edscreen::Overlay lastOverlay;
float lastProjectionShake=0;
uint64_t screenPresentationRevision=0,sourceRenderOnlyFrames=0;
bool screenRenderOnlyActive=false,screenFadeWaiting=false;
struct ScreenPortalRequest {
    uint32_t sourceSceneId=0,portalIndex=0,destinationId=0,camera=0,marker=0;
    std::array<float,3> rotationXYZ{};
    unsigned suppressedEmissionAttempts=0;
};
std::optional<ScreenPortalRequest> screenQueuedPortal;

unsigned screenCamera()const {
    if(cameraTimeline&&cameraTimeline->state)return unsigned(cameraTimeline->state->request.track);
    return 0; // Native scene context's initial camera before first publication.
}
void screenCameraRules() {
    auto& target=nativeScene();auto& placed=nativeActor();
    if(actors)publishProgressBody(actors->at({0,0}));
    if(placed.membership.size()!=target.triggers.size())throw jpc::Error("Source camera membership count differs from scene");
    const Vec2 point{placed.position[0],placed.position[2]};const auto mask=triggerMask(target.triggerGrid,point);
    for(size_t index=0;index<target.triggers.size();++index) {
        const auto& zone=target.triggers[index];const auto overlap=mask&zone.mask;
        if(!(overlap&0xffffu)||!(overlap&0xffff0000u))continue;
        const bool inside=insideTrigger(zone,point);const bool old=placed.membership[index];
        const unsigned event=inside&&!old?1u:!inside&&old?2u:0u;
        placed.membership[index]=inside;if(!event)continue;
        // The camera-event payload is captured once per zone, even when one
        // action or callback changes the camera before later rules execute.
        const unsigned payload=screenCamera();
        if(!zone.rules.empty()) {
            for(const auto& rule:zone.rules)if(rule.event==event&&
                (!rule.cameraEvent||(payload!=0&&payload==rule.cameraPayload)))
                for(unsigned camera:rule.cameras)publishEpisodeCamera(camera);
        } else for(unsigned camera:event==1?zone.cameras:zone.exitCameras)publishEpisodeCamera(camera);
    }
    placed.camera=screenCamera();
}
void applySameSceneTransition(const PackageRequest& request) {
    auto& target=nativeScene();
    if(!actors||!vfx||request.sourceSceneId!=episode_portal_detail::sceneId(target.id)||request.destinationId!=request.sourceSceneId||
       request.camera>=target.cameras.size()||request.marker< -1||
       (request.marker>=0&&size_t(request.marker)>=target.scripting.markers.size())||
       request.rotationXYZ[0]!=0||request.rotationXYZ[2]!=0||!std::isfinite(request.rotationXYZ[1]))throw jpc::Error("Invalid same-scene source transition");
    publishEpisodeCamera(request.camera);auto& ash=actors->at({0,0});
    // shortcut: cached native heading shares model yaw until its independent portal behavior is represented.
    ash.pose.yaw=request.rotationXYZ[1];
    if(request.marker!=-1){const auto marker=target.scripting.markers[size_t(request.marker)];positionProgressBody(ash,{marker.x,0,marker.z});}
    else publishProgressBody(ash);
    pendingSceneSwitch.reset();screenQueuedPortal.reset();
    vfx->clearAndSuppress(60);actors->clearMessages();
    const Vec2 point{ash.pose.x,ash.pose.z};
    nativeActor().clearanceRadius=clearGround(target.ground,point,0)?initialActorRadius(target,point):0;
}
void screenPortalWorker() {
    auto& target=nativeScene();const auto& placed=nativeActor();
    const uint32_t mask=triggerMask(target.triggerGrid,{placed.position[0],placed.position[2]});
    const auto source=episode_portal_detail::sceneId(target.id);
    for(const auto& portal:target.portalData.portals) {
        if(portal.index>=target.enabledPortals.size()||portal.index>=target.nativePortals.size())
            throw jpc::Error("Source portal worker index differs from scene");
        const auto& region=target.nativePortals[portal.index];const uint32_t overlap=mask&region.mask;
        if(!target.enabledPortals[portal.index]||!(overlap&0xffffu)||!(overlap&0xffff0000u)||
           !insideTrigger(region,{placed.position[0],placed.position[2]}))continue;
        if(portal.destinationId==source) {
            applySameSceneTransition({source,source,portal.camera,int32_t(portal.marker),portal.rotationXYZ});
            continue;
        }
        screenQueuedPortal=ScreenPortalRequest{source,portal.index,portal.destinationId,portal.camera,portal.marker,portal.rotationXYZ,0};
        if(actors)actors->clearMessages();
        //416D6A continues through the original portal order; last queue wins.
        // Later precise tests see a relocated actor, retaining the original broadphase mask.
    }
}
void sampleSourceScreen() {
    lastProjectionShake=sourceScreen.sampleProjection();lastOverlay=sourceScreen.sampleOverlay();
}

// Keep staged scene resources alive for an entire pre-roll without committing
// the caller's scene transaction or discarding its current GPU resources.
struct ScreenGraphicsScope {
    Graphics* graphics=nullptr;
    std::unique_ptr<SceneResources> previous;
    bool swapped=false;
    ScreenGraphicsScope(Graphics* value,const Scene& target,const fs::path& directory):graphics(value) {
        if(!graphics)return;
        if(!graphics->window||!graphics->device)throw jpc::Error("Initialize graphics before source fade rendering");
        if(graphics->resources&&graphics->resources->assets.lexically_normal()==directory.lexically_normal())return;
        auto prepared=graphics->prepare(target,directory);previous=std::move(graphics->resources);graphics->resources=std::move(prepared);swapped=true;
    }
    ~ScreenGraphicsScope() {
        if(swapped) {
            SDL_WaitForGPUIdle(graphics->device);
            graphics->resources.swap(previous);
        }
    }
    ScreenGraphicsScope(const ScreenGraphicsScope&)=delete;
};
bool sourceRenderOnlyFrame(Graphics* graphics=nullptr) {
    if(screenRenderOnlyActive)throw jpc::Error("Reentrant source render-only worker");
    struct Guard {bool& active;explicit Guard(bool& value):active(value){active=true;}~Guard(){active=false;}} renderGuard(screenRenderOnlyActive);
    auto& target=nativeScene();auto& placed=nativeActor();
    ScreenGraphicsScope resources(graphics,target,root/target.id);
    screenCameraRules();screenPortalWorker();advanceCameraTimeline();placed.camera=screenCamera();
    lastProjectionShake=sourceScreen.sampleProjection();
    if(actors){sampleEpisodeFloors();sampleEpisodeEffects();}
    if(vfx)vfx->sampleRender(true);
    sampleSourceTexts();
    lastOverlay=sourceScreen.sampleOverlay();
    if(sourceRenderOnlyFrames==std::numeric_limits<uint64_t>::max())throw jpc::Error("Source render-only counter exhausted");
    ++sourceRenderOnlyFrames;
    if(!graphics)return true;
    // The presenter initializes the profile's fonts/inventory/model textures.
    // drawObjects/drawEffects use nativeScene/nativeActor, including a staged
    // target while moviePlay is still inside its original createScene call.
    if(!graphics->render(target,placed.camera,placed.position,placed.yaw,placed.animationTime,placed.walking,
                         scriptScene?std::string{}:hud(),inventoryView.open?&inventoryView:nullptr,actors.get(),
                         drawObjects(),drawEffects(),drawActorTints(),lastOverlay,lastProjectionShake,sourceTextPages,drawGuiSprites()))
        throw jpc::Error("Source fade render target is unavailable");
    return true;
}
double waitForScreenFade(Graphics* graphics=nullptr) {
    if(screenFadeWaiting)throw jpc::Error("Nested source fade wait");
    //42FD00 specifically waits for outDone. Native callers always request an
    //out fade first; do not fabricate success for an unrelated or absent fade.
    if(!sourceScreen.fadeOutRequested())throw jpc::Error("Source fade wait requires fadeOut");
    if(sourceScreen.fadeOutDone())return 0;
    if(screenPresentationRevision==std::numeric_limits<uint64_t>::max())throw jpc::Error("Screen presentation revision exhausted");
    ++screenPresentationRevision;screenFadeWaiting=true;
    uint64_t previous=graphics?SDL_GetTicksNS():0;double elapsed=0;
    auto clock=[&] {
        if(!graphics)return;
        const auto now=SDL_GetTicksNS();const double dt=double(now-previous)/1e9;previous=now;
        elapsed+=dt;advanceIdleTime(dt);
    };
    try {
        {
            ScreenGraphicsScope resources(graphics,nativeScene(),root/nativeScene().id);
            unsigned passes=0;
            while(!sourceScreen.fadeOutDone()) {
                if(passes++==32)throw jpc::Error("Source fade wait exceeded its32-pass bound");
                clock();sourceRenderOnlyFrame(graphics);
            }
        }
        clock();screenFadeWaiting=false;return elapsed;
    } catch(...) {clock();screenFadeWaiting=false;throw;}
}
template<class Binder>void bindScreenNatives(Binder& bind) {
    bind("scrFxFadeOut",[this](auto&,auto,const auto&){sourceScreen.fadeOut();return jpc::Value{};});
    bind("scrFxFadeIn",[this](auto&,auto,const auto&){sourceScreen.fadeIn();return jpc::Value{};});
    // EXE417A60 registers scrFxBusy, but neither supplied JPC package imports
    // that method. State::busy remains available to the native host.
    bind("scrFxComplete",[this](auto&,auto,const auto&){sourceScreen.complete();return jpc::Value{};});
    bind("scrFxShake",[this](auto&,auto,const auto& args){sourceScreen.shake(args.at(0).integer());return jpc::Value{};});
    bind("scrFxLetterbox",[this](auto&,auto,const auto& args){sourceScreen.letterbox(args.at(0).integer());return jpc::Value{};});
}
std::vector<uint8_t> screenSnapshot()const {
    if(screenRenderOnlyActive||screenFadeWaiting)throw jpc::Error("Cannot snapshot inside a source screen worker");
    statebytes::Writer out;out.text("EDWSCREEN1");out.blob(sourceScreen.snapshot());out.flag(lastOverlay.fade);
    out.put(lastOverlay.blackAlpha);out.flag(lastOverlay.bars);out.put(lastOverlay.barHeight);out.put(lastProjectionShake);
    out.put(sourceRenderOnlyFrames);out.flag(screenQueuedPortal.has_value());
    if(screenQueuedPortal) {
        const auto& request=*screenQueuedPortal;
        for(auto value:{request.sourceSceneId,request.portalIndex,request.destinationId,request.camera,request.marker})out.put(value);
        for(float value:request.rotationXYZ)out.put(value);out.put<uint32_t>(request.suppressedEmissionAttempts);
    }
    // Presentation revision is a transient wall-clock invalidation counter.
    // A gameplay rollback must not rewind time already spent in a modal wait.
    return std::move(out.bytes);
}
void restoreScreen(const std::vector<uint8_t>& bytes) {
    if(screenRenderOnlyActive||screenFadeWaiting)throw jpc::Error("Cannot restore inside a source screen worker");
    if(bytes.size()>512)throw jpc::Error("Excessive source screen snapshot");
    statebytes::Reader in{bytes};if(in.text()!="EDWSCREEN1")throw jpc::Error("Unsupported World screen state");
    edscreen::State candidate;candidate.restore(in.blob());edscreen::Overlay overlay;
    overlay.fade=in.flag();overlay.blackAlpha=in.get<uint8_t>();overlay.bars=in.flag();overlay.barHeight=in.get<float>();
    const auto shake=in.get<float>();const auto frames=in.get<uint64_t>();std::optional<ScreenPortalRequest> queued;
    if(in.flag()) {
        ScreenPortalRequest request;request.sourceSceneId=in.get<uint32_t>();request.portalIndex=in.get<uint32_t>();
        request.destinationId=in.get<uint32_t>();request.camera=in.get<uint32_t>();request.marker=in.get<uint32_t>();
        for(auto& value:request.rotationXYZ)value=in.get<float>();request.suppressedEmissionAttempts=in.get<uint32_t>();
        auto validId=[](uint32_t id){return !(id&255)&&((id>>24)=='a'||(id>>24)=='b')&&((id>>16)&255)>='a'&&
            ((id>>16)&255)<='z'&&((id>>8)&255)<=99;};
        if(!validId(request.sourceSceneId)||!validId(request.destinationId)||request.portalIndex>=1000||request.camera>255||request.marker>255||
           (request.suppressedEmissionAttempts!=0&&request.suppressedEmissionAttempts!=60))throw jpc::Error("Invalid queued source portal");
        queued=request;
    }
    in.end();if(overlay.barHeight<0||overlay.barHeight>32||std::abs(shake)>819.2f)throw jpc::Error("Invalid World screen sample");
    if(screenPresentationRevision==std::numeric_limits<uint64_t>::max())throw jpc::Error("Screen presentation revision exhausted");
    sourceScreen=std::move(candidate);lastOverlay=overlay;lastProjectionShake=shake;sourceRenderOnlyFrames=frames;screenQueuedPortal=std::move(queued);
    ++screenPresentationRevision;
}
