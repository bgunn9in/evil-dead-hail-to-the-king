#pragma once
// Full-World integration: every catalog status comes from real file validation.
namespace cart_world_test {
inline void require(bool value,const std::string& message) {
    if(!value)throw std::runtime_error("Original Cart World: "+message);
}
struct Scratch {
    fs::path parent,path;std::vector<fs::path> files,folders;
    explicit Scratch(const fs::path& assets):parent(fs::weakly_canonical(fs::absolute(assets)).parent_path()),
        path(parent/("cart-world-proof-"+std::to_string(SDL_GetTicksNS()))) {
        require(path.parent_path()==parent&&fs::create_directory(path)&&fs::weakly_canonical(path)==path,"cannot create owned scratch");
    }
    fs::path directory(const std::string& name) {
        const auto target=path/name;require(target.parent_path()==path&&fs::create_directory(target),"cannot create scratch child");
        folders.push_back(target);return target;
    }
    void track(const fs::path& file) {files.push_back(file);}
    fs::path graphicsProfile(const fs::path& source,const fs::path& omitted) {
        const auto target=directory("gpu-assets");
        const auto physical=fs::relative(edimage::colorPath(source/omitted),source);
        // These are immutable generated assets. Never write through a link;
        // omit the failing bitmap while creating the owned clone instead.
        for(fs::recursive_directory_iterator it(source),end;it!=end;++it) {
            const auto relative=it->path().lexically_relative(source);
            if(relative.begin()->string()=="levelB"){if(it->is_directory())it.disable_recursion_pending();continue;}
            require(!fs::is_symlink(it->symlink_status()),"source asset profile contains a link");
            if(it->is_directory()){fs::create_directory(target/relative);folders.push_back(target/relative);}
            else if(it->is_regular_file()&&relative!=physical){fs::create_hard_link(it->path(),target/relative);track(target/relative);}
            else require(relative==physical,"unexpected asset file type");
        }
        require(!fs::exists(target/physical)&&fs::exists(source/physical),"GPU fixture did not omit its own bitmap");return target;
    }
    ~Scratch() {
        std::error_code error;const auto resolved=fs::weakly_canonical(path,error);
        if(error||resolved!=path||resolved.parent_path()!=parent||path.filename().string().rfind("cart-world-proof-",0)!=0)return;
        for(auto it=files.rbegin();it!=files.rend();++it)fs::remove(*it,error);
        for(auto it=folders.rbegin();it!=folders.rend();++it)fs::remove(*it,error);
        fs::remove(path,error);
    }
};
inline std::string active(const World& world) {
    const auto& system=SourceGui::at(world.sourceGui.systems,world.sourceGui.activeSystem);
    require(system.pad!=0,"active source GUI has no Pad");return world.game.package.classes[size_t(world.game.vm.objectClass(system.pad))].name;
}
inline void tick(World& world) {
    world.step({},false,false,.033f);require(!world.scriptFault,"source frame failed: "+world.game.diagnostic);
}
inline void confirmNativeStart(World& world) {
    if(world.game.profile.dreamcast&&world.sourceMenuOwnsInput()&&active(world)=="MenStart") {
        require(world.sourceMenuInput({111}),"original Start rejected confirmation");
        for(unsigned n=0;n<20;++n)tick(world);
    }
}
inline std::unique_ptr<World> menu(World& source) {
    const auto I=[](int n){return jpc::Value::integer(n);};const char* id=source.levelBPackage?"bb01":"aa01";
    if(source.levelBPackage)source.game.vm.invoke(source.game.package.methodIndex("ed/base/External","reboot","()V"));
    else source.game.vm.invoke(source.game.package.methodIndex("ed/base/External","sceneSwitch","(IIII)V"),0,
        {I(int(episode_portal_detail::sceneId(id))),I(0),I(-1),I(0)});
    require(source.pendingSceneSwitch.has_value(),"native restart did not queue AA01/BB01");
    const auto request=*source.pendingSceneSwitch;source.pendingSceneSwitch.reset();
    const auto before=source.saveStateBytes();auto result=stagePackageWorld(source,request);
    require(source.saveStateBytes()==before,"menu staging changed the source World");
    for(unsigned n=0;n<8&&!result->sourceMenuOwnsInput();++n)tick(*result);
    confirmNativeStart(*result);
    require(result->scene.id==id&&result->sourceMenuOwnsInput()&&active(*result)=="MenMain","real restart did not reach MenMain");return result;
}
inline void accessLoad(World& world) {
    require(world.sourceMenuInput({106,111})&&active(world)=="MenSaveLoad","MenMain ordinal1 did not select Load");
    require(world.sourceCart&&world.sourceCart->pending()&&world.sourceCart->checkpoint().pending->operation==edcart::Operation::Access&&
        !world.sourceCart->done(),"source setMode2 did not queue real Access with Done0");
    require(!processSourceCart(world)&&world.sourceCart->done()&&!world.sourceCart->error(),"actual catalog scan failed");
    tick(world);require(world.sourceCart->checkValidSlot(0)==2,"F5 slot0 was not fully validated");
    if(world.game.profile.dreamcast) {
        for(unsigned n=0;n<12;++n)tick(world);
        require(world.sourceMenuInput({99}),"original card selection rejected confirmation");
        const unsigned modeField=world.levelBPackage?1078:1020; // Original MenSaveLoad.setMode A/B operands.
        for(unsigned n=0;n<60&&world.game.vm.staticField(modeField).integer()!=1;++n){tick(world);require(!processSourceCart(world),"card access unexpectedly replaced World");}
        require(world.game.vm.staticField(modeField).integer()==1,"original card access did not open the game list");
        tick(world);require(!processSourceCart(world),"game-list access unexpectedly replaced World");
    }
    const auto& room=world.sourceCart->catalog(0).preview.room;
    if(world.game.profile.dreamcast) {
        bool shown=false;
        for(const auto& command:world.sourceGuiCommands)if(const auto* page=std::get_if<edguitext::Page>(&command.value)) {
            std::string text;for(const auto& glyph:page->glyphs)text+=char(glyph.code);shown|=text.find(room)!=std::string::npos;
        }
        require(world.authoredUiComplete&&shown,"native Load did not draw the validated preview");
    } else require(world.sourceMenuText().find(room)!=std::string::npos,"source Load did not show validated preview");
}
inline void catalogReplay(World& warm,const fs::path& prefs) {
    const auto bytes=warm.saveStateBytes();auto expected=stageSavedWorld(warm,bytes);
    auto fresh=std::make_unique<World>(warm.root,nullptr,presentMovieHeadless,true);fresh->configureSourceCart(prefs);
    require(!fresh->sourceCart,"fresh catalog fixture already has a service");
    const auto previous=fresh->saveStateBytes();auto loaded=stageLoadGame(*fresh,bytes);
    require(fresh->saveStateBytes()==previous&&!fresh->sourceCart&&loaded->saveStateBytes()==bytes&&
        loaded->sourceCart->checkValidSlot(0)==2,"cold owning load changed old World or lost validated files");
    tick(*expected);tick(*loaded);
    require(!processSourceCart(*expected)&&!processSourceCart(*loaded),"catalog Access replaced a World");
    require(expected->saveStateBytes()==loaded->saveStateBytes()&&expected->sourceGui.snapshot()==loaded->sourceGui.snapshot()&&
        expected->sourceMenuText()==loaded->sourceMenuText(),"cold catalog changed next-frame World/GUI");
    require(warm.saveStateBytes()==bytes,"catalog staging changed its source World");
}
inline void queueLoad(World& world) {
    require(world.sourceMenuInput({world.game.profile.dreamcast?99:111}),"Load GUI refused confirmation");
    for(unsigned n=0;n<40&&!world.sourceCart->pending();++n)tick(world);
    const auto state=world.sourceCart->checkpoint();
    require(state.pending&&state.pending->operation==edcart::Operation::Load&&state.pending->slot==0&&!world.sourceCart->done(),
        "original Load timer did not queue slot0 with Done0");
}
// Compare the live VM, GUI, actors, scheduler, VFX, camera and draw/cache state
// without bypassing the public rule that pending Cart work cannot be saved.
inline std::vector<uint8_t> liveState(const World& world) {
    statebytes::Writer out;out.text(world.root.string());out.text(world.scene.id);out.blob(world.game.vm.snapshot());
    out.blob(world.actors->snapshot());out.blob(world.progressSnapshot());out.blob(world.routes->snapshot());out.blob(world.vfx->snapshot());
    out.blob(world.cameraTimeline->snapshot());out.blob(world.screenSnapshot());
    out.put(world.sourceTick);out.put(world.rawClockSeconds);out.put(world.tickFraction);out.put(world.guard);out.put(world.pending);
    for(float v:world.actor.position)out.put(v);out.put(world.actor.yaw);out.put(world.actor.camera);out.put(world.actor.floorOffset);
    out.flags(world.actor.membership);out.flags(world.scene.enabledVolumes);out.flags(world.scene.enabledPortals);
    out.put(world.sourceFloorCache);for(float v:world.sourceFloorGrid)out.put(v);out.put<int32_t>(world.game.mode);
    out.put<uint32_t>(uint32_t(world.gates.size()));for(const auto& gate:world.gates){out.flag(gate.enabled);out.flag(gate.pending);out.put(gate.elapsed);}
    return std::move(out.bytes);
}
inline std::vector<uint8_t> invalidVM(const World& source,const std::vector<uint8_t>& valid) {
    const auto vm=source.game.vm.snapshot();auto bytes=valid;
    const auto begin=std::search(bytes.begin(),bytes.end(),vm.begin(),vm.end());
    require(begin!=bytes.end()&&vm.size()>8,"full save does not contain its original VM snapshot");
    require(std::search(begin+1,bytes.end(),vm.begin(),vm.end())==bytes.end(),"VM snapshot is ambiguous in full save");
    *(begin+4)^=0x40;return bytes; // Envelope remains valid; the VM's own magic is invalid.
}
inline void menuModes(World& world) {
    auto& vm=world.game.vm;const auto& p=world.game.package;
    const auto I=[](int n){return jpc::Value::integer(n);};
    auto mode=[&](int value){world.setInterfaceMode(value);
        require(vm.staticField(844).integer()==value,"source Interface did not apply requested mode"+std::to_string(value)+
            " (applied"+std::to_string(vm.staticField(844).integer())+", requested"+std::to_string(world.game.mode)+")");};
    for(int value:{3,6}) {
        mode(value);require(world.sourceMenuOwnsInput()&&active(world)=="Pause"&&!world.sourceMenuText().empty(),"mode3/6 Pause lost source input/text");
        // Pause.enable selects original ordinal1 (Options). All navigation and
        // Keyboard completion below runs original flags0 callbacks.
        require(world.sourceMenuInput({111})&&active(world)=="MenOptionsLeft","Pause Options callback failed");
        for(unsigned n=0;n<8;++n) {
            const auto& system=SourceGui::at(world.sourceGui.systems,world.sourceGui.activeSystem);
            if(system.selected>=0&&SourceGui::at(world.sourceGui.items,system.selected).ordinal==3)break;
            require(world.sourceMenuInput({106}),"paused Options navigation failed");
        }
        require(world.sourceMenuInput({111})&&world.sourceKeyboardMenuActive(),"paused Options did not open Keyboard");
        // Original1427 tests mode==3, not all pause modes. Mode6 takes its
        // authored fallback to MenMain; retain that behavior.
        world.sourceKeyboardMenuClose();require(!world.sourceKeyboardMenuActive()&&active(world)==(value==3?"Pause":"MenMain")&&world.sourceMenuOwnsInput(),"Keyboard close did not take its original mode-specific branch");
        mode(0);require(!world.sourceMenuOwnsInput(),"mode0 accepted source menu input");
    }
    // Real source inventory method fixture: the tape enters through Ash's
    // pickup implementation, and setMode1 computes saveEnable from hasSaveTape.
    require(vm.invoke(p.methodIndex("Ash","pickupItem","(II)Z"),world.game.ash,{I(112),I(1)}).integer()!=0,"source save-tape pickup failed");
    mode(2);require(!world.sourceMenuOwnsInput(),"ordinary inventory should retain its modern input path");
    const auto inventory=vm.staticField(845).reference();require(inventory!=0,"source InterfaceInventory is unavailable");
    vm.invoke(p.methodIndex("InterfaceInventory","setMode","(I)V"),inventory,{I(1)});
    jpc::Ref buttons=0;
    for(const auto& system:world.sourceGui.systems)if(system.pad&&vm.objectClass(system.pad)==p.classIndex("InvMenuButtons")) {
        require(!buttons,"duplicate source inventory button Pad");buttons=system.pad;
    }
    require(buttons!=0,"source inventory button Pad is unavailable");
    vm.startCallback(p.methodIndex("InvMenuButtons","itemUse","(I)V"),buttons,{I(5)},0);
    require(vm.staticField(844).integer()==2&&world.sourceMenuOwnsInput()&&active(world)=="MenSaveLoad"&&!world.sourceMenuText().empty()&&
        world.sourceCart&&world.sourceCart->pending()&&world.sourceCart->checkpoint().pending->operation==edcart::Operation::Access,
        "actual inventory Save callback lost input/text in applied mode2");
    require(!processSourceCart(world),"inventory Save Access unexpectedly replaced World");
    tick(world);require(world.sourceMenuOwnsInput()&&!world.sourceMenuText().empty(),"mode2 Save UI lost input after source frame");
}
inline void saveUI(World& world,const fs::path& prefs) {
    const auto receiver=SourceGui::at(world.sourceGui.systems,world.sourceGui.activeSystem).pad;
    const auto& p=world.game.package;auto& vm=world.game.vm;
    // Exact submethod boundary used by InvMenuButtons.itemUse(5), not an
    // assertion that the episode has earned its allowSave inventory gate.
    vm.invoke(p.methodIndex("MenSaveLoad","setMode","(I)V"),receiver,{jpc::Value::integer(4)});
    vm.invoke(p.methodIndex("MenSaveLoad","enable","()V"),receiver);
    require(world.sourceCart->pending()&&world.sourceCart->checkpoint().pending->operation==edcart::Operation::Access,"Save UI did not request Access");
    require(!processSourceCart(world),"Save catalog unexpectedly replaced World");
    for(unsigned n=0;n<4;++n)tick(world);
    require(world.sourceMenuInput({111}),"source Save UI refused its card selection");
    for(unsigned n=0;n<40&&!world.sourceCart->pending();++n)tick(world);
    const auto pending=world.sourceCart->checkpoint();
    require(pending.pending&&pending.pending->operation==edcart::Operation::Save&&!world.sourceCart->done(),"source Save did not queue with Done0");
    const unsigned slot=pending.pending->slot;const auto file=prefs/edcart::Adapter::filename(slot);
    require(slot==0,"original PC Save fixture did not select its sole card slot0");
    require(!fs::exists(file),"Save fixture should target its initially empty slot");
    require(!processSourceCart(world)&&world.sourceCart->done()&&!world.sourceCart->error()&&fs::is_regular_file(file),"Save completed before or without actual atomic write");
    const auto saved=savefile::read(prefs,edcart::Adapter::filename(slot));auto restored=stageSavedWorld(world,saved);
    require(restored->saveStateBytes()==saved&&restored->sourceCart&&restored->sourceCart->done()&&!restored->sourceCart->error(),"v14 file completion tag was not restored");
    for(unsigned n=0;n<8;++n) {
        const auto& source=world.sourceCart->catalog(n);const auto& copied=restored->sourceCart->catalog(n);
        require(copied.status==source.status&&copied.preview.room==source.preview.room&&copied.preview.seconds==source.preview.seconds,
            "warm staging did not copy the validated catalog");
    }
    auto fresh=std::make_unique<World>(world.root,nullptr,presentMovieHeadless,true);fresh->configureSourceCart(prefs);
    auto uncached=stageSavedWorld(*fresh,saved);
    require(uncached->sourceCart&&uncached->sourceCart->done(),"fresh restore lost incoming Save completion");
    for(unsigned n=0;n<8;++n)require(uncached->sourceCart->checkValidSlot(n)==0,"incoming completion tag fabricated catalog validity");
    tick(world);tick(*restored);
    require(world.game.vm.snapshot()==restored->game.vm.snapshot()&&world.sourceMenuText()==restored->sourceMenuText(),
        "saved pending source UI did not finish identically through its original SaveGameEnd");
    require(world.sourceMenuText().find("SIPA C0XPAHEHA")!=std::string::npos,"original source Save success label is missing");
}

}

void testCartWorld(const fs::path& levelA,bool draw=false,int width=960,int height=600,const fs::path& capture={}) {
    using namespace cart_world_test;std::cout<<std::unitbuf;
    checked(SDL_Init(SDL_INIT_GAMEPAD|(draw?SDL_INIT_VIDEO:0)),"Initialize original Cart World proof");
    struct Quit {~Quit(){SDL_Quit();}} quit;Scratch scratch(levelA);
    for(bool b:{false,true}) {
        const auto root=b?levelA/"levelB":levelA,prefs=scratch.directory(b?"b-saves":"a-saves");
        const auto quick=prefs/"quicksave.eds";scratch.track(quick);
        World source(root,nullptr,presentMovieHeadless,true);source.configureSourceCart(prefs);
        source.saveGame(prefs);const auto saved=savefile::read(prefs,"quicksave.eds");
        require(saved==source.saveStateBytes(),"real F5 quicksave differs from full World bytes");
        auto owner=menu(source);accessLoad(*owner);catalogReplay(*owner,prefs);queueLoad(*owner);const auto old=liveState(*owner);World* oldOwner=owner.get();
        auto incoming=processSourceCart(*owner);
        require(incoming&&incoming->saveStateBytes()==saved&&owner.get()==oldOwner&&liveState(*owner)==old,"successful Load changed old World before owner swap");
        owner.swap(incoming);require(owner.get()!=oldOwner&&owner->scene.id==source.scene.id&&owner->levelBPackage==b,"outer owner did not commit the loaded package");
        incoming.reset();
        for(unsigned kind=0;kind<2;++kind) {
            savefile::atomicWrite(prefs,"quicksave.eds",saved);auto current=menu(source);accessLoad(*current);
            if(kind==0){auto raw=savefile::detail::rawRead(quick,savefile::DefaultLimit);raw.back()^=1;
                std::ofstream output(quick,std::ios::binary|std::ios::trunc);output.write(reinterpret_cast<const char*>(raw.data()),std::streamsize(raw.size()));output.close();}
            else savefile::atomicWrite(prefs,"quicksave.eds",invalidVM(source,saved));
            queueLoad(*current);const auto before=liveState(*current);World* previous=current.get();
            auto rejected=processSourceCart(*current);
            require(!rejected&&current.get()==previous&&liveState(*current)==before&&current->sourceCart->done()&&current->sourceCart->error()==4&&
                current->sourceCart->checkValidSlot(0)==0,"corrupt/checksummed-invalid-VM Load altered live World or reported success");
        }
        // Enter the same real menu, then remove only the owned quicksave. The
        // Save-mode Access below must discover the now-empty physical card.
        savefile::atomicWrite(prefs,"quicksave.eds",saved);auto saving=menu(source);accessLoad(*saving);fs::remove(quick);
        saveUI(*saving,prefs);
        const auto completion=saving->sourceCart->checkpoint();saving->sourceCartAccess();
        auto replay=stageLoadGame(*saving,savefile::read(prefs,"quicksave.eds"));
        const auto restored=replay->sourceCart->checkpoint();
        require(saving->sourceCart->pending()&&!restored.pending&&restored.completed&&restored.completed->success&&
            restored.completed->request.operation==edcart::Operation::Save,"catalog refresh lost incoming completion or old pending request");
        saving->sourceCart->restore(completion);
        auto boundary=std::make_unique<World>(root,nullptr,presentMovieHeadless,true);boundary->configureSourceCart(prefs);menuModes(*boundary);
        std::cout<<"Original Cart "<<(b?"BB01":"AA01")<<": F5 -> real Access/catalog/Load -> fresh owner; corrupt envelope/invalid VM rollback; source Save UI, atomic write and v14 completion passed\n";
    }
    if(draw) {
        const auto clone=scratch.graphicsProfile(levelA,fs::path("ak01")/"background_0.bmp");
        const auto prefs=scratch.directory("gpu-saves");scratch.track(prefs/"quicksave.eds");
        World source(clone,nullptr,presentMovieHeadless,true);source.configureSourceCart(prefs);source.saveGame(prefs);
        auto owner=menu(source);accessLoad(*owner);queueLoad(*owner);
        Graphics graphics;graphics.initialize(owner->scene,owner->directory(),width,height);
        graphics.loadFont(clone);graphics.loadInventory(clone);graphics.loadActorTextures(clone,*owner->actors);
        // The modern error notice is expected to change. Compare retained
        // scene/UI pixels using the same presentation snapshot on both passes.
        const auto frameText=owner->hud();
        auto render=[&](){bool shown=false;for(unsigned n=0;n<30&&!shown;++n){SDL_PumpEvents();shown=graphics.render(owner->scene,owner->actor.camera,
            owner->actor.position,owner->actor.yaw,owner->actor.animationTime,owner->actor.walking,frameText,&owner->inventoryView,
            owner->actors.get(),owner->drawObjects(),owner->drawEffects(),owner->drawActorTints(),owner->lastOverlay,owner->lastProjectionShake);}
            require(shown,"Cart GPU rollback frame is unavailable");};
        for(unsigned n=0;n<3;++n)render();
        const auto* display=SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(graphics.window));
        if(display&&width==display->w&&height==display->h) {
            graphics.setFullscreen(true);
            bool ready=false;for(unsigned n=0;n<30&&!ready;++n){render();ready=graphics.targetWidth==unsigned(width)&&graphics.targetHeight==unsigned(height);if(!ready)SDL_Delay(1);}
            require(ready,"Cart proof target did not reach requested display size");
        }
        const auto beforePath=capture.empty()?scratch.path/"before.bmp":capture.parent_path()/(capture.stem().string()+"-before.bmp");
        const auto afterPath=capture.empty()?scratch.path/"after.bmp":capture.parent_path()/(capture.stem().string()+"-after.bmp");
        if(capture.empty()){scratch.track(beforePath);scratch.track(afterPath);}graphics.screenshot(beforePath);const auto pixels=readFile(beforePath);
        auto* scene=graphics.resources.get();auto* font=graphics.font;auto* hud=graphics.hudBuffer;auto* inventory=graphics.inventoryBuffer;
        auto* atlas=graphics.particleTexture;const auto actorTextures=graphics.actorTextures,objectTextures=graphics.objectTextures;
        const auto inventoryTextures=graphics.inventoryTextures;const auto sourceAssets=graphics.sourceAssets;
        const auto text=graphics.hudText,key=graphics.inventoryKey,charset=graphics.fontCharset;
        const auto before=liveState(*owner);World* previous=owner.get();auto rejected=processSourceCart(*owner,&graphics);
        require(!rejected&&owner.get()==previous&&liveState(*owner)==before&&owner->sourceCart->done()&&owner->sourceCart->error()==4&&
            owner->sourceCart->checkValidSlot(0)==2,"missing target bitmap did not retain live World and validated file");
        require(graphics.resources.get()==scene&&graphics.font==font&&graphics.hudBuffer==hud&&graphics.inventoryBuffer==inventory&&
            graphics.particleTexture==atlas&&graphics.actorTextures==actorTextures&&graphics.objectTextures==objectTextures&&
            graphics.inventoryTextures==inventoryTextures&&graphics.sourceAssets==sourceAssets&&graphics.hudText==text&&
            graphics.inventoryKey==key&&graphics.fontCharset==charset&&!graphics.profileScopeActive,"late Cart GPU failure changed live profile");
        render();graphics.screenshot(afterPath);require(readFile(afterPath)==pixels,"late Cart GPU failure changed menu pixels");
        require(fs::is_regular_file(edimage::colorPath(levelA/"ak01/background_0.bmp")),"Cart GPU fixture changed canonical bitmap");
        std::cout<<"Original Cart GPU: valid full World file, real queued Load, late missing-background failure retains old VM/native/GPU profile and exact menu pixels passed\n";
    }
}
