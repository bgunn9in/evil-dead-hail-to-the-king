// Included inside World; the file envelope and rename live in save_file.h.
std::vector<uint8_t> saveStateBytes(int completingCartSave=-1) const {
    if(completingCartSave< -1||completingCartSave>7)throw std::runtime_error("Invalid Cart save completion slot");
    if(preparingScene||pendingSceneSwitch||quitRequested||(sourceCart&&sourceCart->pending()&&completingCartSave<0)||std::any_of(nativeProperties.movies.begin(),nativeProperties.movies.end(),
       [](const auto& movie){return !movie.played;}))throw std::runtime_error("Wait for the current cutscene before saving");
    statebytes::Writer out;
    out.text("EDPLAY1");out.put<uint32_t>(SaveVersion);out.text(scene.id);out.flag(scene.episode);
    for(float value:actor.position)out.put(value);
    out.put(actor.yaw);out.put(actor.floorOffset);out.put(actor.clearanceRadius);out.put(actor.camera);out.put(actor.floorTriangle);
    out.put(actor.animationTime);out.flag(actor.walking);out.flags(actor.membership);
    out.put(sourceTick);out.put(tickFraction);out.put(rawClockSeconds);out.put(guard);out.put<int32_t>(pending);
    out.put<uint32_t>(uint32_t(gates.size()));
    for(const auto& gate:gates){out.flag(gate.enabled);out.flag(gate.pending);out.put(gate.elapsed);}
    out.flags(scene.enabledVolumes);out.flags(scene.enabledPortals);
    out.put(audio.remaining);
    const auto sound=audioBank.snapshot();out.put<int32_t>(sound.nextVoice);out.put<uint32_t>(uint32_t(sound.voices.size()));
    for(const auto& voice:sound.voices) {
        out.put<int32_t>(voice.token);out.put<int32_t>(voice.clip);out.put<int32_t>(voice.volume);
        out.put(voice.id);out.text(voice.bucket);out.flag(voice.loop);out.put(voice.elapsed);out.put<int32_t>(voice.frequencyHz);out.flag(voice.pending);out.put<uint8_t>(uint8_t(voice.slot));
    }
    const auto& properties=nativeProperties;
    out.put<uint32_t>(uint32_t(properties.objects.size()));
    for(const auto& [index,value]:properties.objects) {
        out.put<int32_t>(index);for(int32_t field:value.setup)out.put(field);out.put(value.flags);
        out.put<int32_t>(value.marker);out.put<int32_t>(value.markerMode);
        out.put(value.position.x);out.put(value.position.y);out.put(value.position.z);
        for(int32_t field:value.orientation)out.put(field);out.put(value.radius);out.put(value.floorTriangle);out.put(value.cachedFloorHeight);out.blob(value.fade.snapshot());
    }
    out.put<uint32_t>(uint32_t(properties.lights.size()));
    for(const auto& [index,value]:properties.lights) {
        out.put<int32_t>(index);for(int32_t field:value.color)out.put(field);out.flag(value.enabled);
        out.put<uint32_t>(uint32_t(value.settings.size()));
        for(const auto& [key,fields]:value.settings) {
            out.text(key);out.put<uint32_t>(uint32_t(fields.size()));for(int32_t field:fields)out.put(field);
        }
    }
    out.put<uint32_t>(uint32_t(properties.voices.size()));
    for(const auto& [index,value]:properties.voices) {
        out.put<int32_t>(index);out.put<int32_t>(value.marker);out.put<int32_t>(value.radius);out.put(value.flags);
        out.flag(value.positionKnown);if(value.positionKnown)for(float coordinate:value.position)out.put(coordinate);
    }
    for(float coordinate:properties.soundListener)out.put(coordinate);
    out.put<uint32_t>(uint32_t(properties.movies.size()));
    for(const auto& value:properties.movies) {out.text(value.scene);out.put(value.resource);out.put<int32_t>(value.flags);out.flag(value.played);}
    out.put<uint32_t>(uint32_t(properties.effects.size()));
    for(const auto& [key,values]:properties.effects) {
        out.text(key);out.put<uint32_t>(uint32_t(values.size()));for(int32_t value:values)out.put(value);
    }
    out.put(properties.randomSeed);out.put(properties.randomIndex);out.flag(properties.randomReady);
    for(uint32_t value:properties.randomTable)out.put(value);
    if(properties.turnAmount<0||properties.turnAmount>12)throw std::runtime_error("Invalid native turn amount");
    out.put(properties.turnAmount);
    out.blob(game.vm.snapshot());
    out.flag(bool(actors));if(actors){out.blob(actors->snapshot());out.blob(progressSnapshot());out.blob(routes->snapshot());out.blob(vfx->snapshot());
        out.put(sourceFloorCache);for(float value:sourceFloorGrid)out.put(value);
        const auto stream=streams->snapshot();out.put(stream.id);out.flag(stream.loop);out.put(stream.elapsed);out.flag(stream.playing);
        out.put<uint32_t>(uint32_t(properties.actorFades.size()));for(const auto& [key,fade]:properties.actorFades){out.put(key.type);out.put(key.index);out.blob(fade.snapshot());}
        out.blob(cameraTimeline->snapshot());}
    out.blob(screenSnapshot());
    if(game.mode<0||game.mode>6||game.guiFlags.size()>250)throw std::runtime_error("Invalid interface state for saving");
    out.put<int32_t>(game.mode);out.put(game.messageAsset);out.put<uint32_t>(uint32_t(game.guiFlags.size()));
    for(const auto& [key,value]:game.guiFlags){out.put<int32_t>(key);out.put<int32_t>(value);}
    out.flag(inventoryView.open);out.put(inventoryView.selected);
    int savedCartCompletion=completingCartSave;
    if(savedCartCompletion<0&&sourceCart&&!sourceCart->pending()) {
        const auto state=sourceCart->checkpoint();
        if(state.completed&&state.completed->success&&state.completed->request.operation==edcart::Operation::Save)
            savedCartCompletion=int(state.completed->request.slot);
    }
    // This tag becomes true only in a successfully committed file; live Cart.done stays false until the write finishes.
    out.put<int32_t>(savedCartCompletion);
    out.flag(inventoryView.reading);
    out.put<int32_t>(inventoryView.combineSlot);out.put<int32_t>(inventoryView.combineItemID);
    out.put<uint64_t>(actors?sourceTexts().textRevision():0);
    if(out.bytes.size()>savefile::DefaultLimit)throw std::runtime_error("Saved world exceeds the file limit");
    return std::move(out.bytes);
}
void restoreStateBytes(const std::vector<uint8_t>& bytes,Graphics* graphics=nullptr) {
    if(preparingScene)throw std::runtime_error("Cannot load during scene preparation");
    statebytes::Reader in{bytes};
    if(in.text()!="EDPLAY1")throw std::runtime_error("Unsupported saved world header");
    const auto version=in.get<uint32_t>();if(version<1||version>SaveVersion)throw std::runtime_error("Unsupported saved world version");
    const std::string identity=in.text();
    if(!validSceneId(identity)||!containsScene(identity))
        throw std::runtime_error("Saved area is not present in this world");
    Scene next=loadScene(root/identity);
    if(next.id!=identity||in.flag()!=next.episode)throw std::runtime_error("Saved scene profile does not match installed assets");
    Actor placed;
    for(float& value:placed.position){value=in.get<float>();if(std::abs(value)>1000000)throw std::runtime_error("Saved actor position is out of range");}
    placed.yaw=in.get<float>();placed.floorOffset=in.get<float>();placed.clearanceRadius=version>=3?in.get<float>():next.radius;
    placed.camera=in.get<unsigned>();placed.floorTriangle=in.get<unsigned>();
    placed.animationTime=in.get<double>();placed.walking=in.flag();placed.membership=in.flags(uint32_t(next.triggers.size()));
    if(placed.camera>=next.cameras.size()||placed.clearanceRadius<0||placed.clearanceRadius>next.radius||std::abs(placed.yaw)>1000000||std::abs(placed.floorOffset)>1000000||
       placed.animationTime<0||placed.animationTime>1e12||(placed.floorTriangle!=std::numeric_limits<unsigned>::max()&&placed.floorTriangle>=next.floor.size()))
        throw std::runtime_error("Saved actor pose is invalid");
    const auto nextTick=in.get<uint64_t>();const double fraction=in.get<double>();const double nextClock=version>=3?in.get<double>():double(nextTick)/30;
    const auto nextGuard=in.get<unsigned>();const int nextPending=in.get<int32_t>();
    if(fraction<0||fraction>=1||nextClock<0||nextClock>1e12||nextGuard>1000000)throw std::runtime_error("Invalid saved source timer");
    if(in.count(uint32_t(next.doors.size()))!=next.doors.size())throw std::runtime_error("Saved passage count does not match scene");
    std::vector<GateState> states(next.doors.size());unsigned opening=0;
    for(size_t i=0;i<states.size();++i) {
        auto& gate=states[i];gate.enabled=in.flag();gate.pending=in.flag();gate.elapsed=in.get<unsigned>();opening+=gate.pending;
        if(gate.pending&&int(i)!=nextPending)throw std::runtime_error("Saved passage timer disagrees with pending passage");
    }
    if(nextPending<-1||nextPending>=int(states.size())||opening!=(nextPending>=0?1u:0u))throw std::runtime_error("Invalid saved pending passage");
    const auto volumes=in.flags(uint32_t(next.enabledVolumes.size()));
    for(unsigned i=0;i<volumes.size();++i)setVolumeEnabled(next,i,volumes[i]);
    next.enabledPortals=in.flags(uint32_t(next.enabledPortals.size()));
    if(next.episode)for(size_t i=0;i<states.size();++i)
        if(states[i].enabled!=next.enabledPortals[next.doors[i].portalIndex])throw std::runtime_error("Saved portal state disagrees with native scene");
    const double remaining=in.get<double>();
    if(remaining<0||remaining>audio.duration)throw std::runtime_error("Invalid saved door sound position");
    AudioBank::State sounds;sounds.nextVoice=in.get<int32_t>();
    for(uint32_t i=0,n=in.count(AudioBank::VoiceLimit);i<n;++i) {
        AudioBank::VoiceState voice;voice.token=in.get<int32_t>();voice.clip=in.get<int32_t>();voice.volume=in.get<int32_t>();
        voice.id=in.get<uint32_t>();voice.bucket=in.text();voice.loop=in.flag();voice.elapsed=in.get<double>();
        if(version>=11)voice.frequencyHz=in.get<int32_t>();if(version>=17)voice.pending=in.flag();
        // Legacy saves retain record order but contain no physical-slot membership.
        voice.slot=version>=18?int(in.get<uint8_t>()):int(i);sounds.voices.push_back(std::move(voice));
    }
    NativeProperties properties;
    for(uint32_t i=0,n=in.count(32);i<n;++i) {
        const int index=in.get<int32_t>();if(index<0||index>=32)throw std::runtime_error("Invalid saved native object slot");
        ObjectProperty value;for(auto& field:value.setup)field=in.get<int32_t>();value.flags=in.get<int16_t>();
        value.marker=in.get<int32_t>();value.markerMode=in.get<int32_t>();
        value.position={in.get<float>(),in.get<float>(),in.get<float>()};
        for(auto& field:value.orientation)field=in.get<int32_t>();value.radius=in.get<float>();
        if(version>=7) {
            value.floorTriangle=in.get<int32_t>();value.cachedFloorHeight=in.get<float>();
            if(value.floorTriangle< -1||value.floorTriangle>=100000||std::abs(value.cachedFloorHeight)>1000000)
                throw std::runtime_error("Invalid saved object floor cache");
        }
        if(version>=9)value.fade.restore(in.blob());
        if(value.marker<-1||value.marker>=int(next.scripting.markers.size())||value.radius<0||value.radius>1000000||
           !properties.objects.emplace(index,value).second)throw std::runtime_error("Invalid saved native object");
    }
    for(uint32_t i=0,n=in.count(256);i<n;++i) {
        const int index=in.get<int32_t>();if(index<0||index>=256)throw std::runtime_error("Invalid saved native light slot");
        LightProperty value;for(auto& field:value.color)field=in.get<int32_t>();value.enabled=in.flag();
        for(uint32_t j=0,count=in.count(16);j<count;++j) {
            const std::string key=in.text();std::vector<int32_t> fields;
            for(uint32_t k=0,size=in.count(16);k<size;++k)fields.push_back(in.get<int32_t>());
            if(!value.settings.emplace(key,std::move(fields)).second)throw std::runtime_error("Duplicate saved light property");
        }
        if(!properties.lights.emplace(index,std::move(value)).second)throw std::runtime_error("Duplicate saved native light");
    }
    for(uint32_t i=0,n=in.count(4096);i<n;++i) {
        const int index=in.get<int32_t>();VoiceProperty value;value.marker=in.get<int32_t>();value.radius=in.get<int32_t>();value.flags=in.get<uint32_t>();
        if(version>=17){value.positionKnown=in.flag();if(value.positionKnown)for(float& coordinate:value.position){
            coordinate=in.get<float>();if(std::abs(coordinate)>1000000)throw std::runtime_error("Saved sound position is out of range");}}
        if(index<0||value.marker<-1||(version<17?value.marker>=int(next.scripting.markers.size()):value.marker>65535)||
           (version>=17&&value.positionKnown&&(value.flags&12u)==12u&&value.radius<=0)||
           !properties.voices.emplace(index,value).second)throw std::runtime_error("Invalid saved native voice");
    }
    if(version>=17)for(float& coordinate:properties.soundListener){coordinate=in.get<float>();
        if(std::abs(coordinate)>1000000)throw std::runtime_error("Saved sound listener is out of range");}
    for(uint32_t i=0,n=in.count(4096);i<n;++i) {
        MovieRequest value;value.scene=in.text();value.resource=in.get<uint32_t>();value.flags=in.get<int32_t>();value.played=in.flag();
        if(!validSceneId(value.scene)||!value.played)throw std::runtime_error("Invalid saved cutscene state");properties.movies.push_back(std::move(value));
    }
    for(uint32_t i=0,n=in.count(4096);i<n;++i) {
        const std::string key=in.text();std::vector<int32_t> values;
        for(uint32_t j=0,size=in.count(16);j<size;++j)values.push_back(in.get<int32_t>());
        properties.effects.emplace_back(key,std::move(values));
    }
    properties.randomSeed=in.get<uint32_t>();properties.randomIndex=in.get<uint32_t>();properties.randomReady=in.flag();
    if(properties.randomIndex>=64)throw std::runtime_error("Invalid saved source random position");
    for(auto& value:properties.randomTable)value=in.get<uint32_t>();
    if(version>=20)properties.turnAmount=in.get<int32_t>();
    if(properties.turnAmount<0||properties.turnAmount>12)throw std::runtime_error("Invalid saved native turn amount");
    const auto script=in.blob();const bool hasActors=version>=2?in.flag():false;
    const auto actorState=hasActors?in.blob():std::vector<uint8_t>{};
    const auto progressState=hasActors&&version>=3?in.blob():std::vector<uint8_t>{};
    const auto routeState=hasActors&&version>=4?in.blob():std::vector<uint8_t>{};
    const auto vfxState=hasActors&&version>=5?in.blob():std::vector<uint8_t>{};
    int32_t floorCache=-1;SourceFloorQuery::CoarseGrid floorGrid{};
    StreamAudio::State streamState;
    std::vector<uint8_t> cameraState;
    if(hasActors) {
        if(version<6)throw std::runtime_error("Saved native floor state is missing");
        floorCache=in.get<int32_t>();if(floorCache< -1||floorCache>=100000)throw std::runtime_error("Invalid saved native floor cache");
        for(float& value:floorGrid)value=in.get<float>();
        if(version>=8){streamState.id=in.get<uint32_t>();streamState.loop=in.flag();streamState.elapsed=in.get<double>();streamState.playing=in.flag();}
        if(version>=9)for(unsigned i=0,n=in.count(edactors::MaxBodies);i<n;++i) {
            const edactors::Key key{in.get<int>(),in.get<int>()};edeffects::State fade;fade.restore(in.blob());
            if(key.index<0||key.index>=edactors::actorCapacity(key.type)||!properties.actorFades.emplace(key,std::move(fade)).second)
                throw std::runtime_error("Invalid saved actor effect slot");
        }
        if(version<10)throw std::runtime_error("Saved animated camera state is missing");
        cameraState=in.blob();
    }
    const auto screenState=version>=12?in.blob():std::vector<uint8_t>{};
    int hostMode=-1;uint32_t messageAsset=0;std::map<int,int> guiFlags;bool inventoryOpen=false;unsigned selectedItem=0;
    if(version>=13) {
        hostMode=in.get<int32_t>();messageAsset=in.get<uint32_t>();
        if(hostMode<0||hostMode>6)throw std::runtime_error("Invalid saved interface mode");
        for(unsigned i=0,n=in.count(250);i<n;++i){const auto key=in.get<int32_t>(),value=in.get<int32_t>();
            if(!guiFlags.emplace(key,value).second)throw std::runtime_error("Duplicate saved GUI handle");}
        inventoryOpen=in.flag();selectedItem=in.get<unsigned>();
        if(inventoryOpen!=(hostMode==2))throw std::runtime_error("Saved inventory visibility disagrees with interface");
    }
    const int completingCartSave=version>=14?in.get<int32_t>():-1;
    if(completingCartSave< -1||completingCartSave>7)throw std::runtime_error("Invalid saved Cart completion slot");
    const bool reading=version>=15?in.flag():false;
    const int combineSlot=version>=16?in.get<int32_t>():-1,combineItemID=version>=16?in.get<int32_t>():-1;
    if(combineSlot< -1||combineSlot>255||combineItemID< -1||combineItemID>65535||(combineSlot==-1)!=(combineItemID==-1))throw jpc::Error("Invalid saved Combine selection");
    const auto savedTextRevision=version>=19?in.get<uint64_t>():0;
    if(!hasActors&&savedTextRevision)throw jpc::Error("Saved text revision has no original interface");
    in.end();
    if(hasActors!=bool(actors))throw std::runtime_error("Saved native actors do not match the running world profile");
    const Vec2 position{placed.position[0],placed.position[2]};
    auto validationTriangle=placed.floorTriangle;
    const auto height=floorHeight(next.floor,position,&validationTriangle);
    if(!next.menuContext&&!actors&&(!height||!clearGround(next.ground,position,placed.clearanceRadius)||
       (!actors&&std::abs(placed.position[1]-(*height+placed.floorOffset))>.001f)))
        throw std::runtime_error("Saved actor is outside the walkable scene");
    auto prepared=graphics?graphics->prepare(next,root/identity):nullptr;
    const auto previous=game.checkpoint();
    const auto previousNative=checkpointNative();const bool soundMuted=audioBank.muted();
    const double previousClock=rawClockSeconds;
    Scene* previousScriptScene=scriptScene;Actor* previousScriptActor=scriptActor;auto* previousScriptGates=scriptGates;
    scriptScene=&next;scriptActor=&placed;scriptGates=&states;
    const auto resetContext=[&]{scriptScene=previousScriptScene;scriptActor=previousScriptActor;scriptGates=previousScriptGates;};
    beginNativeStage();
    InventoryView view;
    try {
        game.restore(script);
        if(version>=12)restoreScreen(screenState);
        else {sourceScreen=edscreen::State{};lastOverlay={};lastProjectionShake=0;screenQueuedPortal.reset();sourceRenderOnlyFrames=0;}
        nativeProperties=properties;
        if(actors){visibilityReady=true;visibilityCamera=placed.camera;sourceFloorCache=floorCache;sourceFloorGrid=floorGrid;}
        if(actors) {
            cameraTimeline=std::make_unique<edcamera::Timeline>(next.cameraTracks,episode_portal_detail::sceneId(next.id));
            cameraTimeline->restore(cameraState);
            if(!cameraTimeline->state||unsigned(cameraTimeline->state->request.track)!=placed.camera)
                throw std::runtime_error("Saved camera controller disagrees with the player camera");
            applyCameraFrame(next);
        }
        std::string owner="Scene";for(char c:identity)owner.push_back(c>='a'&&c<='z'?char(c-'a'+'A'):c);
        const uint32_t expected=next.episode?episode_portal_detail::sceneId(identity):
            (uint32_t(identity[0])<<24)|(uint32_t(identity[1])<<16)|(uint32_t(identity[2])<<8)|uint32_t(identity[3]);
        if(game.vm.objectClass(game.scene)!=game.package.classIndex(owner)||uint32_t(game.vm.staticField(309).integer())!=expected||
           unsigned(game.vm.staticField(395).integer())!=nextGuard)throw std::runtime_error("Saved script state disagrees with world state");
        view.health=game.vm.field(game.ash,13).integer();view.maxHealth=game.vm.field(game.ash,95).integer();
        for(int container:{99,98,100})if(container!=100||hasActors)
            for(const auto& item:game.items(container))view.items.push_back({item.itemID,item.quantity,item.slot,container});
        if(version>=13&&((view.items.empty()&&selectedItem)||(selectedItem>=view.items.size()&&!view.items.empty())))
            throw std::runtime_error("Saved inventory selection is outside its items");
        view.selected=selectedItem;view.open=version>=13?inventoryOpen:game.vm.staticField(844).integer()==2;
        if(reading&&(!view.open||view.items.empty()||view.items[view.selected].containerField!=100))throw std::runtime_error("Saved reader has no selected document");
        view.reading=reading;
        if(combineSlot>=0&&(!view.open||reading||std::none_of(view.items.begin(),view.items.end(),[&](const auto& item){
            return item.containerField==99&&item.slot==combineSlot&&item.itemID==combineItemID;})))throw jpc::Error("Saved Combine selection has no original item");
        view.combineSlot=combineSlot;view.combineItemID=combineItemID;
        if(actors) {
            actors->restore(actorState);const auto& body=actors->at({0,0});
            for(const auto& [key,fade]:nativeProperties.actorFades){(void)fade;actors->at(key);}
            if(std::abs(body.pose.x-placed.position[0])>.001f||std::abs(body.pose.y-placed.position[1])>.001f||
               std::abs(body.pose.z-placed.position[2])>.001f||std::abs(body.pose.yaw-placed.yaw)>.001f||
               std::abs(body.localY-placed.floorOffset)>.001f||actors->cloneState().sourceTick!=nextTick)
                throw std::runtime_error("Saved player pose or native source tick disagrees with world state");
            restoreProgress(progressState);
            if(savedTextRevision!=sourceTexts().textRevision())edguitext::restartTextCursors(sourceGui,game.vm);
            sourceTexts().validate(sourceGui,episode_portal_detail::sceneId(identity),[&](uint32_t id){return sourceCartText(id);},reading?documentReaderItem():-1);
            if(reading) {
                const auto text=documentTextID(view.items[view.selected]);const auto& record=SourceGui::at(sourceGui.items,documentReaderItem());
                const auto data=record.properties.find("guiItemSetDataID");
                if(game.vm.staticField(game.hostField(HostFieldRole::InvBookText1)).integer()!=text.integer()||data==record.properties.end()||data->second!=std::vector<int32_t>{text.integer()})
                    throw jpc::Error("Saved reader disagrees with its selected document");
                view.documentPage=documentPage();
            }
            routes->setScene(next.nativeBytes);routes->restore(routeState);
            vfx->restore(vfxState);
            streams->restore(streamState);
        }
        if(graphics){graphics->prepareCameraFrame(*prepared,next,placed.camera);checked(SDL_WaitForGPUIdle(graphics->device),"Wait before restoring world");}
        audioBank.mute(true);audioBank.restore(sounds);
        if(actors) {
            rawClockSeconds=nextClock;
            game.mode=hostMode>=0?hostMode:game.vm.staticField(844).integer();
        }
        if(!actors)game.mode=hostMode>=0?hostMode:0;
        game.messageAsset=messageAsset;game.guiFlags=std::move(guiFlags);
        if(completingCartSave>=0) {
            edcart::State cart;cart.completed=edcart::Result{{edcart::Operation::Save,unsigned(completingCartSave)},true,{}};
            sourceCartAdapter().restore(cart);
        }
        commitNativeStage();audioBank.mute(soundMuted);properties=std::move(nativeProperties);resetContext();
    } catch(...) {
        game.rollback(previous);rollbackNative(previousNative);rawClockSeconds=previousClock;resetContext();
        audioBank.mute(soundMuted);throw;
    }
    if(graphics)graphics->resources.swap(prepared);
    scene=std::move(next);actor=std::move(placed);gates=std::move(states);nativeProperties=std::move(properties);
    sourceTick=nextTick;tickFraction=fraction;rawClockSeconds=nextClock;guard=nextGuard;pending=nextPending;inventoryView=std::move(view);
    sourceTextPages.clear();sourceGuiCommands.clear();authoredUiComplete=false;
    failedPortal=noticeDoor=-1;notice="game loaded";
    scriptFault=false;
    pendingSceneSwitch.reset();
    quitRequested=false;
    overlayClockPaused=false;
    resetNativeController();
    audio.remaining=remaining;audio.playbackDuration=remaining;audio.started=remaining>0?SDL_GetTicksNS():0;
    if(audio.stream) {
        SDL_ClearAudioStream(audio.stream);
        if(remaining>0) {
            const size_t frame=SDL_AUDIO_FRAMESIZE(audio.spec);
            const size_t offset=std::min(audio.pcm.size(),size_t((audio.duration-remaining)*audio.spec.freq)*frame);
            if(!SDL_PutAudioStreamData(audio.stream,audio.pcm.data()+offset,int(audio.pcm.size()-offset)))
                SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,"Loaded door sound retains source timing: %s",SDL_GetError());
        }
    }
    record("load");
}
bool saveGame(const fs::path& directory) {
    try {
        const auto bytes=saveStateBytes();
        if(originalRuntime) {
            auto checked=stageSavedWorld(*this,bytes);
            if(checked->saveStateBytes()!=bytes)throw jpc::Error("Saved world did not validate exactly");
        }
        savefile::atomicWrite(directory,"quicksave.eds",bytes);notice="game saved";noticeDoor=-1;record("save");return true;
    }
    catch(const std::exception& error){notice="could not save game";noticeDoor=-1;SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Save retained previous file: %s",error.what());return false;}
}
bool loadGame(const fs::path& directory,Graphics* graphics=nullptr) {
    try {restoreStateBytes(savefile::read(directory,"quicksave.eds"),graphics);return true;}
    catch(const std::exception& error){notice="could not load game";noticeDoor=-1;SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Load retained current world: %s",error.what());return false;}
}
