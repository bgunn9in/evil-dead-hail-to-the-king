// Included inside World. Preserve the original lifecycle rather than copying
// mission flags, actor visibility, starter inventory or spawn-wave policy.
SourceGui sourceGui;
bool progressLevelArena=false;
uint32_t progressArenaGeneration=0;
std::map<std::string,std::vector<int32_t>> progressSceneProperties;
edactors::Body& progressBody(const std::vector<jpc::Value>& args) {
    if(!actors||args.size()<2)throw jpc::Error("Source actor setter requires the native registry");
    return actors->at({args[0].integer(),args[1].integer()});
}
void publishProgressBody(edactors::Body& body) {
    if(body.key.type==0&&body.key.index==0) {
        auto& placed=nativeActor();placed.position[0]=body.pose.x;placed.position[1]=body.pose.y;placed.position[2]=body.pose.z;
        placed.yaw=body.pose.yaw;placed.floorOffset=body.localY;placed.floorTriangle=std::numeric_limits<unsigned>::max();
    }
}
void positionProgressBody(edactors::Body& body,GroundPoint local) {
    if(body.key.type==0&&body.key.index==0&&(body.pose.x!=local.x||body.pose.z!=local.z||body.localY!=local.y))actors->cancelPickup();
    body.localY=local.y;body.pose.x=local.x;body.pose.z=local.z;
    body.pose.y=body.cachedFloorHeight+local.y;
    publishProgressBody(body);
}
void publishEpisodeCamera(unsigned camera,unsigned frame=0) {
    if(camera>=nativeScene().cameras.size())throw jpc::Error("Original camera index is missing");
    if(frame)throw jpc::Error("Camera publication expects a source segment, not a frame index");
    controlEpisodeCamera({4,int(camera),0,1,2});
}
template<class Binder>void bindEpisodeProgress(Binder& bind) {
    for(const char* name:SourceGui::Setters) {
        const std::string key=name;
        bind(key,[this,key](auto& vm,auto,const auto& args){return sourceGui.native(vm,key,args);});
    }
    for(const char* name:{"guiSysCreate","guiItemCreate","guiSysGetFlag","guiItemGetDataID","guiSysAtOffset","guiSysSet"}) {
        const std::string key=name;
        bind(key,[this,key](auto& vm,auto,const auto& args){return sourceGui.native(vm,key,args);});
    }
    // Original JDC GuiSys/MenMain/Pause explicitly select their console branches at platform3.
    // PC registration uses the constant-zero function4094D0.
    bind("platform",[this](auto&,auto,const auto&){return jpc::Value::integer(game.profile.dreamcast?3:0);});
    bind("checkRumbleAbility",[this](auto&,auto,const auto& args){return jpc::Value::integer(rumbleHost.supports(args[0].integer())?1:0);});
    bind("checkSoftReset",[](auto&,auto,const auto&){return jpc::Value::integer(0);}); // PC4094D0 returns zero.
    bind("sndMgrGetStereo",[this](auto&,auto,const auto&){return jpc::Value::integer(sourceGui.stereo);});
    bind("sndMgrGetMasterVolume",[this](auto&,auto,const auto&){return jpc::Value::integer(sourceGui.masterVolume);});
    bind("sndMgrSetStereo",[this](auto&,auto,const auto& args){sourceGui.stereo=int(uint8_t(args[0].integer()));return jpc::Value{};});
    bind("sndMgrSetMasterVolume",[this](auto&,auto,const auto& args){sourceGui.masterVolume=int(uint8_t(args[0].integer()));return jpc::Value{};});
    bind("memLevelStart",[this](auto&,auto,const auto&){progressLevelArena=true;return jpc::Value{};});
    bind("memLevelStop",[this](auto&,auto,const auto&){progressLevelArena=false;return jpc::Value{};});
    bind("memLevelClear",[this](auto&,auto,const auto&){
        // shortcut: native434060 rewinds the2560-byte JPC scene arena. The
        // bounded VM currently retains unreachable old scene objects until
        // arena reclamation/GC is implemented, preserving all live numeric refs.
        ++progressArenaGeneration;return jpc::Value{};
    });
    for(const char* name:{"lightSetAmbient","lightSetSunColor","lightSetSunDirection"}) {
        const std::string key=name;
        bind(key,[this,key](auto&,auto,const auto& args){
            auto& values=progressSceneProperties[key];values.clear();
            for(auto argument:args)values.push_back(argument.integer());return jpc::Value{};
        });
    }
    for(const char* name:{"actorSetPositionMarker","actorSetPositionMarkerAbsolute"}) {
        const std::string key=name;
        bind(key,[this,key](auto&,auto,const auto& args){
            const unsigned marker=unsigned(args[2].integer());
            if(marker>=nativeScene().scripting.markers.size())return jpc::Value{}; // Native4035EA unsigned range check.
            auto local=nativeScene().scripting.markers[marker];if(key=="actorSetPositionMarker")local.y=0;
            positionProgressBody(progressBody(args),local);return jpc::Value{};
        });
    }
    bind("actorSetPosition",[this](auto&,auto,const auto& args){
        positionProgressBody(progressBody(args),{float(args[2].integer())*.01f,float(args[3].integer())*.01f,float(args[4].integer())*.01f});return jpc::Value{};
    });
    bind("actorSetPositionFloorHeight",[this](auto&,auto,const auto& args){
        auto& body=progressBody(args);positionProgressBody(body,{body.pose.x,float(args[2].integer())*.01f,body.pose.z});return jpc::Value{};
    });
    bind("actorSetPositionActor",[this](auto&,auto,const auto& args){
        auto& other=actors->at({args[2].integer(),args[3].integer()});
        positionProgressBody(progressBody(args),{other.pose.x,other.localY,other.pose.z});return jpc::Value{};
    });
    bind("actorFace",[this](auto&,auto,const auto& args){
        auto& body=progressBody(args);const float heading=float(double(args[2].integer())*.01745329238474369f);
        body.pose.yaw=edactormath::modelYawFromHeading(heading);publishProgressBody(body);return jpc::Value{};
    });
    bind("actorFaceMarker",[this](auto&,auto,const auto& args){
        const unsigned marker=unsigned(args[2].integer());
        if(marker>=nativeScene().scripting.markers.size())throw jpc::Error("Source actor facing marker is missing");
        auto& body=progressBody(args);const auto point=nativeScene().scripting.markers[marker];
        const float x=point.x-body.pose.x,z=point.z-body.pose.z;
        const float heading=float(std::atan2(double(z),double(x)));
        body.pose.yaw=edactormath::modelYawFromHeading(heading);publishProgressBody(body);return jpc::Value{};
    });
    const auto marker=[this](int index) {
        if(index<0||size_t(index)>=nativeScene().scripting.markers.size())throw jpc::Error("Source actor query marker is missing");
        return nativeScene().scripting.markers[size_t(index)];
    };
    bind("actorDistanceToMarker",[this,marker](auto&,auto,const auto& args){
        const auto& body=progressBody(args);const auto point=marker(args[2].integer());
        return jpc::Value::integer(edactormath::distanceXZCentimeters({{body.pose.x,body.localY,body.pose.z}},{{point.x,point.y,point.z}}));
    });
    bind("actorGetNearestMarker",[this](auto&,auto,const auto& args){
        const auto& body=progressBody(args);std::vector<edactormath::Vector> points;
        for(const auto& point:nativeScene().scripting.markers)points.push_back({{point.x,point.y,point.z}});
        return jpc::Value::integer(edactormath::nearestMarker({{body.pose.x,body.localY,body.pose.z}},points,args[2].integer(),args[3].integer()));
    });
    bind("sceneGetMarkerCount",[this](auto&,auto,const auto&){return jpc::Value::integer(int(nativeScene().scripting.markers.size()));});
    bind("actorHaveLOSToMarker",[this,marker](auto&,auto,const auto& args){
        const auto& body=progressBody(args);const auto point=marker(args[2].integer());
        return jpc::Value::integer(nativeScene().queries.lineOfSight({body.pose.x,body.pose.y,body.pose.z},point,
            body.key.type==0?4:2,sourceQueryCamera(),nativeScene().enabledVolumes,visibilityReady)?1:0);
    });
    for(const char* name:{"sceneIsMarkerVisibleObj","sceneIsMarkerVisibleNPC"}) {
        const std::string key=name;
        bind(key,[this,marker,key](auto&,auto,const auto& args){return jpc::Value::integer(
            visibilityReady&&nativeScene().queries.visible(key=="sceneIsMarkerVisibleNPC"?2:4,sourceQueryCamera(),marker(args[0].integer()))?1:0);});
    }
    bind("controlCamera",[this](auto&,auto,const auto& args){
        controlEpisodeCamera({args[0].integer(),args[1].integer(),args[2].integer(),args[3].integer(),args[4].integer()});return jpc::Value{};
    });
}
std::vector<uint8_t> progressSnapshot() const {
    statebytes::Writer out;out.text("EDPROGRESS1");out.flag(progressLevelArena);out.put(progressArenaGeneration);
    out.put<uint32_t>(uint32_t(progressSceneProperties.size()));
    for(const auto& [name,values]:progressSceneProperties) {
        out.text(name);out.put<uint32_t>(uint32_t(values.size()));for(auto value:values)out.put(value);
    }
    out.blob(sourceGui.snapshot());return std::move(out.bytes);
}
void restoreProgress(const std::vector<uint8_t>& bytes) {
    statebytes::Reader in{bytes};if(in.text()!="EDPROGRESS1")throw jpc::Error("Unsupported original progress state");
    const bool arena=in.flag();const auto generation=in.get<uint32_t>();
    std::map<std::string,std::vector<int32_t>> properties;const auto count=in.count(3);
    for(unsigned i=0;i<count;++i) {
        const auto name=in.text();const auto fields=in.count(3);
        const unsigned expected=name=="lightSetSunDirection"?2u:(name=="lightSetAmbient"||name=="lightSetSunColor")?3u:0u;
        if(!expected||fields!=expected||properties.count(name))throw jpc::Error("Invalid saved source scene property");
        auto& values=properties[name];for(unsigned n=0;n<fields;++n)values.push_back(in.get<int32_t>());
    }
    SourceGui candidate;candidate.uvLayout=sourceGui.uvLayout;candidate.restore(in.blob());in.end();candidate.validateReferences(game.vm);
    sourceGui=std::move(candidate);progressLevelArena=arena;progressArenaGeneration=generation;progressSceneProperties=std::move(properties);
}
void initializeEpisodeProgress() {
    if(!actors)throw jpc::Error("Original episode startup requires the actor registry");
    if(!actors->bodies().empty())throw jpc::Error("Original episode startup cannot create a second actor pool");
    actors->prepareBootstrap();
    game.vm.invoke(game.package.methodIndex("Main","initialize","()V"));
    // ScriptGame's compatibility fixture level must not bypass Main826's real
    // constructor and virtual LevelA.create1778 call.
    game.vm.statics[396]=jpc::Value::reference(0);
    game.vm.invoke(game.package.methodIndex("Main","createLevel","(I)V"),0,{jpc::Value::integer(levelBPackage?1:0)});
    game.level=game.vm.staticField(396).reference();
    actors->sync();
    game.inventory=game.vm.field(game.ash,99).reference();
    if(!game.ash||!game.inventory)throw jpc::Error("Original episode startup did not create Ash and inventory");
}
void createEpisodeScene(bool firstGame) {
    if(!actors)throw jpc::Error("Original scene creation requires the actor registry");
    const uint32_t identity=episode_portal_detail::sceneId(nativeScene().id);
    // Main824 invokes the actual virtual LevelA1783. That method creates the
    // scene, runs SceneA1787, and restores its original342-byte spawn stream.
    game.vm.invoke(game.package.methodIndex("Main","createScene","(II)V"),0,
        {jpc::Value::integer(jpc::signedBits(identity)),jpc::Value::integer(firstGame?1:0)});
    game.scene=game.vm.field(game.level,38).reference();
    if(!game.scene)throw jpc::Error("Original scene creation did not publish a scene");
    actors->sync();
    game.inventory=game.vm.field(game.ash,99).reference();
}
void endEpisodeScene() {
    game.vm.invoke(game.package.methodIndex("Main","endScene","()V"));
    const auto removed=actors->unloadScene();routes->forgetActors(removed);
    for(const auto key:removed)nativeProperties.actorFades.erase(key);
}
void processEpisodeFrame() {
    // Main823 increments source393, services the Use guard394/395, obtains
    // source milliseconds398, then LevelA/Scene frame, Spawn932 and rumble902.
    // Call once before animation/actor-physics/message/scheduler phases.
    game.vm.invoke(game.package.methodIndex("Main","processFrame","()V"));
    game.vm.invoke(game.package.methodIndex("Interface","processFrame","()V"));
}
void changeEpisodeInterfaceMode(int mode) {
    if(mode<0||mode>6)throw jpc::Error("Unsupported original interface mode");
    const int previous=game.vm.staticField(844).integer();
    if(previous==mode){game.mode=mode;return;}
    // Original1147 owns GUI enable/disable and EDSystem pause/resume368/370.
    game.vm.invoke(game.package.methodIndex("Interface","gameStateChange","(II)V"),0,
        {jpc::Value::integer(previous),jpc::Value::integer(mode)});
    game.mode=mode;
}
