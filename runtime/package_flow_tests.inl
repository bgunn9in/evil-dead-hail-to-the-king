#pragma once
// Source request/position fixtures test a real package transaction, not the AU04 fight.
void testPackageFlow(const fs::path& root,const fs::path& levelBRoot) {
    std::cout<<std::unitbuf;checked(SDL_Init(SDL_INIT_GAMEPAD),"Initialize original package handoff proof");
    struct MovieTime {double before=0,after=0;};std::vector<MovieTime> movies;
    World current(root,nullptr,[&](World& active,uint32_t identity,int flags){const double before=active.rawClockSeconds;
        presentMovieHeadless(active,identity,flags);if(active.levelBPackage)movies.push_back({before,active.rawClockSeconds});},true);
    auto require=[&](bool value,const std::string& text){if(!value)throw std::runtime_error("Original package handoff: "+text);};
    require(!current.levelBPackage,"source assets are not LevelA");
    std::cout<<"Package fixture source entry: "<<current.scene.id<<" -> au04\n";
    if(current.scene.id!="au04") {
        current.endEpisodeScene();Scene target=loadScene(root/"au04");Actor placed=resetActor(target,0);
        std::vector<GateState> gates(target.doors.size());current.enterSourceScene(target,placed,gates);
        current.scene=std::move(target);current.actor=std::move(placed);current.gates=std::move(gates);
    }
    auto& vm=current.game.vm;const auto& package=current.game.package;
    auto external=[&](const char* name,const char* signature,std::vector<jpc::Value> args={}){
        return vm.invoke(package.methodIndex("ed/base/External",name,signature),0,std::move(args));};
    auto I=[](int n){return jpc::Value::integer(n);};
    require(vm.invoke(package.methodIndex("Ash","pickupItem","(II)Z"),current.game.ash,{I(29),I(1)}).integer()!=0,"source inventory fixture rejected its real item");
    auto quantity=[](const World& world,int id){int result=0;for(const auto& item:world.game.items())if(item.itemID==id)result+=item.quantity;return result;};
    const int held=quantity(current,29);require(held>0,"source inventory does not contain its transfer fixture");
    external("sndMgrSetMasterVolume","(I)V",{I(37)});external("sndMgrSetStereo","(I)V",{I(0)});
    current.nativeProperties.turnAmount=4; // Explicit process-global fixture, independent of player stock/AI.
    external("randomRange","(II)I",{I(0),I(100)});external("random","(I)I",{I(7)});
    external("vfxSpawnMarkerOffset","(IIIIIII)V",{I(9),I(0),I(0),I(0),I(0),I(0),I(0)});
    external("scrFxShake","(I)V",{I(31)});external("scrFxFadeOut","()V");
    external("sceneSwitch","(IIII)V",{I(int(episode_portal_detail::sceneId("bm01"))),I(10),I(0),I(90)});
    require(current.pendingSceneSwitch&&current.pendingSceneSwitch->sourceSceneId==episode_portal_detail::sceneId("au04")&&
        current.pendingSceneSwitch->destinationId==episode_portal_detail::sceneId("bm01")&&current.pendingSceneSwitch->camera==10&&
        current.pendingSceneSwitch->marker==0&&current.pendingSceneSwitch->rotationXYZ==std::array<float,3>{{0,float(double(90)*.01745329238474369f),0}},
        "actual native sceneSwitch did not queue source IDs/camera/marker/MODEL radians");
    external("sceneSwitch","(IIII)V",{I(int(episode_portal_detail::sceneId("bm01"))),I(10),I(0),I(0)});
    PackageRequest request=*current.pendingSceneSwitch;current.pendingSceneSwitch.reset();
    current.advanceIdleTime(.371);const double beforeClock=current.rawClockSeconds;
    const auto before=current.saveStateBytes();const auto beforeScreen=current.sourceScreen.snapshot();const auto table=current.nativeProperties.randomTable;
    const auto vfxGlobals=current.vfx->globals();
    // Original SceneAU04.processFrame2116 PCs203/213: BM01,camera10,marker0,MODEL Euler0.
    std::cout<<"Package fixture entering BM01 at source clock "<<beforeClock<<"\n";
    auto next=stagePackageWorld(current,request,levelBRoot);
    std::cout<<"Package fixture marker0 stage constructed\n";
    require(current.saveStateBytes()==before,"successful staging changed the old World");
    require(next->levelBPackage&&next->scene.id=="bm01"&&next->game.vm.objectClass(next->game.level)==next->game.package.classIndex("LevelB"),"target did not construct its original LevelB context");
    require(quantity(*next,29)==held,"original player inventory did not survive Main.savePlayer/restorePlayer");
    const auto marker=next->scene.scripting.markers.at(0);const auto& ash=next->actors->at({0,0});
    require(ash.pose.x==marker.x&&ash.pose.z==marker.z&&ash.localY==0&&ash.pose.yaw==0&&next->actor.camera==10,"queued marker/MODEL Euler/camera was not applied after target creation");
    require(!movies.empty()&&movies.front().before==std::floor(beforeClock)&&next->rawClockSeconds==movies.back().after,
        "package clock did not rebase before source startup or discarded subsequent movie time");
    require(next->sourceGui.masterVolume==37&&next->sourceGui.stereo==0,"native audio settings did not precede source Interface initialization");
    require(next->nativeProperties.randomTable==table,"native cached random table was reset across the package boundary");
    require(next->nativeProperties.turnAmount==4,"native cached turn amount was reset across the package boundary");
    const auto imported=next->vfx->globals();
    require(std::tie(imported.frameTimers,imported.childTimers,imported.emitterModes,imported.suppressedEmissions,imported.nextSerial)==
        std::tie(vfxGlobals.frameTimers,vfxGlobals.childTimers,vfxGlobals.emitterModes,vfxGlobals.suppressedEmissions,vfxGlobals.nextSerial),
        "verified process-global emitter state did not survive before target activation");
    for(const auto& [kind,particles]:next->vfx->particles()){(void)kind;require(particles.empty(),"old scene particles migrated with emitter globals");}
    uint32_t seed=12345;unsigned draws=0;while(seed!=next->nativeProperties.randomSeed&&draws<4096){seed=seed*0x41c64e6du+0x3039u;++draws;}
    std::cout<<"Package source constructor single-step draws: "<<draws<<" seed="<<next->nativeProperties.randomSeed<<"\n";
    require(draws==1,"this source BM01 setup did not consume its one bounded draw after LCG reset");
    require(next->sourceScreen.fadeOutRequested()&&next->sourceScreen.fadeOutProgress()==current.sourceScreen.fadeOutProgress(),"native fade globals were lost at context reset");
    // Native Main.restorePlayer restores fixed-point LOCAL pose and passes saved
    // MODEL degrees through original actorFace. Marker-1 must leave that source result alone.
    std::array<int,4> savedPose{};unsigned axis=0;
    for(const char* name:{"actorGetPositionX","actorGetPositionY","actorGetPositionZ","actorGetRotY"})
        savedPose[axis++]=external(name,"(II)I",{I(0),I(0)}).integer();
    request.marker=-1;request.rotationXYZ[1]=1.234f;
    auto retained=stagePackageWorld(current,request,levelBRoot);const auto& preserved=retained->actors->at({0,0});
    const float restoredYaw=edactormath::modelYawFromHeading(float(double(savedPose[3])*.01745329238474369f));
    require(preserved.pose.x==float(savedPose[0])*.01f&&preserved.localY==float(savedPose[1])*.01f&&
        preserved.pose.z==float(savedPose[2])*.01f&&preserved.pose.yaw==restoredYaw,"marker-1 applied an arrival pose/rotation over the original restored result");
    require(current.saveStateBytes()==before&&current.sourceScreen.snapshot()==beforeScreen,"marker-1 staging mutated the source context");
    auto rejects=[&](PackageRequest bad,const fs::path& directory){bool rejected=false;try{stagePackageWorld(current,bad,directory);}catch(const std::exception&){rejected=true;}
        require(rejected&&current.saveStateBytes()==before,"failed package stage damaged the old scene/player/VM/native state");};
    auto bad=request;bad.camera=255;rejects(bad,levelBRoot);rejects(request,levelBRoot/"missing-package");
    bad=request;bad.sourceSceneId=episode_portal_detail::sceneId("ak01");rejects(bad,levelBRoot);
    // Full saves select a fresh matching package independently of the live one.
    const auto aState=current.saveStateBytes(),bState=next->saveStateBytes();
    auto bLoaded=stageSavedWorld(current,bState);
    require(bLoaded->levelBPackage&&bLoaded->saveStateBytes()==bState&&current.saveStateBytes()==aState,"B save staged from A changed its bytes or old context");
    auto aLoaded=stageSavedWorld(*next,aState);
    require(!aLoaded->levelBPackage&&aLoaded->saveStateBytes()==aState&&next->saveStateBytes()==bState,"A save staged from B changed its bytes or old context");
    auto corrupt=bState;corrupt.pop_back();bool corruptRejected=false;
    try{stageSavedWorld(current,corrupt);}catch(const std::exception&){corruptRejected=true;}
    require(corruptRejected&&current.saveStateBytes()==aState,"corrupt cross-package save changed the live A World");
    require(edpackage::restorePlayer(0x61750100,0x626d0100)&&edpackage::restorePlayer(0x62620100,0x616b0100)&&
        !edpackage::restorePlayer(0x626d0100,0x616b0100)&&!edpackage::restorePlayer(0x61750100,0x62610100)&&
        edpackage::restart(0x61610100)&&edpackage::restart(0x62620100),"native exact restore/restart predicates differ");
    std::cout<<"Original AU04->BM01 World package stage: fresh B VM, source player transfer, post-create marker/Euler/camera,marker-1,global lifetimes/clock,cross-module saves and failure atomicity passed\n";
    SDL_Quit();
}

void testPackageRestartClock(const fs::path& sourceRoot,const fs::path& menuRoot,const std::string& destination) {
    checked(SDL_Init(SDL_INIT_GAMEPAD),"Initialize source package restart clock proof");
    World current(sourceRoot,nullptr,presentMovieHeadless,true);current.advanceIdleTime(123.371);
    const auto before=current.saveStateBytes();const PackageRequest request{episode_portal_detail::sceneId(current.scene.id),
        episode_portal_detail::sceneId(destination),0,-1,{{0,0,0}}};
    if(!edpackage::restart(request.destinationId))throw std::runtime_error("Restart clock fixture must target AA01/BB01");
    auto next=stagePackageWorld(current,request,menuRoot);
    if(!next->scene.menuContext||next->rawClockSeconds!=0||current.saveStateBytes()!=before)
        throw std::runtime_error("Original menu reload did not reset time before fresh JPC creation or mutated the old World");
    std::cout<<"Original restart "<<destination<<": clock123.371+ ->0 before fresh Main/menu, old World unchanged\n";
    SDL_Quit();
}
