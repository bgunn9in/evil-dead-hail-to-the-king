#pragma once
void testAudioWorld(const fs::path& root) {
    checked(SDL_Init(SDL_INIT_GAMEPAD),"Initialize source audio World proof");
    struct Quit{~Quit(){SDL_Quit();}} quit;
    auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(std::string("Source audio World: ")+message);};
    World world(root,nullptr,presentMovieHeadless,true);
    auto tick=[&]{world.step({},false,false,.033f);require(!world.scriptFault,world.game.diagnostic.c_str());};
    auto voice=[&](int token){const auto sounds=world.audioBank.snapshot();
        const auto found=std::find_if(sounds.voices.begin(),sounds.voices.end(),[&](const auto& v){return v.token==token;});
        require(found!=sounds.voices.end(),"expected source voice is absent");return *found;};
    const auto initial=world.saveStateBytes();tick();world.restoreStateBytes(initial);
    require(world.saveStateBytes()==initial,"initial pending sounds did not restore exactly");
    const auto I=[](int value){return jpc::Value::integer(value);};
    world.beginNativeStage();
    const int markerVoice=world.game.vm.invoke(world.game.package.methodIndex("ed/base/EDBase","soundPlay3D","(IIIII)I"),0,
        {I(jpc::signedBits(0xee9221edu)),I(68),I(0),I(15),I(800)}).integer();
    const int clip=world.audioBank.resolve("ak01",0xee9221edu);
    const auto& ash=world.actors->at({0,0});const std::array<float,3> origin{ash.pose.x,ash.localY,ash.pose.z};
    const int actorVoice=world.game.vm.invoke(world.game.package.methodIndex("ed/base/External","sndMgrActorPlaySound","(IIIII)I"),0,
        {I(0),I(0),I(clip),I(80),I(0)}).integer();world.commitNativeStage();
    require(markerVoice>=0&&actorVoice>=0&&voice(markerVoice).pending&&voice(actorVoice).pending,"source calls started before audio phase");
    world.game.vm.invoke(world.game.package.methodIndex("ed/base/External","sndMgrVoiceSetFrequency","(II)V"),0,{I(actorVoice),I(12345)});
    require(voice(actorVoice).frequencyHz==0,"registered PC frequency no-op changed the voice");
    const auto clipDone=world.game.package.methodIndex("ed/base/External","sndMgrIsSNDIDDone","(I)I");
    require(!world.game.vm.invoke(clipDone,0,{I(clip)}).integer()&&world.game.vm.invoke(clipDone,0,{I(-1)}).integer(),
        "source SNDID query ignored pending voices or the missing-clip sentinel");
    const auto marker=world.scene.scripting.markers.at(15);
    require(world.nativeProperties.voices.at(markerVoice).position==std::array<float,3>{marker.x,marker.y,marker.z},"source marker XYZ was not snapshotted");
    require(world.nativeProperties.voices.at(actorVoice).position==origin,"actor call did not snapshot LOCAL XYZ");
    const auto pending=world.saveStateBytes();tick();world.restoreStateBytes(pending);
    require(world.saveStateBytes()==pending&&voice(actorVoice).pending,"pending source voice did not roundtrip");
    uint32_t navOffset=0;std::memcpy(&navOffset,world.scene.nativeBytes.data()+0x34,4);
    std::array<float,3> nav{};std::memcpy(nav.data(),world.scene.nativeBytes.data()+navOffset+23*44+8,12);
    require(floorHeight(world.scene.floor,{nav[0],nav[2]}).has_value(),"original nav23 audio fixture left floor");
    auto& player=world.actors->at({0,0});world.positionProgressBody(player,{nav[0],0,nav[2]});world.publishProgressBody(player);
    world.actor.clearanceRadius=initialActorRadius(world.scene,{nav[0],nav[2]});tick();
    require(!voice(markerVoice).pending&&!voice(actorVoice).pending&&voice(actorVoice).elapsed==0,"audio phase did not start pending voices once");
    require(world.nativeProperties.soundListener==std::array<float,3>{nav[0],0,nav[2]},"listener did not sample pre-physics Ash LOCAL XYZ");
    require(world.nativeProperties.voices.at(actorVoice).position==origin,"source position followed the actor after play");
    require(edaudiospatial::Percent(world.nativeProperties.soundListener,world.nativeProperties.voices.at(markerVoice).position,68,12,8)==6,
        "actual JPC marker15/radius800/nav23 did not match native6-percent golden");
    const auto active=world.saveStateBytes();tick();const auto later=world.saveStateBytes();world.restoreStateBytes(active);tick();
    require(world.saveStateBytes()==later,"active spatial voices/listener did not replay exactly");
    const auto good=world.saveStateBytes();world.nativeProperties.voices.at(actorVoice).position[0]=1000001;
    const auto invalid=world.saveStateBytes();world.nativeProperties.voices.at(actorVoice).position=origin;
    bool rejected=false;try{world.restoreStateBytes(invalid);}catch(const std::exception&){rejected=true;}
    require(rejected&&world.saveStateBytes()==good,"out-of-range saved sound mutated the live world");
    const auto checkpoint=world.checkpointNative();world.beginNativeStage();world.audioBank.advance(.125);world.rollbackNative(checkpoint);
    require(world.saveStateBytes()==good,"rollback lost source voice phase or properties");
    world.beginNativeStage();world.stopNativeVoice(actorVoice);
    require(!world.game.vm.invoke(world.game.package.methodIndex("ed/base/External","sndMgrIsVoiceDone","(I)I"),0,{I(actorVoice)}).integer(),
        "queued source StopVoice completed before audio phase");world.processSourceAudio();
    require(world.audioBank.done(actorVoice),"started voice did not stop at audio phase");world.rollbackNative(checkpoint);
    require(world.saveStateBytes()==good,"queued StopVoice rollback changed the world");
    world.beginNativeStage();const int stoppedPending=world.audioBank.play(clip,50);world.stopNativeVoice(stoppedPending);
    world.processSourceAudio();require(!world.audioBank.done(stoppedPending)&&!voice(stoppedPending).pending,"pending stop bypassed native start priority");
    world.processSourceAudio();require(world.audioBank.done(stoppedPending),"pending stop did not complete on the following phase");world.rollbackNative(checkpoint);
    require(world.saveStateBytes()==good,"pending StopVoice rollback changed the world");
    const int sceneClip=world.audioBank.resolve("ak01",0x0e8a08afu);require(sceneClip>=0,"original scene-only sound is absent");
    const int sceneVoice=world.audioBank.play(sceneClip,50,0);require(sceneVoice>=0,"scene voice did not allocate");
    world.stopSceneAudio();require(!world.audioBank.done(markerVoice),"scene cleanup stopped a protected global voice");
    require(world.audioBank.done(sceneVoice),"scene cleanup retained an unprotected scene sample");
    std::cout<<"Source audio World: real JPC3D/actor LOCAL snapshots, pending phase, nav23 attenuation, exact active/pending replay and malformed-state rejection passed\n";
}
