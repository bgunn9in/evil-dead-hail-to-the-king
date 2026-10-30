// Included inside World. Presentation properties are stateful, while passage
// predicates and writes continue to execute the original JPC.
struct ObjectProperty {
    std::array<int32_t,4> setup{};
    int16_t flags=0x0b; // 423C6D initializes0x0f; objSetup40E318 clears bit4.
    int marker=-1,markerMode=0;
    GroundPoint position{};
    std::array<int32_t,3> orientation{};
    float radius=0;
    int32_t floorTriangle=-1;
    float cachedFloorHeight=0;
    edeffects::State fade;
};
struct LightProperty {
    std::array<int32_t,4> color{};
    std::map<std::string,std::vector<int32_t>> settings;
    bool enabled=false;
};
struct VoiceProperty {
    int marker=-1,radius=0;uint32_t flags=0;
    std::array<float,3> position{};bool positionKnown=false;
};
struct MovieRequest {std::string scene;uint32_t resource=0;int flags=0;bool played=false;};
struct NativeProperties {
    std::map<int,ObjectProperty> objects;
    std::map<int,LightProperty> lights;
    std::map<int,VoiceProperty> voices;
    std::array<float,3> soundListener{};
    int32_t turnAmount=8;
    std::vector<MovieRequest> movies;
    std::vector<std::pair<std::string,std::vector<int32_t>>> effects;
    std::map<edactors::Key,edeffects::State> actorFades;
    uint32_t randomSeed=12345,randomIndex=0;
    bool randomReady=false;
    std::array<uint32_t,64> randomTable{};
    int32_t randomBound(int32_t bound) {
        if(!bound)return 0;
        randomSeed=randomSeed*0x41c64e6du+0x3039u;
        const uint32_t high=uint32_t((uint64_t(randomSeed)*0x88888889u)>>32);
        return int32_t(((high>>3)%0x07ffffffu)%uint32_t(bound<0?-int64_t(bound):bound));
    }
    int32_t randomRange(int32_t a,int32_t b) {
        const int32_t width=jpc::signedBits(uint32_t(b)-uint32_t(a));if(!width)return a;
        if(!randomReady) {
            for(auto& value:randomTable) {
                randomSeed=randomSeed*0x41c64e6du+0x3039u;
                const uint32_t high=uint32_t((uint64_t(randomSeed)*0x88888889u)>>32);
                value=(high>>3)%0x07ffffffu;
            }
            randomReady=true;
        }
        if(++randomIndex>=64){randomIndex=0;randomReady=false;}
        return jpc::signedBits(uint32_t(a)+randomTable[randomIndex]%uint32_t(width<0?-int64_t(width):width));
    }
} nativeProperties;
Scene* scriptScene=nullptr;
Actor* scriptActor=nullptr;
std::vector<GateState>* scriptGates=nullptr;
bool preparingScene=false;
bool preparedPinned=false;
std::vector<int> preparedVoices,preparedStops;
std::vector<edrumble::Command> preparedRumbles;
void requestNativeRumble(edrumble::Command command) {
    if(preparingScene)preparedRumbles.push_back(command);
    else rumbleHost.send(command);
}
static constexpr int PinnedDoorClip=0x7ffffffe;
void queueEffect(const std::string& name,std::vector<int32_t> values) {
    if(name=="actorEffectFadeInfo"||name=="actorEffectFadeStart") {
        if(!actors||values.size()!=(name=="actorEffectFadeInfo"?7u:4u))throw jpc::Error("Invalid source actor fade ABI");
        const edactors::Key key{values[0],values[1]};actors->at(key);auto& fade=nativeProperties.actorFades[key];
        if(name=="actorEffectFadeInfo")fade.info(values[2],values[3],values[4],values[5],values[6]);
        else fade.start(values[2],values[3]);return;
    }
    if(vfx&&!values.empty()&&vfx->supports(uint32_t(values[0]))&&
       (name=="vfxSpawnMarker"||name=="vfxSpawnMarkerOffset"||name=="vfxSpawnActorJoint"||
        name=="vfxSpawnActorJointRnd"||name=="vfxSpawnActorOffset")) {
        vfx->request(name,values);return;
    }
    // shortcut: retain at most256 requests for unrecovered emitters and screen effects until their native behavior is verified.
    if(nativeProperties.effects.size()>=256)nativeProperties.effects.erase(nativeProperties.effects.begin());
    nativeProperties.effects.emplace_back(name,std::move(values));
}
Scene& nativeScene(){return scriptScene?*scriptScene:scene;}
const Scene& nativeScene()const{return scriptScene?*scriptScene:scene;}
unsigned sourceQueryCamera()const{return visibilityReady?visibilityCamera:std::numeric_limits<unsigned>::max();}
Actor& nativeActor(){return scriptActor?*scriptActor:actor;}
const Actor& nativeActor()const{return scriptActor?*scriptActor:actor;}
std::vector<GateState>& nativeGates(){return scriptGates?*scriptGates:gates;}
void initializeVfx() {
    edvfx::Host host;
    host.marker=[this](int index) {
        const auto& markers=nativeScene().scripting.markers;
        if(index<0||unsigned(index)>=markers.size())throw jpc::Error("Source VFX marker is missing");
        const auto& p=markers[unsigned(index)];return edvfx::Vector{p.x,p.y,p.z};
    };
    host.randomBound=[this](int bound){return nativeProperties.randomBound(bound);};
    host.cachedBound=[this](int bound){return nativeProperties.randomRange(0,bound);};
    host.visible=[this](edvfx::Vector p){return visibilityReady&&nativeScene().queries.visible(8,sourceQueryCamera(),{p[0],p[1],p[2]});};
    host.coarseCell=[this](float x,float z){return nativeScene().sourceFloor.coarseCell(x,z,sourceFloorGrid);};
    host.coarseHeight=[this](float x,float z){return nativeScene().sourceFloor.coarseHeight(x,z,sourceFloorGrid);};
    host.floorSample=[this](float x,float y,float z){return nativeScene().sourceFloor.height(x,y,z,sourceFloorCache);};
    host.repellingActors=[this]{
        edparticle::PhysicalActors result{};
        if(actors)for(const auto& [key,body]:actors->bodies())if(body.nativeSlot>=0) {
            (void)key;result.at(size_t(body.nativeSlot))={uint32_t(body.flags),{{body.pose.x,body.localY,body.pose.z}},body.cachedFloorHeight};
        }
        return result;
    };
    host.actorWorld=[this](int type,int index){
        const auto& body=actors->at({type,index});return edvfx::Vector{body.pose.x,body.pose.y,body.pose.z};
    };
    host.jointWorld=[this](int type,int index,unsigned joint,const std::array<int32_t,3>& offset){
        return actors->jointWorld({type,index},joint,offset);
    };
    vfx=std::make_unique<edvfx::SourceVfx>(root,std::move(host));
}
std::vector<ObjectDraw> drawObjects()const {
    std::vector<ObjectDraw> result;
    if(!actors||!visibilityReady)return result;
    for(const auto& [index,object]:nativeProperties.objects) {
        (void)index;if(!(object.flags&1))continue;
        const float floor=object.cachedFloorHeight;
        const auto& p=object.position;const float worldY=p.y+((object.flags&2)?floor:0);
        if(!nativeScene().queries.visible(4,sourceQueryCamera(),{p.x,worldY,p.z}))continue;
        const auto tint=effectTint(object.fade);
        result.push_back({uint32_t(object.setup[1]),uint32_t(object.setup[3]),
            {{p.x,p.y,p.z},object.orientation,floor,uint16_t(object.flags)},int16_t(object.setup[0]),tint.color,tint.mode});
    }
    return result;
}
std::vector<edvfx::Batch> drawEffects()const {
    if(!vfx)return {};
    const auto& camera=nativeScene().cameras.at(nativeActor().camera);const auto& vp=camera.vp;
    const float projectionY=float(std::sqrt(double(vp[1])*vp[1]+double(vp[5])*vp[5]+double(vp[9])*vp[9]));
    return vfx->draw(camera.right3,camera.up3,camera.eye3,projectionY);
}
void sampleEpisodeFloors() {
    actors->sampleFloors();
    for(auto& [index,object]:nativeProperties.objects) {
        (void)index;
        if((object.flags&3)==3)object.cachedFloorHeight=float(nativeScene().sourceFloor.actorHeight(
            object.position.x,object.position.y,object.position.z,object.floorTriangle,sourceFloorCache));
    }
}
void advanceStream(double dt) {
    if(!preparingScene)audioBank.setMasterVolume(sourceGui.masterVolume);
    if(streams){streams->advance(dt);streams->setMasterVolume(sourceGui.masterVolume);if(!preparingScene)streams->commit();}
}
void applyCameraFrame(Scene& target) {
    if(!cameraTimeline||!cameraTimeline->state)return;
    const auto& state=*cameraTimeline->state;setCameraFrame(target,unsigned(state.request.track),state.frame);
    target.foregroundFrame.at(size_t(state.request.track))=eddcprofile::foregroundFrame(target.id,unsigned(state.request.track),state.start);
}
void controlEpisodeCamera(edcamera::Request request) {
    if(!cameraTimeline)throw jpc::Error("Original camera controller is unavailable");
    cameraTimeline->control(request,[this](int track,int frame){
        game.vm.invoke(game.package.methodIndex("Main","cameraUpdate","(II)V"),0,
            {jpc::Value::integer(track),jpc::Value::integer(frame)});
    });
    if(cameraTimeline->inControl()||!cameraTimeline->state)return;
    applyCameraFrame(nativeScene());visibilityCamera=unsigned(cameraTimeline->state->request.track);
    visibilityReady=true;nativeActor().camera=visibilityCamera;
}
void advanceCameraTimeline() {
    if(cameraTimeline&&cameraTimeline->tick(game.vm.staticField(844).integer()))applyCameraFrame(nativeScene());
}
std::array<float,3> sceneEffectTint()const {
    std::array<float,3> result{edeffects::DefaultTint,edeffects::DefaultTint,edeffects::DefaultTint};
    const auto found=progressSceneProperties.find("lightSetAmbient");
    if(found!=progressSceneProperties.end()) {
        if(found->second.size()!=3)throw jpc::Error("Invalid original ambient RGB");
        for(unsigned i=0;i<3;++i)result[i]=float(double(found->second[i])*.003921568859368563f);
    }
    return result;
}
ActorDrawTint effectTint(edeffects::State effect)const {
    if(!effect.sampled)effect.sample(sceneEffectTint());
    return {0xff000000u|(uint32_t(effect.rgb[0])<<16)|(uint32_t(effect.rgb[1])<<8)|effect.rgb[2],effect.fromMode};
}
std::map<edactors::Key,ActorDrawTint> drawActorTints()const {
    std::map<edactors::Key,ActorDrawTint> result;if(!actors)return result;
    for(const auto& [key,body]:actors->bodies())if(body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)) {
        const auto found=nativeProperties.actorFades.find(key);
        result.emplace(key,effectTint(found==nativeProperties.actorFades.end()?edeffects::State{}:found->second));
    }
    return result;
}
void sampleEpisodeEffects() {
    const auto tint=sceneEffectTint();
    for(const auto& [key,body]:actors->bodies())if(body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&visibilityReady&&
       nativeScene().queries.visible(key.type==0?1:2,sourceQueryCamera(),{body.pose.x,body.pose.y,body.pose.z}))
        nativeProperties.actorFades[key].sample(tint);
    for(auto& [index,object]:nativeProperties.objects) {
        (void)index;const auto& p=object.position;const float y=p.y+((object.flags&2)?object.cachedFloorHeight:0);
        if((object.flags&1)&&visibilityReady&&nativeScene().queries.visible(4,sourceQueryCamera(),{p.x,y,p.z})) {
            // Native08a180 uses one simple bounded draw, then the original kind23 emitter.
            if(game.profile.dreamcast&&(object.flags&8)&&nativeProperties.randomBound(256)<=6)vfx->spawn(23,{{p.x,y,p.z}},0);
            object.fade.sample(tint);
        }
    }
}
GroundPoint smartObjectPosition(const edactors::Body& body) {
    const auto& target=nativeScene();const auto& data=target.nativeBytes;
    const GroundPoint local{body.pose.x,body.localY,body.pose.z};
    auto u32=[&](size_t offset){if(offset>data.size()||data.size()-offset<4)throw jpc::Error("Truncated source navigation table");
        uint32_t value;std::memcpy(&value,data.data()+offset,4);return value;};
    const size_t count=u32(48),offset=u32(52),portals=u32(44);
    if(count>100000||offset>data.size()||count*44>data.size()-offset)throw jpc::Error("Invalid source navigation table");
    GroundPoint bestPoint=local;float best=0;bool found=false;
    for(size_t i=0;i<count;++i) {
        GroundPoint point;std::memcpy(&point,data.data()+offset+i*44+8,12);
        for(float value:{point.x,point.y,point.z})if(!std::isfinite(value))throw jpc::Error("Non-finite source navigation point");
        const auto mask=target.queries.pointMask(point);bool excluded=false;
        for(size_t j=0;j<target.nativePortals.size();++j)if(data[portals+j*72]==0&&target.enabledPortals[j]&&
            SourceSceneQueries::maskMatch(mask,target.nativePortals[j].mask)&&insideTrigger(target.nativePortals[j],{point.x,point.z})) {
            excluded=true;break;
        }
        if(excluded)continue;
        const double x=double(point.x)-local.x,z=double(point.z)-local.z,squared=x*x+z*z;
        if(!found||(squared<best&&target.queries.lineOfSight(point,local,body.key.type==0?4:2,
           sourceQueryCamera(),target.enabledVolumes,visibilityReady))) {best=float(squared);bestPoint=point;found=true;}
    }
    bestPoint.y=local.y;return bestPoint;
}
void requireAsh(const std::vector<jpc::Value>& args) const {
    if(args.size()<2||args[0].integer()!=game.vm.field(game.ash,6).integer()||args[1].integer()!=game.vm.field(game.ash,5).integer())
        throw jpc::Error("Native actor registry currently contains only Ash");
}
bool sourceVoicePlaying() const {
    const int token=game.vm.staticField(game.hostField(HostFieldRole::ScenePortalSound)).integer();
    if(token==1)return preparingScene&&preparedPinned?audio.duration>0:audio.playing();
    if(token>=0x10000)return !audioBank.done(token);
    return false;
}
void nativePortalEnable(unsigned index,bool enabled) {
    auto& target=nativeScene();auto& states=nativeGates();
    if(index>=target.enabledPortals.size())throw jpc::Error("Native scene portal is missing");
    target.enabledPortals[index]=enabled;
    for(size_t i=0;i<target.doors.size();++i)if(target.doors[i].portalIndex==index)states.at(i).enabled=enabled;
}
void stopNativeVoice(int token) {
    if(originalRuntime&&token>=0x10000){if(!audioBank.done(token))nativeProperties.voices[token].flags|=0x10000u;return;}
    if(preparingScene){preparedStops.push_back(token);return;}
    if(token==1){audio.remaining=0;if(audio.stream)SDL_ClearAudioStream(audio.stream);}
    else audioBank.stop(token);
}
void beginNativeStage() {
    if(preparingScene)throw jpc::Error("Nested world native transaction");
    preparingScene=true;preparedPinned=false;preparedVoices.clear();preparedStops.clear();preparedRumbles.clear();
}
void syncSpatialAudio() {
    if(!originalRuntime)return;
    for(const auto& voice:audioBank.snapshot().voices) {
        const auto found=nativeProperties.voices.find(voice.token);int percent=voice.volume;
        if(found!=nativeProperties.voices.end()&&found->second.positionKnown) {
            const auto& source=found->second;
            percent=edaudiospatial::Percent(nativeProperties.soundListener,source.position,voice.volume,int(source.flags),float(source.radius)*.01f);
        }
        audioBank.setSpatialVolume(voice.token,percent);
    }
}
void stopSceneAudio() {
    for(const auto& voice:audioBank.snapshot().voices)if(voice.bucket!="global"&&voice.bucket!="global_b") {
        audioBank.stop(voice.token);nativeProperties.voices.erase(voice.token);
    }
}
void processSourceAudio() {
    audioBank.setMasterVolume(sourceGui.masterVolume);
    for(const auto& voice:audioBank.snapshot().voices) {
        const auto source=nativeProperties.voices.find(voice.token);
        if(!voice.pending&&source!=nativeProperties.voices.end()&&(source->second.flags&0x10000u)) {
            audioBank.stop(voice.token);nativeProperties.voices.erase(source);
        }
    }
    syncSpatialAudio();audioBank.sourceFrame(!preparingScene);
}
void commitNativeStage() {
    audioBank.setMasterVolume(sourceGui.masterVolume);
    if(streams){streams->setMasterVolume(sourceGui.masterVolume);streams->commit();}
    preparingScene=false;for(int token:preparedStops)stopNativeVoice(token);
    if(preparedPinned)audio.play();preparedPinned=false;preparedVoices.clear();preparedStops.clear();
    syncSpatialAudio();audioBank.commitSourceFrame();
    const auto rumbles=std::move(preparedRumbles);preparedRumbles.clear();for(const auto command:rumbles)rumbleHost.send(command);
}
void discardNativeStage() {
    preparingScene=false;for(int token:preparedVoices)audioBank.stop(token);
    preparedPinned=false;preparedVoices.clear();preparedStops.clear();preparedRumbles.clear();
}
struct NativeCheckpoint {
    NativeProperties properties;std::vector<GateState> gates;std::vector<bool> volumes,portals;
    unsigned guard;int pending;
    edactors::State actors;
    std::vector<uint8_t> progress;
    std::vector<uint8_t> routes;
    std::vector<uint8_t> vfx;
    int32_t floorCache;
    SourceFloorQuery::CoarseGrid floorGrid;
    StreamAudio::State stream;
    std::vector<uint8_t> camera;
    Actor cpuActor;
    std::vector<uint8_t> screen;
    std::optional<PackageRequest> sceneSwitch;
    bool quit;
    std::optional<edcart::State> cart;
    bool visibilityReady;unsigned visibilityCamera;
    InventoryView inventory;
    std::vector<edguitext::Page> textPages;
    std::vector<edguipresentation::Command> guiCommands;bool authoredComplete;
    AudioBank::State sounds;
};
NativeCheckpoint checkpointNative() const {return {nativeProperties,gates,scene.enabledVolumes,scene.enabledPortals,guard,pending,
    actors?actors->cloneState():edactors::State{},progressSnapshot(),routes?routes->snapshot():std::vector<uint8_t>{},
    vfx?vfx->snapshot():std::vector<uint8_t>{},sourceFloorCache,sourceFloorGrid,
    streams?streams->snapshot():StreamAudio::State{},cameraTimeline?cameraTimeline->snapshot():std::vector<uint8_t>{},actor,
    screenSnapshot(),pendingSceneSwitch,quitRequested,sourceCartCheckpoint(),visibilityReady,visibilityCamera,inventoryView,sourceTextPages,sourceGuiCommands,authoredUiComplete,audioBank.snapshot()};}
void rollbackNative(const NativeCheckpoint& previous) {
    discardNativeStage();nativeProperties=previous.properties;gates=previous.gates;scene.enabledPortals=previous.portals;
    for(unsigned i=0;i<previous.volumes.size();++i)setVolumeEnabled(scene,i,previous.volumes[i]);
    guard=previous.guard;pending=previous.pending;
    if(actors)actors->restore(previous.actors);
    restoreProgress(previous.progress);
    const bool soundMuted=audioBank.muted();audioBank.mute(true);
    try {audioBank.restore(previous.sounds);syncSpatialAudio();audioBank.setMasterVolume(sourceGui.masterVolume);audioBank.mute(soundMuted);}
    catch(...){audioBank.mute(soundMuted);throw;}
    if(routes){routes->setScene(scene.nativeBytes);routes->restore(previous.routes);}
    if(vfx)vfx->restore(previous.vfx);
    sourceFloorCache=previous.floorCache;sourceFloorGrid=previous.floorGrid;
    if(streams){streams->restore(previous.stream);streams->setMasterVolume(sourceGui.masterVolume);streams->commit();}
    if(cameraTimeline){cameraTimeline=std::make_unique<edcamera::Timeline>(scene.cameraTracks,episode_portal_detail::sceneId(scene.id));
        cameraTimeline->restore(previous.camera);applyCameraFrame(scene);}
    actor=previous.cpuActor;
    restoreScreen(previous.screen);
    pendingSceneSwitch=previous.sceneSwitch;
    quitRequested=previous.quit;
    restoreSourceCartCheckpoint(previous.cart);
    visibilityReady=previous.visibilityReady;visibilityCamera=previous.visibilityCamera;
    inventoryView=previous.inventory;sourceTextPages=previous.textPages;sourceGuiCommands=previous.guiCommands;authoredUiComplete=previous.authoredComplete;
}
template<class Binder>void bindEpisodeScripts(Binder& bind) {
    bind("audioPlay",[this](auto&,auto,const auto& args){
        if(!streams)throw jpc::Error("Original streamed audio has not been exported for this profile");
        try {streams->play(uint32_t(args[0].integer()),args[1].integer());if(!preparingScene)streams->commit();}
        catch(const std::exception& error){throw jpc::Error(error.what());}return jpc::Value{};
    });
    bind("audioStatus",[this](auto&,auto,const auto&){return jpc::Value::integer(streams&&streams->status()?1:0);});
    bind("audioStop",[this](auto&,auto,const auto&){
        // DC8c064040 stops the stream and clears status; PC4094D0 is RET.
        if(game.profile.dreamcast&&streams){
            if(scene.menuContext&&sourceGui.activeSystem>=0){
                const auto& menu=SourceGui::at(sourceGui.systems,sourceGui.activeSystem);
                // Main ordinal2 opens settings; keep its title loop across that transition.
                if(menu.pad&&menu.selected>=0&&game.vm.objectClass(menu.pad)==game.package.classIndex("MenMain")&&
                   SourceGui::at(sourceGui.items,menu.selected).ordinal==2)return jpc::Value{};
            }
            streams->contextReset();if(!preparingScene)streams->commit();
        }
        return jpc::Value{};
    });
    bind("scenePortalEnable",[this](auto&,auto,const auto& args){
        const int enabled=args[1].integer();if(enabled!=0&&enabled!=1)throw jpc::Error("Invalid native portal activation");
        nativePortalEnable(unsigned(args[0].integer()),enabled==1);return jpc::Value{};
    });
    bind("sceneTriggerEnable",[this](auto&,auto,const auto& args){
        const int enabled=args[1].integer();if(enabled!=0&&enabled!=1)throw jpc::Error("Invalid native volume activation");
        setVolumeEnabled(nativeScene(),unsigned(args[0].integer()),enabled==1);return jpc::Value{};
    });
    bind("actorInPortal",[this](auto&,auto,const auto& args){
        const unsigned index=unsigned(args[2].integer());
        const auto& target=nativeScene();const auto placed=scriptActorPosition(args);
        if(index>=target.nativePortals.size())throw jpc::Error("Native actorInPortal volume is missing");
        return jpc::Value::integer(insideTrigger(target.nativePortals[index],{placed.x,placed.z}));
    });
    bind("sndMgrGetSNDID",[this](auto&,auto,const auto& args){
        const uint32_t id=uint32_t(args[0].integer());
        return jpc::Value::integer(!game.profile.dreamcast&&id==0x0e8a4aca?PinnedDoorClip:audioBank.resolve(nativeScene().id,id,levelBPackage));
    });
    auto play=[this](int clip,int volume,int loop)->int {
        if(clip==PinnedDoorClip||(!game.profile.dreamcast&&uint32_t(clip)==0x0e8a4aca)) {
            if(loop>=0)throw jpc::Error("Pinned original door sound cannot loop");
            if(preparingScene)preparedPinned=true;else audio.play();return 1;
        }
        const int token=loop<0?audioBank.play(clip,volume):audioBank.play(clip,volume,loop);
        if(token>=0){nativeProperties.voices[token]={};nativeProperties.voices[token].positionKnown=true;
            if(preparingScene)preparedVoices.push_back(token);}
        return token;
    };
    bind("sndMgrPlaySoundVol",[play](auto&,auto,const auto& args){return jpc::Value::integer(play(args[0].integer(),args[1].integer(),-1));});
    bind("sndMgrPlaySoundVolLoop",[play](auto&,auto,const auto& args){return jpc::Value::integer(play(args[0].integer(),args[1].integer(),args[2].integer()));});
    bind("sndMgrActorPlaySound",[this](auto&,auto,const auto& args){
        const auto point=scriptActorPosition(args);
        const int token=audioBank.playAtFrequency(args[2].integer(),args[3].integer(),edanimsound::actorFrequencyHz(args[4].integer()));
        if(token>=0){auto& source=nativeProperties.voices[token];source.flags|=0x404u;
            source.position={point.x,point.y,point.z};source.positionKnown=true;if(preparingScene)preparedVoices.push_back(token);}
        return jpc::Value::integer(token);
    });
    bind("sndMgrIsVoiceDone",[this](auto&,auto,const auto& args){
        return jpc::Value::integer(args[0].integer()==1?(preparingScene&&preparedPinned?audio.duration<=0:!audio.playing()):audioBank.done(args[0].integer()));
    });
    bind("sndMgrIsSNDIDDone",[this](auto&,auto,const auto& args){
        const int clip=args[0].integer();
        if(clip==PinnedDoorClip)return jpc::Value::integer(!(preparingScene&&preparedPinned)&&!audio.playing());
        const auto sounds=audioBank.snapshot();return jpc::Value::integer(std::none_of(sounds.voices.begin(),sounds.voices.end(),
            [&](const auto& voice){return voice.clip==clip;}));
    });
    bind("sndMgrStopVoice",[this](auto&,auto,const auto& args){stopNativeVoice(args[0].integer());return jpc::Value{};});
    // Dreamcast8c077740 is a RET; frequency8c077700 reaches the RET at8c09a620.
    bind("sndMgrVoiceSetVolume",[](auto&,auto,const auto&){return jpc::Value{};});
    bind("sndMgrVoiceSetFrequency",[](auto&,auto,const auto&){return jpc::Value{};});
    bind("sndMgrAddVoiceFlags",[this](auto&,auto,const auto& args){if(args[0].integer()>=0)nativeProperties.voices[args[0].integer()].flags|=uint32_t(args[1].integer());return jpc::Value{};});
    bind("sndMgrSetVoiceRadius",[this](auto&,auto,const auto& args){if(args[0].integer()>=0)nativeProperties.voices[args[0].integer()].radius=args[1].integer();return jpc::Value{};});
    bind("sndMgrSetVoicePositionMarker",[this](auto&,auto,const auto& args){
        if(args[0].integer()<0)return jpc::Value{};
        const unsigned marker=unsigned(args[1].integer());if(marker>=nativeScene().scripting.markers.size())throw jpc::Error("Native sound marker is missing");
        auto& source=nativeProperties.voices[args[0].integer()];const auto& point=nativeScene().scripting.markers[marker];
        source.marker=int(marker);source.position={point.x,point.y,point.z};source.positionKnown=true;return jpc::Value{};
    });
    bind("randomRange",[this](auto&,auto,const auto& args){return jpc::Value::integer(nativeProperties.randomRange(args[0].integer(),args[1].integer()));});
    bind("random",[this](auto&,auto,const auto& args){return jpc::Value::integer(nativeProperties.randomBound(args[0].integer()));});
    bind("debugPrintStringInt",[this](auto&,auto,const auto& args){game.diagnostic=game.string(args[1].reference())+std::to_string(args[0].integer());return jpc::Value{};});
    auto object=[this](int index)->ObjectProperty& {
        if(index<0||index>=32)throw jpc::Error("Native object slot is outside original pool");return nativeProperties.objects[index];
    };
    const auto objectInteger=[](double value){
        const double truncated=std::trunc(value);
        if(!std::isfinite(truncated)||truncated<std::numeric_limits<int32_t>::min()||truncated>std::numeric_limits<int32_t>::max())
            throw jpc::Error("Original object integer conversion is out of range");
        return int32_t(truncated);
    };
    const char* positionGetters[]={"objGetPositionX","objGetPositionY","objGetPositionZ"};
    for(unsigned axis=0;axis<3;++axis)bind(positionGetters[axis],[object,objectInteger,axis](auto&,auto,const auto& args){
        const auto& point=object(args[0].integer()).position;const float values[]={point.x,point.y,point.z};
        return jpc::Value::integer(objectInteger(double(values[axis])*100));
    });
    bind("objGetRotY",[object,objectInteger](auto&,auto,const auto& args){
        const float radians=float(double(object(args[0].integer()).orientation[1])*double(0.01745329238474369f));
        return jpc::Value::integer(objectInteger(double(radians)*double(57.295780181884766f)));
    });
    bind("objSetPosition",[object](auto&,auto,const auto& args){
        auto& value=object(args[0].integer());
        value.position={float(double(args[1].integer())*double(.01f)),float(double(args[2].integer())*double(.01f)),float(double(args[3].integer())*double(.01f))};
        value.marker=-1;value.markerMode=0;return jpc::Value{};
    });
    bind("objInTrigger",[this,object](auto&,auto,const auto& args){
        return jpc::Value::integer(nativeScene().scripting.inside(unsigned(args[1].integer()),object(args[0].integer()).position));
    });
    bind("objEffectFadeInfo",[object](auto&,auto,const auto& args){object(args[0].integer()).fade.info(
        args[1].integer(),args[2].integer(),args[3].integer(),args[4].integer(),args[5].integer());return jpc::Value{};});
    bind("objEffectFadeStart",[object](auto&,auto,const auto& args){object(args[0].integer()).fade.start(args[1].integer(),args[2].integer());return jpc::Value{};});
    bind("objEffectFadeDone",[object](auto&,auto,const auto& args){return jpc::Value::integer(object(args[0].integer()).fade.done()?1:0);});
    bind("objEffectSetFlag",[object](auto&,auto,const auto& args){object(args[0].integer()).fade.setFlag(args[1].integer(),args[2].integer()!=0);return jpc::Value{};});
    bind("objEffectReflect",[](auto&,auto,const auto&){return jpc::Value{};}); // DC069e00 delegates to085f60, which is RTS.
    auto actorFade=[this](int type,int index)->edeffects::State& {
        if(!actors)throw jpc::Error("Source actor fade requires the native actor registry");
        const edactors::Key key{type,index};actors->at(key);return nativeProperties.actorFades[key];
    };
    bind("actorEffectFadeDone",[actorFade](auto&,auto,const auto& args){return jpc::Value::integer(actorFade(args[0].integer(),args[1].integer()).done()?1:0);});
    bind("actorEffectSetFlag",[actorFade](auto&,auto,const auto& args){actorFade(args[0].integer(),args[1].integer()).setFlag(args[2].integer(),args[3].integer()!=0);return jpc::Value{};});
    bind("actorEffectReflect",[](auto&,auto,const auto&){return jpc::Value{};}); // DC reflection implementation085f60 is RTS.
    bind("objSetup",[this,object](auto&,auto,const auto& args){
        auto& value=object(args[0].integer());if(actors)actors->invalidatePickup(args[0].integer());value=ObjectProperty{};
        for(unsigned i=0;i<4;++i)value.setup[i]=args[i+1].integer();return jpc::Value{};
    });
    bind("objSetType",[object](auto&,auto,const auto& args){
        object(args[0].integer()).setup[0]=int16_t(args[1].integer());return jpc::Value{};
    });
    bind("objFlagsSub",[object](auto&,auto,const auto& args){
        auto& value=object(args[0].integer());const int mask=args[1].integer();
        if(int(value.flags)&mask)value.flags=int16_t(uint16_t(value.flags)^uint16_t(mask));
        return jpc::Value::integer(value.flags);
    });
    bind("objFlagsAdd",[object](auto&,auto,const auto& args){
        auto& value=object(args[0].integer());value.flags=int16_t(uint16_t(value.flags)|uint16_t(args[1].integer()));
        return jpc::Value::integer(value.flags);
    });
    bind("objSetVisible",[object](auto&,auto,const auto& args){auto& value=object(args[0].integer());
        value.flags=int16_t(args[1].integer()?uint16_t(value.flags)|1u:uint16_t(value.flags)&~1u);return jpc::Value{};});
    bind("objSetBRadius",[object](auto&,auto,const auto& args){object(args[0].integer()).radius=float(args[1].integer())*.01f;return jpc::Value{};});
    bind("objSetOrientation",[object](auto&,auto,const auto& args){auto& value=object(args[0].integer());
        for(unsigned i=0;i<3;++i)value.orientation[i]=args[i+1].integer();return jpc::Value{};});
    bind("objSetPositionMarker",[this,object](auto&,auto,const auto& args){
        auto& value=object(args[0].integer());const unsigned marker=unsigned(args[1].integer());
        if(marker>=nativeScene().scripting.markers.size())throw jpc::Error("Native object marker is missing");
        value.marker=int(marker);value.markerMode=args[2].integer();value.position=nativeScene().scripting.markers[marker];
        if(value.markerMode==1){value.flags=int16_t(uint16_t(value.flags)|2u);value.position.y=0;}
        else value.flags=int16_t(uint16_t(value.flags)&~2u);
        return jpc::Value{};
    });
    bind("objSetPositionActor",[this,object](auto&,auto,const auto& args){
        if(!actors)throw jpc::Error("Source object placement requires the native actor registry");
        const auto& body=actors->at({args[1].integer(),args[2].integer()});auto& value=object(args[0].integer());
        value.position=args[3].integer()==1?smartObjectPosition(body):GroundPoint{body.pose.x,body.localY,body.pose.z};
        value.marker=-1;value.markerMode=0;return jpc::Value{};
    });
    bind("objTranslatePosition",[object](auto&,auto,const auto& args){
        auto& value=object(args[0].integer());
        if(args[1].integer()||args[2].integer()||args[3].integer()) {
            value.position.x+=float(double(args[1].integer())*double(.01f));
            value.position.y+=float(double(args[2].integer())*double(.01f));
            value.position.z+=float(double(args[3].integer())*double(.01f));
            value.marker=-1;value.markerMode=0;
        }
        return jpc::Value{};
    });
    bind("lightSetColor",[this](auto&,auto,const auto& args){auto& light=nativeProperties.lights[args[0].integer()];
        for(unsigned i=0;i<4;++i)light.color[i]=args[i+1].integer();return jpc::Value{};});
    bind("lightSetEnable",[this](auto&,auto,const auto& args){nativeProperties.lights[args[0].integer()].enabled=args[1].integer()!=0;return jpc::Value{};});
    for(const char* name:{"lightSetMode","lightSetSpeed","lightSetFrameSpeed","lightSetPositionMarker"}) {
        const std::string key=name;bind(key,[this,key](auto&,auto,const auto& args){
            std::vector<int32_t> values;for(size_t i=1;i<args.size();++i)values.push_back(args[i].integer());
            nativeProperties.lights[args[0].integer()].settings[key]=std::move(values);return jpc::Value{};
        });
    }
    bind("moviePlay",[this](auto&,auto,const auto& args){
        const uint32_t identity=uint32_t(args[0].integer());const int flags=args[1].integer();
        nativeProperties.movies.push_back({nativeScene().id,identity,flags,false});
        if(moviePresenter){moviePresenter(*this,identity,flags);nativeProperties.movies.back().played=true;}
        return jpc::Value{};
    });
    bind("rumbleP1",[this](auto&,auto,const auto& args){
        const edrumble::Command command{args[0].integer(),args[1].integer(),args[2].integer()};
        queueEffect("rumbleP1",{command.port,command.a,command.b});requestNativeRumble(command);return jpc::Value{};
    });
    for(const char* name:{"vfxSpawnMarker","vfxSpawnMarkerOffset","vfxSpawnActorJointRnd"}) {
        const std::string key=name;bind(key,[this,key](auto&,auto,const auto& args){
            std::vector<int32_t> values;for(const auto& value:args)values.push_back(value.integer());
            queueEffect(key,std::move(values));return jpc::Value{};
        });
    }
}
void enterSourceScene(Scene& target,Actor& placed,std::vector<GateState>& states,bool firstGame=false,bool applyRequestedArrival=true) {
    Scene* oldScene=scriptScene;Actor* oldActor=scriptActor;auto* oldGates=scriptGates;
    scriptScene=&target;scriptActor=&placed;scriptGates=&states;
    try {
        if(actors) {
            const Actor requestedArrival=placed;
            visibilityReady=false;
            streams->contextReset();
            if(target.cameraFrames.empty())throw jpc::Error("Original scene camera frames have not been exported");
            cameraTimeline=std::make_unique<edcamera::Timeline>(target.cameraTracks,episode_portal_detail::sceneId(target.id));
            sourceFloorGrid=target.sourceFloor.coarseGrid(sourceFloorCache);
            vfx->clear();
            stopSceneAudio(); //430020 unloads scene samples; bootstrap-protected global samples survive.
            nativeProperties.objects.clear();nativeProperties.lights.clear();
            routes->setScene(target.nativeBytes);
            if(!firstGame)actors->resetSceneRuntime();
            const unsigned arrivalCamera=placed.camera;
            for(size_t i=0;i<target.doors.size();++i)states[i].enabled=target.enabledPortals.at(target.doors[i].portalIndex);
            placed.membership.assign(target.triggers.size(),false);
            createEpisodeScene(firstGame);
            if(!firstGame&&applyRequestedArrival&&!target.menuContext){placed=requestedArrival;placeNativePlayer(placed);publishEpisodeCamera(arrivalCamera);}
            readNativePlayer(placed,target,applyRequestedArrival);
            guard=unsigned(game.vm.staticField(395).integer());
            scriptScene=oldScene;scriptActor=oldActor;scriptGates=oldGates;return;
        }
        if(target.episode) {
            game.vm.invoke(game.package.methodIndex("GenericScene","soundTrackReset","()V")); // The source LevelA.endScene1782@56 soundtrack reset.
            game.vm.invoke(game.package.methodIndex("ObjDB","reinitialize","()V"));  // The source LevelA.endScene1782@65 ObjDB slot release.
            nativeProperties.objects.clear();nativeProperties.lights.clear();nativeProperties.voices.clear();
        }
        game.enterScene(target.id);
        if(target.episode) {
            game.vm.statics[309]=jpc::Value::integer(jpc::signedBits(episode_portal_detail::sceneId(target.id)));
            for(size_t i=0;i<target.doors.size();++i)states[i].enabled=target.enabledPortals[target.doors[i].portalIndex];
            std::string owner="Scene";for(char value:target.id)owner.push_back(value>='a'&&value<='z'?char(value-'a'+'A'):value);
            game.vm.invoke(game.package.methodIndex(owner,"activateScene","(Z)V"),game.scene,{jpc::Value::integer(0)});
        }
    } catch(...) {scriptScene=oldScene;scriptActor=oldActor;scriptGates=oldGates;throw;}
    scriptScene=oldScene;scriptActor=oldActor;scriptGates=oldGates;
}
