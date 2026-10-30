// Included inside World; source context reset precedes these constructor hooks.
bool packageClockLive=false;
double packageClockBase=0;
uint64_t packageClockAnchorNS=0;
double packageClockNow() const {
    if(!packageClockLive)return rawClockSeconds;
    const auto now=SDL_GetTicksNS();if(now<packageClockAnchorNS)throw jpc::Error("Package wall clock moved backwards");
    return std::max(rawClockSeconds,packageClockBase+double(now-packageClockAnchorNS)/1e9);
}
void preparePackageArrival(const PackageArrival& arrival) {
    if(!originalRuntime||!actors||!actors->bodies().empty()||arrival.scene!=scene.id||
       edpackage::sceneName(arrival.request.destinationId)!=scene.id||
       ((arrival.request.destinationId>>24)=='b')!=levelBPackage||
       !std::isfinite(arrival.rawSeconds)||arrival.rawSeconds<0||
       arrival.randomIndex>=64||arrival.sourceFloorCache< -1||arrival.sourceFloorCache>=100000||
       arrival.masterVolume<0||arrival.masterVolume>100||(arrival.stereo!=0&&arrival.stereo!=1)||arrival.turnAmount<0||arrival.turnAmount>12)
        throw jpc::Error("Invalid original package constructor state");
    for(uint32_t value:arrival.randomTable)if(value>=0x07ffffffu)throw jpc::Error("Invalid original cached random table");
    const uint64_t now=arrival.liveClock?SDL_GetTicksNS():0;
    if(arrival.liveClock&&now<arrival.wallStartNS)throw jpc::Error("Invalid package wall clock anchor");
    // 420f37 rebases whole seconds after native registrations, BEFORE Main/JPC
    // startup40c9f0. Movies during that startup then retain their fractional time.
    rawClockSeconds=std::floor(arrival.rawSeconds+(arrival.liveClock?double(now-arrival.wallStartNS)/1e9:0));
    packageClockLive=arrival.liveClock;packageClockBase=rawClockSeconds;packageClockAnchorNS=now;
    // 40C93F->418D8C resets the seed; the cached table/index have no reset writer.
    nativeProperties.randomSeed=12345;nativeProperties.randomIndex=arrival.randomIndex;
    nativeProperties.randomReady=arrival.randomReady;nativeProperties.randomTable=arrival.randomTable;
    nativeProperties.turnAmount=arrival.turnAmount;
    sourceGui.masterVolume=arrival.masterVolume;sourceGui.stereo=arrival.stereo;
    audioBank.setMasterVolume(sourceGui.masterVolume);
    sourceScreen.restore(arrival.screen);lastProjectionShake=sourceScreen.lastShakeOffset();lastOverlay={};
    vfx->restoreGlobals(arrival.vfxGlobals);
    sourceFloorCache=arrival.sourceFloorCache;
}
void restorePackagePlayer(const PackageArrival& arrival) {
    if(!edpackage::restorePlayer(arrival.request.sourceSceneId,arrival.request.destinationId))return;
    auto buffer=arrival.player;game.restorePlayerTransfer(buffer);actors->sync();
    game.inventory=game.vm.field(game.ash,99).reference();
}
void enterPackageArrival(const PackageArrival& arrival) {
    const auto& request=arrival.request;
    if(request.camera>=scene.cameras.size()||request.marker< -1||
       (!scene.menuContext&&request.marker>=0&&size_t(request.marker)>=scene.scripting.markers.size()))
        throw jpc::Error("Original package arrival camera/marker is missing");
    for(float angle:request.rotationXYZ)if(!std::isfinite(angle))throw jpc::Error("Non-finite original package Euler");
    if(request.marker!=-1&&(request.rotationXYZ[0]!=0||request.rotationXYZ[2]!=0))
        throw jpc::Error("Original package pitch/roll needs native actor Euler support");
    // No requested-pose restoration here: marker-1 keeps the actual result of
    // restorePlayer followed by the original target scene activation.
    enterSourceScene(scene,actor,gates,false,false);
    if(request.marker!=-1) {
        auto& ash=actors->at({0,0});
        // Death restarts request menu marker0; native4035EA ignores it when the menu has no markers.
        if(size_t(request.marker)<scene.scripting.markers.size()) {
            const auto marker=scene.scripting.markers[size_t(request.marker)];positionProgressBody(ash,{marker.x,0,marker.z});
        }
        ash.pose.yaw=edactormath::normalizeAngle(request.rotationXYZ[1]);
        publishProgressBody(ash);
    }
    publishEpisodeCamera(request.camera);readNativePlayer(actor,scene,false);
    const Vec2 point{actor.position[0],actor.position[2]};
    actor.clearanceRadius=clearGround(scene.ground,point,0)?initialActorRadius(scene,point):0;
}
