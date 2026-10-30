// A fresh matching VM receives the saved snapshot; source bootstrap movies stay headless during staging.
std::unique_ptr<World> stageSavedWorld(World& current,const std::vector<uint8_t>& bytes) {
    statebytes::Reader in{bytes};if(in.text()!="EDPLAY1")throw jpc::Error("Unsupported saved world header");
    const auto version=in.get<uint32_t>();if(version<1||version>World::SaveVersion)throw jpc::Error("Unsupported saved world version");
    const auto identity=in.text();
    if(!current.originalRuntime||!validSceneId(identity)||(identity[0]!='a'&&identity[0]!='b'))throw jpc::Error("Invalid saved package identity");
    fs::path target=current.root;
    if((identity[0]=='b')!=current.levelBPackage)target=current.levelBPackage?current.root.parent_path():current.root/"levelB";
    edrumble::Host stagedFeedback{current.rumbleHost.available,{}};
    auto staged=std::make_unique<World>(target,nullptr,presentMovieHeadless,true,nullptr,false,std::move(stagedFeedback));
    if(!current.sourceCartDirectory.empty())staged->configureSourceCart(current.sourceCartDirectory);
    staged->setUiLanguage(current.uiLanguage());
    staged->restoreStateBytes(bytes);
    if(current.sourceCart) {
        auto state=staged->sourceCartAdapter().checkpoint();
        for(unsigned slot=0;slot<state.slots.size();++slot)state.slots[slot]=current.sourceCart->catalog(slot);
        staged->sourceCartAdapter().restore(state);
    }
    staged->moviePresenter=current.moviePresenter;staged->trace=current.trace;staged->rumbleHost=current.rumbleHost;
    return staged;
}

// Read files only at the owning load boundary; validators stage without scanning.
std::unique_ptr<World> stageLoadGame(World& current,const std::vector<uint8_t>& bytes) {
    auto staged=stageSavedWorld(current,bytes);
    if(!staged->sourceCartDirectory.empty()) {
        auto state=staged->sourceCartAdapter().checkpoint();
        edcart::Adapter catalog(staged->sourceCartDirectory);
        edcart::Hooks hooks;
        hooks.validate=[&](const auto& file){auto checked=stageSavedWorld(*staged,file);return edcart::Preview{checked->scene.id,checked->rawClockSeconds};};
        catalog.queue(edcart::Operation::Access);
        const auto result=catalog.process(hooks);
        if(!result||!result->success)throw jpc::Error("Cannot validate saved-game catalog: "+(result?result->diagnostic:std::string{}));
        for(unsigned slot=0;slot<state.slots.size();++slot)state.slots[slot]=catalog.catalog(slot);
        staged->sourceCartAdapter().restore(state);
    }
    return staged;
}
