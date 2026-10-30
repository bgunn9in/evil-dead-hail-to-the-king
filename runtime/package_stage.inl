// Included after World. Construction is independent; callers commit only once
// their target GPU resources are ready and retain the old World on any failure.
std::unique_ptr<World> stagePackageWorld(World& current,const PackageRequest& request,const fs::path& explicitRoot={},bool liveClock=false) {
    const uint64_t started=liveClock?SDL_GetTicksNS():0;
    const auto source=edpackage::sceneName(request.sourceSceneId),destination=edpackage::sceneName(request.destinationId);
    if(source!=current.scene.id||!current.originalRuntime||!edpackage::reload(request.sourceSceneId,request.destinationId)||
       request.camera>255||request.marker< -1||request.marker>255)
        throw jpc::Error("Invalid original package handoff request");
    fs::path target=explicitRoot;
    if(target.empty()) {
        if((request.sourceSceneId>>24)==(request.destinationId>>24))target=current.root;
        else if((request.destinationId>>24)=='b')target=current.root/"levelB";
        else target=current.root.parent_path();
    }
    if(!fs::is_regular_file(target/"world.txt")||!fs::is_regular_file(target/"scripts"/((request.destinationId>>24)=='a'?"levelA.jpc":"levelB.jpc")))
        throw jpc::Error("Original destination package has not been exported");
    PackageArrival arrival;arrival.request=request;arrival.scene=destination;
    arrival.rawSeconds=edpackage::restart(request.destinationId)?0:current.rawClockSeconds;
    arrival.liveClock=liveClock;arrival.wallStartNS=started;
    arrival.randomIndex=current.nativeProperties.randomIndex;arrival.randomReady=current.nativeProperties.randomReady;
    arrival.randomTable=current.nativeProperties.randomTable;arrival.screen=current.sourceScreen.snapshot();
    arrival.sourceFloorCache=current.sourceFloorCache;arrival.masterVolume=current.sourceGui.masterVolume;arrival.stereo=current.sourceGui.stereo;
    arrival.turnAmount=current.nativeProperties.turnAmount;
    arrival.vfxGlobals=current.vfx->globals();
    const auto before=current.game.checkpoint();
    try {
        if(!edpackage::restart(request.destinationId))current.game.savePlayerTransfer(arrival.player);
        current.game.rollback(before);
    } catch(...) {current.game.rollback(before);throw;}
    edrumble::Host stagedFeedback{current.rumbleHost.available,{}};
    auto staged=std::make_unique<World>(target,nullptr,current.moviePresenter,true,&arrival,false,std::move(stagedFeedback));
    staged->setUiLanguage(current.uiLanguage());
    if(!current.sourceCartDirectory.empty())staged->configureSourceCart(current.sourceCartDirectory);
    staged->rawClockSeconds=staged->packageClockNow();staged->packageClockLive=false;
    staged->rumbleHost=current.rumbleHost;
    return staged;
}
